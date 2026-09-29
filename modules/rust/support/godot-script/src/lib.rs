//! Attachable Rust scripts for Godot.
//!
//! A Rust type can act as a *script*: instead of being a node type of its own,
//! it is attached to an existing node in a scene, exactly like a GDScript
//! file. The engine side lives in the `rust` module compiled into Godot; this
//! crate builds the bridge:
//!
//! 1. Implement [`RustScript`] for a type and call [`register_script!`] once.
//! 2. The engine calls the registered factory whenever a node with the script
//!    attached is created.
//! 3. [`ScriptInstanceHandle`] implements gdext's `ScriptInstance` trait and
//!    forwards property access and lifecycle callbacks to the user's type.
//!
//! ```no_run
//! use godot::prelude::*;
//! use godot_script::prelude::*;
//!
//! struct Player {
//!     owner: Gd<Node2D>,
//!     speed: f32,
//! }
//!
//! impl RustScript for Player {
//!     type Base = Node2D;
//!     const CLASS_NAME: &'static str = "Player";
//!     const BASE_NAME: &'static str = "Node2D";
//!
//!     fn new(owner: Gd<Self::Base>) -> Self {
//!         Self { owner, speed: 100.0 }
//!     }
//!
//!     fn properties() -> Vec<PropertyInfo> {
//!         vec![PropertyInfo::new_export::<f32>("speed")]
//!     }
//!
//!     fn get_property(&self, name: &str) -> Option<Variant> {
//!         match name {
//!             "speed" => Some(self.speed.to_variant()),
//!             _ => None,
//!         }
//!     }
//!
//!     fn set_property(&mut self, name: &str, value: &Variant) -> bool {
//!         match name {
//!             "speed" => {
//!                 self.speed = f32::from_variant(value);
//!                 true
//!             }
//!             _ => false,
//!         }
//!     }
//!
//!     fn ready(&mut self) {
//!         godot_print!("Player ready, speed = {}", self.speed);
//!     }
//! }
//!
//! godot_script::register_script!(Player);
//! ```

use std::ffi::c_void;

use godot::builtin::{GString, StringName, VarArray, VarDictionary, Variant, VariantType};
use godot::classes::{Engine, Script, ScriptLanguage};
use godot::meta::error::CallErrorType;
use godot::meta::{ClassId, FromGodot, ToGodot};
use godot::obj::script::{ScriptInstance, SiMut, create_script_instance};
use godot::obj::{EngineBitfield as _, EngineEnum as _, Gd, GodotClass, InstanceId, Singleton};
use godot::register::info::{
    MethodFlags, MethodInfo, PropertyHintInfo, PropertyInfo, PropertyUsageFlags,
};
use godot::register::property::Var;

// The derive and the attribute macro live in their own proc-macro crate; they are
// re-exported here so a script only depends on `godot-script`.
pub use godot_script_derive::{RustScript, godot_script_api};

pub mod prelude {
    //! Everything a script file needs on top of `godot::prelude`.
    //!
    //! Only the names that `godot::prelude` does not already export are listed,
    //! so `use godot::prelude::*;` and `use godot_script::prelude::*;` can be
    //! combined without ambiguous glob imports.
    pub use crate::{
        RustScript, ScriptApi, ScriptArgument, ScriptMethod, ScriptSignal, argument_value,
        call_argument, godot_script_api, register_script,
    };
    pub use godot::meta::error::CallErrorType;
    pub use godot::register::info::PropertyInfo;
}

/// One parameter of a script method or signal.
#[derive(Clone, Debug)]
pub struct ScriptArgument {
    /// Name shown in the editor and in error messages.
    pub name: &'static str,
    /// Godot type of the value, e.g. VariantType::FLOAT.
    pub variant_type: VariantType,
}

impl ScriptArgument {
    pub fn new(name: &'static str, variant_type: VariantType) -> Self {
        Self { name, variant_type }
    }
}

