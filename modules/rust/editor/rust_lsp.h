#pragma once

#include "core/io/file_access.h"
#include "core/object/object.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

// Minimal LSP client for rust-analyzer.
//
// The editor drives it through the Rust ScriptLanguage: validate() pushes the
// current buffer, complete_code() asks for completions, lookup_code() asks for
// the definition (falling back to hover). Diagnostics the server publishes are
// merged into RustDiagnostics, so they show up in the script editor the same
// way cargo build errors do.
//
// The server is a long-lived subprocess speaking Content-Length framed JSON-RPC
// over stdin/stdout; a worker thread reads replies and notifications so the
// editor thread only ever waits for a specific response with a timeout.
class RustLsp : public Object {
	GDCLASS(RustLsp, Object);

public:
	static RustLsp *get_singleton();
	static void create_singleton();
	static void destroy_singleton();

	// Starts rust-analyzer for the given crate root (absolute path).
	bool start(const String &p_crate_root_global);
	void stop();
	bool is_running() const;
	String get_analyzer_path() const { return analyzer_path; }

	// Document synchronization (paths are res:// paths).
	void did_open(const String &p_path, const String &p_text);
	// didOpen on first use, didChange afterwards.
	void sync_document(const String &p_path, const String &p_text);
	void did_change(const String &p_path, const String &p_text);
	void did_close(const String &p_path);

	// Requests. Positions are 0-based, matching LSP.
	bool complete(const String &p_path, int p_line, int p_column, Array &r_items, String &r_error);
	bool definition(const String &p_path, int p_line, int p_column, String &r_path, int &r_line, int &r_column, String &r_error);
	bool hover(const String &p_path, int p_line, int p_column, String &r_text, String &r_error);

	// Diagnostics published by the server, per res:// path (for tests/tools).
	PackedStringArray get_diagnostics_for_path(const String &p_path) const;

	// GDScript-facing wrappers (out parameters cannot be bound directly).
	Dictionary complete_gd(const String &p_path, int p_line, int p_column);
	Dictionary hover_gd(const String &p_path, int p_line, int p_column);
	Dictionary definition_gd(const String &p_path, int p_line, int p_column);
	bool start_gd(const String &p_crate_root_global) { return start(p_crate_root_global); }

	RustLsp();
	~RustLsp();

protected:
	static void _bind_methods();

private:
	static void _thread_func(void *p_userdata);
	void _read_loop();
	void _handle_message(const Dictionary &p_message);
	bool _write_message(const Dictionary &p_message);
	bool _request(const String &p_method, const Dictionary &p_params, Dictionary &r_result, int p_timeout_ms, String &r_error);
	String _find_analyzer() const;
	String _path_to_uri(const String &p_path) const;
	String _uri_to_path(const String &p_uri) const;
	static String _lsp_to_res_path(const String &p_uri, const String &p_crate_root);

	mutable Mutex mutex;
	Thread thread;
	Ref<FileAccess> stdio;
	Ref<FileAccess> stderr_pipe;
	Vector<String> analyzer_stderr;
	int64_t child_pid = 0;
	bool running = false;
	bool initialized = false;
	bool thread_started = false;
	int next_id = 1;
	String analyzer_path;
	String crate_root;
	HashMap<int, Dictionary> responses;
	HashMap<String, String> open_docs;
	HashMap<String, PackedStringArray> diagnostics;
};
