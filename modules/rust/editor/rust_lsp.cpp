#include "rust_lsp.h"

#include "../rust_diagnostics.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "editor/settings/editor_settings.h"

static RustLsp *rust_lsp_singleton = nullptr;

RustLsp *RustLsp::get_singleton() {
	return rust_lsp_singleton;
}

void RustLsp::create_singleton() {
	if (rust_lsp_singleton == nullptr) {
		rust_lsp_singleton = memnew(RustLsp);
	}
}

void RustLsp::destroy_singleton() {
	if (rust_lsp_singleton != nullptr) {
		rust_lsp_singleton->stop();
		memdelete(rust_lsp_singleton);
		rust_lsp_singleton = nullptr;
	}
}

RustLsp::RustLsp() {
}

RustLsp::~RustLsp() {
	stop();
}

bool RustLsp::is_running() const {
	MutexLock lock(mutex);
	return running && initialized;
}

String RustLsp::_find_analyzer() const {
	String from_env = OS::get_singleton()->get_environment("GODOT_RUST_ANALYZER");
	if (!from_env.is_empty()) {
		return from_env;
	}
	if (EditorSettings::get_singleton() != nullptr) {
		String configured = EditorSettings::get_singleton()->get_setting("rust/rust_analyzer_path");
		if (!configured.is_empty()) {
			return configured;
		}
	}
	String path_env = OS::get_singleton()->get_environment("PATH");
	for (const String &dir : path_env.split(":", false)) {
		String candidate = dir.path_join("rust-analyzer");
		if (FileAccess::exists(candidate)) {
			return candidate;
		}
	}
	String home = OS::get_singleton()->get_environment("HOME");
	if (!home.is_empty()) {
		String candidate = home.path_join(".cargo/bin/rust-analyzer");
		if (FileAccess::exists(candidate)) {
			return candidate;
		}
	}
	return String();
}

String RustLsp::_path_to_uri(const String &p_path) const {
	String global = p_path.begins_with("res://") ? ProjectSettings::get_singleton()->globalize_path(p_path) : p_path;
	return "file://" + global;
}

String RustLsp::_uri_to_path(const String &p_uri) const {
	String path = p_uri;
	if (path.begins_with("file://")) {
		path = path.substr(7);
	}
	if (!crate_root.is_empty() && path.begins_with(crate_root)) {
		String relative = path.substr(crate_root.length()).trim_prefix("/");
		return "res://" + relative;
	}
	return path;
}

bool RustLsp::start(const String &p_crate_root_global) {
	stop();

	const String analyzer = _find_analyzer();
	if (analyzer.is_empty()) {
		print_verbose("Rust: rust-analyzer was not found; completion and hover stay disabled. Set rust/rust_analyzer_path to enable them.");
		return false;
	}

	List<String> args;
	Dictionary pipe = OS::get_singleton()->execute_with_pipe(analyzer, args, false);
	if (!pipe.has("stdio")) {
		ERR_PRINT(vformat("Rust: could not start rust-analyzer at '%s'.", analyzer));
		return false;
	}

	{
		MutexLock lock(mutex);
		stdio = pipe["stdio"];
		child_pid = pipe.has("pid") ? (int64_t)pipe["pid"] : 0;
		analyzer_path = analyzer;
		crate_root = p_crate_root_global.simplify_path();
		running = true;
	}
	thread.start(_thread_func, this);
	{
		MutexLock lock(mutex);
		thread_started = true;
	}

	Dictionary params;
	params["processId"] = (int64_t)OS::get_singleton()->get_process_id();
	Dictionary client_info;
	client_info["name"] = "godot-rust-module";
	params["clientInfo"] = client_info;
	params["rootUri"] = _path_to_uri(p_crate_root_global);
	Dictionary capabilities;
	capabilities["completion"] = Dictionary();
	capabilities["hover"] = Dictionary();
	capabilities["definition"] = Dictionary();
	Dictionary synchronization;
	Dictionary change;
	change["syncKind"] = 1;
	synchronization["textDocument"] = change;
	capabilities["textDocument"] = synchronization;
	params["capabilities"] = capabilities;
	Dictionary init_options;
	Array linked_projects;
	linked_projects.push_back(crate_root.path_join("Cargo.toml"));
	init_options["linkedProjects"] = linked_projects;
	Dictionary cache_priming;
	cache_priming["enable"] = false;
	init_options["cachePriming"] = cache_priming;
	init_options["checkOnSave"] = false;
	params["initializationOptions"] = init_options;

	Dictionary result;
	String error;
	if (!_request("initialize", params, result, 20000, error)) {
		ERR_PRINT(vformat("Rust: rust-analyzer initialization failed: %s", error));
		stop();
		return false;
	}

	Dictionary initialized_message;
	initialized_message["jsonrpc"] = "2.0";
	initialized_message["method"] = "initialized";
	initialized_message["params"] = Dictionary();
	_write_message(initialized_message);

	{
		MutexLock lock(mutex);
		initialized = true;
	}
	print_verbose(vformat("Rust: rust-analyzer is running for '%s'.", crate_root));
	return true;
}

