#include "rust_debug.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/variant/variant.h"

static RustDebug *rust_debug_singleton = nullptr;

RustDebug *RustDebug::get_singleton() {
	return rust_debug_singleton;
}

void RustDebug::create_singleton() {
	ERR_FAIL_COND(rust_debug_singleton != nullptr);
	rust_debug_singleton = memnew(RustDebug);
}

void RustDebug::destroy_singleton() {
	ERR_FAIL_NULL(rust_debug_singleton);
	memdelete(rust_debug_singleton);
	rust_debug_singleton = nullptr;
}

RustDebug::RustDebug() {
	state = "idle";
}

RustDebug::~RustDebug() {
	stop();
}

void RustDebug::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start_gd", "program", "args", "cwd", "breakpoints"), &RustDebug::start_gd);
	ClassDB::bind_method(D_METHOD("stop"), &RustDebug::stop);
	ClassDB::bind_method(D_METHOD("is_running"), &RustDebug::is_running);
	ClassDB::bind_method(D_METHOD("get_state"), &RustDebug::get_state);
	ClassDB::bind_method(D_METHOD("get_stop_location_gd"), &RustDebug::get_stop_location_gd);
	ClassDB::bind_method(D_METHOD("get_stack_gd"), &RustDebug::get_stack_gd);
	ClassDB::bind_method(D_METHOD("get_variables_gd", "frame"), &RustDebug::get_variables_gd);
	ClassDB::bind_method(D_METHOD("get_output"), &RustDebug::get_output);
	ClassDB::bind_method(D_METHOD("get_adapter_path"), &RustDebug::get_adapter_path);
	ClassDB::bind_method(D_METHOD("get_error"), &RustDebug::get_error);
	ClassDB::bind_method(D_METHOD("continue_"), &RustDebug::continue_);
	ClassDB::bind_method(D_METHOD("step_over"), &RustDebug::step_over);
	ClassDB::bind_method(D_METHOD("step_in"), &RustDebug::step_in);
	ClassDB::bind_method(D_METHOD("step_out"), &RustDebug::step_out);
	ClassDB::bind_method(D_METHOD("pause"), &RustDebug::pause);
	ClassDB::bind_method(D_METHOD("set_breakpoints_gd", "breakpoints"), &RustDebug::set_breakpoints);
	ClassDB::bind_method(D_METHOD("update"), &RustDebug::update);
}

String RustDebug::_find_adapter() const {
	// An explicit setting wins.
	if (ProjectSettings::get_singleton()->has_setting("rust/lldb_dap_path")) {
		const String configured = GLOBAL_GET("rust/lldb_dap_path");
		if (!configured.is_empty() && FileAccess::exists(configured)) {
			return configured;
		}
	}
	const char *candidates[] = {
		"/usr/bin/lldb-dap",
		"/Library/Developer/CommandLineTools/usr/bin/lldb-dap",
		"/Applications/Xcode.app/Contents/Developer/usr/bin/lldb-dap",
		"/usr/local/bin/lldb-dap",
		"/opt/homebrew/bin/lldb-dap",
	};
	for (const char *candidate : candidates) {
		if (FileAccess::exists(candidate)) {
			return candidate;
		}
	}
	// Fall back to PATH, the way rust-analyzer is looked up.
	const String path_env = OS::get_singleton()->get_environment("PATH");
	for (const String &dir : path_env.split(":")) {
		if (dir.is_empty()) {
			continue;
		}
		const String candidate = dir.path_join("lldb-dap");
		if (FileAccess::exists(candidate)) {
			return candidate;
		}
	}
	return String();
}

String RustDebug::_res_to_global(const String &p_path) {
	return ProjectSettings::get_singleton()->globalize_path(p_path);
}

Dictionary RustDebug::_breakpoints_to_dap(const Dictionary &p_breakpoints) {
	// {"res://src/player.rs": [12, 30]} -> {"/abs/src/player.rs": [12, 30]}
	Dictionary out;
	for (const Variant *key = p_breakpoints.next(nullptr); key != nullptr; key = p_breakpoints.next(key)) {
		const String path = *key;
		const Array lines = p_breakpoints[*key];
		Array values;
		for (int i = 0; i < lines.size(); i++) {
			values.push_back((int)lines[i]);
		}
		out[_res_to_global(path)] = values;
	}
	return out;
}

bool RustDebug::_write_message(const Dictionary &p_message) {
	MutexLock lock(mutex);
	if (stdio.is_null()) {
		return false;
	}
	const String body = JSON::stringify(p_message);
	const String header = vformat("Content-Length: %d\r\n\r\n", body.utf8().length());
	stdio->store_string(header + body);
	return true;
}

