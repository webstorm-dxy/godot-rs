#pragma once

#include "core/object/script_language.h"
#include "core/os/mutex.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

// One entry of "cargo build --message-format=json" output, normalized for the
// editor (1-based line/column, res:// path when the file is inside the project).
struct RustDiagnostic {
	enum Type {
		TYPE_ERROR,
		TYPE_WARNING,
	};

	Type type = TYPE_ERROR;
	String file;
	int line = 0;
	int column = 0;
	String code;
	String message;
};

// Thread-safe store shared by the build thread (writer) and the script editor
// (reader). The script language only needs the read side to report inline
// errors, so this lives in the module core and is compiled in every build.
class RustDiagnostics {
	static RustDiagnostics *singleton;

	mutable Mutex mutex;
	HashMap<String, Vector<RustDiagnostic>> by_file;

public:
	static RustDiagnostics *get_singleton();

	void publish(const HashMap<String, Vector<RustDiagnostic>> &p_by_file);
	void clear();
	void fill_script_errors(const String &p_path, List<ScriptLanguage::ScriptError> *r_errors, List<ScriptLanguage::Warning> *r_warnings) const;
	int get_error_count() const;
	int get_warning_count() const;

	RustDiagnostics();
	~RustDiagnostics();
};
