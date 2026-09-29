#include "rust_language.h"

#include "rust_diagnostics.h"
#include "rust_script.h"
#include "rust_script_registry.h"

RustLanguage *RustLanguage::singleton = nullptr;

static const char *const rust_keywords[] = {
	"as", "async", "await", "break", "const", "continue", "crate", "dyn", "else", "enum",
	"extern", "false", "fn", "for", "if", "impl", "in", "let", "loop", "match",
	"mod", "move", "mut", "pub", "ref", "return", "self", "Self", "static", "struct",
	"super", "trait", "true", "type", "unsafe", "use", "where", "while", "union", "box",
	nullptr
};

Vector<String> RustLanguage::get_reserved_words() const {
	Vector<String> words;
	for (int i = 0; rust_keywords[i] != nullptr; i++) {
		words.push_back(rust_keywords[i]);
	}
	return words;
}

bool RustLanguage::is_control_flow_keyword(const String &p_string) const {
	return p_string == "break" || p_string == "continue" || p_string == "else" || p_string == "for" ||
			p_string == "if" || p_string == "loop" || p_string == "match" || p_string == "return" ||
			p_string == "while" || p_string == "await" || p_string == "yield";
}

Vector<String> RustLanguage::get_comment_delimiters() const {
	Vector<String> delimiters;
	delimiters.push_back("//");
	delimiters.push_back("/* */");
	return delimiters;
}

Vector<String> RustLanguage::get_doc_comment_delimiters() const {
	// The editor requires delimiter keys to consist of symbols only (see
	// CodeEdit::_add_delimiter) and rejects duplicates, so every entry here must
	// be unique and start with a symbol.
	Vector<String> delimiters;
	delimiters.push_back("///"); // Outer line doc comment.
	delimiters.push_back("//!"); // Inner line doc comment.
	delimiters.push_back("/** */"); // Outer block doc comment.
	return delimiters;
}

Vector<String> RustLanguage::get_string_delimiters() const {
	Vector<String> delimiters;
	delimiters.push_back("\" \"");
	delimiters.push_back("' '");
	// Rust's prefixed literals (r"...", r#"..."#, b"...") cannot be listed
	// here: the editor only accepts delimiters made of symbols, and 'r'/'b' are
	// letters. They are still covered by the generic quote highlighting.
	return delimiters;
}

String RustLanguage::validate_path(const String &p_path) const {
	if (!p_path.get_extension().to_lower().is_empty() && p_path.get_extension().to_lower() != "rs") {
		return "Rust scripts must use the .rs extension.";
	}
	return String();
}

bool RustLanguage::validate(const String &p_script, const String &p_path, List<String> *r_functions, List<ScriptError> *r_errors, List<Warning> *r_warnings, HashSet<int> *r_safe_lines) const {
	RustDiagnostics::get_singleton()->fill_script_errors(p_path, r_errors, r_warnings);
	if (r_errors != nullptr && !r_errors->is_empty()) {
		return false;
	}
	return true;
}

String RustLanguage::get_global_class_name(const String &p_path, String *r_base_type, String *r_icon_path, bool *r_is_abstract, bool *r_is_tool) const {
	RustScriptRegistry *registry = RustScriptRegistry::get_singleton();
	RustScriptRegistry::ScriptType type;
	if (registry == nullptr || !registry->get_script_type(p_path, type)) {
		// The library has not been built yet; the editor then treats the file as a
		// plain Rust script without a known class name.
		return String();
	}

	if (r_base_type != nullptr) {
		*r_base_type = type.base;
	}
	if (r_is_abstract != nullptr) {
		*r_is_abstract = false;
	}
	if (r_is_tool != nullptr) {
		*r_is_tool = type.is_tool;
	}
	return type.class_name;
}

Ref<Script> RustLanguage::make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const {
	Ref<RustScript> script;
	script.instantiate();

	String base = p_base_class_name.is_empty() ? String("Object") : p_base_class_name;
	String class_name = p_class_name.is_empty() ? String("MyClass") : p_class_name;

	// Attachable-script template: the class extends `base`, so the script can be
	// attached to any node of that type. The saver adds the `mod` declaration and
	// the `register` call to the crate root on first save.
	String source;
	source += "use godot::prelude::*;\n";
	source += "use godot_script::prelude::*;\n\n";
	source += vformat("pub struct %s {\n", class_name);
	source += vformat("    owner: Gd<%s>,\n", base);
	source += "}\n\n";
	source += vformat("impl RustScript for %s {\n", class_name);
	source += vformat("    type Base = %s;\n", base);
	source += vformat("    const CLASS_NAME: &'static str = \"%s\";\n", class_name);
	source += vformat("    const BASE_NAME: &'static str = \"%s\";\n\n", base);
	source += vformat("    fn new(owner: Gd<Self::Base>) -> Self {\n        Self { owner }\n    }\n\n");
	source += "    fn ready(&mut self) {\n";
	source += vformat("        godot_print!(\"%s ready\");\n", class_name);
	source += "    }\n";
	source += "}\n\n";
	source += vformat("godot_script::register_script!(%s);\n", class_name);

	script->set_source_code(source);
	return script;
}

void RustLanguage::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("rs");
}

RustLanguage::RustLanguage() {
	singleton = this;
}

RustLanguage::~RustLanguage() {
	singleton = nullptr;
}
