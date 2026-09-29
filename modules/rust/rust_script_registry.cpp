#include "rust_script_registry.h"

#include "core/object/class_db.h"
#include "core/variant/variant.h"

RustScriptRegistry *RustScriptRegistry::singleton = nullptr;

RustScriptRegistry *RustScriptRegistry::get_singleton() {
	return singleton;
}

void RustScriptRegistry::create_singleton() {
	if (singleton == nullptr) {
		singleton = memnew(RustScriptRegistry);
	}
}

void RustScriptRegistry::destroy_singleton() {
	if (singleton != nullptr) {
		memdelete(singleton);
		singleton = nullptr;
	}
}

void RustScriptRegistry::register_script_type(const String &p_path, const String &p_class_name, const String &p_base, bool p_is_tool, int64_t p_create_fn) {
	ERR_FAIL_COND_MSG(p_path.is_empty(), "Rust: script type without a source path.");
	ERR_FAIL_COND_MSG(p_class_name.is_empty(), "Rust: script type without a class name.");

	ScriptType type;
	type.path = p_path;
	type.class_name = p_class_name;
	type.base = p_base;
	type.is_tool = p_is_tool;
	type.create_fn = p_create_fn;

	MutexLock lock(mutex);
	by_path[p_path] = type;

	print_verbose(vformat("Rust: registered script '%s' (%s, base %s).", p_path, p_class_name, p_base));
}

void RustScriptRegistry::clear_script_types() {
	MutexLock lock(mutex);
	by_path.clear();
}

bool RustScriptRegistry::has_script_type(const String &p_path) const {
	MutexLock lock(mutex);
	return by_path.has(p_path);
}

bool RustScriptRegistry::get_script_type(const String &p_path, ScriptType &r_type) const {
	MutexLock lock(mutex);
	const ScriptType *type = by_path.getptr(p_path);
	if (type == nullptr) {
		return false;
	}
	r_type = *type;
	return true;
}

PackedStringArray RustScriptRegistry::get_registered_paths() const {
	MutexLock lock(mutex);
	PackedStringArray paths;
	for (const KeyValue<String, ScriptType> &E : by_path) {
		paths.push_back(E.key);
	}
	return paths;
}

void RustScriptRegistry::_bind_methods() {
	ClassDB::bind_method(D_METHOD("register_script_type", "path", "class_name", "base", "is_tool", "create_fn"), &RustScriptRegistry::register_script_type);
	ClassDB::bind_method(D_METHOD("clear_script_types"), &RustScriptRegistry::clear_script_types);
	ClassDB::bind_method(D_METHOD("has_script_type", "path"), &RustScriptRegistry::has_script_type);
	ClassDB::bind_method(D_METHOD("get_registered_paths"), &RustScriptRegistry::get_registered_paths);
}

RustScriptRegistry::RustScriptRegistry() {
}

RustScriptRegistry::~RustScriptRegistry() {
}