bool RustDebug::_send_request(const String &p_command, const Dictionary &p_arguments, int &r_id, String &r_error) {
	int id = 0;
	{
		MutexLock lock(mutex);
		if (!running) {
			r_error = "the debug session is not running";
			return false;
		}
		id = next_id++;
	}
	Dictionary message;
	message["seq"] = id;
	message["type"] = "request";
	message["command"] = p_command;
	message["arguments"] = p_arguments;
	if (!_write_message(message)) {
		r_error = "could not write to lldb-dap";
		return false;
	}
	r_id = id;
	return true;
}

bool RustDebug::_wait_response(int p_id, Dictionary &r_result, int p_timeout_ms, String &r_error) {
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + (uint64_t)p_timeout_ms;
	while (true) {
		{
			MutexLock lock(mutex);
			if (responses.has(p_id)) {
				const Dictionary response = responses[p_id];
				responses.erase(p_id);
				if (!(bool)response.get("success", false)) {
					r_error = response.get("message", "the debug adapter rejected the request");
					return false;
				}
				r_result = response.has("body") ? (Dictionary)response["body"] : Dictionary();
				return true;
			}
			if (!running) {
				r_error = "the debug session ended";
				return false;
			}
		}
		if (OS::get_singleton()->get_ticks_msec() > deadline) {
			r_error = vformat("timed out waiting for request %d", p_id);
			return false;
		}
		OS::get_singleton()->delay_usec(2000);
	}
}

bool RustDebug::_request(const String &p_command, const Dictionary &p_arguments, Dictionary &r_result, int p_timeout_ms, String &r_error) {
	int id = 0;
	if (!_send_request(p_command, p_arguments, id, r_error)) {
		return false;
	}
	return _wait_response(id, r_result, p_timeout_ms, r_error);
}

void RustDebug::_thread_func(void *p_userdata) {
	static_cast<RustDebug *>(p_userdata)->_read_loop();
}

void RustDebug::_handle_event(const Dictionary &p_event) {
	const String event = p_event.get("event", "");
	const Dictionary body = p_event.has("body") ? (Dictionary)p_event["body"] : Dictionary();
	MutexLock lock(mutex);
	if (event == "initialized") {
		initialized_event = true;
	} else if (event == "stopped") {
		current_thread = (int64_t)body.get("threadId", 0);
		state = "stopped";
		refresh_needed = true;
	} else if (event == "continued") {
		state = "running";
		stack.clear();
	} else if (event == "terminated" || event == "exited") {
		state = "terminated";
		running = false;
	} else if (event == "output") {
		output += String(body.get("output", ""));
		// Keep the tail only: a long session would grow without bound.
		if (output.length() > 200000) {
			output = output.substr(output.length() - 100000);
		}
	}
}

void RustDebug::_handle_message(const Dictionary &p_message) {
	const String type = p_message.get("type", "");
	if (type == "response") {
		MutexLock lock(mutex);
		responses[(int)p_message.get("request_seq", 0)] = p_message;
	} else if (type == "event") {
		_handle_event(p_message);
	}
}