/// A method other scripts can call on the node.
///
/// Declare it in [`RustScript::methods`] and do the actual work in
/// [`RustScript::call_method`]:
///
/// ```ignore
/// fn methods() -> Vec<ScriptMethod> {
///     vec![ScriptMethod::new("jump").arg("height", VariantType::FLOAT)]
/// }
///
/// fn call_method(&mut self, name: &str, args: &[&Variant]) -> Result<Variant, CallErrorType> {
///     match name {
///         "jump" => {
///             let height = args.first().map_or(2.0, |v| f32::from_variant(v));
///             Ok((self.height + height).to_variant())
///         }
///         _ => Err(CallErrorType::InvalidMethod),
///     }
/// }
/// ```
///
/// GDScript can then write `node.jump(4.0)`, and the editor lists the method
/// like it lists a function of a GDScript file.
#[derive(Clone, Debug)]
pub struct ScriptMethod {
    /// Name used by call() and has_method().
    pub name: &'static str,
    /// Parameters, in call order.
    pub arguments: Vec<ScriptArgument>,
    /// Value returned by the method; VariantType::NIL for nothing.
    pub return_type: VariantType,
    /// Const methods may be called on a read-only instance.
    pub is_const: bool,
    /// Default values of the trailing optional parameters.
    pub defaults: Vec<Variant>,
}

impl ScriptMethod {
    pub fn new(name: &'static str) -> Self {
        Self {
            name,
            arguments: Vec::new(),
            return_type: VariantType::NIL,
            is_const: false,
            defaults: Vec::new(),
        }
    }

    /// Adds one parameter.
    pub fn arg(mut self, name: &'static str, variant_type: VariantType) -> Self {
        self.arguments.push(ScriptArgument::new(name, variant_type));
        self
    }

    /// Sets the return type.
    pub fn returns(mut self, variant_type: VariantType) -> Self {
        self.return_type = variant_type;
        self
    }

    /// Adds one parameter, taking its Godot type from the Rust type.
    ///
    /// Used by `#[func]`; `arg_of::<f32>("height")` is the same as
    /// `arg("height", VariantType::FLOAT)`.
    pub fn arg_of<T: Var>(mut self, name: &'static str) -> Self {
        self.arguments
            .push(ScriptArgument::new(name, PropertyInfo::new_var::<T>("").variant_type));
        self
    }

    /// Sets the return type from the Rust type, as `#[func]` does.
    pub fn returns_of<T: Var>(mut self) -> Self {
        self.return_type = PropertyInfo::new_var::<T>("").variant_type;
        self
    }

    /// Declares a default for the next optional parameter, in call order.
    ///
    /// `#[func]` writes one of these for every `#[opt(default = ...)]` parameter;
    /// the editor shows them and callers may leave those arguments out.
    pub fn default_value<T: ToGodot>(mut self, value: T) -> Self {
        self.defaults.push(value.to_variant());
        self
    }

    /// Marks the method as const.
    pub fn as_const(mut self) -> Self {
        self.is_const = true;
        self
    }

    /// The gdext description the engine reads from a script instance.
    fn to_method_info(&self) -> MethodInfo {
        MethodInfo {
            id: 0,
            method_name: StringName::from(self.name),
            class_name: ClassId::none(),
            return_type: PropertyInfo {
                variant_type: self.return_type,
                class_name: StringName::default(),
                property_name: StringName::default(),
                hint_info: PropertyHintInfo::none(),
                usage: PropertyUsageFlags::DEFAULT,
            },
            arguments: self
                .arguments
                .iter()
                .map(|argument| PropertyInfo {
                    variant_type: argument.variant_type,
                    class_name: StringName::default(),
                    property_name: StringName::from(argument.name),
                    hint_info: PropertyHintInfo::none(),
                    usage: PropertyUsageFlags::DEFAULT,
                })
                .collect(),
            default_arguments: self.defaults.clone(),
            flags: if self.is_const {
                MethodFlags::CONST
            } else {
                MethodFlags::DEFAULT
            },
        }
    }
}

