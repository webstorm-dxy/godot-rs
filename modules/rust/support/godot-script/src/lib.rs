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

use godot::builtin::{GString, StringName, Variant, VariantType};
use godot::classes::{Engine, Script, ScriptLanguage};
use godot::meta::error::CallErrorType;
use godot::meta::{FromGodot, ToGodot};
use godot::obj::script::{ScriptInstance, SiMut, create_script_instance};
use godot::obj::{Gd, GodotClass, InstanceId, Singleton};
use godot::register::info::{MethodInfo, PropertyInfo};

pub mod prelude {
    //! Everything a script file needs on top of `godot::prelude`.
    //!
    //! Only the names that `godot::prelude` does not already export are listed,
    //! so `use godot::prelude::*;` and `use godot_script::prelude::*;` can be
    //! combined without ambiguous glob imports.
    pub use crate::{RustScript, register_script};
    pub use godot::register::info::PropertyInfo;
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

    /// Writes one of [`RustScript::properties`].
    fn set_property(&mut self, _name: &str, _value: &Variant) -> bool {
        false
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

    fn get_property_list(&self) -> Vec<PropertyInfo> {
        T::properties()
    }

    fn get_method_list(&self) -> Vec<MethodInfo> {
        Vec::new()
    }

    fn call(
        mut this: SiMut<Self>,
        method: StringName,
        args: &[&Variant],
    ) -> Result<Variant, CallErrorType> {
        let delta = args.first().map(|value| f64::from_variant(value)).unwrap_or(0.0);

        match method.to_string().as_str() {
            "_ready" => this.user.ready(),
            "_process" => this.user.process(delta),
            "_physics_process" => this.user.physics_process(delta),
            "_enter_tree" => this.user.enter_tree(),
            "_exit_tree" => this.user.exit_tree(),
            _ => return Err(CallErrorType::InvalidMethod),
        }

        Ok(Variant::nil())
    }

    fn is_placeholder(&self) -> bool {
        false
    }

    fn has_method(&self, method: StringName) -> bool {
        let method = method.to_string();
        LIFECYCLE_METHODS.contains(&method.as_str())
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

    fn get_method_argument_count(&self, _method: StringName) -> Option<u32> {
        None
    }
}

/// Factory called by the engine when a node with the script is created.
extern "C" fn create_instance<T: RustScript>(owner_id: i64, script_id: i64, language_id: i64) -> *mut c_void {
    let owner = Gd::<T::Base>::from_instance_id(InstanceId::from_i64(owner_id));
    let script = Gd::<Script>::from_instance_id(InstanceId::from_i64(script_id));
    let language = Gd::<ScriptLanguage>::from_instance_id(InstanceId::from_i64(language_id));

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
