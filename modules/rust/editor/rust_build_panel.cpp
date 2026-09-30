#include "rust_build_panel.h"

#include "rust_build.h"
#include "rust_debug.h"
#include "rust_project.h"

#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "core/os/os.h"
#include "core/object/callable_mp.h"
#include "editor/editor_log.h"
#include "editor/settings/editor_settings.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/script/script_editor_plugin.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/tree.h"

RustBuildPanel::RustBuildPanel() {
	set_title("Rust");
	set_name("Rust");
	set_icon_name(SNAME("RustScript"));
	set_default_slot(EditorDock::DOCK_SLOT_BOTTOM);
	set_available_layouts(EditorDock::DOCK_LAYOUT_HORIZONTAL | EditorDock::DOCK_LAYOUT_FLOATING);
	set_global(false);
	set_transient(true);

	VBoxContainer *vbox = memnew(VBoxContainer);
	vbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	vbox->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	add_child(vbox);

	HBoxContainer *toolbar = memnew(HBoxContainer);
	vbox->add_child(toolbar);

	build_button = memnew(Button);
	build_button->set_text("Build");
	build_button->set_tooltip_text("Run cargo build for this project");
	build_button->connect(SceneStringName(pressed), callable_mp(this, &RustBuildPanel::_build_pressed));
	toolbar->add_child(build_button);

	cancel_button = memnew(Button);
	cancel_button->set_text("Cancel");
	cancel_button->set_disabled(true);
	cancel_button->connect(SceneStringName(pressed), callable_mp(this, &RustBuildPanel::_cancel_pressed));
	toolbar->add_child(cancel_button);

	Button *clear_button = memnew(Button);
	clear_button->set_text("Clear");
	clear_button->connect(SceneStringName(pressed), callable_mp(this, &RustBuildPanel::_clear_pressed));
	toolbar->add_child(clear_button);

	status_label = memnew(Label);
	status_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	status_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
	toolbar->add_child(status_label);

	tabs = memnew(TabContainer);
	tabs->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	vbox->add_child(tabs);

	problems = memnew(Tree);
	problems->set_name("Problems");
	problems->set_columns(4);
	problems->set_column_titles_visible(true);
	problems->set_column_title(0, "Message");
	problems->set_column_title(1, "File");
	problems->set_column_title(2, "Line");
	problems->set_column_title(3, "Code");
	problems->set_column_expand(0, true);
	problems->set_column_expand(1, false);
	problems->set_column_custom_minimum_width(1, 220);
	problems->set_column_expand(2, false);
	problems->set_column_custom_minimum_width(2, 60);
	problems->set_column_expand(3, false);
	problems->set_column_custom_minimum_width(3, 80);
	problems->set_hide_root(true);
	problems->connect("item_activated", callable_mp(this, &RustBuildPanel::_problem_activated));
	tabs->add_child(problems);

	output_view = memnew(RichTextLabel);
	output_view->set_name("Output");
	output_view->set_scroll_follow(true);
	output_view->set_selection_enabled(true);
	output_view->set_context_menu_enabled(true);
	output_view->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	output_view->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	tabs->add_child(output_view);

	// Debug tab: breakpoints come from the script editor, the session itself from
	// RustDebug (lldb-dap).
	VBoxContainer *debug_box = memnew(VBoxContainer);
	debug_box->set_name("Debug");

	HBoxContainer *debug_toolbar = memnew(HBoxContainer);
	debug_box->add_child(debug_toolbar);

	debug_button = memnew(Button);
	debug_button->set_text("Debug");
	debug_button->set_tooltip_text("Build, then run the project under lldb-dap with the breakpoints set in .rs files");
	debug_button->connect(SceneStringName(pressed), callable_mp(this, &RustBuildPanel::_debug_start_pressed));
	debug_toolbar->add_child(debug_button);

	debug_stop_button = memnew(Button);
	debug_stop_button->set_text("Stop");
	debug_stop_button->set_disabled(true);
	debug_stop_button->connect(SceneStringName(pressed), callable_mp(this, &RustBuildPanel::_debug_stop_pressed));
	debug_toolbar->add_child(debug_stop_button);

	debug_continue_button = memnew(Button);
	debug_continue_button->set_text("Continue");
	debug_continue_button->set_disabled(true);
	debug_continue_button->connect(SceneStringName(pressed), callable_mp(RustDebug::get_singleton(), &RustDebug::continue_));
	debug_toolbar->add_child(debug_continue_button);

	debug_step_over_button = memnew(Button);
	debug_step_over_button->set_text("Step Over");
	debug_step_over_button->set_disabled(true);
	debug_step_over_button->connect(SceneStringName(pressed), callable_mp(RustDebug::get_singleton(), &RustDebug::step_over));
	debug_toolbar->add_child(debug_step_over_button);

	debug_step_in_button = memnew(Button);
	debug_step_in_button->set_text("Step Into");
	debug_step_in_button->set_disabled(true);
	debug_step_in_button->connect(SceneStringName(pressed), callable_mp(RustDebug::get_singleton(), &RustDebug::step_in));
	debug_toolbar->add_child(debug_step_in_button);

	debug_step_out_button = memnew(Button);
	debug_step_out_button->set_text("Step Out");
	debug_step_out_button->set_disabled(true);
	debug_step_out_button->connect(SceneStringName(pressed), callable_mp(RustDebug::get_singleton(), &RustDebug::step_out));
	debug_toolbar->add_child(debug_step_out_button);

	debug_status = memnew(Label);
	debug_status->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	debug_status->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
	debug_toolbar->add_child(debug_status);

	HSplitContainer *debug_split = memnew(HSplitContainer);
	debug_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	debug_box->add_child(debug_split);

	debug_stack = memnew(Tree);
	debug_stack->set_columns(3);
	debug_stack->set_column_titles_visible(true);
	debug_stack->set_column_title(0, "Function");
	debug_stack->set_column_title(1, "File");
	debug_stack->set_column_title(2, "Line");
	debug_stack->set_column_expand(0, true);
	debug_stack->set_column_expand(1, false);
	debug_stack->set_column_custom_minimum_width(1, 200);
	debug_stack->set_column_expand(2, false);
	debug_stack->set_column_custom_minimum_width(2, 60);
	debug_stack->set_hide_root(true);
	debug_stack->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	debug_stack->connect("item_activated", callable_mp(this, &RustBuildPanel::_debug_stack_activated));
	debug_split->add_child(debug_stack);

	debug_variables = memnew(Tree);
	debug_variables->set_columns(3);
	debug_variables->set_column_titles_visible(true);
	debug_variables->set_column_title(0, "Name");
	debug_variables->set_column_title(1, "Value");
	debug_variables->set_column_title(2, "Type");
	debug_variables->set_column_expand(0, true);
	debug_variables->set_column_expand(1, true);
	debug_variables->set_column_expand(2, false);
	debug_variables->set_column_custom_minimum_width(2, 120);
	debug_variables->set_hide_root(true);
	debug_variables->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	debug_split->add_child(debug_variables);

	tabs->add_child(debug_box);

	set_process(true);
}

void RustBuildPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PROCESS: {
			update_ui();
		} break;
		default: {
		} break;
	}
}

void RustBuildPanel::_build_pressed() {
	RustBuild *build = RustBuild::get_singleton();
	if (build == nullptr || build->is_running()) {
		return;
	}
	String profile = GLOBAL_GET("rust/build/profile");
	if (!build->start_build(profile)) {
		status_label->set_text("Nothing to build");
	}
}

void RustBuildPanel::_cancel_pressed() {
	RustBuild *build = RustBuild::get_singleton();
	if (build != nullptr) {
		build->cancel();
	}
}

void RustBuildPanel::_clear_pressed() {
	RustBuild *build = RustBuild::get_singleton();
	if (build != nullptr) {
		build->clear_output();
	}
	output_view->clear();
	problems->clear();
	error_count = 0;
	warning_count = 0;
	status_label->set_text(String());
}

void RustBuildPanel::_problem_activated() {
	TreeItem *item = problems->get_selected();
	if (item == nullptr) {
		return;
	}
	String file = item->get_metadata(0);
	int line = item->get_metadata(1);
	int column = item->get_metadata(2);
	_navigate_to(file, line, column);
}

void RustBuildPanel::_navigate_to(const String &p_file, int p_line, int p_column) {
	if (p_file.is_empty() || !p_file.begins_with("res://")) {
		return;
	}
	Ref<Resource> res = ResourceLoader::load(p_file, "Script");
	if (res.is_null()) {
		res = ResourceLoader::load(p_file);
	}
	if (res.is_null()) {
		return;
	}
	ScriptEditor::get_singleton()->edit(res, p_line - 1, p_column - 1, true);
	if (EditorNode::get_singleton() != nullptr && EditorNode::get_singleton()->get_editor_main_screen() != nullptr) {
		EditorNode::get_singleton()->get_editor_main_screen()->select(EditorMainScreen::EDITOR_SCRIPT);
	}
}