/// A signal the script declares, e.g. `signal jumped(height: float)`.
///
/// Signals become visible to connect() and in the editor's signal dock, and the
/// script can emit them with `owner.emit_signal("jumped", &[height.to_variant()])`.
#[derive(Clone, Debug)]
pub struct ScriptSignal {
    /// Signal name.
    pub name: &'static str,
    /// Parameters carried by the signal.
    pub arguments: Vec<ScriptArgument>,
}

impl ScriptSignal {
    pub fn new(name: &'static str) -> Self {
        Self {
            name,
            arguments: Vec::new(),
        }
    }

    /// Adds one parameter.
    pub fn arg(mut self, name: &'static str, variant_type: VariantType) -> Self {
        self.arguments.push(ScriptArgument::new(name, variant_type));
        self
    }

    /// Adds one parameter, taking its Godot type from the Rust type.
    pub fn arg_of<T: Var>(mut self, name: &'static str) -> Self {
        self.arguments
            .push(ScriptArgument::new(name, PropertyInfo::new_var::<T>("").variant_type));
        self
    }
}

/// A Rust type that can be attached to a node as a script.

///
/// Only [`RustScript::new`] and the two class name constants are required; every
/// other callback has a sensible default.
pub trait RustScript: Sized + 'static {
    /// The Godot class this script extends, e.g. `Node2D`.
    type Base: GodotClass;

    /// Name shown in the editor (also used as the global class name).
    const CLASS_NAME: &'static str;

    /// Engine name of [`Self::Base`], e.g. "Node2D".
    const BASE_NAME: &'static str;

    /// Tool scripts also run inside the editor.
    const IS_TOOL: bool = false;

    /// Called once per attached node to build the script state.
    fn new(owner: Gd<Self::Base>) -> Self;

    /// Properties shown in the inspector and saved in scenes.
    fn properties() -> Vec<PropertyInfo> {
        Vec::new()
    }

    /// Reads one of [`RustScript::properties`].
    fn get_property(&self, _name: &str) -> Option<Variant> {
        None
    }

    /// Value the inspector offers when a property is reverted.
    ///
    /// `#[derive(RustScript)]` answers this for every exported field: the
    /// `#[export(default = ...)]` expression, or `Default::default()`.
    fn property_default(_name: &str) -> Option<Variant> {
        None
    }

    /// Writes one of [`RustScript::properties`].
    fn set_property(&mut self, _name: &str, _value: &Variant) -> bool {
        false
    }

    /// Methods that other scripts and the editor can call on the node.
    ///
    /// The list is what the editor shows and what `has_method()` answers; the
    /// actual work happens in [`RustScript::call_method`].
    fn methods() -> Vec<ScriptMethod> {
        Vec::new()
    }

    /// Runs one of [`RustScript::methods`].
    ///
    /// Return [`CallErrorType::InvalidMethod`] for names that are not yours, so
    /// the engine can report the usual error.
    fn call_method(
        &mut self,
        _name: &str,
        _args: &[&Variant],
    ) -> Result<Variant, CallErrorType> {
        Err(CallErrorType::InvalidMethod)
    }

    /// Signals the script declares.
    ///
    /// They show up in the editor's signal list and can be connected with
    /// `connect()`; emit them from Rust with
    /// `owner.emit_signal("jumped", &[height.to_variant()])`.
    fn signals() -> Vec<ScriptSignal> {
        Vec::new()
    }

    /// Called when the node is ready (its children entered the tree).
    fn ready(&mut self) {}

    /// Called every rendered frame.
    fn process(&mut self, _delta: f64) {}

    /// Called every physics frame.
    fn physics_process(&mut self, _delta: f64) {}

    /// Called when the node enters the scene tree.
    fn enter_tree(&mut self) {}

    /// Called when the node leaves the scene tree.
    fn exit_tree(&mut self) {}
}

