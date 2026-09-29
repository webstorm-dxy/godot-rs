#include "rust_diagnostics.h"

RustDiagnostics *RustDiagnostics::singleton = nullptr;

RustDiagnostics *RustDiagnostics::get_singleton() {
	return singleton;
}

void RustDiagnostics::publish(const HashMap<String, Vector<RustDiagnostic>> &p_by_file) {
	MutexLock lock(mutex);
	by_file = p_by_file;
}

void RustDiagnostics::clear() {
	MutexLock lock(mutex);
	by_file.clear();
}

void RustDiagnostics::fill_script_errors(const String &p_path, List<ScriptLanguage::ScriptError> *r_errors, List<ScriptLanguage::Warning> *r_warnings) const {
	if (p_path.is_empty()) {
		return;
	}

	MutexLock lock(mutex);
	const Vector<RustDiagnostic> *diags = by_file.getptr(p_path);
	if (diags == nullptr) {
		return;
	}

	for (const RustDiagnostic &diag : *diags) {
		if (diag.type == RustDiagnostic::TYPE_ERROR) {
			if (r_errors != nullptr) {
				ScriptLanguage::ScriptError err;
				err.path = diag.file;
				err.line = diag.line;
				err.column = diag.column;
				err.message = diag.message;
				r_errors->push_back(err);
			}
		} else if (r_warnings != nullptr) {
			ScriptLanguage::Warning warn;
			warn.start_line = diag.line;
			warn.end_line = diag.line;
			warn.code = 0;
			warn.string_code = diag.code;
			warn.message = diag.message;
			r_warnings->push_back(warn);
		}
	}
}

int RustDiagnostics::get_error_count() const {
	MutexLock lock(mutex);
	int count = 0;
	for (const KeyValue<String, Vector<RustDiagnostic>> &E : by_file) {
		for (const RustDiagnostic &diag : E.value) {
			if (diag.type == RustDiagnostic::TYPE_ERROR) {
				count++;
			}
		}
	}
	return count;
}

int RustDiagnostics::get_warning_count() const {
	MutexLock lock(mutex);
	int count = 0;
	for (const KeyValue<String, Vector<RustDiagnostic>> &E : by_file) {
		for (const RustDiagnostic &diag : E.value) {
			if (diag.type == RustDiagnostic::TYPE_WARNING) {
				count++;
			}
		}
	}
	return count;
}

RustDiagnostics::RustDiagnostics() {
	singleton = this;
}

RustDiagnostics::~RustDiagnostics() {
	singleton = nullptr;
}
