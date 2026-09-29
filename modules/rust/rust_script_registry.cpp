#include "rust_script_registry.h"

#include "core/object/class_db.h"
#include "core/variant/variant.h"

RustScriptRegistry *RustScriptRegistry::singleton = nullptr;

// One entry of the descriptor dictionary the Rust side sends: either a property,
// or one parameter of a method/signal.
static PropertyInfo _descriptor_property(const Dictionary &p_dict) {
	PropertyInfo info;
	info.name = p_dict.get("name", String());
	const int type = (int)p_dict.get("type", (int)Variant::NIL);
	if (type >= 0 && type < Variant::VARIANT_MAX) {
		info.type = (Variant::Type)type;
	}
	info.class_name = p_dict.get("class_name", String());
	const int hint = (int)p_dict.get("hint", (int)PROPERTY_HINT_NONE);
	if (hint >= 0 && hint < PROPERTY_HINT_MAX) {
		info.hint = (PropertyHint)hint;
	}
	info.hint_string = p_dict.get("hint_string", String());
	info.usage = (uint32_t)(int64_t)p_dict.get("usage", (int64_t)PROPERTY_USAGE_DEFAULT);
	return info;
}

static MethodInfo _descriptor_method(const Dictionary &p_dict, const StringName &p_name, int p_id) {
	MethodInfo info;
	info.name = p_name;
	info.id = p_id;
	const int type = (int)p_dict.get("return_type", (int)Variant::NIL);
	if (type >= 0 && type < Variant::VARIANT_MAX) {
		info.return_val.type = (Variant::Type)type;
	}
	info.flags = (bool)p_dict.get("is_const", false) ? METHOD_FLAG_CONST : METHOD_FLAG_NORMAL;
	for (const Variant &argument : (Array)p_dict.get("args", Array())) {
		info.arguments.push_back(_descriptor_property(argument));
	}
	for (const Variant &value : (Array)p_dict.get("defaults", Array())) {
		info.default_arguments.push_back(value);
	}
	return info;
}

// Reads the properties/methods/signals part of a descriptor dictionary.
static void _fill_descriptor(RustScriptRegistry::ScriptType &r_type, const Dictionary &p_descriptor) {
	r_type.properties.clear();
	r_type.methods.clear();
	r_type.signals.clear();

	for (const Variant &entry : (Array)p_descriptor.get("properties", Array())) {
		r_type.properties.push_back(_descriptor_property(entry));
	}
	int id = 0;
	for (const Variant &entry : (Array)p_descriptor.get("methods", Array())) {
		const Dictionary method = entry;
		r_type.methods.push_back(_descriptor_method(method, method.get("name", String()), id++));
	}
	id = 0;
	for (const Variant &entry : (Array)p_descriptor.get("signals", Array())) {
		const Dictionary signal = entry;
		r_type.signals.push_back(_descriptor_method(signal, signal.get("name", String()), id++));
	}
}

// The inverse of _fill_descriptor, used to write the script cache.
static Dictionary _property_dictionary(const PropertyInfo &p_info) {
	Dictionary dict;
	dict["name"] = p_info.name;
	dict["type"] = (int)p_info.type;
	dict["class_name"] = p_info.class_name;
	dict["hint"] = (int)p_info.hint;
	dict["hint_string"] = p_info.hint_string;
	dict["usage"] = (int64_t)p_info.usage;
	return dict;
}

static Dictionary _method_dictionary(const MethodInfo &p_info) {
	Dictionary dict;
	dict["name"] = p_info.name;
	dict["return_type"] = (int)p_info.return_val.type;
	dict["is_const"] = (p_info.flags & METHOD_FLAG_CONST) != 0;
	Array args;
	for (const PropertyInfo &argument : p_info.arguments) {
		args.push_back(_property_dictionary(argument));
	}
	dict["args"] = args;
	dict["defaults"] = p_info.default_arguments;
	return dict;
}

MethodInfo RustScriptRegistry::ScriptType::find_method(const StringName &p_name) const {
	for (const MethodInfo &method : methods) {
		if (method.name == p_name) {
			return method;
		}
	}
	return MethodInfo();
}

MethodInfo RustScriptRegistry::ScriptType::find_signal(const StringName &p_name) const {
	for (const MethodInfo &signal : signals) {
		if (signal.name == p_name) {
			return signal;
		}
	}
	return MethodInfo();
}

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

void RustScriptRegistry::set_script_descriptor(const String &p_path, const Dictionary &p_descriptor) {
	if (p_path.is_empty()) {
		return;
	}

	MutexLock lock(mutex);
	ScriptType *type = by_path.getptr(p_path);
	if (type == nullptr) {
		// The registration call comes first; ignore a stray descriptor instead of
		// inventing an entry without an instance factory.
		return;
	}
	_fill_descriptor(*type, p_descriptor);
}

void RustScriptRegistry::register_script_descriptor(const String &p_path, const Dictionary &p_data) {
	ERR_FAIL_COND_MSG(p_path.is_empty(), "Rust: cached script without a source path.");

	ScriptType type;
	type.path = p_path;
	type.class_name = p_data.get("class_name", String());
	type.base = p_data.get("base", String());
	type.is_tool = p_data.get("is_tool", false);
	type.create_fn = 0;
	_fill_descriptor(type, p_data);

	MutexLock lock(mutex);
	const ScriptType *loaded = by_path.getptr(p_path);
	if (loaded != nullptr && loaded->create_fn != 0) {
		// The library knows more than the cache does; keep its entry.
		return;
	}
	by_path[p_path] = type;
}

Dictionary RustScriptRegistry::describe_script(const String &p_path) const {
	ScriptType type;
	if (!get_script_type(p_path, type)) {
		return Dictionary();
	}

	Dictionary data;
	data["class_name"] = type.class_name;
	data["base"] = type.base;
	data["is_tool"] = type.is_tool;
	Array properties;
	for (const PropertyInfo &property : type.properties) {
		properties.push_back(_property_dictionary(property));
	}
	data["properties"] = properties;
	Array methods;
	for (const MethodInfo &method : type.methods) {
		methods.push_back(_method_dictionary(method));
	}
	data["methods"] = methods;
	Array signals;
	for (const MethodInfo &signal : type.signals) {
		signals.push_back(_method_dictionary(signal));
	}
	data["signals"] = signals;
	return data;
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
	ClassDB::bind_method(D_METHOD("set_script_descriptor", "path", "descriptor"), &RustScriptRegistry::set_script_descriptor);
	ClassDB::bind_method(D_METHOD("clear_script_types"), &RustScriptRegistry::clear_script_types);
	ClassDB::bind_method(D_METHOD("has_script_type", "path"), &RustScriptRegistry::has_script_type);
	ClassDB::bind_method(D_METHOD("get_registered_paths"), &RustScriptRegistry::get_registered_paths);
}

RustScriptRegistry::RustScriptRegistry() {
}

RustScriptRegistry::~RustScriptRegistry() {
}
