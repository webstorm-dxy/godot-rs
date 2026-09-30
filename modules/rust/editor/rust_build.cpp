#include "rust_build.h"

#include "rust_bindings.h"
#include "rust_project.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#include "editor/settings/editor_settings.h"
#include "main/main.h"
#include "servers/display/display_server.h"
#endif

static RustBuild *rust_build_singleton = nullptr;

RustBuild *RustBuild::get_singleton() {
	return rust_build_singleton;
}

RustBuild::RustBuild() {
	rust_build_singleton = this;
	// Headless/CLI builds have no build dock, so mirror cargo output to the
	// console to keep `--build-solutions` useful in CI.
	print_to_console = DisplayServer::get_singleton() != nullptr && DisplayServer::get_singleton()->get_name() == "headless";
}

RustBuild::~RustBuild() {
	if (thread_running) {
		cancel_requested = true;
		if (child_pid != 0) {
			OS::get_singleton()->kill((ProcessID)child_pid);
		}
	}
	if (thread_started) {
		thread.wait_to_finish();
		thread_started = false;
	}
	rust_build_singleton = nullptr;
}

bool RustBuild::is_running() const {
	MutexLock lock(mutex);
	return thread_running;
}

RustBuild::Status RustBuild::get_status() const {
	MutexLock lock(mutex);
	return status;
}

String RustBuild::get_last_error() const {
	MutexLock lock(mutex);
	return last_error;
}

String RustBuild::get_library_path() const {
	MutexLock lock(mutex);
	return library_path;
}

uint64_t RustBuild::get_result_serial() const {
	MutexLock lock(mutex);
	return result_serial;
}

String RustBuild::get_output() const {
	MutexLock lock(mutex);
	String text;
	for (const String &line : output) {
		text += line + "\n";
	}
	return text;
}

String RustBuild::drain_new_output() {
	MutexLock lock(mutex);
	String text;
	for (int i = output_read_index; i < output.size(); i++) {
		text += output[i] + "\n";
	}
	output_read_index = output.size();
	return text;
}

void RustBuild::clear_output() {
	MutexLock lock(mutex);
	output.clear();
	output_read_index = 0;
}

Vector<RustDiagnostic> RustBuild::get_diagnostics() const {
	MutexLock lock(mutex);
	return diagnostics;
}

void RustBuild::_append_output(const String &p_line) {
	if (print_to_console) {
		OS::get_singleton()->print((p_line + "\n").utf8().get_data());
	}
	output.push_back(p_line);
	if (output.size() > 20000) {
		output.remove_at(0);
		if (output_read_index > 0) {
			output_read_index--;
		}
	}
}

String RustBuild::_find_cargo() const {
	String env_cargo = OS::get_singleton()->get_environment("GODOT_RUST_CARGO");
	if (!env_cargo.is_empty()) {
		return env_cargo;
	}
#ifdef TOOLS_ENABLED
	if (EditorSettings::get_singleton() != nullptr) {
		String configured = EditorSettings::get_singleton()->get_setting("rust/cargo_path");
		if (!configured.is_empty()) {
			return configured;
		}
	}
#endif
	String path_env = OS::get_singleton()->get_environment("PATH");
	for (const String &dir : path_env.split(":", false)) {
		String candidate = dir.path_join("cargo");
		if (FileAccess::exists(candidate)) {
			return candidate;
		}
	}
	String home = OS::get_singleton()->get_environment("HOME");
	if (!home.is_empty()) {
		String candidate = home.path_join(".cargo/bin/cargo");
		if (FileAccess::exists(candidate)) {
			return candidate;
		}
	}
	for (const String &candidate : { String("/usr/local/bin/cargo"), String("/opt/homebrew/bin/cargo") }) {
		if (FileAccess::exists(candidate)) {
			return candidate;
		}
	}
	return "cargo";
}

String RustBuild::_to_res_path(const String &p_global_path) const {
	String root = RustProject::get_crate_root_global();
	if (!root.is_empty() && p_global_path.begins_with(root)) {
		String rel = p_global_path.substr(root.length()).trim_prefix("/");
		return "res://" + rel;
	}
	return p_global_path;
}