void RustLsp::stop() {
	bool was_running = false;
	{
		MutexLock lock(mutex);
		was_running = running;
		running = false;
		initialized = false;
	}
	if (!was_running && !thread_started) {
		return;
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
	responses.clear();
	open_docs.clear();
}

void RustLsp::_thread_func(void *p_userdata) {
	RustLsp *self = (RustLsp *)p_userdata;
	self->_read_loop();
}

bool RustLsp::_write_message(const Dictionary &p_message) {
	MutexLock lock(mutex);
	if (stdio.is_null()) {
		return false;
	}
	String body = JSON::stringify(p_message);
	String header = vformat("Content-Length: %d\r\n\r\n", body.utf8().length());
	stdio->store_string(header + body);
	return true;
}

void RustLsp::_handle_message(const Dictionary &p_message) {
	if (p_message.has("id") && !p_message.has("method")) {
		MutexLock lock(mutex);
		responses[(int)p_message["id"]] = p_message;
		return;
	}

	const String method = p_message.get("method", String());
	if (method != "textDocument/publishDiagnostics") {
		return;
	}

	Dictionary params = p_message.get("params", Dictionary());
	const String path = _uri_to_path(params.get("uri", String()));
	Array items = params.get("diagnostics", Array());

	Vector<RustDiagnostic> parsed;
	PackedStringArray messages;
	for (int i = 0; i < items.size(); i++) {
		Dictionary item = items[i];
		RustDiagnostic diag;
		const int severity = (int)item.get("severity", 1);
		diag.type = severity == 2 ? RustDiagnostic::TYPE_WARNING : RustDiagnostic::TYPE_ERROR;
		diag.file = path;
		Dictionary range = item.get("range", Dictionary());
		Dictionary start = range.get("start", Dictionary());
		diag.line = (int)start.get("line", 0) + 1;
		diag.column = (int)start.get("character", 0) + 1;
		diag.message = item.get("message", String());
		diag.code = item.get("code", String());
		parsed.push_back(diag);
		messages.push_back(vformat("%s:%d - %s", path, diag.line, diag.message));
	}

	{
		MutexLock lock(mutex);
		diagnostics[path] = messages;
	}
	RustDiagnostics::get_singleton()->set_file_diagnostics(path, parsed);
}

void RustLsp::_read_loop() {
	String buffer;
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
		}
		if (read == 0) {
			OS::get_singleton()->delay_usec(2000);
			continue;
		}
		buffer += String::utf8((const char *)chunk, (int)read);

		while (true) {
			const int header_end = buffer.find("\r\n\r\n");
			if (header_end == -1) {
				break;
			}
			const String header = buffer.substr(0, header_end);
			int length = -1;
			for (const String &line : header.split("\r\n")) {
				if (line.begins_with("Content-Length:")) {
					length = line.substr(15).strip_edges().to_int();
				}
			}
			if (length < 0 || (int)buffer.length() < header_end + 4 + length) {
				break;
			}

			const String body = buffer.substr(header_end + 4, length);
			buffer = buffer.substr(header_end + 4 + length);

			JSON json;
			if (json.parse(body) == OK && json.get_data().get_type() == Variant::DICTIONARY) {
				_handle_message(json.get_data());
			}
		}
	}
}

