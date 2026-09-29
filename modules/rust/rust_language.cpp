#include "rust_language.h"

#include "rust_diagnostics.h"
#include "rust_script.h"
#include "rust_script_registry.h"

#ifdef TOOLS_ENABLED
#include "editor/rust_lsp.h"
#include "core/io/resource_loader.h"
#endif

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

static bool _is_valid_rust_identifier(const String &p_name) {
	if (p_name.is_empty()) {
		return false;
	}
	if (p_name[0] >= '0' && p_name[0] <= '9') {
		return false;
	}
	for (int i = 0; i < p_name.length(); i++) {
		const char32_t c = p_name[i];
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
		if (!ok) {
			return false;
		}
	}
	return true;
}

static bool _is_valid_rust_module_name(const String &p_name) {
	if (!_is_valid_rust_identifier(p_name)) {
		return false;
	}
	static const char *const keywords[] = { "as", "break", "const", "continue", "crate", "dyn", "else", "enum", "extern", "false", "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return", "self", "static", "struct", "super", "trait", "true", "type", "unsafe", "use", "where", "while", nullptr };
	for (int i = 0; keywords[i] != nullptr; i++) {
		if (p_name == keywords[i]) {
			return false;
		}
	}
	return true;
}

// Turns a file base name ("my-script", "player") into a Rust type name
// ("MyScript", "Player"); the script dialog derives the class name from the
// file name, which is not necessarily a valid Rust identifier.
static String _to_rust_type_name(const String &p_name) {
	String out;
	bool capitalize = true;
	for (int i = 0; i < p_name.length(); i++) {
		const char32_t c = p_name[i];
		const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
		if (!alnum) {
			capitalize = true;
			continue;
		}
		out += capitalize ? String::chr(c).to_upper() : String::chr(c);
		capitalize = false;
	}
	if (out.is_empty()) {
		out = "MyScript";
	}
	if (out[0] >= '0' && out[0] <= '9') {
		out = "Rust" + out;
	}
	if (out == "Self") {
		out = "SelfScript";
	}
	return out;
}

String RustLanguage::validate_path(const String &p_path) const {
	if (!p_path.get_extension().to_lower().is_empty() && p_path.get_extension().to_lower() != "rs") {
		return "Rust scripts must use the .rs extension.";
	}
	const String stem = p_path.get_file().get_basename();
	if (!stem.is_empty() && !_is_valid_rust_module_name(stem)) {
		return "Rust script file names must be valid module names: lowercase letters, digits and underscores (e.g. \"my_script.rs\").";
	}
	return String();
}

// 0-based position of the caret: the editor hands `complete_code` the text up to
// the cursor, so the end of that buffer is where the request belongs.
static void _caret_at_end(const String &p_code, int &r_line, int &r_column) {
	r_line = 0;
	r_column = 0;
	for (int i = 0; i < p_code.length(); i++) {
		if (p_code[i] == '\n') {
			r_line++;
			r_column = 0;
		} else {
			r_column++;
		}
	}
}

static void _position_from_offset(const String &p_code, int p_offset, int &r_line, int &r_column) {
	r_line = 0;
	r_column = 0;
	for (int i = 0; i < p_offset && i < p_code.length(); i++) {
		if (p_code[i] == '\n') {
			r_line++;
			r_column = 0;
		} else {
			r_column++;
		}
	}
}

#ifdef TOOLS_ENABLED
static ScriptLanguage::CodeCompletionKind _lsp_completion_kind(int p_lsp_kind) {
	switch (p_lsp_kind) {
		case 7: // Class
		case 8: // Interface
			return ScriptLanguage::CODE_COMPLETION_KIND_CLASS;
		case 2: // Method
		case 3: // Function
		case 4: // Constructor
			return ScriptLanguage::CODE_COMPLETION_KIND_FUNCTION;
		case 5: // Field
		case 10: // Property
			return ScriptLanguage::CODE_COMPLETION_KIND_MEMBER;
		case 6: // Variable
			return ScriptLanguage::CODE_COMPLETION_KIND_VARIABLE;
		case 13: // Enum
			return ScriptLanguage::CODE_COMPLETION_KIND_ENUM;
		case 21: // Constant
			return ScriptLanguage::CODE_COMPLETION_KIND_CONSTANT;
		case 14: // Keyword
			return ScriptLanguage::CODE_COMPLETION_KIND_KEYWORD;
		default:
			return ScriptLanguage::CODE_COMPLETION_KIND_PLAIN_TEXT;
	}
}
#endif

