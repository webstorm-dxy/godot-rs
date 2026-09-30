#include "register_types.h"

#include "rust_diagnostics.h"
#include "rust_language.h"
#include "rust_paths.h"
#include "rust_script_registry.h"
#include "rust_script.h"
#include "rust_script_resource_format.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/extension/gdextension_manager.h"
#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/class_db.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#include "editor/plugins/editor_plugin.h"
#include "editor/project_manager/project_dialog.h"
#include "editor/rust_build.h"
#include "editor/rust_editor_plugin.h"
#include "editor/rust_highlighter.h"
#include "editor/script/script_editor_base.h"
#include "editor/script/script_editor_plugin.h"

#include "editor/rust_lsp.h"
#include "editor/rust_project.h"

static bool syntax_highlighter_registered = false;

// Registers the .rs highlighter with the script editor. Called once the editor
// exists; also callable again (the editor plugin checks on startup) in case this
// ran before the script editor was built.
void rust_register_syntax_highlighter() {
	if (syntax_highlighter_registered || ScriptEditor::get_singleton() == nullptr) {
		return;
	}
	syntax_highlighter_registered = true;

	Ref<RustSyntaxHighlighter> rust_syntax_highlighter;
	rust_syntax_highlighter.instantiate();
	ScriptEditor::get_singleton()->register_syntax_highlighter(rust_syntax_highlighter);
	print_line("Rust: syntax highlighter registered for .rs scripts");

	// Scripts that are already open picked a highlighter when their tab was made,
	// so give them the Rust one as well. (get_open_script_editors() is the bound
	// accessor; the C++ one is private to ScriptEditor.)
	TypedArray<ScriptEditorBase> open_editors = ScriptEditor::get_singleton()->call("get_open_script_editors");
	int applied = 0;
	for (int i = 0; i < open_editors.size(); i++) {
		ScriptEditorBase *base = Object::cast_to<ScriptEditorBase>(open_editors[i]);
		TextEditorBase *editor = Object::cast_to<TextEditorBase>(base);
		if (editor == nullptr || base->get_edited_resource().is_null()) {
			continue;
		}
		Ref<Script> script = base->get_edited_resource();
		if (script.is_null() || script->get_language() != RustLanguage::get_singleton()) {
			continue;
		}
		Ref<EditorSyntaxHighlighter> instance = rust_syntax_highlighter->_create();
		if (instance.is_null()) {
			continue;
		}
		editor->add_syntax_highlighter(instance);
		editor->set_syntax_highlighter(instance);
		applied++;
	}
	if (applied > 0) {
		print_line(vformat("Rust: syntax highlighter applied to %d open script(s)", applied));
	}
}
#endif

static RustLanguage *rust_language = nullptr;
static RustDiagnostics *rust_diagnostics = nullptr;
static Ref<ResourceFormatLoaderRustScript> rust_resource_loader;
static Ref<ResourceFormatSaverRustScript> rust_resource_saver;

#ifdef TOOLS_ENABLED
static RustBuild *rust_build = nullptr;

static void _rust_project_created(const String &p_project_path, const String &p_project_name) {
	RustProject::scaffold_project(p_project_path, p_project_name);
}
#endif

// The engine's extension_list.cfg is rewritten by the editor from the
// .gdextension files found under res://; the generated configuration lives in
// .godot/ instead, so the module loads it directly. This also makes the game
// subprocess load the freshly built library without any manual wiring.
static bool _any_library_exists(const String &p_config_global) {
	Ref<ConfigFile> config;
	config.instantiate();
	if (config->load(p_config_global) != OK) {
		return false;
	}
	for (const String &key : config->get_section_keys("libraries")) {
		const String path = config->get_value("libraries", key);
		if (!path.is_empty() && FileAccess::exists(ProjectSettings::get_singleton()->globalize_path(path))) {
			return true;
		}
	}
	return false;
}

static void _load_project_extension() {
	if (ProjectSettings::get_singleton()->get_resource_path().is_empty()) {
		return;
	}
	const String config = RustPaths::get_extension_config_res();
	if (!_any_library_exists(RustPaths::get_extension_config_global())) {
		// Not built yet; the editor plugin builds and loads it after cargo ran.
		return;
	}
	GDExtensionManager *manager = GDExtensionManager::get_singleton();
	if (manager == nullptr || manager->is_extension_loaded(config)) {
		return;
	}
	GDExtensionManager::LoadStatus status = manager->load_extension(config);
	if (status == GDExtensionManager::LOAD_STATUS_FAILED) {
		ERR_PRINT(vformat("Rust: failed to load '%s'. Build the project and check the Rust output for details.", config));
	}
}

void initialize_rust_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(RustScript);
		GDREGISTER_CLASS(RustScriptRegistry);
		// Registered for its static naming helpers (to_rust_type_name, ...).
		GDREGISTER_CLASS(RustLanguage);

		rust_diagnostics = memnew(RustDiagnostics);

		rust_language = memnew(RustLanguage);
		ScriptServer::register_language(rust_language);

		rust_resource_loader.instantiate();
		ResourceLoader::add_resource_format_loader(rust_resource_loader);
		rust_resource_saver.instantiate();
		ResourceSaver::add_resource_format_saver(rust_resource_saver);

		// Rust scripts register themselves through this object, reachable from the
		// extension as the engine singleton "RustScriptRegistry".
		RustScriptRegistry::create_singleton();
		Engine::Singleton rust_registry_singleton("RustScriptRegistry", RustScriptRegistry::get_singleton());
		Engine::get_singleton()->add_singleton(rust_registry_singleton);

		_load_project_extension();
	}
#ifdef TOOLS_ENABLED
	else if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		RustProject::register_settings();
		rust_build = memnew(RustBuild);
		EditorPlugins::add_by_type<RustEditorPlugin>();
		ProjectDialog::add_project_creation_callback(&_rust_project_created);

		// Syntax coloring for .rs files: the editor picks a highlighter by script
		// language name, so this one answers to "Rust". Registration has to wait
		// until the script editor exists (same dance GDScript does).
		GDREGISTER_CLASS(RustSyntaxHighlighter);
		EditorNode::add_init_callback(rust_register_syntax_highlighter);

		// rust-analyzer bridge, reachable from the editor as "RustLsp".
		GDREGISTER_CLASS(RustLsp);
		RustLsp::create_singleton();
		Engine::Singleton rust_lsp_singleton("RustLsp", RustLsp::get_singleton());
		rust_lsp_singleton.editor_only = true;
		Engine::get_singleton()->add_singleton(rust_lsp_singleton);

		RustProject::handle_cmdline();
	}
#endif
}

void uninitialize_rust_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		ScriptServer::unregister_language(rust_language);

		Engine::get_singleton()->remove_singleton("RustScriptRegistry");
		RustScriptRegistry::destroy_singleton();

		ResourceLoader::remove_resource_format_loader(rust_resource_loader);
		rust_resource_loader.unref();
		ResourceSaver::remove_resource_format_saver(rust_resource_saver);
		rust_resource_saver.unref();

		if (rust_language != nullptr) {
			memdelete(rust_language);
			rust_language = nullptr;
		}
		if (rust_diagnostics != nullptr) {
			memdelete(rust_diagnostics);
			rust_diagnostics = nullptr;
		}
	}
#ifdef TOOLS_ENABLED
	else if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		Engine::get_singleton()->remove_singleton("RustLsp");
		RustLsp::destroy_singleton();
		if (rust_build != nullptr) {
			memdelete(rust_build);
			rust_build = nullptr;
		}
	}
#endif
}