void RustDebug::_read_loop() {
	Vector<uint8_t> buffer;
	Vector<uint8_t> stderr_pending;
	uint8_t chunk[4096];

	while (true) {
		{
			MutexLock lock(mutex);
			if (!running) {
				break;
			}
		}

		uint64_t read = 0;
		{
			MutexLock lock(mutex);
			if (stdio.is_valid()) {
				read = stdio->get_buffer(chunk, sizeof(chunk));
			}
			if (stderr_pipe.is_valid()) {
				uint8_t err_chunk[1024];
				const uint64_t err_read = stderr_pipe->get_buffer(err_chunk, sizeof(err_chunk));
				if (err_read > 0) {
					adapter_stderr.push_back(String::utf8((const char *)err_chunk, (int)err_read).strip_edges());
					while (adapter_stderr.size() > 20) {
						adapter_stderr.remove_at(0);
					}
				}
			}
		}
		if (read == 0) {
			bool alive = true;
			{
				MutexLock lock(mutex);
				alive = child_pid == 0 || OS::get_singleton()->is_process_running((ProcessID)child_pid);
			}
			if (!alive) {
				MutexLock lock(mutex);
				running = false;
				if (state != "terminated") {
					state = "terminated";
				}
				break;
			}
			OS::get_singleton()->delay_usec(2000);
			continue;
		}
		if (read > 0) {
			const int buffer_old = buffer.size();
			buffer.resize(buffer_old + (int)read);
			memcpy(buffer.ptrw() + buffer_old, chunk, (size_t)read);
		}

		while (true) {
			int header_end = -1;
			for (int i = 0; i + 3 < buffer.size(); i++) {
				if (buffer[i] == '\r' && buffer[i + 1] == '\n' && buffer[i + 2] == '\r' && buffer[i + 3] == '\n') {
					header_end = i;
					break;
				}
			}
			if (header_end < 0) {
				break;
			}
			const String header = String::utf8((const char *)buffer.ptr(), header_end);
			int length = -1;
			for (const String &line : header.split("\r\n")) {
				if (line.begins_with("Content-Length:")) {
					length = line.substr(15).strip_edges().to_int();
				}
			}
			const int body_start = header_end + 4;
			if (length < 0) {
				// Not a DAP header: the debuggee's own output can share this pipe.
				// Drop the junk and look for the next frame.
				buffer = buffer.slice(body_start);
				continue;
			}
			if (buffer.size() < body_start + length) {
				break;
			}
			const String body = String::utf8((const char *)buffer.ptr() + body_start, length);
			buffer = buffer.slice(body_start + length);

			JSON json;
			if (json.parse(body) == OK && json.get_data().get_type() == Variant::DICTIONARY) {
				_handle_message(json.get_data());
			}
		}
	}
}

bool RustDebug::is_running() const {
	MutexLock lock(mutex);
	return running;
}

String RustDebug::get_state() const {
	MutexLock lock(mutex);
	return state;
}

Dictionary RustDebug::get_stop_location() const {
	MutexLock lock(mutex);
	return stop_location;
}

Array RustDebug::get_stack() {
	update();
	MutexLock lock(mutex);
	return stack;
}

String RustDebug::get_output() const {
	MutexLock lock(mutex);
	return output;
}

String RustDebug::get_adapter_path() const {
	MutexLock lock(mutex);
	return adapter_path;
}

String RustDebug::get_error() const {
	MutexLock lock(mutex);
	return last_error;
}

void RustDebug::update() {
	bool needs_refresh = false;
	{
		MutexLock lock(mutex);
		needs_refresh = refresh_needed && state == "stopped" && running;
		refresh_needed = false;
	}
	if (needs_refresh) {
		_refresh_stack();
	}
}

String RustDebug::_global_to_res(const String &p_path) {
	const String root = ProjectSettings::get_singleton()->globalize_path("res://");
	if (p_path.begins_with(root)) {
		return "res://" + p_path.substr(root.length());
	}
	return p_path;
}

void RustDebug::_refresh_stack() {
	String error;
	Dictionary threads_result;
	if (!_request("threads", Dictionary(), threads_result, 5000, error)) {
		return;
	}

	int64_t thread_id = 0;
	{
		MutexLock lock(mutex);
		thread_id = current_thread;
	}
	const Array threads = threads_result.get("threads", Array());
	if (thread_id == 0 && !threads.is_empty()) {
		thread_id = (int64_t)((Dictionary)threads[0]).get("id", 0);
	}
	if (thread_id == 0) {
		return;
	}

	Dictionary args;
	args["threadId"] = thread_id;
	args["startFrame"] = 0;
	args["levels"] = 32;
	Dictionary stack_result;
	if (!_request("stackTrace", args, stack_result, 5000, error)) {
		return;
	}

	const Array frames = stack_result.get("stackFrames", Array());
	Array entries;
	Dictionary location;
	for (int i = 0; i < frames.size(); i++) {
		const Dictionary frame = frames[i];
		const Dictionary source = frame.has("source") ? (Dictionary)frame["source"] : Dictionary();
		const String path = source.get("path", "");
		Dictionary entry;
		entry["id"] = frame.get("id", 0);
		entry["name"] = frame.get("name", String());
		entry["line"] = (int)frame.get("line", 0);
		entry["source"] = path;
		entry["file"] = path.is_empty() ? String() : _global_to_res(path);
		entries.push_back(entry);
		if (i == 0) {
			location["file"] = entry["file"];
			location["line"] = entry["line"];
			location["function"] = entry["name"];
			location["source"] = path;
		}
	}

	MutexLock lock(mutex);
	stack = entries;
	current_thread = thread_id;
	top_frame = entries.is_empty() ? 0 : (int64_t)((Dictionary)entries[0]).get("id", 0);
	if (!location.is_empty()) {
		stop_location = location;
	}
}

