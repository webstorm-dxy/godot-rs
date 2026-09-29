#include "rust_script_resource_format.h"

#include "rust_script.h"

#include "core/io/file_access.h"

#ifdef TOOLS_ENABLED
#include "core/config/project_settings.h"

// Adds the new script to the crate root: `mod <name>;` plus a call inside
// `pub fn register_scripts()`, so a script created from the editor is picked up
// by the next build without manual wiring.
static void _register_script_in_crate(const String &p_path) {
	const String crate_root = ProjectSettings::get_singleton()->globalize_path(GLOBAL_GET("rust/crate_root"));
	const String crate_file = crate_root.path_join("src").path_join("lib.rs");
	if (!FileAccess::exists(crate_file)) {
		return;
	}

	const String stem = p_path.get_file().get_basename();
	if (stem.is_empty()) {
		return;
	}

	Error err;
	Ref<FileAccess> file = FileAccess::open(crate_file, FileAccess::READ, &err);
	if (err != OK) {
		return;
	}

	Vector<String> lines;
	while (!file->eof_reached()) {
		lines.push_back(file->get_line());
	}
	file->close();

	const String mod_line = "mod " + stem + ";";
	const String register_line = stem + "::register();";

	bool has_mod = false;
	bool has_register = false;
	for (const String &line : lines) {
		const String stripped = line.strip_edges();
		has_mod = has_mod || stripped == mod_line;
		has_register = has_register || stripped == register_line;
	}
	if (has_mod && has_register) {
		return;
	}

	if (!has_mod) {
		int insert_at = 0;
		for (int i = 0; i < lines.size(); i++) {
			if (lines[i].begins_with("mod ")) {
				insert_at = i + 1;
			}
		}
		lines.insert(insert_at, mod_line);
	}

	if (!has_register) {
		for (int i = 0; i < lines.size(); i++) {
			if (!lines[i].contains("fn register_scripts()")) {
				continue;
			}
			for (int j = i + 1; j < lines.size(); j++) {
				if (lines[j].strip_edges() == "}") {
					lines.insert(j, "\t" + register_line);
					break;
				}
			}
			break;
		}
	}

	String content;
	for (const String &line : lines) {
		content += line + "\n";
	}
	Ref<FileAccess> out = FileAccess::open(crate_file, FileAccess::WRITE, &err);
	if (err == OK) {
		out->store_string(content);
	}
}
#endif // TOOLS_ENABLED


Ref<Resource> ResourceFormatLoaderRustScript::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) {
	if (r_error) {
		*r_error = ERR_FILE_CANT_OPEN;
	}

	Ref<RustScript> script;
	script.instantiate();

	Error err = script->load_source_code(p_path);
	if (err != OK) {
		if (r_error) {
			*r_error = err;
		}
		return Ref<Resource>();
	}

	if (p_cache_mode == CACHE_MODE_REPLACE || p_cache_mode == CACHE_MODE_REPLACE_DEEP) {
		script->set_path(p_original_path.is_empty() ? p_path : p_original_path, true);
	} else {
		script->set_path(p_original_path.is_empty() ? p_path : p_original_path);
	}
	script->reload();

	if (r_error) {
		*r_error = OK;
	}
	return script;
}

void ResourceFormatLoaderRustScript::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("rs");
}

bool ResourceFormatLoaderRustScript::handles_type(const String &p_type) const {
	return p_type == "Script" || p_type == "RustScript";
}

String ResourceFormatLoaderRustScript::get_resource_type(const String &p_path) const {
	return p_path.get_extension().to_lower() == "rs" ? "RustScript" : "";
}

Error ResourceFormatSaverRustScript::save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags) {
	Ref<RustScript> script = p_resource;
	ERR_FAIL_COND_V(script.is_null(), ERR_INVALID_PARAMETER);

	const bool is_new_file = !FileAccess::exists(p_path);

	Error err;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, vformat("Cannot save Rust script '%s'.", p_path));
	file->store_string(script->get_source_code());
	file->close();

#ifdef TOOLS_ENABLED
	if (is_new_file) {
		_register_script_in_crate(p_path);
	}
#endif // TOOLS_ENABLED

	script->set_path(p_path, true);
	return OK;
}

void ResourceFormatSaverRustScript::get_recognized_extensions(const Ref<Resource> &p_resource, List<String> *p_extensions) const {
	if (Object::cast_to<RustScript>(p_resource.ptr()) != nullptr) {
		p_extensions->push_back("rs");
	}
}

bool ResourceFormatSaverRustScript::recognize(const Ref<Resource> &p_resource) const {
	return Object::cast_to<RustScript>(p_resource.ptr()) != nullptr;
}
