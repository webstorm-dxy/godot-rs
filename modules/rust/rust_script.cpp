#include "rust_script.h"

#include "rust_language.h"
#include "rust_script_registry.h"

#include "core/io/file_access.h"
#include "core/object/class_db.h"

// Defined in core/extension/gdextension_interface.cpp. It performs the same cast
// the engine itself uses when attaching a GDExtension-created script instance
// to an object (see `gdextension_object_set_script_instance`).
ScriptInstance *gdextension_script_instance_wrap(void *p_instance);

typedef void *(*RustScriptCreateFunc)(int64_t p_owner_id, int64_t p_script_id, int64_t p_language_id);

static bool _rust_script_lookup(const String &p_path, RustScriptRegistry::ScriptType &r_type) {
	RustScriptRegistry *registry = RustScriptRegistry::get_singleton();
	return registry != nullptr && registry->get_script_type(p_path, r_type);
}

void RustScript::_bind_methods() {
}

bool RustScript::can_instantiate() const {
	RustScriptRegistry::ScriptType type;
	return valid && _rust_script_lookup(path_cache, type);
}

StringName RustScript::get_global_name() const {
	RustScriptRegistry::ScriptType type;
	if (!_rust_script_lookup(path_cache, type)) {
		return StringName();
	}
	return StringName(type.class_name);
}

StringName RustScript::get_instance_base_type() const {
	RustScriptRegistry::ScriptType type;
	if (!_rust_script_lookup(path_cache, type)) {
		return StringName();
	}
	return StringName(type.base);
}

bool RustScript::is_tool() const {
	RustScriptRegistry::ScriptType type;
	if (!_rust_script_lookup(path_cache, type)) {
		return false;
	}
	return type.is_tool;
}

#ifdef TOOLS_ENABLED
StringName RustScript::get_doc_class_name() const {
	return get_global_name();
}
#endif

ScriptInstance *RustScript::instance_create(Object *p_this) {
	RustScriptRegistry::ScriptType type;
	if (!_rust_script_lookup(path_cache, type) || type.create_fn == 0) {
		// Not built yet (or the script does not define a class): the editor uses a
		// placeholder, a running game simply has no script instance.
		return nullptr;
	}

	if (!ClassDB::is_parent_class(p_this->get_class_name(), type.base)) {
		ERR_PRINT(vformat("Rust: script '%s' extends '%s', so it cannot be attached to an object of type '%s'.", path_cache, type.base, p_this->get_class_name()));
		return nullptr;
	}

	RustScriptCreateFunc create = (RustScriptCreateFunc)(intptr_t)type.create_fn;
	void *instance = create((int64_t)p_this->get_instance_id(), (int64_t)get_instance_id(), (int64_t)RustLanguage::get_singleton()->get_instance_id());
	if (instance == nullptr) {
		ERR_PRINT(vformat("Rust: failed to create a script instance for '%s'.", path_cache));
		return nullptr;
	}

	return gdextension_script_instance_wrap(instance);
}

PlaceHolderScriptInstance *RustScript::placeholder_instance_create(Object *p_this) {
#ifdef TOOLS_ENABLED
	// Exporting property lists for unbuilt projects would require a descriptor
	// cache; until then placeholders simply carry no properties.
	return memnew(PlaceHolderScriptInstance(RustLanguage::get_singleton(), Ref<Script>(this), p_this));
#else
	return nullptr;
#endif
}

Error RustScript::reload(bool p_keep_state) {
	if (path_cache.is_empty()) {
		valid = false;
		return ERR_UNCONFIGURED;
	}

	Error err = load_source_code(path_cache);
	valid = (err == OK);
	return err;
}

Error RustScript::load_source_code(const String &p_path) {
	Error err;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ, &err);
	if (err != OK) {
		source_code = String();
		return err;
	}

	source_code = file->get_as_utf8_string();
	path_cache = p_path;
	return OK;
}

ScriptLanguage *RustScript::get_language() const {
	return RustLanguage::get_singleton();
}

RustScript::RustScript() {
}
