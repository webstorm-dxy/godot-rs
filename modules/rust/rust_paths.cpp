#include "rust_paths.h"

#include "core/config/project_settings.h"

String RustPaths::get_data_dir_res() {
	return ProjectSettings::get_singleton()->get_project_data_path().path_join("rust");
}

String RustPaths::get_data_dir_global() {
	return ProjectSettings::get_singleton()->globalize_path(get_data_dir_res());
}

String RustPaths::get_bindings_dir_global() {
	return get_data_dir_global().path_join("bindings");
}

String RustPaths::get_target_dir_global() {
	return get_data_dir_global().path_join("target");
}

String RustPaths::get_script_cache_res() {
	return get_data_dir_res().path_join("script_cache.json");
}

String RustPaths::get_extension_config_res() {
	return get_data_dir_res().path_join("rust.gdextension");
}

String RustPaths::get_extension_config_global() {
	return get_data_dir_global().path_join("rust.gdextension");
}

String RustPaths::get_extension_list_global() {
	return ProjectSettings::get_singleton()->globalize_path(ProjectSettings::get_singleton()->get_project_data_path().path_join("extension_list.cfg"));
}
