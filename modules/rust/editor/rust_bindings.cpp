#include "rust_bindings.h"

#include "rust_project.h"

#include "core/extension/extension_api_dump.h"
#include "core/extension/gdextension_interface_dump.gen.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/config/engine.h"
#include "core/os/os.h"
#include "core/version.h"

String RustBindings::get_api_json_path() {
	return RustProject::get_bindings_dir_global().path_join("extension_api.json");
}

String RustBindings::get_interface_json_path() {
	return RustProject::get_bindings_dir_global().path_join("gdextension_interface.json");
}

static String _bindings_cache_key() {
	String key = GODOT_VERSION_FULL_BUILD;
	key += vformat("|api=%d", ClassDB::get_api_hash(ClassDB::API_EXTENSION));
	key += vformat("|bin=%d", (int64_t)FileAccess::get_modified_time(OS::get_singleton()->get_executable_path()));
	return key;
}

Error RustBindings::ensure(bool p_force) {
	const String dir = RustProject::get_bindings_dir_global();
	if (dir.is_empty()) {
		return ERR_UNCONFIGURED;
	}

	Error err = DirAccess::make_dir_recursive_absolute(dir);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot create the Rust bindings directory.");

	const String manifest_path = dir.path_join("manifest.txt");
	const String key = _bindings_cache_key();

	if (!p_force) {
		Error read_err;
		Ref<FileAccess> manifest = FileAccess::open(manifest_path, FileAccess::READ, &read_err);
		if (read_err == OK && manifest->get_line() == key &&
				FileAccess::exists(get_api_json_path()) && FileAccess::exists(get_interface_json_path())) {
			return OK;
		}
	}

	// "extension_api.json" always contains editor singletons, mirroring
	// `godot --dump-extension-api`.
	const bool was_editor_hint = Engine::get_singleton()->is_editor_hint();
	Engine::get_singleton()->set_editor_hint(true);
	// Matches `godot --dump-extension-api` (no docs): gdext only needs the API
	// description, and the doc data is not available this early in startup.
	GDExtensionAPIDump::generate_extension_json_file(get_api_json_path(), false);
	GDExtensionInterfaceDump::generate_gdextension_interface_file(get_interface_json_path());
	Engine::get_singleton()->set_editor_hint(was_editor_hint);

	err = RustProject::ensure_extension_config();
	ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot refresh the GDExtension configuration.");

	Ref<FileAccess> manifest = FileAccess::open(manifest_path, FileAccess::WRITE, &err);
	if (err == OK) {
		manifest->store_line(key);
	}
	return OK;
}