bool RustLanguage::validate(const String &p_script, const String &p_path, List<String> *r_functions, List<ScriptError> *r_errors, List<Warning> *r_warnings, HashSet<int> *r_safe_lines) const {
#ifdef TOOLS_ENABLED
	if (RustLsp *lsp = RustLsp::get_singleton(); lsp != nullptr && lsp->is_running() && !p_path.is_empty()) {
		// Keep the language server's view of the buffer in sync; its diagnostics
		// arrive asynchronously and land in RustDiagnostics.
		lsp->sync_document(p_path, p_script);
	}
#endif
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
	if (!_is_valid_rust_identifier(base)) {
		// The dialog passes custom types as a quoted script path; those cannot be
		// expressed as a Rust base type, so fall back to a plain Node.
		base = "Node";
	}
	const String class_name = _to_rust_type_name(p_class_name);

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

Error RustLanguage::complete_code(const String &p_code, const String &p_path, Object *p_owner, List<CodeCompletionOption> *r_options, bool &r_force, String &r_call_hint) {
#ifdef TOOLS_ENABLED
	RustLsp *lsp = RustLsp::get_singleton();
	if (lsp == nullptr || !lsp->is_running() || p_path.is_empty() || r_options == nullptr) {
		return ERR_UNAVAILABLE;
	}

	int line = 0;
	int column = 0;
	_caret_at_end(p_code, line, column);
	lsp->sync_document(p_path, p_code);

	Array items;
	String error;
	if (!lsp->complete(p_path, line, column, items, error)) {
		return ERR_UNAVAILABLE;
	}

	for (int i = 0; i < items.size(); i++) {
		Dictionary item = items[i];
		const String label = item.get("label", String());
		if (label.is_empty()) {
			continue;
		}
		String insert = item.get("insertText", String());
		if (insert.is_empty()) {
			Dictionary text_edit = item.get("textEdit", Dictionary());
			insert = text_edit.get("newText", String());
		}
		if (insert.is_empty()) {
			insert = label;
		}

		CodeCompletionOption option(insert, _lsp_completion_kind((int)item.get("kind", 0)), LOCATION_OTHER, "");
		option.display = label;
		option.insert_text = insert;
		r_options->push_back(option);
	}
	r_force = true;
	return OK;
#else
	return ERR_UNAVAILABLE;
#endif
}

Error RustLanguage::lookup_code(const String &p_code, const String &p_symbol, const String &p_path, Object *p_owner, LookupResult &r_result) {
#ifdef TOOLS_ENABLED
	RustLsp *lsp = RustLsp::get_singleton();
	if (lsp == nullptr || !lsp->is_running() || p_path.is_empty() || p_symbol.is_empty()) {
		return ERR_UNAVAILABLE;
	}

	// The editor hands us the whole buffer and the symbol name, so look for the
	// first occurrence of that symbol to pick a position for the request.
	const int offset = p_code.find(p_symbol);
	if (offset == -1) {
		return ERR_UNAVAILABLE;
	}
	int line = 0;
	int column = 0;
	_position_from_offset(p_code, offset, line, column);

	String error;
	String target_path;
	int target_line = 0;
	int target_column = 0;
	if (lsp->definition(p_path, line, column, target_path, target_line, target_column, error) && !target_path.is_empty()) {
		r_result.type = LOOKUP_RESULT_SCRIPT_LOCATION;
		r_result.script_path = target_path;
		r_result.location = target_line;
		r_result.script = ResourceLoader::load(target_path, "Script");
		r_result.description = vformat("%s:%d", target_path, target_line);
		return OK;
	}

	String text;
	if (lsp->hover(p_path, line, column, text, error) && !text.is_empty()) {
		r_result.type = LOOKUP_RESULT_SCRIPT_LOCATION;
		r_result.location = -1;
		r_result.description = text;
		return OK;
	}
#endif
	return ERR_UNAVAILABLE;
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
