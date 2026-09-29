#include "rust_build_panel.h"

#include "rust_build.h"
#include "rust_project.h"

#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "editor/editor_log.h"
#include "editor/settings/editor_settings.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/script/script_editor_plugin.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/rich_text_label.h"
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

void RustBuildPanel::update_ui() {
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