/// What `#[godot_script_api]` fills in for a `#[derive(RustScript)]` struct.
///
/// Only the macro implements this trait; the default implementations below make
/// an empty script work, so a script without methods is still valid.
#[diagnostic::on_unimplemented(
    message = "`{Self}` is missing its script API block",
    label = "add `#[godot_script_api] impl {Self} {{ ... }}` below the struct",
    note = "`#[derive(RustScript)]` only reads the fields; lifecycle hooks, `#[func]` methods and `#[signal]` declarations are collected from this impl block"
)]
pub trait ScriptApi: Sized {
    /// Methods exposed to GDScript and the editor.
    fn api_methods() -> Vec<ScriptMethod> {
        Vec::new()
    }

    /// Dispatches `node.call("method", ...)` to the `#[func]` methods.
    fn api_call(&mut self, _name: &str, _args: &[&Variant]) -> Result<Variant, CallErrorType> {
        Err(CallErrorType::InvalidMethod)
    }

    /// Signals declared with `#[signal]`.
    fn api_signals() -> Vec<ScriptSignal> {
        Vec::new()
    }

    fn api_ready(&mut self) {}

    fn api_process(&mut self, _delta: f64) {}

    fn api_physics_process(&mut self, _delta: f64) {}

    fn api_enter_tree(&mut self) {}

    fn api_exit_tree(&mut self) {}
}

/// The frame time the engine passes to `_process`/`_physics_process`.
///
/// Returns 0.0 when the engine (or a direct call) passes something else.
fn frame_delta(args: &[&Variant]) -> f64 {
    args.first()
        .and_then(|value| f64::try_from_variant(value).ok())
        .unwrap_or(0.0)
}

/// Converts one argument of a script call.
pub fn argument_value<T: FromGodot>(value: &Variant) -> Result<T, CallErrorType> {
    T::try_from_variant(value).map_err(|_| CallErrorType::InvalidArgument)
}

/// Reads argument `index` of a script call, reporting the usual call errors.
///
/// Used by the generated dispatch code; write it by hand in `call_method` when a
/// method has optional arguments.
pub fn call_argument<T: FromGodot>(args: &[&Variant], index: usize) -> Result<T, CallErrorType> {
    match args.get(index) {
        Some(value) => argument_value(value),
        None => Err(CallErrorType::TooFewArguments),
    }
}

/// Lifecycle method names forwarded to the user's hooks.
const LIFECYCLE_METHODS: [&str; 5] = ["_ready", "_process", "_physics_process", "_enter_tree", "_exit_tree"];

/// The engine-facing script instance; bridges `T` to Godot's script API.
pub struct ScriptInstanceHandle<T: RustScript> {
    owner: Gd<T::Base>,
    script: Gd<Script>,
    language: Gd<ScriptLanguage>,
    user: T,
}

impl<T: RustScript> ScriptInstanceHandle<T> {
    /// The node this script is attached to.
    pub fn owner(&self) -> &Gd<T::Base> {
        &self.owner
    }

    /// Read-only access to the user state.
    pub fn user(&self) -> &T {
        &self.user
    }
}

impl<T: RustScript> ScriptInstance for ScriptInstanceHandle<T> {
    type Base = T::Base;

    fn class_name(&self) -> GString {
        GString::from(T::CLASS_NAME)
    }

    fn set_property(mut this: SiMut<Self>, name: StringName, value: &Variant) -> bool {
        let name = name.to_string();
        this.user.set_property(&name, value)
    }

    fn get_property(&self, name: StringName) -> Option<Variant> {
        self.user.get_property(&name.to_string())
    }

    fn on_property_get_revert(&self, name: StringName) -> Option<Variant> {
        T::property_default(&name.to_string())
    }

    fn get_property_list(&self) -> Vec<PropertyInfo> {
        T::properties()
    }

    fn get_method_list(&self) -> Vec<MethodInfo> {
        T::methods()
            .iter()
            .map(|method| method.to_method_info())
            .collect()
    }