void RustBuild::_handle_json_message(const String &p_line) {
	JSON json;
	if (json.parse(p_line) != OK) {
		MutexLock lock(mutex);
		_append_output(p_line);
		return;
	}
	Variant data = json.get_data();
	if (data.get_type() != Variant::DICTIONARY) {
		return;
	}
	Dictionary msg = data;
	String reason = msg.get("reason", String());

	if (reason == "compiler-message") {
		Dictionary message = msg.get("message", Dictionary());
		String level = message.get("level", String());
		String text = message.get("message", String());
		String rendered = message.get("rendered", String());

		String file;
		int line = 0;
		int column = 0;
		Array spans = message.get("spans", Array());
		for (int i = 0; i < spans.size(); i++) {
			Dictionary span = spans[i];
			if ((bool)span.get("is_primary", false)) {
				file = _to_res_path(span.get("file_name", String()));
				line = (int)span.get("line_start", 0);
				column = (int)span.get("column_start", 0);
				break;
			}
		}

		if (!file.is_empty() && (level == "error" || level == "warning")) {
			RustDiagnostic diag;
			diag.type = level == "error" ? RustDiagnostic::TYPE_ERROR : RustDiagnostic::TYPE_WARNING;
			diag.file = file;
			diag.line = line;
			diag.column = column;
			diag.message = text;
			Dictionary code = message.get("code", Dictionary());
			if (!code.is_empty()) {
				diag.code = code.get("code", String());
			}
			pending_diags[file].push_back(diag);
		}

		MutexLock lock(mutex);
		String pretty = rendered.strip_edges();
		if (pretty.is_empty()) {
			pretty = vformat("%s: %s", level, text);
		}
		for (const String &pretty_line : pretty.split("\n")) {
			_append_output(pretty_line);
		}
		return;
	}

	if (reason == "compiler-artifact") {
		Dictionary target = msg.get("target", Dictionary());
		PackedStringArray kinds = target.get("kind", PackedStringArray());
		bool is_cdylib = false;
		for (const String &kind : kinds) {
			if (kind == "cdylib") {
				is_cdylib = true;
			}
		}
		if (is_cdylib) {
			PackedStringArray filenames = msg.get("filenames", PackedStringArray());
			for (const String &filename : filenames) {
				String ext = filename.get_extension().to_lower();
				if (ext == "dylib" || ext == "so" || ext == "dll") {
					MutexLock lock(mutex);
					library_path = filename;
				}
			}
		}
		return;
	}

	if (reason == "build-finished") {
		bool ok = msg.get("success", false);
		MutexLock lock(mutex);
		status = ok ? STATUS_SUCCESS : STATUS_FAILED;
		return;
	}
}

bool RustBuild::_pump_pipe(Ref<FileAccess> p_pipe, Vector<uint8_t> &r_partial, bool p_stdout) {
	if (p_pipe.is_null()) {
		return false;
	}
	uint8_t buffer[4096];
	uint64_t read = p_pipe->get_buffer(buffer, sizeof(buffer));
	if (read == 0) {
		return false;
	}
	const int old_size = r_partial.size();
	r_partial.resize(old_size + (int)read);
	memcpy(r_partial.ptrw() + old_size, buffer, (size_t)read);

	// Lines are cut out of the raw bytes: a read stops wherever the pipe decides, and
	// decoding a UTF-8 character split across two reads turns it into U+FFFD and logs
	// "Unicode parsing error".
	while (true) {
		int newline = -1;
		for (int i = 0; i < r_partial.size(); i++) {
			if (r_partial[i] == '\n') {
				newline = i;
				break;
			}
		}
		if (newline < 0) {
			break;
		}
		const String line = String::utf8((const char *)r_partial.ptr(), newline).strip_edges();
		r_partial = r_partial.slice(newline + 1);
		if (!line.is_empty()) {
			if (p_stdout) {
				_handle_json_message(line);
			} else {
				MutexLock lock(mutex);
				_append_output(line);
			}
		}
	}
	return true;
}

void RustBuild::_thread_func(void *p_userdata) {
	RustBuild *self = (RustBuild *)p_userdata;
	self->_run(self->request_profile);
}

