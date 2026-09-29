#pragma once

#include "core/string/ustring.h"

// Path conventions shared by the module core (all builds) and the editor-only
// project/build code. Everything Rust-related lives under the project data dir
// (<project>/.godot/rust), which keeps generated files out of the project tree.
class RustPaths {
public:
	static String get_data_dir_res();
	static String get_data_dir_global();
	static String get_bindings_dir_global();
	static String get_target_dir_global();
	static String get_script_cache_res();
	static String get_extension_config_res();
	static String get_extension_config_global();
	static String get_extension_list_global();
};
