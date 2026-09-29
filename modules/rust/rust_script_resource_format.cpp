#include "rust_script_resource_format.h"

#include "rust_script.h"

#include "core/io/file_access.h"

#ifdef TOOLS_ENABLED
#include "editor/rust_project.h"
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

	Error err;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, vformat("Cannot save Rust script '%s'.", p_path));
	file->store_string(script->get_source_code());
	file->close();

#ifdef TOOLS_ENABLED
	// Keep the crate root in sync with the scripts on disk (mod declarations and
	// registrations); this also heals projects created before the feature.
	RustProject::sync_crate_modules();
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
