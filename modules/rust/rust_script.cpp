#include "rust_script.h"

#include "rust_language.h"
#include "rust_script_registry.h"

#include "core/config/engine.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/object/object.h"
#include "scene/main/node.h"

// Defined in core/extension/gdextension_interface.cpp. It performs the same cast
// the engine itself uses when attaching a GDExtension-created script instance
// to an object (see `gdextension_object_set_script_instance`).
ScriptInstance *gdextension_script_instance_wrap(void *p_instance);

typedef void *(*RustScriptCreateFunc)(int64_t p_owner_id, int64_t p_script_id, int64_t p_language_id);

Mutex RustScript::tracked_mutex;
HashMap<ObjectID, String> RustScript::tracked_instances;

void RustScript::_track_instance(Object *p_object, const String &p_path) {
	if (p_object == nullptr || p_path.is_empty()) {
		return;
	}
	MutexLock lock(tracked_mutex);
	tracked_instances[p_object->get_instance_id()] = p_path;
}

static bool _rust_script_lookup(const String &p_path, RustScriptRegistry::ScriptType &r_type) {
	RustScriptRegistry *registry = RustScriptRegistry::get_singleton();
	return registry != nullptr && registry->get_script_type(p_path, r_type);
}

void RustScript::_bind_methods() {
}

bool RustScript::can_instantiate() const {
	RustScriptRegistry::ScriptType type;
	// A descriptor loaded from the script cache has no instance factory, so the
	// script only becomes instantiable once its library has been built and loaded.
	if (!valid || !_rust_script_lookup(path_cache, type) || type.create_fn == 0) {
		return false;
	}
#ifdef TOOLS_ENABLED
	// The same rule GDScript follows: inside the editor only tool scripts run, so
	// a plain script gets a placeholder instance instead of a live one.
	if (!type.is_tool && (!ScriptServer::is_scripting_enabled() || Engine::get_singleton()->is_recovery_mode_hint())) {
		return false;
	}
#endif
	return true;
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

bool RustScript::has_method(const StringName &p_method) const {
	RustScriptRegistry::ScriptType type;
	if (!_rust_script_lookup(path_cache, type)) {
		return false;
	}
	return type.find_method(p_method).name != StringName();
}

MethodInfo RustScript::get_method_info(const StringName &p_method) const {
	RustScriptRegistry::ScriptType type;
	if (!_rust_script_lookup(path_cache, type)) {
		return MethodInfo();
	}
	return type.find_method(p_method);
}

void RustScript::get_script_method_list(List<MethodInfo> *p_list) const {
	RustScriptRegistry::ScriptType type;
	if (!_rust_script_lookup(path_cache, type)) {
		return;
	}
	for (const MethodInfo &method : type.methods) {
		p_list->push_back(method);
	}
}

void RustScript::get_script_property_list(List<PropertyInfo> *p_list) const {
	RustScriptRegistry::ScriptType type;
	if (!_rust_script_lookup(path_cache, type)) {
		return;
	}
	for (const PropertyInfo &property : type.properties) {
		p_list->push_back(property);
	}
}

bool RustScript::has_script_signal(const StringName &p_signal) const {
	RustScriptRegistry::ScriptType type;
	if (!_rust_script_lookup(path_cache, type)) {
		return false;
	}
	return type.find_signal(p_signal).name != StringName();
}

void RustScript::get_script_signal_list(List<MethodInfo> *r_signals) const {
	RustScriptRegistry::ScriptType type;
	if (!_rust_script_lookup(path_cache, type)) {
		return;
	}
	for (const MethodInfo &signal : type.signals) {
		r_signals->push_back(signal);
	}
}

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

	// Remember the object: when a new library is loaded, this instance has to be
	// re-created with it (and its property values carried over).
	_track_instance(p_this, path_cache);

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
	// The placeholder carries the properties known so far (the ones the library
	// registered last time), so the inspector can show a script that has not been
	// built yet instead of an empty list.
	PlaceHolderScriptInstance *placeholder = memnew(PlaceHolderScriptInstance(RustLanguage::get_singleton(), Ref<Script>(this), p_this));
	RustScriptRegistry::ScriptType type;
	if (_rust_script_lookup(path_cache, type)) {
		List<PropertyInfo> properties;
		for (const PropertyInfo &property : type.properties) {
			properties.push_back(property);
		}
		placeholder->update(properties, HashMap<StringName, Variant>());
	}
	_track_instance(p_this, path_cache);
	return placeholder;
#else
	return nullptr;
#endif
}

void RustScript::migrate_instances() {
	Vector<ObjectID> tracked;
	{
		MutexLock lock(tracked_mutex);
		for (const KeyValue<ObjectID, String> &E : tracked_instances) {
			tracked.push_back(E.key);
		}
	}

	Vector<ObjectID> stale;
	for (const ObjectID &id : tracked) {
		Object *object = ObjectDB::get_instance(id);
		Ref<Script> script_ref;
		if (object != nullptr) {
			script_ref = object->get_script();
		}
		RustScript *script = Object::cast_to<RustScript>(script_ref.ptr());
		// Gone, re-scripted, or not instantiable here (no library yet, or a plain
		// script in the editor, which uses a placeholder): stop tracking it.
		if (object == nullptr || script == nullptr || !script->can_instantiate()) {
			stale.push_back(id);
			continue;
		}

		ScriptInstance *current = object->get_script_instance();
		if (current == nullptr) {
			stale.push_back(id);
			continue;
		}

		// Same dance the editor does when a script changes: keep the values of the
		// exported properties, then let the fresh instance (created by the library
		// that was just loaded) take over.
		const bool was_placeholder = current->is_placeholder();
		List<Pair<StringName, Variant>> state;
		current->get_property_state(state);

		ScriptInstance *fresh = script->instance_create(object);
		if (fresh == nullptr) {
			continue;
		}
		for (const Pair<StringName, Variant> &E : state) {
			fresh->set(E.first, E.second);
		}
		object->set_script_instance(fresh);
		object->notify_property_list_changed();

		Node *node = Object::cast_to<Node>(object);
		if (node == nullptr) {
			continue;
		}
		if (was_placeholder) {
			// The script never ran on this node (the scene was opened before the
			// library existed), so give it the startup notification it missed; the
			// engine then also enables processing for the new instance.
			node->notification(Node::NOTIFICATION_READY);
		} else {
			// Keep the process flags in sync with the new code without re-running
			// the script's own startup work.
			if (fresh->has_method(StringName("_process"))) {
				node->set_process(true);
			}
			if (fresh->has_method(StringName("_physics_process"))) {
				node->set_physics_process(true);
			}
		}
	}

	if (!stale.is_empty()) {
		MutexLock lock(tracked_mutex);
		for (const ObjectID &id : stale) {
			tracked_instances.erase(id);
		}
	}
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
