#pragma once

#include "core/io/file_access.h"
#include "core/object/object.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/variant/dictionary.h"

// Minimal Debug Adapter Protocol client for lldb-dap.
//
// The editor's script editor already lets the user toggle breakpoints in any
// script, `.rs` included. This singleton takes those breakpoints, starts
// `lldb-dap`, launches the game under it and translates both ways: breakpoints
// and stepping go to the adapter, stops, call stacks and variables come back
// for the Rust dock.
//
// A worker thread reads the adapter's stdout (Content-Length framed JSON), the
// editor thread only ever waits for a specific response with a timeout, so no
// editor code runs on the adapter thread.
class RustDebug : public Object {
	GDCLASS(RustDebug, Object);

public:
	static RustDebug *get_singleton();
	static void create_singleton();
	static void destroy_singleton();

	// Starts the adapter and the game; breakpoints map a res:// path to the
	// (1-based) lines to stop at.
	bool start(const String &p_program, const PackedStringArray &p_args, const String &p_cwd, const Dictionary &p_breakpoints, String &r_error);
	void stop();
	bool is_running() const;

	// "idle", "starting", "running", "stopped" or "terminated".
	String get_state() const;
	// {file, line, function, reason} of the last stop (empty when never stopped).
	Dictionary get_stop_location() const;
	// [{id, name, file, line}] of the stopped thread, top frame first.
	Array get_stack();
	// [{name, value, type}] of a frame of get_stack().
	Array get_variables(int p_frame);
	String get_output() const;
	String get_adapter_path() const;
	String get_error() const;

	void continue_();
	void step_over();
	void step_in();
	void step_out();
	void pause();
	void set_breakpoints(const Dictionary &p_breakpoints);

	// Fetches whatever arrived since the last call (state, stop location, stack);
	// the dock calls this from _process, commands call it before answering.
	void update();

	bool start_gd(const String &p_program, const PackedStringArray &p_args, const String &p_cwd, const Dictionary &p_breakpoints);
	Dictionary get_stop_location_gd() { update(); return get_stop_location(); }
	Array get_stack_gd() { update(); return get_stack(); }
	Array get_variables_gd(int p_frame) { update(); return get_variables(p_frame); }

	RustDebug();
	~RustDebug();

protected:
	static void _bind_methods();

private:
	static void _thread_func(void *p_userdata);
	void _read_loop();
	void _handle_message(const Dictionary &p_message);
	void _handle_event(const Dictionary &p_event);
	bool _write_message(const Dictionary &p_message);
	bool _send_request(const String &p_command, const Dictionary &p_arguments, int &r_id, String &r_error);
	bool _wait_response(int p_id, Dictionary &r_result, int p_timeout_ms, String &r_error);
	bool _request(const String &p_command, const Dictionary &p_arguments, Dictionary &r_result, int p_timeout_ms, String &r_error);
	void _step(const String &p_command);
	void _apply_breakpoints(const Dictionary &p_breakpoints, String &r_error);
	void _refresh_stack();
	String _find_adapter() const;
	static String _res_to_global(const String &p_path);
	static String _global_to_res(const String &p_path);
	static Dictionary _breakpoints_to_dap(const Dictionary &p_breakpoints);

	mutable Mutex mutex;
	Thread thread;
	Ref<FileAccess> stdio;
	Ref<FileAccess> stderr_pipe;
	Vector<String> adapter_stderr;
	int64_t child_pid = 0;
	bool running = false;
	bool thread_started = false;
	int next_id = 1;
	String adapter_path;
	String program;
	String cwd;
	String state;
	String last_error;
	bool initialized_event = false;
	bool refresh_needed = false;
	int64_t current_thread = 0;
	int64_t top_frame = 0;
	Dictionary stop_location;
	Array stack;
	Dictionary breakpoints;
	String output;
	HashMap<int, Dictionary> responses;
};
