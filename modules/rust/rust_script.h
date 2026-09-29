#pragma once

#include "core/object/script_language.h"

// Resource representation of a single .rs file.
//
// The file itself only carries the source code; the script *class* it defines
// (name, base class, exported properties, lifecycle callbacks) is registered by
// the Rust side through `RustScriptRegistry`. When the library has not been
// built yet the registry has no entry, `can_instantiate()` stays false and the
// editor falls back to a placeholder instance.
class RustScript : public Script {
	GDCLASS(RustScript, Script);

	String source_code;
	String path_cache;
	bool valid = false;

protected:
	static void _bind_methods();
	virtual bool editor_can_reload_from_file() override { return true; }

public:
	virtual bool can_instantiate() const override;
	virtual Ref<Script> get_base_script() const override { return Ref<Script>(); }
	virtual StringName get_global_name() const override;
	virtual bool inherits_script(const Ref<Script> &p_script) const override { return false; }
	virtual StringName get_instance_base_type() const override;
	virtual ScriptInstance *instance_create(Object *p_this) override;
	virtual PlaceHolderScriptInstance *placeholder_instance_create(Object *p_this) override;

	virtual bool has_source_code() const override { return !source_code.is_empty(); }
	virtual String get_source_code() const override { return source_code; }
	virtual void set_source_code(const String &p_code) override { source_code = p_code; }
	virtual Error reload(bool p_keep_state = false) override;

#ifdef TOOLS_ENABLED
	virtual StringName get_doc_class_name() const override;
	virtual Vector<DocData::ClassDoc> get_documentation() const override { return Vector<DocData::ClassDoc>(); }
	virtual String get_class_icon_path() const override { return String(); }
#endif

	virtual bool has_method(const StringName &p_method) const override { return false; }
	virtual MethodInfo get_method_info(const StringName &p_method) const override { return MethodInfo(); }
	virtual bool is_tool() const override;
	virtual bool is_valid() const override { return valid; }
	virtual bool is_abstract() const override { return false; }
	virtual ScriptLanguage *get_language() const override;

	virtual bool has_script_signal(const StringName &p_signal) const override { return false; }
	virtual void get_script_signal_list(List<MethodInfo> *r_signals) const override {}
	virtual bool get_property_default_value(const StringName &p_property, Variant &r_value) const override { return false; }
	virtual void get_script_method_list(List<MethodInfo> *p_list) const override {}
	virtual void get_script_property_list(List<PropertyInfo> *p_list) const override {}
	virtual const Variant get_rpc_config() const override { return Variant(); }

	Error load_source_code(const String &p_path);

	RustScript();
};
