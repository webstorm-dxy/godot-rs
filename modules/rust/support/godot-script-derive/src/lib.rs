//! Derive and attribute macros for `godot-script`.
//!
//! A script is written in two parts:
//!
//! ```ignore
//! #[derive(RustScript)]
//! #[script(base = Node2D)]
//! pub struct Player {
//!     owner: Gd<Node2D>,
//!     #[export] speed: f32,
//! }
//!
//! #[godot_script_api]
//! impl Player {
//!     fn ready(&mut self) { godot_print!("ready"); }
//!
//!     #[func]
//!     fn jump(&mut self, height: f32) -> f32 { self.speed + height }
//!
//!     #[signal]
//!     fn jumped(height: f32) {}
//! }
//! ```
//!
//! The derive reads the **fields** (exports, the owner field, defaults) and the
//! attribute macro reads the **impl block** (lifecycle hooks, `#[func]` methods,
//! `#[signal]` declarations); together they generate the `RustScript` impl and
//! the dispatch table the engine calls.

use proc_macro::TokenStream;
use proc_macro2::TokenStream as TokenStream2;
use quote::{format_ident, quote};
use syn::{
    Data, DeriveInput, Expr, Fields, FnArg, ImplItem, ImplItemFn, ItemImpl, LitStr, Path,
    ReturnType, Type, parse_macro_input,
};

/// Derives `RustScript` for a struct with named fields.
///
/// Struct attributes:
///
/// - `#[script(base = Node2D)]` (required): the engine class the script extends;
/// - `#[script(tool)]`: also runs inside the editor;
/// - `#[script(name = "Custom")]`: class name other than the struct name.
///
/// Field attributes:
///
/// - `#[export]`: show in the inspector and save in the scene;
/// - `#[export(default = 42.0)]`: initial value instead of `Default::default()`;
/// - `#[export(range = (0.0, 100.0))]`: slider range in the inspector.
///
/// A field called `owner` receives the node the script is attached to.
#[proc_macro_derive(RustScript, attributes(script, export))]
pub fn derive_rust_script(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);
    expand_derive(&input)
        .unwrap_or_else(syn::Error::into_compile_error)
        .into()
}

