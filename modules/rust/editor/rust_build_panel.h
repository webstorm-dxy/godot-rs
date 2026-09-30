#pragma once

#include "editor/docks/editor_dock.h"

class Button;
class Label;
class RichTextLabel;
class TabContainer;
class Tree;
class HSplitContainer;

// Bottom dock showing cargo output, clickable build diagnostics and the lldb-dap
// debug session (breakpoints from the script editor, stepping, stack, variables).
class RustBuildPanel : public EditorDock {
	GDCLASS(RustBuildPanel, EditorDock);

	TabContainer *tabs = nullptr;
	Tree *problems = nullptr;
	RichTextLabel *output_view = nullptr;
	Button *build_button = nullptr;
	Button *cancel_button = nullptr;
	Label *status_label = nullptr;

	// Debug tab.
	Button *debug_button = nullptr;
	Button *debug_stop_button = nullptr;
	Button *debug_continue_button = nullptr;
	Button *debug_step_over_button = nullptr;
	Button *debug_step_in_button = nullptr;
	Button *debug_step_out_button = nullptr;
	Label *debug_status = nullptr;
	Tree *debug_stack = nullptr;
	Tree *debug_variables = nullptr;
	int debug_stack_frames = 0;
	String debug_opened_location;

	uint64_t last_serial = 0;
	int error_count = 0;
	int warning_count = 0;

	void _build_pressed();
	void _cancel_pressed();
	void _clear_pressed();
	void _problem_activated();
	void _navigate_to(const String &p_file, int p_line, int p_column);
	void _refresh_problems();

	// Debug tab.
	void _debug_start_pressed();
	void _debug_stop_pressed();
	void _debug_stack_activated();
	void _refresh_debug_variables(int p_frame);
	Dictionary _collect_breakpoints() const;
	void _refresh_debug();

protected:
	void _notification(int p_what);

public:
	void update_ui();

	RustBuildPanel();
};
