#include "rust_language.h"

#include "rust_diagnostics.h"

#include "core/object/class_db.h"
#include "core/templates/hash_set.h"
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

static bool _is_rust_keyword(const String &p_name) {
	static const char *const keywords[] = { "as", "break", "const", "continue", "crate", "dyn", "else", "enum", "extern", "false", "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return", "self", "static", "struct", "super", "trait", "true", "type", "unsafe", "use", "where", "while", nullptr };
	for (int i = 0; keywords[i] != nullptr; i++) {
		if (p_name == keywords[i]) {
			return true;
		}
	}
	return false;
}

bool RustLanguage::is_valid_module_name(const String &p_name) {
	// Rust accepts any identifier here (snake_case is only a lint), so only reject
	// what cannot compile: keywords, reserved type names and invalid identifiers.
	if (p_name.is_empty() || _is_rust_keyword(p_name) || p_name == "Self") {
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

String RustLanguage::to_rust_type_name(const String &p_name) {
	// Split on anything that is not alphanumeric: those are word boundaries.
	Vector<String> words;
	String current;
	for (int i = 0; i < p_name.length(); i++) {
		const char32_t c = p_name[i];
		const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
		if (alnum) {
			current += String::chr(c);
		} else if (!current.is_empty()) {
			words.push_back(current);
			current = String();
		}
	}
	if (!current.is_empty()) {
		words.push_back(current);
	}
	if (words.is_empty()) {
		return "MyScript";
	}

	String out;
	for (const String &word : words) {
		// An all-uppercase word (e.g. PLAYER) counts as one word and is lowered
		// first, as Rust style treats acronyms as ordinary words.
		bool all_upper = true;
		bool has_letter = false;
		for (int i = 0; i < word.length(); i++) {
			const char32_t c = word[i];
			if (c >= 'a' && c <= 'z') {
				all_upper = false;
				has_letter = true;
			} else if (c >= 'A' && c <= 'Z') {
				has_letter = true;
			}
		}
		const String normalized = (has_letter && all_upper) ? word.to_lower() : word;

		// Upper-case the first *letter* of the word. Digits do not consume that
		// position, so "sprite_2d" becomes "Sprite2D".
		bool upper_next_letter = true;
		for (int i = 0; i < normalized.length(); i++) {
			const char32_t c = normalized[i];
			const bool is_letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
			if (is_letter && upper_next_letter) {
				out += String::chr(c).to_upper();
				upper_next_letter = false;
			} else {
				out += String::chr(c);
			}
		}
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

void RustLanguage::_bind_methods() {
	ClassDB::bind_static_method("RustLanguage", D_METHOD("to_rust_type_name", "name"), &RustLanguage::to_rust_type_name);
	ClassDB::bind_static_method("RustLanguage", D_METHOD("is_valid_module_name", "name"), &RustLanguage::is_valid_module_name);
	ClassDB::bind_static_method("RustLanguage", D_METHOD("to_rust_class_name", "class_name"), &RustLanguage::to_rust_class_name);
	ClassDB::bind_static_method("RustLanguage", D_METHOD("make_script_source", "class_name", "base_class_name"), &RustLanguage::make_script_source);
}

String RustLanguage::validate_path(const String &p_path) const {
	if (!p_path.get_extension().to_lower().is_empty() && p_path.get_extension().to_lower() != "rs") {
		return "Rust scripts must use the .rs extension.";
	}
	const String stem = p_path.get_file().get_basename();
	if (!stem.is_empty() && !is_valid_module_name(stem)) {
		return "The script file name must be a valid Rust module name: letters, digits and underscores, not starting with a digit (e.g. \"my_script.rs\").";
	}
	return String();
}

// CodeEdit hands the whole buffer to complete_code() with a marker where the caret
// is (CodeEdit::get_text_for_code_completion), so the request position has to be
// recovered from it. Without the marker the caret sits at the end of the text.
static const char32_t RUST_CARET_MARKER = 0xFFFF;

static int _caret_offset(const String &p_code) {
	const int marker = p_code.find(String::chr(RUST_CARET_MARKER));
	return marker >= 0 ? marker : p_code.length();
}

static String _code_without_caret(const String &p_code) {
	return p_code.replace(String::chr(RUST_CARET_MARKER), String());
}

// 0-based caret position, which is what the language server expects.
static void _caret_position(const String &p_code, int p_offset, int &r_line, int &r_column) {
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

// Classes that `godot::prelude` already exports (godot/src/prelude.rs): a script
// based on one of them needs no extra `use`.
static bool _is_in_godot_prelude(const String &p_class_name) {
	static const char *const prelude_classes[] = {
		"INode", "INode2D", "INode3D", "IObject", "IPackedScene", "IRefCounted", "IResource",
		"ISceneTree", "Node", "Node2D", "Node3D", "Object", "PackedScene", "RefCounted",
		"Resource", "SceneTree",
	};
	for (const char *prelude_class : prelude_classes) {
		if (p_class_name == prelude_class) {
			return true;
		}
	}
	return false;
}

String RustLanguage::to_rust_class_name(const String &p_class_name) {
	// Mirrors gdext's class naming (godot-codegen/src/conv/name_conversions.rs):
	// Rust types are PascalCase, so acronym runs are lowered and a few names are
	// spelled out by hand.
	if (p_class_name == "JSONRPC") {
		return "JsonRpc";
	}
	if (p_class_name == "OpenXRAPIExtension") {
		return "OpenXrApiExtension";
	}
	if (p_class_name == "OpenXRIPBinding") {
		return "OpenXrIpBinding";
	}

	Vector<String> words;
	String word;
	for (int i = 0; i < p_class_name.length(); i++) {
		const char32_t c = p_class_name[i];
		const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
		if (!alnum) {
			if (!word.is_empty()) {
				words.push_back(word);
				word = String();
			}
			continue;
		}
		if (!word.is_empty()) {
			const char32_t previous = p_class_name[i - 1];
			const char32_t next = i + 1 < p_class_name.length() ? p_class_name[i + 1] : 0;
			const bool upper = c >= 'A' && c <= 'Z';
			const bool previous_lower = previous >= 'a' && previous <= 'z';
			// A capital right after digits starts a word when the digits end a real
			// word ("Node2|D", "X509|Certificate") but not inside an acronym run
			// ("CCDIK3D" stays one word, which heck renders as "Ccdik3d").
			bool after_word_digits = false;
			if (previous >= '0' && previous <= '9') {
				int before_digits = i - 1;
				while (before_digits >= 0 && p_class_name[before_digits] >= '0' && p_class_name[before_digits] <= '9') {
					before_digits--;
				}
				const bool digits_after_lower = before_digits >= 0 && p_class_name[before_digits] >= 'a' && p_class_name[before_digits] <= 'z';
				after_word_digits = digits_after_lower || (next >= 'a' && next <= 'z');
			}
			// An acronym followed by a word ("HTTPRequest") splits before that word.
			const bool boundary = upper && (previous_lower || after_word_digits || (previous >= 'A' && previous <= 'Z' && next >= 'a' && next <= 'z'));
			if (boundary) {
				words.push_back(word);
				word = String();
			}
		}
		word += String::chr(c);
	}
	if (!word.is_empty()) {
		words.push_back(word);
	}

	String out;
	for (const String &part : words) {
		if (!part.is_empty()) {
			out += part.substr(0, 1).to_upper() + part.substr(1).to_lower();
		}
	}
	if (out.is_empty()) {
		return p_class_name;
	}
	return out.replace("GdExtension", "GDExtension").replace("GdNative", "GDNative").replace("GdScript", "GDScript").replace("Vsync", "VSync").replace("Sdfgiy", "SdfgiY");
}

String RustLanguage::make_script_source(const String &p_class_name, const String &p_base_class_name) {
	String base_engine = p_base_class_name.is_empty() ? String("Object") : p_base_class_name;
	if (!_is_valid_rust_identifier(base_engine) || !ClassDB::class_exists(StringName(base_engine)) || !ClassDB::is_class_exposed(StringName(base_engine))) {
		// Custom types arrive as a quoted script path; engine classes that no scripting
		// extension can see (editor internals) cannot be a base either: use a Node.
		base_engine = "Node";
	}
	const String base = to_rust_class_name(base_engine);
	const String class_name = to_rust_type_name(p_class_name);

	String source;
	source += "use godot::prelude::*;\n";
	if (!_is_in_godot_prelude(base)) {
		// The base type is not exported by the prelude, so name it explicitly.
		source += vformat("use godot::classes::%s;\n", base);
	}
	source += "use godot_script::prelude::*;\n\n";
	source += vformat("#[derive(RustScript)]\n#[script(base = %s)]\n", base);
	source += vformat("pub struct %s {\n", class_name);
	source += vformat("    owner: Gd<%s>,\n", base);
	source += "    #[export]\n    speed: f32,\n";
	source += "}\n\n";
	source += vformat("#[godot_script_api]\nimpl %s {\n", class_name);
	source += "    fn ready(&mut self) {\n";
	source += "        // `owner` is the node this script is attached to (a Gd<Base>).\n";
	source += "        let _ = &self.owner;\n";
	source += vformat("        godot_print!(\"%s ready\");\n", class_name);
	source += "    }\n}\n\n";
	source += vformat("godot_script::register_script!(%s);\n", class_name);
	return source;
}

Ref<Script> RustLanguage::make_template(const String &p_template, const String &p_class_name, const String &p_base_class_name) const {
	Ref<RustScript> script;
	script.instantiate();
	// The dialog picks the base class, so the template can name it (and import it);
	// the saver adds the `mod` declaration and the `register` call to the crate root.
	script->set_source_code(make_script_source(p_class_name, p_base_class_name));
	return script;
}

// The editor hands complete_code() the text up to the caret, so the word being
// typed (and whether it follows a dot) can be read off the end of the buffer.
static String _completion_word(const String &p_code) {
	const int caret = _caret_offset(p_code);
	int start = caret;
	while (start > 0) {
		const char32_t c = p_code[start - 1];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
			start--;
			continue;
		}
		break;
	}
	return p_code.substr(start, caret - start);
}

static bool _completion_after_member_access(const String &p_code) {
	const int end = _caret_offset(p_code) - _completion_word(p_code).length();
	if (end <= 0) {
		return false;
	}
	if (p_code[end - 1] == '.') {
		return true;
	}
	return end >= 2 && p_code[end - 1] == ':' && p_code[end - 2] == ':';
}

// rust-analyzer answers with snippets that contain placeholders; the editor
// inserts the text verbatim, so those placeholders have to become plain text.
static String _plain_text_from_snippet(const String &p_snippet) {
	String out;
	int i = 0;
	while (i < p_snippet.length()) {
		const char32_t c = p_snippet[i];
		if (c == '\\' && i + 1 < p_snippet.length()) {
			out += p_snippet[i + 1];
			i += 2;
			continue;
		}
		if (c == '$') {
			if (i + 1 < p_snippet.length() && p_snippet[i + 1] == '{') {
				const int end = p_snippet.find("}", i + 2);
				if (end > 0) {
					const String inner = p_snippet.substr(i + 2, end - i - 2);
					const int colon = inner.find(":");
					if (colon >= 0) {
						out += inner.substr(colon + 1);
					}
					i = end + 1;
					continue;
				}
			} else if (i + 1 < p_snippet.length() && p_snippet[i + 1] >= '0' && p_snippet[i + 1] <= '9') {
				int j = i + 1;
				while (j < p_snippet.length() && p_snippet[j] >= '0' && p_snippet[j] <= '9') {
					j++;
				}
				i = j;
				continue;
			}
		}
		out += String::chr(c);
		i++;
	}
	return out;
}


// Types and macros used by the built-in completion fallback.
static const char *rust_completion_types[] = {
	"bool", "char", "str", "String", "i8", "i16", "i32", "i64", "i128", "isize",
	"u8", "u16", "u32", "u64", "u128", "usize", "f32", "f64",
	"Option", "Some", "None", "Result", "Ok", "Err", "Vec", "Box", "Rc", "Arc",
	"RefCell", "Cell", "Mutex", "RwLock", "HashMap", "HashSet", "BTreeMap", "BTreeSet",
	"VecDeque", "PathBuf", "Duration", "Instant",
	"Gd", "GdMut", "Variant", "StringName", "GString", "NodePath",
	"Node", "Node2D", "Node3D", "Control", "CanvasItem", "RefCounted", "Resource",
	"SceneTree", "Input", "Engine", "Time", "Timer", "Tween", "Area2D", "CharacterBody2D",
	"Sprite2D", "Camera2D", "CollisionShape2D", "Label", "Button", "Texture2D", "PackedScene",
	nullptr
};

static const char *rust_completion_macros[] = {
	"println!", "print!", "format!", "vec!", "panic!", "assert!", "assert_eq!",
	"todo!", "unimplemented!", "unreachable!", "matches!", "dbg!",
	"godot_print!", "godot_print_rich!", "godot_error!", "godot_warn!", "godot_script_error!",
	nullptr
};

// Snippets offered on top of whatever rust-analyzer returns: the pieces of a
// script that are annoying to type and the attribute markers of the module.
struct RustCompletionSnippet {
	const char *display;
	const char *insert;
	ScriptLanguage::CodeCompletionKind kind;
};

static const RustCompletionSnippet rust_completion_snippets[] = {
	{ "fn", "fn name() {\n\t\n}", ScriptLanguage::CODE_COMPLETION_KIND_FUNCTION },
	{ "impl", "impl Type {\n\t\n}", ScriptLanguage::CODE_COMPLETION_KIND_PLAIN_TEXT },
	{ "struct", "struct Name {\n\t\n}", ScriptLanguage::CODE_COMPLETION_KIND_CLASS },
	{ "enum", "enum Name {\n\t\n}", ScriptLanguage::CODE_COMPLETION_KIND_ENUM },
	{ "match", "match value {\n\t_ => {}\n}", ScriptLanguage::CODE_COMPLETION_KIND_KEYWORD },
	{ "if let", "if let Some(value) = option {\n\t\n}", ScriptLanguage::CODE_COMPLETION_KIND_KEYWORD },
	{ "godot_print!", "godot_print!(\"\");", ScriptLanguage::CODE_COMPLETION_KIND_PLAIN_TEXT },
	{ "godot_error!", "godot_error!(\"\");", ScriptLanguage::CODE_COMPLETION_KIND_PLAIN_TEXT },
	{ "#[func]", "#[func]\n", ScriptLanguage::CODE_COMPLETION_KIND_PLAIN_TEXT },
	{ "#[signal]", "#[signal]\n", ScriptLanguage::CODE_COMPLETION_KIND_PLAIN_TEXT },
	{ "#[export]", "#[export]\n", ScriptLanguage::CODE_COMPLETION_KIND_PLAIN_TEXT },
	{ "RustScript", "#[derive(RustScript)]\n#[script(base = Node2D)]\n", ScriptLanguage::CODE_COMPLETION_KIND_PLAIN_TEXT },
	{ nullptr, nullptr, ScriptLanguage::CODE_COMPLETION_KIND_PLAIN_TEXT },
};

Error RustLanguage::complete_code(const String &p_code, const String &p_path, Object *p_owner, List<CodeCompletionOption> *r_options, bool &r_force, String &r_call_hint) {
#ifdef TOOLS_ENABLED
	if (r_options == nullptr) {
		return ERR_UNAVAILABLE;
	}

	// The editor passes the text up to the caret; the word being typed filters the
	// built-in lists the same way GDScript's language does it.
	const String prefix = _completion_word(p_code);
	const String lowered_prefix = prefix.to_lower();
	const bool member_access = _completion_after_member_access(p_code);
	const int before = r_options->size();
	HashSet<String> seen;

	auto add_option = [&](const String &p_display, CodeCompletionKind p_kind, const String &p_insert) {
		if (seen.has(p_display)) {
			return;
		}
		seen.insert(p_display);
		CodeCompletionOption option(p_insert.is_empty() ? p_display : p_insert, p_kind, LOCATION_OTHER, p_display);
		option.display = p_display;
		option.insert_text = p_insert.is_empty() ? p_display : p_insert;
		r_options->push_back(option);
	};

	bool lsp_answered = false;
	RustLsp *lsp = RustLsp::get_singleton();
	if (lsp != nullptr && lsp->is_running() && !p_path.is_empty()) {
		int line = 0;
		int column = 0;
		_caret_position(p_code, _caret_offset(p_code), line, column);
		// The caret marker is ours, the language server must not see it.
		lsp->sync_document(p_path, _code_without_caret(p_code));

		Array items;
		String error;
		if (lsp->complete(p_path, line, column, items, error)) {
			lsp_answered = true;
			for (int i = 0; i < items.size(); i++) {
				Dictionary item = items[i];
				const String label = item.get("label", String());
				if (label.is_empty() || seen.has(label)) {
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
				if ((int)item.get("insertTextFormat", 1) == 2) {
					insert = _plain_text_from_snippet(insert);
				}
				seen.insert(label);
				CodeCompletionOption option(insert, _lsp_completion_kind((int)item.get("kind", 0)), LOCATION_OTHER, "");
				option.display = label;
				option.insert_text = insert;
				r_options->push_back(option);
			}
		}
	}

	// Snippets are always offered; the keyword/type lists are the completion for
	// when rust-analyzer is not running (or not installed at all). After a dot the
	// only sensible answers are members, so nothing global is added there.
	for (int i = 0; !member_access && rust_completion_snippets[i].display != nullptr; i++) {
		const RustCompletionSnippet &snippet = rust_completion_snippets[i];
		const String display = snippet.display;
		if (!prefix.is_empty() && !display.to_lower().begins_with(lowered_prefix)) {
			continue;
		}
		add_option(display, snippet.kind, snippet.insert);
	}

	if (!lsp_answered && !member_access) {
		for (const String &word : get_reserved_words()) {
			if (!prefix.is_empty() && !word.begins_with(prefix)) {
				continue;
			}
			add_option(word, is_control_flow_keyword(word) ? CODE_COMPLETION_KIND_KEYWORD : CODE_COMPLETION_KIND_KEYWORD, String());
		}
		List<String> core_types;
		get_core_type_words(&core_types);
		for (const String &type : core_types) {
			if (!prefix.is_empty() && !type.to_lower().begins_with(lowered_prefix)) {
				continue;
			}
			add_option(type, CODE_COMPLETION_KIND_CLASS, String());
		}
		for (int i = 0; rust_completion_types[i] != nullptr; i++) {
			const String type = rust_completion_types[i];
			if (!prefix.is_empty() && !type.to_lower().begins_with(lowered_prefix)) {
				continue;
			}
			add_option(type, CODE_COMPLETION_KIND_CLASS, String());
		}
		for (int i = 0; rust_completion_macros[i] != nullptr; i++) {
			const String macro = rust_completion_macros[i];
			if (!prefix.is_empty() && !macro.begins_with(prefix)) {
				continue;
			}
			add_option(macro, CODE_COMPLETION_KIND_FUNCTION, String());
		}

		// Functions declared in this file, so a script still completes its own API
		// without a language server.
		int at = p_code.find("fn ");
		while (at >= 0) {
			const int start = at + 3;
			int end = start;
			while (end < p_code.length()) {
				const char32_t c = p_code[end];
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
					end++;
					continue;
				}
				break;
			}
			const String function = p_code.substr(start, end - start);
			if (!function.is_empty() && (prefix.is_empty() || function.begins_with(prefix))) {
				add_option(function + "()", CODE_COMPLETION_KIND_FUNCTION, function + "()");
			}
			at = p_code.find("fn ", end);
		}
	}

	// After `self.` the interesting names are the script's own API.
	if (member_access) {
		RustScriptRegistry *registry = RustScriptRegistry::get_singleton();
		RustScriptRegistry::ScriptType type;
		if (registry != nullptr && !p_path.is_empty() && registry->get_script_type(p_path, type)) {
			for (const MethodInfo &method : type.methods) {
				if (prefix.is_empty() || String(method.name).begins_with(prefix)) {
					add_option(String(method.name) + "()", CODE_COMPLETION_KIND_FUNCTION, String(method.name) + "()");
				}
			}
			for (const PropertyInfo &property : type.properties) {
				if (prefix.is_empty() || String(property.name).begins_with(prefix)) {
					add_option(property.name, CODE_COMPLETION_KIND_MEMBER, String());
				}
			}
			for (const MethodInfo &signal : type.signals) {
				if (prefix.is_empty() || String(signal.name).begins_with(prefix)) {
					add_option(signal.name, CODE_COMPLETION_KIND_SIGNAL, String());
				}
			}
		}
	}

	if (r_options->size() == before) {
		return ERR_UNAVAILABLE;
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