    fn call(
        mut this: SiMut<Self>,
        method: StringName,
        args: &[&Variant],
    ) -> Result<Variant, CallErrorType> {
        let name = method.to_string();

        match name.as_str() {
            "_ready" => this.user.ready(),
            // Only the frame hooks take a delta; other methods must not touch the
            // arguments before their own conversion runs (a String or int first
            // argument is perfectly valid).
            "_process" => this.user.process(frame_delta(args)),
            "_physics_process" => this.user.physics_process(frame_delta(args)),
            "_enter_tree" => this.user.enter_tree(),
            "_exit_tree" => this.user.exit_tree(),
            _ => return this.user.call_method(&name, args),
        }

        Ok(Variant::nil())
    }

    fn is_placeholder(&self) -> bool {
        false
    }

    fn has_method(&self, method: StringName) -> bool {
        let method = method.to_string();
        LIFECYCLE_METHODS.contains(&method.as_str())
            || T::methods().iter().any(|user| user.name == method)
    }

    fn get_script(&self) -> &Gd<Script> {
        &self.script
    }

    fn get_property_type(&self, name: StringName) -> VariantType {
        T::properties()
            .into_iter()
            .find(|property| property.property_name == name)
            .map(|property| property.variant_type)
            .unwrap_or(VariantType::NIL)
    }

    fn to_string(&self) -> GString {
        GString::from(format!("<{} script instance>", T::CLASS_NAME).as_str())
    }

    fn get_property_state(&self) -> Vec<(StringName, Variant)> {
        T::properties()
            .into_iter()
            .filter_map(|property| {
                let name = property.property_name;
                self.user
                    .get_property(&name.to_string())
                    .map(|value| (name, value))
            })
            .collect()
    }

    fn get_language(&self) -> Gd<ScriptLanguage> {
        self.language.clone()
    }

    fn on_refcount_decremented(&self) -> bool {
        true
    }

    fn on_refcount_incremented(&self) {}

    fn property_get_fallback(&self, _name: StringName) -> Option<Variant> {
        None
    }

    fn property_set_fallback(_this: SiMut<Self>, _name: StringName, _value: &Variant) -> bool {
        false
    }

    fn get_method_argument_count(&self, method: StringName) -> Option<u32> {
        let name = method.to_string();
        T::methods()
            .iter()
            .find(|user| user.name == name)
            .map(|user| user.arguments.len() as u32)
    }
}

/// Factory called by the engine when a node with the script is created.
extern "C" fn create_instance<T: RustScript>(owner_id: i64, script_id: i64, language_id: i64) -> *mut c_void {
    let owner = Gd::<T::Base>::from_instance_id(InstanceId::from_i64(owner_id));
    let script = Gd::<Script>::from_instance_id(InstanceId::from_i64(script_id));
    let language = Gd::<ScriptLanguage>::from_instance_id(InstanceId::from_i64(language_id));

    // Signals are declared on the script (see RustScript::signals), exactly like
    // GDScript ones: the engine consults the script when connecting or emitting.
    let user = T::new(owner.clone());
    let handle = ScriptInstanceHandle::<T> {
        owner,
        script,
        language,
        user,
    };

    // SAFETY: the owner outlives the returned instance, and Godot releases the
    // instance through the info table built by `create_script_instance`.
    unsafe { create_script_instance(handle, owner_for_instance::<T>(owner_id)) }.ptr()
}

/// One method/signal parameter, in the shape `Object::add_user_signal` expects.
fn argument_dictionary(argument: &ScriptArgument) -> VarDictionary {
    let mut entry = VarDictionary::new();
    entry.set("name", &argument.name.to_variant());
    entry.set("type", &(argument.variant_type.ord() as i64).to_variant());
    entry
}

/// The script's exported properties, as the engine registry stores them.
fn properties_dictionary<T: RustScript>() -> VarArray {
    let mut array = VarArray::new();
    for property in T::properties() {
        let mut entry = VarDictionary::new();
        entry.set("name", &property.property_name.to_string().to_variant());
        entry.set("type", &(property.variant_type.ord() as i64).to_variant());
        entry.set("class_name", &property.class_name.to_string().to_variant());
        entry.set("hint", &(property.hint_info.hint.ord() as i64).to_variant());
        entry.set("hint_string", &property.hint_info.hint_string.to_variant());
        entry.set("usage", &(property.usage.ord() as i64).to_variant());
        array.push(&entry);
    }
    array
}

