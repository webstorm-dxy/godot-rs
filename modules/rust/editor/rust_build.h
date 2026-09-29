#pragma once

#include "core/object/object.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/templates/hash_map.h"
#include "../rust_diagnostics.h"

// Runs `cargo build` for the project crate and turns cargo's JSON output
// into editor diagnostics.
//
// Two call styles are supported:
//  - start_build(): fully asynchronous, polled from the UI (Build button).
//  - build_blocking(): waits while pumping the editor main loop, used by
//    EditorPlugin::build() so that F5 and --build-solutions build first.
class RustBuild : public Object {
	GDCLASS(RustBuild, Object);

public:
	enum Status {
		STATUS_IDLE,
		STATUS_RUNNING,
		STATUS_SUCCESS,
		STATUS_FAILED,
	};

	static RustBuild *get_singleton();

	bool is_running() const;
	Status get_status() const;
	String get_last_error() const;
	String get_library_path() const;
	uint64_t get_result_serial() const;

	String get_output() const;
	String drain_new_output();
	void clear_output();
	Vector<RustDiagnostic> get_diagnostics() const;

	bool start_build(const String &p_profile = "debug");
	bool build_blocking(const String &p_profile = "debug");
	bool wait_for_build();
	void cancel();

	RustBuild();
	~RustBuild();

private:
	static void _thread_func(void *p_userdata);
	void _run(const String &p_profile);
	void _append_output(const String &p_line);
	bool _pump_pipe(Ref<FileAccess> p_pipe, String &r_partial, bool p_stdout);
	void _handle_json_message(const String &p_line);
	String _to_res_path(const String &p_global_path) const;
	String _find_cargo() const;

	mutable Mutex mutex;
	Thread thread;
	Status status = STATUS_IDLE;
	String last_error;
	String library_path;
	Vector<String> output;
	int output_read_index = 0;
	Vector<RustDiagnostic> diagnostics;
	HashMap<String, Vector<RustDiagnostic>> pending_diags;
	uint64_t result_serial = 0;
	bool thread_running = false;
	bool thread_started = false;
	bool cancel_requested = false;
	int64_t child_pid = 0;
	String request_profile;
	bool print_to_console = false;
};