bool RustLsp::_request(const String &p_method, const Dictionary &p_params, Dictionary &r_result, int p_timeout_ms, String &r_error) {
	int id = 0;
	{
		MutexLock lock(mutex);
		id = next_id++;
	}

	Dictionary message;
	message["jsonrpc"] = "2.0";
	message["id"] = id;
	message["method"] = p_method;
	message["params"] = p_params;
	if (!_write_message(message)) {
		r_error = "the language server is not running";
		return false;
	}

	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + (uint64_t)MAX(1, p_timeout_ms);
	while (OS::get_singleton()->get_ticks_msec() < deadline) {
		Dictionary response;
		bool found = false;
		bool alive = true;
		{
			MutexLock lock(mutex);
			Dictionary *entry = responses.getptr(id);
			if (entry != nullptr) {
				response = *entry;
				responses.erase(id);
				found = true;
			}
			alive = running;
		}
		if (found) {
			if (response.has("error")) {
				Dictionary error = response["error"];
				r_error = error.get("message", "unknown language server error");
				return false;
			}
			Variant result = response.get("result", Variant());
			if (result.get_type() == Variant::DICTIONARY) {
				r_result = result;
			} else {
				r_result = Dictionary();
				r_result["array"] = result;
			}
			return true;
		}
		if (!alive) {
			r_error = "the language server stopped";
			return false;
		}
		OS::get_singleton()->delay_usec(2000);
	}

	r_error = "timed out waiting for the language server";
	return false;
}

void RustLsp::did_open(const String &p_path, const String &p_text) {
	MutexLock lock(mutex);
	open_docs[p_path] = p_text;
	Dictionary params;
	Dictionary text_document;
	text_document["uri"] = _path_to_uri(p_path);
	text_document["languageId"] = "rust";
	text_document["version"] = 1;
	text_document["text"] = p_text;
	params["textDocument"] = text_document;
	Dictionary message;
	message["jsonrpc"] = "2.0";
	message["method"] = "textDocument/didOpen";
	message["params"] = params;
	_write_message(message);
}

void RustLsp::sync_document(const String &p_path, const String &p_text) {
	bool known = false;
	{
		MutexLock lock(mutex);
		known = open_docs.has(p_path);
	}
	if (known) {
		did_change(p_path, p_text);
	} else {
		did_open(p_path, p_text);
	}
}

void RustLsp::did_change(const String &p_path, const String &p_text) {
	MutexLock lock(mutex);
	open_docs[p_path] = p_text;
	Dictionary params;
	Dictionary text_document;
	text_document["uri"] = _path_to_uri(p_path);
	text_document["version"] = 1;
	params["textDocument"] = text_document;
	Array changes;
	Dictionary change;
	change["text"] = p_text;
	changes.push_back(change);
	params["contentChanges"] = changes;
	Dictionary message;
	message["jsonrpc"] = "2.0";
	message["method"] = "textDocument/didChange";
	message["params"] = params;
	_write_message(message);
}

void RustLsp::did_close(const String &p_path) {
	MutexLock lock(mutex);
	open_docs.erase(p_path);
	Dictionary params;
	Dictionary text_document;
	text_document["uri"] = _path_to_uri(p_path);
	params["textDocument"] = text_document;
	Dictionary message;
	message["jsonrpc"] = "2.0";
	message["method"] = "textDocument/didClose";
	message["params"] = params;
	_write_message(message);
}

bool RustLsp::complete(const String &p_path, int p_line, int p_column, Array &r_items, String &r_error) {
	if (!is_running()) {
		r_error = "the language server is not running";
		return false;
	}
	Dictionary params;
	Dictionary text_document;
	text_document["uri"] = _path_to_uri(p_path);
	params["textDocument"] = text_document;
	Dictionary position;
	position["line"] = p_line;
	position["character"] = p_column;
	params["position"] = position;
	Dictionary context;
	context["triggerKind"] = 1;
	params["context"] = context;

	Dictionary result;
	if (!_request("textDocument/completion", params, result, 300, r_error)) {
		return false;
	}

	if (result.has("array")) {
		r_items = result["array"];
	} else if (result.has("items")) {
		r_items = result["items"];
	}
	return true;
}

