#pragma once

#include "editor/plugins/editor_plugin.h"

class Button;
class RustBuildPanel;

// Editor-side glue: Rust build panel, run-bar build button, cargo build
// before running the project, and loading of the built extension library.
class RustEditorPlugin : public EditorPlugin {
	GDCLASS(RustEditorPlugin, EditorPlugin);

	RustBuildPanel *panel = nullptr;
	Button *build_button = nullptr;
	uint64_t seen_serial = 0;
	bool project_checked = false;

	void _check_project();
	void _try_load_extension();
	// Loads a snapshot of the freshly built library, so the editor picks up script
	// changes without a restart; the previously loaded library stays mapped.
	void _hot_reload_scripts();
	void _reload_open_scenes();
	void _build_pressed();

protected:
	void _notification(int p_what);

public:
	virtual String get_plugin_name() const override { return "Rust"; }
	virtual bool build() override;

	RustEditorPlugin();
	~RustEditorPlugin();
};