void RustBuildPanel::_refresh_problems() {
	RustBuild *build = RustBuild::get_singleton();
	problems->clear();
	error_count = 0;
	warning_count = 0;
	if (build == nullptr) {
		return;
	}

	TreeItem *root = problems->create_item();
	Vector<RustDiagnostic> diags = build->get_diagnostics();
	const Color error_color = get_theme_color(SNAME("error_color"), SNAME("Editor"));
	const Color warning_color = get_theme_color(SNAME("warning_color"), SNAME("Editor"));

	EditorLog *log = EditorNode::get_singleton() != nullptr ? EditorNode::get_log() : nullptr;

	for (const RustDiagnostic &diag : diags) {
		TreeItem *item = problems->create_item(root);
		item->set_text(0, diag.message);
		item->set_text(1, diag.file);
		item->set_text(2, itos(diag.line));
		item->set_text(3, diag.code);
		item->set_metadata(0, diag.file);
		item->set_metadata(1, diag.line);
		item->set_metadata(2, diag.column);
		item->set_custom_color(0, diag.type == RustDiagnostic::TYPE_ERROR ? error_color : warning_color);

		if (diag.type == RustDiagnostic::TYPE_ERROR) {
			error_count++;
		} else {
			warning_count++;
		}

		if (log != nullptr && !diag.file.is_empty()) {
			log->add_message(vformat("[url]%s:%d[/url] - %s", diag.file, diag.line, diag.message), diag.type == RustDiagnostic::TYPE_ERROR ? EditorLog::MSG_TYPE_ERROR : EditorLog::MSG_TYPE_WARNING);
		}
	}
}

Dictionary RustBuildPanel::_collect_breakpoints() const {
	Dictionary out;
	ScriptEditor *script_editor = ScriptEditor::get_singleton();
	if (script_editor == nullptr) {
		return out;
	}
	List<String> breakpoints;
	script_editor->get_breakpoints(&breakpoints);
	for (const String &entry : breakpoints) {
		// "res://src/player.rs:12"
		const int separator = entry.rfind(":");
		if (separator <= 0) {
			continue;
		}
		const String path = entry.substr(0, separator);
		const int line = entry.substr(separator + 1).to_int();
		if (!path.ends_with(".rs") || line <= 0) {
			continue;
		}
		if (!out.has(path)) {
			out[path] = Array();
		}
		Array lines = out[path];
		lines.push_back(line);
		out[path] = lines;
	}
	return out;
}

void RustBuildPanel::_debug_start_pressed() {
	RustDebug *debug = RustDebug::get_singleton();
	if (debug == nullptr || debug->is_running()) {
		return;
	}

	// Debug builds carry the debug info the adapter needs.
	RustBuild *build = RustBuild::get_singleton();
	if (build == nullptr || !build->build_blocking("debug")) {
		status_label->set_text("Build failed");
		debug_status->set_text("build failed");
		return;
	}

	const String project_dir = ProjectSettings::get_singleton()->globalize_path("res://").trim_suffix("/");
	PackedStringArray args;
	args.push_back("--path");
	args.push_back(project_dir);
	const Dictionary breakpoints = _collect_breakpoints();
	if (breakpoints.is_empty()) {
		debug_status->set_text("no .rs breakpoints set");
	}
	debug->start_gd(OS::get_singleton()->get_executable_path(), args, project_dir, breakpoints);
	debug_opened_location = String();
}

void RustBuildPanel::_debug_stop_pressed() {
	RustDebug *debug = RustDebug::get_singleton();
	if (debug != nullptr) {
		debug->stop();
	}
	debug_opened_location = String();
}

