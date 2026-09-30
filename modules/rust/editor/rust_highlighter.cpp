#include "rust_highlighter.h"

#include "../rust_language.h"
#include "../rust_script_registry.h"

#include "core/object/class_db.h"
#include "editor/settings/editor_settings.h"

#include "core/variant/variant.h"

// gdext names that are not ClassDB classes but appear in every script.
static const char *rust_gdext_types[] = {
	"Gd", "GdMut", "GdRef", "DynGd", "Variant", "ToGodot", "FromGodot", "GodotClass",
	"GodotConvert", "Base", "RustScript", "ScriptApi", "ScriptMethod", "ScriptSignal",
	"ScriptArgument", "PropertyInfo", "MethodInfo", "PropertyHintInfo", "GString",
	"StringName", "NodePath", "InitStage", "ExtensionLibrary",
	nullptr
};

// Rust's own built-in words: primitives and the std prelude (ScriptLanguage's
// get_core_type_words() covers the Godot types instead).
static const char *rust_builtin_types[] = {
	"bool", "char", "str", "String", "i8", "i16", "i32", "i64", "i128", "isize",
	"u8", "u16", "u32", "u64", "u128", "usize", "f32", "f64",
	"Option", "Some", "None", "Result", "Ok", "Err", "Vec", "Box", "Rc", "Arc",
	"RefCell", "Cell", "Mutex", "RwLock", "HashMap", "HashSet", "BTreeMap",
	"BTreeSet", "VecDeque", "PathBuf", "Duration", "Instant", "Ordering", "Range",
	nullptr
};

// Macros worth coloring: ours plus the usual standard ones. The highlighter
// matches whole words, so the trailing bang is not part of the keyword here.
static const char *rust_macros[] = {
	"println", "print", "eprintln", "eprint", "format", "vec", "panic", "assert",
	"assert_eq", "assert_ne", "debug_assert", "todo", "unimplemented", "unreachable",
	"matches", "write", "writeln", "dbg", "include_str", "include_bytes",
	"godot_print", "godot_print_rich", "godot_error", "godot_warn", "godot_script_error",
	nullptr
};
PackedStringArray RustSyntaxHighlighter::_get_supported_languages() const {
	// The script editor matches this against ScriptLanguage::get_name() for scripts
	// and against the file extension for everything else.
	return PackedStringArray{ "Rust", "rs" };
}

Ref<EditorSyntaxHighlighter> RustSyntaxHighlighter::_create() const {
	Ref<RustSyntaxHighlighter> syntax_highlighter;
	syntax_highlighter.instantiate();
	return syntax_highlighter;
}

void RustSyntaxHighlighter::_update_cache() {
	highlighter->set_text_edit(text_edit);
	highlighter->clear_keyword_colors();
	highlighter->clear_member_keyword_colors();
	highlighter->clear_color_regions();

	highlighter->set_symbol_color(EDITOR_GET("text_editor/theme/highlighting/symbol_color"));
	highlighter->set_function_color(EDITOR_GET("text_editor/theme/highlighting/function_color"));
	highlighter->set_number_color(EDITOR_GET("text_editor/theme/highlighting/number_color"));
	highlighter->set_member_variable_color(EDITOR_GET("text_editor/theme/highlighting/member_variable_color"));

	const Color keyword_color = EDITOR_GET("text_editor/theme/highlighting/keyword_color");
	const Color control_flow_color = EDITOR_GET("text_editor/theme/highlighting/control_flow_keyword_color");
	const Color base_type_color = EDITOR_GET("text_editor/theme/highlighting/base_type_color");
	const Color engine_type_color = EDITOR_GET("text_editor/theme/highlighting/engine_type_color");
	const Color user_type_color = EDITOR_GET("text_editor/theme/highlighting/user_type_color");
	const Color function_color = EDITOR_GET("text_editor/theme/highlighting/function_color");
	const Color comment_color = EDITOR_GET("text_editor/theme/highlighting/comment_color");
	const Color doc_comment_color = EDITOR_GET("text_editor/theme/highlighting/doc_comment_color");
	const Color string_color = EDITOR_GET("text_editor/theme/highlighting/string_color");
	// Attributes are the closest thing Rust has to GDScript annotations, and that
	// colour only exists in the GDScript section of the settings.
	const Variant annotation_setting = EditorSettings::get_singleton()->get_setting("text_editor/theme/highlighting/gdscript/annotation_color");
	const Color annotation_color = annotation_setting.get_type() == Variant::COLOR ? (Color)annotation_setting : keyword_color;

	RustLanguage *language = RustLanguage::get_singleton();
	if (language != nullptr) {
		for (const String &word : language->get_reserved_words()) {
			highlighter->add_keyword_color(word, language->is_control_flow_keyword(word) ? control_flow_color : keyword_color);
		}
		// Godot's Variant types (Vector2, Color, ...) plus Rust's own words.
		List<String> core_types;
		language->get_core_type_words(&core_types);
		for (const String &type : core_types) {
			highlighter->add_keyword_color(type, base_type_color);
		}
		for (int i = 0; rust_builtin_types[i] != nullptr; i++) {
			highlighter->add_keyword_color(rust_builtin_types[i], base_type_color);
		}
	}

	// Engine classes (Node2D, Vector2, ...) and the gdext names scripts use.
	LocalVector<StringName> classes;
	ClassDB::get_class_list(classes);
	for (const StringName &class_name : classes) {
		highlighter->add_keyword_color(class_name, engine_type_color);
	}
	for (int i = 0; rust_gdext_types[i] != nullptr; i++) {
		highlighter->add_keyword_color(rust_gdext_types[i], engine_type_color);
	}

	// Script classes of this project, so `Player` reads like a type.
	RustScriptRegistry *registry = RustScriptRegistry::get_singleton();
	if (registry != nullptr) {
		for (const String &path : registry->get_registered_paths()) {
			RustScriptRegistry::ScriptType type;
			if (registry->get_script_type(path, type) && !type.class_name.is_empty()) {
				highlighter->add_keyword_color(type.class_name, user_type_color);
			}
		}
	}

	for (int i = 0; rust_macros[i] != nullptr; i++) {
		highlighter->add_keyword_color(rust_macros[i], function_color);
	}

	// Attributes (#[export], #[derive(...)]) and literals. Regions are matched
	// longest-first internally, so `///` wins over `//`.
	highlighter->add_color_region("#[", "]", annotation_color, false);
	highlighter->add_color_region("\"", "\"", string_color, false);
	highlighter->add_color_region("'", "'", string_color, true);
	highlighter->add_color_region("///", "", doc_comment_color, true);
	highlighter->add_color_region("//!", "", doc_comment_color, true);
	highlighter->add_color_region("/**", "*/", doc_comment_color, false);
	highlighter->add_color_region("//", "", comment_color, true);
	highlighter->add_color_region("/*", "*/", comment_color, false);
}

RustSyntaxHighlighter::RustSyntaxHighlighter() {
	highlighter.instantiate();
}