bool RustLsp::definition(const String &p_path, int p_line, int p_column, String &r_path, int &r_line, int &r_column, String &r_error) {
	if (!is_running()) {
		r_error = "the language server is not running";
		return false;
	}
	Dictionary params;
	Dictionary text_document;
	text_document["uri"] = _path_to_uri(p_path);
	params["textDocument"] = text_document;
	Dictionary position;
	position["line"] = p_line;
	position["character"] = p_column;
	params["position"] = position;

	Dictionary result;
	if (!_request("textDocument/definition", params, result, 1500, r_error)) {
		return false;
	}

	Dictionary location;
	if (result.has("array")) {
		Array array = result["array"];
		if (array.is_empty()) {
			r_error = "no definition found";
			return false;
		}
		Variant first = array[0];
		if (first.get_type() == Variant::DICTIONARY) {
			location = first;
		}
	} else {
		location = result;
	}
	if (location.is_empty()) {
		r_error = "no definition found";
		return false;
	}

	if (location.has("targetUri")) {
		r_path = _uri_to_path(location.get("targetUri", String()));
		Dictionary range = location.get("targetRange", Dictionary());
		Dictionary start = range.get("start", Dictionary());
		r_line = (int)start.get("line", 0) + 1;
		r_column = (int)start.get("character", 0) + 1;
		return true;
	}

	r_path = _uri_to_path(location.get("uri", String()));
	Dictionary range = location.get("range", Dictionary());
	Dictionary start = range.get("start", Dictionary());
	r_line = (int)start.get("line", 0) + 1;
	r_column = (int)start.get("character", 0) + 1;
	return !r_path.is_empty();
}

bool RustLsp::hover(const String &p_path, int p_line, int p_column, String &r_text, String &r_error) {
	if (!is_running()) {
		r_error = "the language server is not running";
		return false;
	}
	Dictionary params;
	Dictionary text_document;
	text_document["uri"] = _path_to_uri(p_path);
	params["textDocument"] = text_document;
	Dictionary position;
	position["line"] = p_line;
	position["character"] = p_column;
	params["position"] = position;

	Dictionary result;
	if (!_request("textDocument/hover", params, result, 1500, r_error)) {
		return false;
	}
	if (result.is_empty()) {
		r_error = "no hover information";
		return false;
	}

	Variant contents = result.get("contents", Variant());
	if (contents.get_type() == Variant::DICTIONARY) {
		Dictionary dictionary = contents;
		r_text = dictionary.get("value", String());
	} else if (contents.get_type() == Variant::STRING) {
		r_text = contents;
	} else if (contents.get_type() == Variant::ARRAY) {
		Array array = contents;
		for (int i = 0; i < array.size(); i++) {
			Variant entry = array[i];
			if (entry.get_type() == Variant::DICTIONARY) {
				Dictionary dictionary = entry;
				r_text += dictionary.get("value", String()) + "\n";
			} else {
				r_text += String(entry) + "\n";
			}
		}
	}
	return !r_text.is_empty();
}

Dictionary RustLsp::complete_gd(const String &p_path, int p_line, int p_column) {
	Dictionary result;
	Array items;
	String error;
	result["ok"] = complete(p_path, p_line, p_column, items, error);
	result["items"] = items;
	result["error"] = error;
	return result;
}

Dictionary RustLsp::hover_gd(const String &p_path, int p_line, int p_column) {
	Dictionary result;
	String text;
	String error;
	result["ok"] = hover(p_path, p_line, p_column, text, error);
	result["text"] = text;
	result["error"] = error;
	return result;
}

Dictionary RustLsp::definition_gd(const String &p_path, int p_line, int p_column) {
	Dictionary result;
	String path;
	int line = 0;
	int column = 0;
	String error;
	result["ok"] = definition(p_path, p_line, p_column, path, line, column, error);
	result["path"] = path;
	result["line"] = line;
	result["column"] = column;
	result["error"] = error;
	return result;
}

void RustLsp::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start", "crate_root"), &RustLsp::start_gd);
	ClassDB::bind_method(D_METHOD("stop"), &RustLsp::stop);
	ClassDB::bind_method(D_METHOD("is_running"), &RustLsp::is_running);
	ClassDB::bind_method(D_METHOD("sync_document", "path", "text"), &RustLsp::sync_document);
	ClassDB::bind_method(D_METHOD("complete_at", "path", "line", "column"), &RustLsp::complete_gd);
	ClassDB::bind_method(D_METHOD("hover_at", "path", "line", "column"), &RustLsp::hover_gd);
	ClassDB::bind_method(D_METHOD("definition_at", "path", "line", "column"), &RustLsp::definition_gd);
	ClassDB::bind_method(D_METHOD("get_diagnostics_for_path", "path"), &RustLsp::get_diagnostics_for_path);
}

PackedStringArray RustLsp::get_diagnostics_for_path(const String &p_path) const {
	MutexLock lock(mutex);
	const PackedStringArray *entry = diagnostics.getptr(p_path);
	return entry != nullptr ? *entry : PackedStringArray();
}