/// The script's callable methods, as the engine registry stores them.
fn methods_dictionary<T: RustScript>() -> VarArray {
    let mut array = VarArray::new();
    for method in T::methods() {
        let mut entry = VarDictionary::new();
        entry.set("name", &method.name.to_variant());
        entry.set("return_type", &(method.return_type.ord() as i64).to_variant());
        entry.set("is_const", &method.is_const.to_variant());
        let mut arguments = VarArray::new();
        for argument in &method.arguments {
            arguments.push(&argument_dictionary(argument));
        }
        entry.set("args", &arguments.to_variant());
        let mut defaults = VarArray::new();
        for value in &method.defaults {
            defaults.push(value);
        }
        entry.set("defaults", &defaults.to_variant());
        array.push(&entry);
    }
    array
}

/// The script's signals, as the engine registry stores them.
fn signals_dictionary<T: RustScript>() -> VarArray {
    let mut array = VarArray::new();
    for signal in T::signals() {
        let mut entry = VarDictionary::new();
        entry.set("name", &signal.name.to_variant());
        let mut arguments = VarArray::new();
        for argument in &signal.arguments {
            arguments.push(&argument_dictionary(argument));
        }
        entry.set("args", &arguments.to_variant());
        array.push(&entry);
    }
    array
}

fn owner_for_instance<T: RustScript>(owner_id: i64) -> Gd<T::Base> {
    Gd::<T::Base>::from_instance_id(InstanceId::from_i64(owner_id))
}

/// Registers `T` with the engine. Called by [`register_script!`].
pub fn register<T: RustScript>(file_path: &str) {
    let path = normalize_path(file_path);
    let create_fn = create_instance::<T> as *const () as usize as i64;

    let engine = Engine::singleton();
    let Some(mut registry) = engine.get_singleton("RustScriptRegistry") else {
        godot::global::godot_error!(
            "godot-script: RustScriptRegistry is missing. Is the engine built with the rust module enabled?"
        );
        return;
    };

    // The engine keeps this descriptor so the editor can list the script's
    // class, base type, properties, methods and signals without loading it.
    let mut descriptor = VarDictionary::new();
    descriptor.set("properties", &properties_dictionary::<T>().to_variant());
    descriptor.set("methods", &methods_dictionary::<T>().to_variant());
    descriptor.set("signals", &signals_dictionary::<T>().to_variant());

    registry.call(
        "register_script_type",
        &[
            path.to_variant(),
            T::CLASS_NAME.to_variant(),
            T::BASE_NAME.to_variant(),
            T::IS_TOOL.to_variant(),
            create_fn.to_variant(),
        ],
    );

    // The descriptor travels in its own call: libraries compiled against an older
    // engine must keep resolving `register_script_type` by its method hash.
    registry.call(
        "set_script_descriptor",
        &[path.to_variant(), descriptor.to_variant()],
    );

    godot::global::godot_print!(
        "godot-script: registered '{}' (base {}) for {}",
        T::CLASS_NAME,
        T::BASE_NAME,
        path
    );
}

/// Turns `file!()` output ("src/player.rs") into a `res://` path.
fn normalize_path(file_path: &str) -> String {
    if file_path.starts_with("res://") {
        file_path.to_string()
    } else {
        format!("res://{}", file_path)
    }
}

/// Declares the script type of the current file and registers it with the
/// engine.
///
/// Put one call at the bottom of every script file; it generates a
/// `pub fn register()` that the crate root calls from `on_level_init`.
#[macro_export]
macro_rules! register_script {
    ($ty:ty) => {
        /// Registers this script with the engine.
        pub fn register() {
            $crate::register::<$ty>(file!());
        }
    };
}