fn expand_derive(input: &DeriveInput) -> syn::Result<TokenStream2> {
    let struct_name = &input.ident;
    let mut base: Option<Path> = None;
    let mut class_name: Option<String> = None;
    let mut is_tool = false;

    for attr in &input.attrs {
        if !attr.path().is_ident("script") {
            continue;
        }
        // A bare #[script] has no options: syn only reads the list form.
        if let syn::Meta::Path(_) = &attr.meta {
            continue;
        }
        attr.parse_nested_meta(|meta| {
            if meta.path.is_ident("base") {
                base = Some(meta.value()?.parse()?);
            } else if meta.path.is_ident("tool") {
                is_tool = true;
            } else if meta.path.is_ident("name") {
                let value: LitStr = meta.value()?.parse()?;
                class_name = Some(value.value());
            } else {
                return Err(meta.error(
                    "unknown option: expected base = YourNode, tool or name = \"...\"",
                ));
            }
            Ok(())
        })?;
    }

    let Some(base) = base else {
        return Err(syn::Error::new_spanned(
            struct_name,
            "add #[script(base = YourBaseNode)] to say which engine class this script extends",
        ));
    };
    let base_name = base
        .segments
        .last()
        .map(|segment| segment.ident.to_string())
        .unwrap_or_default();
    let class_name = class_name.unwrap_or_else(|| struct_name.to_string());

    let fields = match &input.data {
        Data::Struct(data) => match &data.fields {
            Fields::Named(named) => &named.named,
            _ => {
                return Err(syn::Error::new_spanned(
                    struct_name,
                    "RustScript needs a struct with named fields",
                ));
            }
        },
        _ => {
            return Err(syn::Error::new_spanned(
                struct_name,
                "RustScript can only be derived for structs",
            ));
        }
    };

    let mut inits = Vec::new();
    let mut properties = Vec::new();
    let mut getters = Vec::new();
    let mut setters = Vec::new();
    let mut reverts = Vec::new();
    let mut has_owner = false;

    for field in fields {
        let Some(ident) = &field.ident else {
            continue;
        };
        let ty = &field.ty;
        let name = ident.to_string();

        let mut export = false;
        let mut default: Option<Expr> = None;
        let mut hint: Option<TokenStream2> = None;
        let mut storage = false;
        for attr in &field.attrs {
            if !attr.path().is_ident("export") {
                continue;
            }
            export = true;
            // #[export] on its own is the common case: nothing to parse then,
            // and syn only accepts the list form through parse_nested_meta.
            if let syn::Meta::Path(_) = &attr.meta {
                continue;
            }
            if let syn::Meta::NameValue(_) = &attr.meta {
                return Err(syn::Error::new_spanned(
                    attr,
                    "use #[export] or #[export(default = ..., range = (min, max), enum = [...], ...)]",
                ));
            }
            attr.parse_nested_meta(|meta| {
                if meta.path.is_ident("default") {
                    default = Some(meta.value()?.parse()?);
                    return Ok(());
                }
                if meta.path.is_ident("storage") {
                    if hint.is_some() {
                        return Err(meta.error("storage cannot be combined with a hint"));
                    }
                    storage = true;
                    return Ok(());
                }

                let function = if meta.path.is_ident("range") {
                    let value: Expr = meta.value()?.parse()?;
                    let Expr::Tuple(tuple) = value else {
                        return Err(meta.error("range expects a tuple, e.g. #[export(range = (0.0, 10.0))]"));
                    };
                    if tuple.elems.len() < 2 || tuple.elems.len() > 3 {
                        return Err(meta.error("range expects (minimum, maximum) or (minimum, maximum, step)"));
                    }
                    let min = &tuple.elems[0];
                    let max = &tuple.elems[1];
                    let step = match tuple.elems.get(2) {
                        Some(step) => quote!(::core::option::Option::Some(#step)),
                        None => quote!(::core::option::Option::None),
                    };
                    quote! {
                        ::godot::register::property::export_fns::export_range(
                            #min, #max, #step, false, false, false, false, false, false,
                            ::core::option::Option::None,
                        )
                    }
                } else if meta.path.is_ident("enum") || meta.path.is_ident("flags") {
                    let is_flags = meta.path.is_ident("flags");
                    let value: Expr = meta.value()?.parse()?;
                    let Expr::Array(array) = value else {
                        return Err(meta.error("expected a list of names, e.g. enum = [\"Low\", \"High\"]"));
                    };
                    let names: Vec<&Expr> = array.elems.iter().collect();
                    if is_flags {
                        quote!(::godot::register::property::export_fns::export_flags(&[#((#names, ::core::option::Option::None)),*]))
                    } else {
                        quote!(::godot::register::property::export_fns::export_enum(&[#((#names, ::core::option::Option::None)),*]))
                    }
                } else if meta.path.is_ident("file") || meta.path.is_ident("global_file") {
                    let is_global = meta.path.is_ident("global_file");
                    let filter: LitStr = meta.value()?.parse()?;
                    quote!(::godot::register::property::export_fns::export_file_or_dir::<#ty>(true, #is_global, #filter))
                } else if meta.path.is_ident("dir") {
                    quote!(::godot::register::property::export_fns::export_file_or_dir::<#ty>(false, false, ""))
                } else if meta.path.is_ident("node") {
                    let allowed: LitStr = meta.value()?.parse()?;
                    quote!(::godot::register::property::export_fns::export_node_path::<#ty>(&[#allowed]))
                } else if meta.path.is_ident("placeholder") {
                    let text: LitStr = meta.value()?.parse()?;
                    quote!(::godot::register::property::export_fns::export_placeholder(#text))
                } else if meta.path.is_ident("multiline") {
                    quote!(::godot::register::property::export_fns::export_multiline())
                } else if meta.path.is_ident("color_no_alpha") {
                    quote!(::godot::register::property::export_fns::export_color_no_alpha())
                } else if meta.path.is_ident("exp_easing") {
                    quote!(::godot::register::property::export_fns::export_exp_easing(false, false))
                } else {
                    return Err(meta.error(
                        "unknown option: expected default, range, enum, flags, file, global_file, dir, node, placeholder, multiline, color_no_alpha, storage or exp_easing",
                    ));
                };

                if hint.is_some() {
                    return Err(meta.error("a property can only have one hint"));
                }
                hint = Some(quote!(.with_hint_info(#function)));
                Ok(())
            })?;
        }

        if name == "owner" {
            has_owner = true;
            inits.push(quote!(#ident: owner));
        } else if let Some(default) = &default {
            inits.push(quote!(#ident: #default));
        } else {
            inits.push(quote!(#ident: ::core::default::Default::default()));
        }

        if export {
            // `#[export(storage)]` is a usage flag, not a hint: the property is saved
            // with the scene but stays out of the inspector.
            properties.push(if storage {
                quote! {{
                    let mut info = ::godot::register::info::PropertyInfo::new_export::<#ty>(#name);
                    info.usage = ::godot::register::info::PropertyUsageFlags::STORAGE;
                    info
                }}
            } else {
                quote!(::godot::register::info::PropertyInfo::new_export::<#ty>(#name) #hint)
            });
            getters.push(quote! {
                #name => ::core::option::Option::Some(::godot::meta::ToGodot::to_variant(&self.#ident)),
            });
            // The owner field is a Gd<T>, which has no Default: leave its revert
            // value unset rather than failing to compile.
            if name != "owner" {
                let revert = if let Some(default) = &default {
                    quote!(#default)
                } else {
                    quote!(<#ty as ::core::default::Default>::default())
                };
                // The field type pins the expression: `default = 5` has to become
                // an i64 before it can be turned into a Variant.
                reverts.push(quote! {
                    #name => ::core::option::Option::Some({
                        let value: #ty = #revert;
                        ::godot::meta::ToGodot::to_variant(&value)
                    }),
                });
            }
            setters.push(quote! {
                #name => {
                    self.#ident = ::godot::meta::FromGodot::from_variant(value);
                    true
                }
            });
        }
    }

    let owner_setup = if has_owner {
        quote!()
    } else {
        quote!(let _ = owner;)
    };

    Ok(quote! {
        impl ::godot_script::RustScript for #struct_name {
            type Base = #base;
            const CLASS_NAME: &'static str = #class_name;
            const BASE_NAME: &'static str = #base_name;
            const IS_TOOL: bool = #is_tool;

            fn new(owner: ::godot::obj::Gd<Self::Base>) -> Self {
                #owner_setup
                Self { #(#inits),* }
            }

            fn properties() -> ::std::vec::Vec<::godot::register::info::PropertyInfo> {
                ::std::vec![#(#properties),*]
            }

            fn get_property(
                &self,
                name: &str,
            ) -> ::core::option::Option<::godot::builtin::Variant> {
                match name {
                    #(#getters)*
                    _ => ::core::option::Option::None,
                }
            }

            fn property_default(
                name: &str,
            ) -> ::core::option::Option<::godot::builtin::Variant> {
                match name {
                    #(#reverts)*
                    _ => ::core::option::Option::None,
                }
            }

            fn set_property(&mut self, name: &str, value: &::godot::builtin::Variant) -> bool {
                match name {
                    #(#setters)*
                    _ => false,
                }
            }

            fn methods() -> ::std::vec::Vec<::godot_script::ScriptMethod> {
                <Self as ::godot_script::ScriptApi>::api_methods()
            }

            fn call_method(
                &mut self,
                name: &str,
                args: &[&::godot::builtin::Variant],
            ) -> ::core::result::Result<
                ::godot::builtin::Variant,
                ::godot::meta::error::CallErrorType,
            > {
                <Self as ::godot_script::ScriptApi>::api_call(self, name, args)
            }

            fn signals() -> ::std::vec::Vec<::godot_script::ScriptSignal> {
                <Self as ::godot_script::ScriptApi>::api_signals()
            }

            fn ready(&mut self) {
                <Self as ::godot_script::ScriptApi>::api_ready(self)
            }

            fn process(&mut self, delta: f64) {
                <Self as ::godot_script::ScriptApi>::api_process(self, delta)
            }

            fn physics_process(&mut self, delta: f64) {
                <Self as ::godot_script::ScriptApi>::api_physics_process(self, delta)
            }

            fn enter_tree(&mut self) {
                <Self as ::godot_script::ScriptApi>::api_enter_tree(self)
            }

            fn exit_tree(&mut self) {
                <Self as ::godot_script::ScriptApi>::api_exit_tree(self)
            }
        }
    })
}

/// Collects what a script implements: lifecycle hooks, `#[func]` methods and
/// `#[signal]` declarations.
///
/// Write it on an inherent `impl` block next to the struct:
///
/// ```ignore
/// #[godot_script_api]
/// impl Player {
///     fn ready(&mut self) {}                       // lifecycle hook
///     fn process(&mut self, delta: f64) {}         // lifecycle hook
///
///     #[func]
///     fn jump(&mut self, height: f32) -> f32 { self.speed + height }
///
///     #[signal]
///     fn jumped(height: f32) {}
/// }
/// ```
///
/// The methods without `#[func]` stay private to Rust; only annotated ones are
/// visible to GDScript and the editor.
#[proc_macro_attribute]
pub fn godot_script_api(_attributes: TokenStream, item: TokenStream) -> TokenStream {
    let item = parse_macro_input!(item as ItemImpl);
    expand_api(&item)
        .unwrap_or_else(syn::Error::into_compile_error)
        .into()
}

/// The lifecycle names the trait knows about, with the shape the engine calls.
const LIFECYCLE_HOOKS: [(&str, bool); 5] = [
    ("ready", false),
    ("process", true),
    ("physics_process", true),
    ("enter_tree", false),
    ("exit_tree", false),
];

fn expand_api(item: &ItemImpl) -> syn::Result<TokenStream2> {
    let self_ty = &item.self_ty;
    let mut kept = Vec::new();
    let mut method_infos = Vec::new();
    let mut dispatches = Vec::new();
    let mut signal_infos = Vec::new();
    let mut hook_impls = Vec::new();

    for impl_item in &item.items {
        let ImplItem::Fn(method) = impl_item else {
            kept.push(quote!(#impl_item));
            continue;
        };
        let ident = &method.sig.ident;
        let name = ident.to_string();
        let is_func = method.attrs.iter().any(|attr| attr.path().is_ident("func"));
        let is_signal = method
            .attrs
            .iter()
            .any(|attr| attr.path().is_ident("signal"));

        // These markers are ours, not real attributes: strip them before the
        // compiler sees the impl block again.
        let mut cleaned = method.clone();
        cleaned
            .attrs
            .retain(|attr| !attr.path().is_ident("func") && !attr.path().is_ident("signal"));
        for input in &mut cleaned.sig.inputs {
            if let FnArg::Typed(pat_type) = input {
                pat_type.attrs.retain(|attr| !attr.path().is_ident("opt"));
            }
        }

        let arguments = method_arguments(method)?;
        let argument_infos = arguments.iter().map(|argument| {
            let arg_name = &argument.name;
            let arg_type = &argument.ty;
            quote!(.arg_of::<#arg_type>(#arg_name))
        });
        let default_infos = arguments.iter().filter_map(|argument| {
            let default = argument.default.as_ref()?;
            Some(quote!(.default_value(#default)))
        });

        if is_signal {
            signal_infos.push(quote! {
                ::godot_script::ScriptSignal::new(#name) #(#argument_infos)*
            });
            continue;
        }

        if let Some((hook, has_delta)) = LIFECYCLE_HOOKS.iter().find(|(hook, _)| *hook == name) {
            let api_name = format_ident!("api_{}", hook);
            hook_impls.push(if *has_delta {
                quote! {
                    fn #api_name(&mut self, delta: f64) {
                        Self::#ident(self, delta)
                    }
                }
            } else {
                quote! {
                    fn #api_name(&mut self) {
                        Self::#ident(self)
                    }
                }
            });
        }

        if is_func {
            let returns = match &method.sig.output {
                ReturnType::Default => quote!(),
                ReturnType::Type(_, ty) if is_unit(ty) => quote!(),
                ReturnType::Type(_, ty) => quote!(.returns_of::<#ty>()),
            };
            method_infos.push(quote! {
                ::godot_script::ScriptMethod::new(#name) #(#argument_infos)* #(#default_infos)* #returns
            });

            let mut bindings = Vec::new();
            let mut call_arguments = Vec::new();
            for (index, argument) in arguments.iter().enumerate() {
                let binding = format_ident!("__arg{}", index);
                let argument_type = &argument.ty;
                match &argument.default {
                    // #[opt(default = ...)]: use the default when the caller leaves
                    // the argument out, exactly like a GDScript default parameter.
                    Some(default) => bindings.push(quote! {
                        let #binding: #argument_type = match args.get(#index) {
                            ::core::option::Option::Some(value) => {
                                ::godot_script::argument_value(value)?
                            }
                            ::core::option::Option::None => #default,
                        };
                    }),
                    None => bindings.push(quote! {
                        let #binding: #argument_type = ::godot_script::call_argument(args, #index)?;
                    }),
                }
                call_arguments.push(quote!(#binding));
            }
            let call = quote!(Self::#ident(self #(, #call_arguments)*));
            let body = match &method.sig.output {
                ReturnType::Default => quote! {
                    #(#bindings)*
                    #call;
                    ::core::result::Result::Ok(::godot::builtin::Variant::nil())
                },
                ReturnType::Type(_, ty) if is_unit(ty) => quote! {
                    #(#bindings)*
                    #call;
                    ::core::result::Result::Ok(::godot::builtin::Variant::nil())
                },
                ReturnType::Type(_, _) => quote! {
                    #(#bindings)*
                    let __result = #call;
                    ::core::result::Result::Ok(::godot::meta::ToGodot::to_variant(&__result))
                },
            };
            dispatches.push(quote! {
                #name => {
                    #body
                }
            });
        }

        kept.push(quote!(#cleaned));
    }

    Ok(quote! {
        impl #self_ty {
            #(#kept)*
        }

        impl ::godot_script::ScriptApi for #self_ty {
            fn api_methods() -> ::std::vec::Vec<::godot_script::ScriptMethod> {
                ::std::vec![#(#method_infos),*]
            }

            fn api_call(
                &mut self,
                name: &str,
                args: &[&::godot::builtin::Variant],
            ) -> ::core::result::Result<
                ::godot::builtin::Variant,
                ::godot::meta::error::CallErrorType,
            > {
                match name {
                    #(#dispatches)*
                    _ => ::core::result::Result::Err(
                        ::godot::meta::error::CallErrorType::InvalidMethod,
                    ),
                }
            }

            fn api_signals() -> ::std::vec::Vec<::godot_script::ScriptSignal> {
                ::std::vec![#(#signal_infos),*]
            }

            #(#hook_impls)*
        }
    })
}

/// One parameter of a `#[func]` method.
struct MethodArgument {
    name: String,
    ty: Type,
    /// Set by `#[opt(default = ...)]`: the value used when the caller omits it.
    default: Option<Expr>,
}

/// The named parameters of a method, skipping the receiver.
fn method_arguments(method: &ImplItemFn) -> syn::Result<Vec<MethodArgument>> {
    let mut arguments = Vec::new();
    let mut optional_seen = false;
    for input in &method.sig.inputs {
        let FnArg::Typed(pat_type) = input else {
            continue;
        };
        let name = match &*pat_type.pat {
            syn::Pat::Ident(pat_ident) => pat_ident.ident.to_string(),
            other => {
                return Err(syn::Error::new_spanned(
                    other,
                    "script method parameters need plain names, e.g. height: f32",
                ));
            }
        };

        let mut default: Option<Expr> = None;
        for attr in &pat_type.attrs {
            if !attr.path().is_ident("opt") {
                continue;
            }
            attr.parse_nested_meta(|meta| {
                if meta.path.is_ident("default") {
                    default = Some(meta.value()?.parse()?);
                    Ok(())
                } else {
                    Err(meta.error("expected #[opt(default = ...)]"))
                }
            })?;
        }
        match (&default, optional_seen) {
            (Some(_), _) => optional_seen = true,
            (None, true) => {
                return Err(syn::Error::new_spanned(
                    pat_type,
                    "parameters with #[opt(default = ...)] must come last",
                ));
            }
            (None, false) => {}
        }

        arguments.push(MethodArgument {
            name,
            ty: (*pat_type.ty).clone(),
            default,
        });
    }
    Ok(arguments)
}

fn is_unit(ty: &Type) -> bool {
    matches!(ty, Type::Tuple(tuple) if tuple.elems.is_empty())
}
