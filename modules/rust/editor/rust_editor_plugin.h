#pragma once

#include "editor/plugins/editor_plugin.h"

#include "core/templates/hash_set.h"

class Button;
class RustBuildPanel;
class ScriptEditorBase;

// Editor-side glue: Rust build panel, run-bar build button, cargo build
// before running the project, and loading of the built extension library.
class RustEditorPlugin : public EditorPlugin {
	GDCLASS(RustEditorPlugin, EditorPlugin);

	RustBuildPanel *panel = nullptr;
	Button *build_button = nullptr;
	uint64_t seen_serial = 0;
	bool project_checked = false;

	// Tabs whose plain-text highlighter was replaced by the Rust one (see
	// _apply_rust_highlighter): a script is only nudged once per session, so a
	// deliberate choice of another highlighter is respected afterwards.
	HashSet<String> highlighter_nudged_scripts;
	ScriptEditorBase *last_highlighter_editor = nullptr;
	int highlighter_scan_frames = 0;
	int highlighter_recheck_frames = 0;
	bool highlighter_scan_done = false;

	void _check_project();
	void _ensure_rust_highlighter();
	void _apply_rust_highlighter(ScriptEditorBase *p_editor);
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
