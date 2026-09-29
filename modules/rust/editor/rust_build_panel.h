#pragma once

#include "editor/docks/editor_dock.h"

class Button;
class Label;
class RichTextLabel;
class TabContainer;
class Tree;

// Bottom dock showing cargo output and clickable build diagnostics.
class RustBuildPanel : public EditorDock {
	GDCLASS(RustBuildPanel, EditorDock);

	TabContainer *tabs = nullptr;
	Tree *problems = nullptr;
	RichTextLabel *output_view = nullptr;
	Button *build_button = nullptr;
	Button *cancel_button = nullptr;
	Label *status_label = nullptr;

	uint64_t last_serial = 0;
	int error_count = 0;
	int warning_count = 0;

	void _build_pressed();
	void _cancel_pressed();
	void _clear_pressed();
	void _problem_activated();
	void _navigate_to(const String &p_file, int p_line, int p_column);
	void _refresh_problems();

protected:
	void _notification(int p_what);

public:
	void update_ui();

	RustBuildPanel();
};
