#include "rust_editor_plugin.h"

#include "rust_build.h"
#include "rust_lsp.h"
#include "rust_build_panel.h"
#include "rust_bindings.h"
#include "rust_project.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/extension/gdextension_manager.h"
#include "core/io/file_access.h"
#include "editor/editor_data.h"
#include "editor/editor_node.h"
#include "editor/run/editor_run_bar.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"

RustEditorPlugin::RustEditorPlugin() {
}

RustEditorPlugin::~RustEditorPlugin() {
}

void RustEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (panel == nullptr) {
				panel = memnew(RustBuildPanel);
				add_dock(panel);
			}
			if (build_button == nullptr) {
				build_button = memnew(Button);
				build_button->set_theme_type_variation("RunBarButton");
				build_button->set_tooltip_text("Build Rust project");
				build_button->connect(SceneStringName(pressed), callable_mp(this, &RustEditorPlugin::_build_pressed));
				HBoxContainer *buttons = EditorRunBar::get_singleton() != nullptr ? EditorRunBar::get_singleton()->get_buttons_container() : nullptr;
				if (buttons != nullptr) {
					buttons->add_child(build_button);
					buttons->move_child(build_button, 0);
					// Theme lookups only resolve for controls that are inside the
					// editor tree, so query the icon through the run bar container.
					const Ref<Texture2D> icon = buttons->get_editor_theme_icon(SNAME("BuildRust"));
					if (icon.is_null()) {
						WARN_PRINT("Rust: editor icon 'BuildRust' could not be resolved.");
					}
					build_button->set_button_icon(icon);
				} else {
					memdelete(build_button);
					build_button = nullptr;
				}
			}
			RustProject::register_editor_settings();
			set_process(true);
			callable_mp(this, &RustEditorPlugin::_check_project).call_deferred();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			set_process(false);
			if (build_button != nullptr) {
				build_button->queue_free();
				build_button = nullptr;
			}
			if (panel != nullptr) {
				remove_dock(panel);
				memdelete(panel);
				panel = nullptr;
			}
		} break;
		case NOTIFICATION_PROCESS: {
			RustBuild *build = RustBuild::get_singleton();
			if (build == nullptr) {
				return;
			}
			uint64_t serial = build->get_result_serial();
			if (serial == seen_serial) {
				return;
			}
			seen_serial = serial;
			if (build->get_status() != RustBuild::STATUS_SUCCESS) {
				return;
			}

			GDExtensionManager *manager = GDExtensionManager::get_singleton();
			const String config = RustProject::get_extension_config_res();
			if (!manager->is_extension_loaded(config)) {
				_try_load_extension();
			} else {
				WARN_PRINT("Rust library rebuilt. Restart the editor to refresh Rust classes in the editor; running the game always uses the new library.");
			}
		} break;
		default: {
		} break;
	}
}

void RustEditorPlugin::_check_project() {
	project_checked = true;
	if (!RustProject::project_uses_rust()) {
		return;
	}

	RustBindings::ensure();
	RustProject::sync_crate_modules();

	// Language server: completion, hover, go-to-definition and inline diagnostics.
	if ((bool)GLOBAL_GET("rust/lsp/enabled")) {
		if (RustLsp *lsp = RustLsp::get_singleton(); lsp != nullptr && !lsp->is_running()) {
			lsp->start(RustProject::get_crate_root_global());
		}
	}

	String lib = RustProject::find_existing_library();
	bool need_build = lib.is_empty() || RustProject::is_library_stale(lib);
	if (need_build && (bool)GLOBAL_GET("rust/build/on_editor_startup")) {
		RustBuild::get_singleton()->start_build(GLOBAL_GET("rust/build/profile"));
	} else {
		_try_load_extension();
	}
}

void RustEditorPlugin::_try_load_extension() {
	GDExtensionManager *manager = GDExtensionManager::get_singleton();
	const String config = RustProject::get_extension_config_res();
	if (manager->is_extension_loaded(config)) {
		return;
	}
	if (!FileAccess::exists(RustProject::get_extension_config_global())) {
		return;
	}
	if (RustProject::find_existing_library().is_empty()) {
		return;
	}

	GDExtensionManager::LoadStatus status = manager->load_extension(config);
	if (status == GDExtensionManager::LOAD_STATUS_OK) {
		print_line(vformat("Rust extension loaded: %s", config));

		// Scenes opened before the build could not instantiate Rust node types;
		// refresh the ones without unsaved changes so they show up.
		EditorNode *editor = EditorNode::get_singleton();
		if (editor != nullptr) {
			EditorData &data = editor->get_editor_data();
			for (int i = 0; i < data.get_edited_scene_count(); i++) {
				if (data.is_scene_changed(i)) {
					continue;
				}
				const String scene_path = data.get_scene_path(i);
				if (!scene_path.is_empty()) {
					editor->reload_scene(scene_path);
				}
			}
		}
	} else if (status == GDExtensionManager::LOAD_STATUS_FAILED) {
		ERR_PRINT(vformat("Failed to load the Rust extension '%s'. Check the build output for details.", config));
	}
}

void RustEditorPlugin::_build_pressed() {
	RustBuild *build = RustBuild::get_singleton();
	if (build == nullptr || build->is_running()) {
		return;
	}
	if (panel != nullptr) {
		panel->make_visible();
	}
	build->start_build(GLOBAL_GET("rust/build/profile"));
}

bool RustEditorPlugin::build() {
	RustBuild *build = RustBuild::get_singleton();
	if (build == nullptr || !RustProject::project_uses_rust()) {
		return true;
	}
	if (!(bool)GLOBAL_GET("rust/build/before_playing")) {
		return true;
	}

	bool ok = build->build_blocking(GLOBAL_GET("rust/build/profile"));
	if (!ok) {
		if (panel != nullptr) {
			panel->make_visible();
		}
		if (EditorNode::get_singleton() != nullptr) {
			EditorNode::get_singleton()->show_accept("Failed to build the Rust project. Check the Rust panel for details.", "OK");
		}
	}
	return ok;
}