Array RustDebug::get_variables(int p_frame) {
	update();

	int64_t frame_id = 0;
	{
		MutexLock lock(mutex);
		if (state != "stopped" || p_frame < 0 || p_frame >= stack.size()) {
			return Array();
		}
		frame_id = (int64_t)((Dictionary)stack[p_frame]).get("id", 0);
	}
	if (frame_id == 0) {
		return Array();
	}

	String error;
	Dictionary scopes_args;
	scopes_args["frameId"] = frame_id;
	Dictionary scopes_result;
	if (!_request("scopes", scopes_args, scopes_result, 5000, error)) {
		return Array();
	}

	int64_t reference = 0;
	for (const Variant &scope : (Array)scopes_result.get("scopes", Array())) {
		const int64_t candidate = (int64_t)((Dictionary)scope).get("variablesReference", 0);
		if (candidate > 0) {
			reference = candidate;
			break;
		}
	}
	if (reference == 0) {
		return Array();
	}

	Dictionary variables_args;
	variables_args["variablesReference"] = reference;
	Dictionary variables_result;
	if (!_request("variables", variables_args, variables_result, 5000, error)) {
		return Array();
	}

	Array out;
	for (const Variant &variable : (Array)variables_result.get("variables", Array())) {
		const Dictionary entry = variable;
		Dictionary item;
		item["name"] = entry.get("name", String());
		item["value"] = entry.get("value", String());
		item["type"] = entry.get("type", String());
		out.push_back(item);
	}
	return out;
}

void RustDebug::_apply_breakpoints(const Dictionary &p_breakpoints, String &r_error) {
	const Dictionary mapped = _breakpoints_to_dap(p_breakpoints);
	for (const Variant *key = mapped.next(nullptr); key != nullptr; key = mapped.next(key)) {
		const String path = *key;
		const Array lines = mapped[*key];

		Array breakpoints;
		for (int i = 0; i < lines.size(); i++) {
			Dictionary breakpoint;
			breakpoint["line"] = (int)lines[i];
			breakpoints.push_back(breakpoint);
		}
		Dictionary source;
		source["path"] = path;
		source["name"] = path.get_file();

		Dictionary arguments;
		arguments["source"] = source;
		arguments["breakpoints"] = breakpoints;

		Dictionary result;
		String error;
		if (!_request("setBreakpoints", arguments, result, 10000, error)) {
			r_error = vformat("could not set breakpoints in %s: %s", path, error);
			return;
		}
	}
}

void RustDebug::set_breakpoints(const Dictionary &p_breakpoints) {
	if (!is_running()) {
		return;
	}
	{
		MutexLock lock(mutex);
		breakpoints = p_breakpoints;
	}
	String error;
	_apply_breakpoints(p_breakpoints, error);
	if (!error.is_empty()) {
		WARN_PRINT(vformat("Rust: %s", error));
	}
}

