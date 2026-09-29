#pragma once

#include "core/object/script_language.h"

class RustLanguage : public ScriptLanguage {
	GDCLASS(RustLanguage, ScriptLanguage);

	static RustLanguage *singleton;

public:
	static RustLanguage *get_singleton() { return singleton; }

	virtual String get_name() const override { return "Rust"; }
	virtual void init() override {}
	virtual String get_type() const override { return "RustScript"; }
	virtual String get_extension() const override { return "rs"; }
	virtual void finish() override {}

	virtual Vector<String> get_reserved_words() const override;
	virtual bool is_control_flow_keyword(const String &p_string) const override;
	virtual Vector<String> get_comment_delimiters() const override;
	virtual Vector<String> get_doc_comment_delimiters() const override;
	virtual Vector<String> get_string_delimiters() const override;

	virtual bool validate(const String &p_script, const String &p_path = "", List<String> *r_functions = nullptr, List<ScriptError> *r_errors = nullptr, List<Warning> *r_warnings = nullptr, HashSet<int> *r_safe_lines = nullptr) const override;
	virtual bool supports_builtin_mode() const override { return false; }
	virtual bool can_inherit_from_file() const override { return false; }
	virtual bool can_make_function() const override { return false; }
	virtual int find_function(const String &p_function, const String &p_code) const override { return -1; }
	virtual String make_function(const String &p_class, const String &p_name, const PackedStringArray &p_args) const override { return String(); }
	virtual ScriptNameCasing preferred_file_name_casing() const override { return SCRIPT_NAME_CASING_SNAKE_CASE; }
	virtual Ref<Script> make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const override;
	virtual String validate_path(const String &p_path) const override;

	virtual void auto_indent_code(String &p_code, int p_from_line, int p_to_line) const override {}
	virtual void add_global_constant(const StringName &p_variable, const Variant &p_value) override {}

	virtual String debug_get_error() const override { return String(); }
	virtual int debug_get_stack_level_count() const override { return 0; }
	virtual int debug_get_stack_level_line(int p_level) const override { return 0; }
	virtual String debug_get_stack_level_function(int p_level) const override { return String(); }
	virtual String debug_get_stack_level_source(int p_level) const override { return String(); }
	virtual void debug_get_stack_level_locals(int p_level, List<String> *p_locals, List<Variant> *p_values, int p_max_subitems = -1, int p_max_depth = -1) override {}
	virtual void debug_get_stack_level_members(int p_level, List<String> *p_members, List<Variant> *p_values, int p_max_subitems = -1, int p_max_depth = -1) override {}
	virtual void debug_get_globals(List<String> *p_globals, List<Variant> *p_values, int p_max_subitems = -1, int p_max_depth = -1) override {}
	virtual String debug_parse_stack_level_expression(int p_level, const String &p_expression, int p_max_subitems = -1, int p_max_depth = -1) override { return String(); }

	virtual void reload_all_scripts() override {}
	virtual void reload_scripts(const Array &p_scripts, bool p_soft_reload) override {}
	virtual void reload_tool_script(const Ref<Script> &p_script, bool p_soft_reload) override {}

	virtual void get_recognized_extensions(List<String> *p_extensions) const override;
	virtual void get_public_functions(List<MethodInfo> *p_functions) const override {}
	virtual void get_public_constants(List<Pair<String, Variant>> *p_constants) const override {}
	virtual void get_public_annotations(List<MethodInfo> *p_annotations) const override {}

	virtual void profiling_start() override {}
	virtual void profiling_stop() override {}
	virtual void profiling_set_save_native_calls(bool p_enable) override {}
	virtual int profiling_get_accumulated_data(ProfilingInfo *p_info_arr, int p_info_max) override { return 0; }
	virtual int profiling_get_frame_data(ProfilingInfo *p_info_arr, int p_info_max) override { return 0; }

	virtual bool handles_global_class_type(const String &p_type) const override { return p_type == get_type(); }
	virtual String get_global_class_name(const String &p_path, String *r_base_type = nullptr, String *r_icon_path = nullptr, bool *r_is_abstract = nullptr, bool *r_is_tool = nullptr) const override;

	RustLanguage();
	~RustLanguage();
};