void RustBuild::_run(const String &p_profile) {
	pending_diags.clear();

	const String cargo = _find_cargo();
	const String manifest = RustProject::get_cargo_manifest_global();

	if (manifest.is_empty() || !FileAccess::exists(manifest)) {
		MutexLock lock(mutex);
		last_error = vformat("No Rust project found at '%s'.", manifest);
		_append_output(last_error);
		status = STATUS_FAILED;
		result_serial++;
		thread_running = false;
		return;
	}

	const String target_dir = RustProject::get_target_dir_global().replace("\\", "/");
	const String bindings_dir = RustProject::get_bindings_dir_global().replace("\\", "/");

	List<String> args;
	args.push_back("build");
	args.push_back("--manifest-path");
	args.push_back(manifest);
	args.push_back("--message-format=json-render-diagnostics");
	if (p_profile == "release") {
		args.push_back("--release");
	}
	// Cargo rejects inline tables on the command line, so the env entries use
	// dotted keys (`env.NAME.value` / `env.NAME.force`).
	args.push_back("--config");
	args.push_back(vformat("build.target-dir=\"%s\"", target_dir));
	args.push_back("--config");
	args.push_back(vformat("env.GDRUST_GODOT_API_JSON.value=\"%s/extension_api.json\"", bindings_dir));
	args.push_back("--config");
	args.push_back("env.GDRUST_GODOT_API_JSON.force=true");
	args.push_back("--config");
	args.push_back(vformat("env.GDRUST_GODOT_INTERFACE_JSON.value=\"%s/gdextension_interface.json\"", bindings_dir));
	args.push_back("--config");
	args.push_back("env.GDRUST_GODOT_INTERFACE_JSON.force=true");
	args.push_back("--config");
	args.push_back("term.color=\"never\"");

	String extra = GLOBAL_GET("rust/build/extra_flags");
	for (const String &flag : extra.split(" ", false)) {
		if (!flag.is_empty()) {
			args.push_back(flag);
		}
	}

	String cmdline = "$ cargo";
	for (const String &arg : args) {
		cmdline += " " + arg;
	}
	{
		MutexLock lock(mutex);
		_append_output(cmdline);
		library_path = String();
	}

	Dictionary pipe = OS::get_singleton()->execute_with_pipe(cargo, args, false);
	if (!pipe.has("stdio")) {
		MutexLock lock(mutex);
		last_error = "Could not launch cargo. Install the Rust toolchain or set rust/cargo_path in the editor settings.";
		_append_output(last_error);
		status = STATUS_FAILED;
		result_serial++;
		thread_running = false;
		return;
	}

	Ref<FileAccess> stdio = pipe["stdio"];
	Ref<FileAccess> stderr_pipe = pipe["stderr"];
	int64_t pid = pipe.has("pid") ? (int64_t)pipe["pid"] : 0;
	{
		MutexLock lock(mutex);
		child_pid = pid;
	}

	Vector<uint8_t> stdout_partial;
	Vector<uint8_t> stderr_partial;
	while (true) {
		bool got = _pump_pipe(stdio, stdout_partial, true);
		got = _pump_pipe(stderr_pipe, stderr_partial, false) || got;
		bool running = pid != 0 && OS::get_singleton()->is_process_running((ProcessID)pid);
		bool cancelled = false;
		{
			MutexLock lock(mutex);
			cancelled = cancel_requested;
		}
		if (!running || cancelled) {
			break;
		}
		if (!got) {
			OS::get_singleton()->delay_usec(2000);
		}
	}
	_pump_pipe(stdio, stdout_partial, true);
	_pump_pipe(stderr_pipe, stderr_partial, false);

	bool success = false;
	{
		MutexLock lock(mutex);
		success = status == STATUS_SUCCESS;
	}

	RustDiagnostics::get_singleton()->publish(pending_diags);
	if (success) {
		RustProject::ensure_extension_config();
	}

	{
		MutexLock lock(mutex);
		diagnostics.clear();
		for (const KeyValue<String, Vector<RustDiagnostic>> &E : pending_diags) {
			for (const RustDiagnostic &diag : E.value) {
				diagnostics.push_back(diag);
			}
		}
		status = success ? STATUS_SUCCESS : STATUS_FAILED;
		if (!success && last_error.is_empty()) {
			last_error = "cargo build failed. See the Rust output panel for details.";
		}
		result_serial++;
		thread_running = false;
		child_pid = 0;
	}
}

bool RustBuild::start_build(const String &p_profile) {
	{
		MutexLock lock(mutex);
		if (thread_running) {
			return false;
		}
	}

	// Godot threads must be joined before they can be started again.
	if (thread_started) {
		thread.wait_to_finish();
		thread_started = false;
	}

	if (!RustProject::project_uses_rust() || RustProject::get_cargo_manifest_global().is_empty()) {
		return false;
	}

	// Bindings generation touches ClassDB, so it must run on the main thread.
	// The crate is synced here as well (not only on editor startup) so that a
	// project created by an older build is repaired before cargo ever runs.
	RustProject::sync_crate();
	RustBindings::ensure();

	{
		MutexLock lock(mutex);
		status = STATUS_RUNNING;
		last_error = String();
		cancel_requested = false;
		thread_running = true;
		request_profile = p_profile;
	}
	thread.start(_thread_func, this);
	thread_started = true;
	return true;
}

bool RustBuild::wait_for_build() {
#ifdef TOOLS_ENABLED
	static bool pumping = false;
	// Headless runs print progress lines for every step, so only show the
	// progress dialog when there is a display server.
	const bool has_display = DisplayServer::get_singleton() != nullptr && DisplayServer::get_singleton()->get_name() != "headless";
	EditorProgress *progress = has_display ? memnew(EditorProgress("rust_build", "Building Rust project...", 1)) : nullptr;
	while (is_running()) {
		if (Thread::is_main_thread() && !pumping && EditorNode::get_singleton() != nullptr) {
			pumping = true;
			Main::iteration();
			pumping = false;
		} else {
			OS::get_singleton()->delay_usec(2000);
		}
	}
	if (progress != nullptr) {
		memdelete(progress);
	}
	if (thread_started) {
		thread.wait_to_finish();
		thread_started = false;
	}
	return get_status() == STATUS_SUCCESS;
#else
	while (is_running()) {
		OS::get_singleton()->delay_usec(2000);
	}
	return get_status() == STATUS_SUCCESS;
#endif
}

bool RustBuild::build_blocking(const String &p_profile) {
	if (is_running()) {
		return wait_for_build();
	}
#ifdef TOOLS_ENABLED
	if (EditorSettings::get_singleton() != nullptr && (bool)EditorSettings::get_singleton()->get_setting("rust/skip_build_before_playing")) {
		return true;
	}
#endif
	if (!start_build(p_profile)) {
		return get_status() != STATUS_FAILED;
	}
	return wait_for_build();
}

void RustBuild::cancel() {
	MutexLock lock(mutex);
	cancel_requested = true;
	if (child_pid != 0) {
		OS::get_singleton()->kill((ProcessID)child_pid);
	}
}