bool RustDebug::start(const String &p_program, const PackedStringArray &p_args, const String &p_cwd, const Dictionary &p_breakpoints, String &r_error) {
	stop();

	if (!FileAccess::exists(p_program)) {
		r_error = vformat("'%s' does not exist", p_program);
		return false;
	}

	const String adapter = _find_adapter();
	if (adapter.is_empty()) {
		r_error = "lldb-dap was not found; install the Xcode command line tools or set rust/lldb_dap_path";
		return false;
	}

	List<String> args;
	const Dictionary pipe = OS::get_singleton()->execute_with_pipe(adapter, args, false);
	if (!pipe.has("stdio")) {
		r_error = vformat("could not start '%s'", adapter);
		return false;
	}

	{
		MutexLock lock(mutex);
		stdio = pipe["stdio"];
		stderr_pipe = pipe.has("stderr") ? (Ref<FileAccess>)pipe["stderr"] : Ref<FileAccess>();
		child_pid = pipe.has("pid") ? (int64_t)pipe["pid"] : 0;
		adapter_path = adapter;
		program = p_program;
		cwd = p_cwd;
		breakpoints = p_breakpoints;
		running = true;
		thread_started = true;
		initialized_event = false;
		refresh_needed = false;
		current_thread = 0;
		top_frame = 0;
		stack.clear();
		output.clear();
		last_error.clear();
		stop_location = Dictionary();
		state = "starting";
	}
	thread.start(_thread_func, this);

	Dictionary init_args;
	init_args["clientID"] = "godot-rust-module";
	init_args["adapterID"] = "lldb-dap";
	init_args["linesStartAt1"] = true;
	init_args["columnsStartAt1"] = true;
	init_args["pathFormat"] = "path";
	init_args["supportsVariableType"] = true;
	init_args["supportsVariablePaging"] = false;
	init_args["supportsRunInTerminalRequest"] = false;

	Dictionary result;
	if (!_request("initialize", init_args, result, 20000, r_error)) {
		stop();
		return false;
	}

	// The adapter announces its readiness with the `initialized` event.
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + 20000;
	while (true) {
		{
			MutexLock lock(mutex);
			if (initialized_event || !running) {
				break;
			}
		}
		if (OS::get_singleton()->get_ticks_msec() > deadline) {
			break;
		}
		OS::get_singleton()->delay_usec(2000);
	}

	// `launch` is answered once the configuration is done, so it goes out without
	// waiting: breakpoints and configurationDone have to be sent first.
	Dictionary launch_args;
	launch_args["program"] = p_program;
	launch_args["args"] = p_args;
	launch_args["cwd"] = p_cwd;
	launch_args["stopOnEntry"] = false;
	// The game's own output then arrives as `output` events.
	launch_args["terminal"] = "console";
	int launch_id = 0;
	if (!_send_request("launch", launch_args, launch_id, r_error)) {
		stop();
		return false;
	}

	String breakpoint_error;
	_apply_breakpoints(p_breakpoints, breakpoint_error);

	Dictionary config_result;
	if (!_request("configurationDone", Dictionary(), config_result, 20000, r_error)) {
		stop();
		return false;
	}
	if (!_wait_response(launch_id, result, 20000, r_error)) {
		stop();
		return false;
	}

	{
		MutexLock lock(mutex);
		if (state == "starting") {
			state = "running";
		}
	}
	if (!breakpoint_error.is_empty()) {
		WARN_PRINT(vformat("Rust: %s", breakpoint_error));
	}
	return true;
}

void RustDebug::stop() {
	bool was_running = false;
	{
		MutexLock lock(mutex);
		was_running = running;
		running = false;
	}
	if (was_running) {
		// Ask the adapter to take the debuggee down with it, then make sure it is
		// gone: a killed adapter would leave the game running.
		Dictionary result;
		String error;
		_request("disconnect", Dictionary(), result, 2000, error);
	}
	if (child_pid != 0) {
		OS::get_singleton()->kill((ProcessID)child_pid);
		child_pid = 0;
	}
	if (thread_started) {
		thread.wait_to_finish();
		thread_started = false;
	}
	MutexLock lock(mutex);
	stdio.unref();
	stderr_pipe.unref();
	responses.clear();
	stack.clear();
	stop_location = Dictionary();
	state = "idle";
}

void RustDebug::_step(const String &p_command) {
	int64_t thread_id = 0;
	{
		MutexLock lock(mutex);
		thread_id = current_thread;
	}
	if (thread_id == 0) {
		return;
	}
	Dictionary arguments;
	arguments["threadId"] = thread_id;
	Dictionary result;
	String error;
	if (!_request(p_command, arguments, result, 5000, error)) {
		WARN_PRINT(vformat("Rust: %s failed: %s", p_command, error));
	}
}

void RustDebug::continue_() {
	int64_t thread_id = 0;
	{
		MutexLock lock(mutex);
		thread_id = current_thread;
	}
	Dictionary arguments;
	arguments["threadId"] = thread_id;
	Dictionary result;
	String error;
	if (!_request("continue", arguments, result, 5000, error)) {
		WARN_PRINT(vformat("Rust: continue failed: %s", error));
	}
}

void RustDebug::step_over() {
	_step("next");
}

void RustDebug::step_in() {
	_step("stepIn");
}

void RustDebug::step_out() {
	_step("stepOut");
}

void RustDebug::pause() {
	int64_t thread_id = 0;
	{
		MutexLock lock(mutex);
		thread_id = current_thread;
	}
	Dictionary arguments;
	if (thread_id != 0) {
		arguments["threadId"] = thread_id;
	}
	Dictionary result;
	String error;
	if (!_request("pause", arguments, result, 5000, error)) {
		WARN_PRINT(vformat("Rust: pause failed: %s", error));
	}
}

bool RustDebug::start_gd(const String &p_program, const PackedStringArray &p_args, const String &p_cwd, const Dictionary &p_breakpoints) {
	String error;
	if (start(p_program, p_args, p_cwd, p_breakpoints, error)) {
		return true;
	}
	{
		MutexLock lock(mutex);
		last_error = error;
	}
	WARN_PRINT(vformat("Rust: could not start the debugger: %s", error));
	return false;
}
