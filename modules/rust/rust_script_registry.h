#pragma once

#include "core/object/object.h"
#include "core/os/mutex.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"

// Registry of Rust script types (see modules/rust/support/godot-script).
//
// A `godot_script::register_script!` call on the Rust side looks this object up
// through the engine singleton `RustScriptRegistry` and hands over:
//   * the res:// path of the .rs file it was compiled from,
//   * the script class name and the Godot base class it extends,
//   * whether it is a tool script,
//   * a function pointer that creates the script instance for a given object,
//   * a descriptor with the exported properties, callable methods and signals.
//
// The instance pointer returned by that function is an engine
// `GDExtensionScriptInstance` created by `script_instance_create3`, which is
// exactly what `Script::instance_create()` has to return.
class RustScriptRegistry : public Object {
	GDCLASS(RustScriptRegistry, Object);

public:
	struct ScriptType {
		String path;
		String class_name;
		String base;
		bool is_tool = false;
		int64_t create_fn = 0;
		Vector<PropertyInfo> properties;
		Vector<MethodInfo> methods;
		Vector<MethodInfo> signals;

		// The method or signal called `p_name`, or an empty MethodInfo.
		MethodInfo find_method(const StringName &p_name) const;
		MethodInfo find_signal(const StringName &p_name) const;
	};

	static RustScriptRegistry *get_singleton();
	static void create_singleton();
	static void destroy_singleton();

	// Registers a script whose library is not loaded (`.godot/rust/script_cache.json`).
	// The entry carries the descriptor but no instance factory.
	void register_script_descriptor(const String &p_path, const Dictionary &p_data);

	// The descriptor of one script, in the same shape `register_script_type` takes.
	// Empty when the path is unknown.
	Dictionary describe_script(const String &p_path) const;

	// Called from Rust; `p_create_fn` is a `extern "C" fn(i64, i64, i64) -> void *`.
	// The signature may not change: compiled script libraries look the method up
	// by a hash of it, and the engine has no compatibility fallback for module
	// classes. Descriptors therefore travel in `set_script_descriptor()`.
	void register_script_type(const String &p_path, const String &p_class_name, const String &p_base, bool p_is_tool, int64_t p_create_fn);
	// Called right after it, with the properties/methods/signals of the script.
	void set_script_descriptor(const String &p_path, const Dictionary &p_descriptor);
	void clear_script_types();
	bool has_script_type(const String &p_path) const;
	bool get_script_type(const String &p_path, ScriptType &r_type) const;
	PackedStringArray get_registered_paths() const;

	RustScriptRegistry();
	~RustScriptRegistry();

protected:
	static void _bind_methods();

private:
	static RustScriptRegistry *singleton;

	mutable Mutex mutex;
	HashMap<String, ScriptType> by_path;
};