void RustBuildPanel::_debug_stack_activated() {
	TreeItem *selected = debug_stack->get_selected();
	if (selected == nullptr) {
		return;
	}
	const String file = selected->get_metadata(1);
	const int line = selected->get_metadata(2);
	if (!file.is_empty()) {
		_navigate_to(file, line, 0);
	}
	_refresh_debug_variables(selected->get_metadata(0));
}

void RustBuildPanel::_refresh_debug_variables(int p_frame) {
	debug_variables->clear();
	RustDebug *debug = RustDebug::get_singleton();
	if (debug == nullptr) {
		return;
	}
	const Array variables = debug->get_variables(p_frame);
	TreeItem *root = debug_variables->create_item();
	for (int i = 0; i < variables.size(); i++) {
		const Dictionary variable = variables[i];
		TreeItem *item = debug_variables->create_item(root);
		item->set_text(0, variable.get("name", String()));
		item->set_text(1, variable.get("value", String()));
		item->set_text(2, variable.get("type", String()));
	}
}

void RustBuildPanel::_refresh_debug() {
	RustDebug *debug = RustDebug::get_singleton();
	if (debug == nullptr || debug_status == nullptr) {
		return;
	}
	debug->update();
	const String state = debug->get_state();
	const String adapter = debug->get_adapter_path();
	debug_status->set_text(state + (adapter.is_empty() ? String() : " · " + adapter.get_file()));

	const bool running = debug->is_running();
	const bool stopped = state == "stopped";
	debug_button->set_disabled(running);
	debug_stop_button->set_disabled(!running);
	debug_continue_button->set_disabled(!stopped);
	debug_step_over_button->set_disabled(!stopped);
	debug_step_in_button->set_disabled(!stopped);
	debug_step_out_button->set_disabled(!stopped);

	if (!stopped) {
		if (debug_opened_location != String()) {
			debug_stack->clear();
			debug_variables->clear();
			debug_opened_location = String();
		}
		return;
	}

	const Dictionary location = debug->get_stop_location();
	const String current = vformat("%s:%d", location.get("file", ""), (int)location.get("line", 0));
	if (current == debug_opened_location) {
		return;
	}
	debug_opened_location = current;

	const String file = location.get("file", "");
	if (!file.is_empty()) {
		_navigate_to(file, (int)location.get("line", 0), 0);
	}

	debug_stack->clear();
	const Array stack = debug->get_stack();
	TreeItem *root = debug_stack->create_item();
	TreeItem *first = nullptr;
	for (int i = 0; i < stack.size(); i++) {
		const Dictionary frame = stack[i];
		TreeItem *item = debug_stack->create_item(root);
		item->set_text(0, frame.get("name", String()));
		item->set_text(1, frame.get("file", String()));
		item->set_text(2, itos((int)frame.get("line", 0)));
		item->set_metadata(0, i);
		item->set_metadata(1, frame.get("file", String()));
		item->set_metadata(2, (int)frame.get("line", 0));
		if (i == 0) {
			first = item;
		}
	}
	if (first != nullptr) {
		first->select(0);
	}
	_refresh_debug_variables(0);
}

void RustBuildPanel::update_ui() {
	_refresh_debug();

	RustBuild *build = RustBuild::get_singleton();
	if (build == nullptr) {
		return;
	}

	String new_text = build->drain_new_output();
	if (!new_text.is_empty()) {
		output_view->add_text(new_text);
	}

	uint64_t serial = build->get_result_serial();
	if (serial != last_serial) {
		last_serial = serial;
		_refresh_problems();
		bool failed = build->get_status() == RustBuild::STATUS_FAILED;
		if (failed) {
			if ((bool)EDITOR_GET("rust/show_output_panel_on_error")) {
				make_visible();
				tabs->set_current_tab(1);
			}
			String launch_error = build->get_last_error();
			if (!launch_error.is_empty()) {
				ERR_PRINT(launch_error);
			}
		} else {
			status_label->set_text("Build succeeded");
		}
	}

	bool running = build->is_running();
	build_button->set_disabled(running);
	cancel_button->set_disabled(!running);
	if (running) {
		status_label->set_text("Building...");
	} else if (error_count > 0 || warning_count > 0) {
		status_label->set_text(vformat("%d error(s), %d warning(s)", error_count, warning_count));
	}
}
