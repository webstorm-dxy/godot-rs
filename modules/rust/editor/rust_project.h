#pragma once

#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "../rust_paths.h"

// Project-side layout and scaffolding for Rust projects.
//
// Everything Rust-related lives in the project data dir (<project>/.godot/rust),
// which is ignored by the .gitignore Godot generates, so the project tree only
// contains the plain Cargo crate. The generated .gdextension file is registered
// in <project>/.godot/extension_list.cfg, the engine-native way of loading
// GDExtension libraries in both the editor and the running game.
class RustProject {
public:
	static String get_data_dir_res() { return RustPaths::get_data_dir_res(); }
	static String get_data_dir_global() { return RustPaths::get_data_dir_global(); }
	static String get_bindings_dir_global() { return RustPaths::get_bindings_dir_global(); }
	static String get_target_dir_global() { return RustPaths::get_target_dir_global(); }
	static String get_extension_config_res() { return RustPaths::get_extension_config_res(); }
	static String get_extension_config_global() { return RustPaths::get_extension_config_global(); }
	static String get_extension_list_global() { return RustPaths::get_extension_list_global(); }

	static bool has_project();
	static bool is_enabled();
	static bool project_uses_rust();
	static String get_crate_root_res();
	static String get_crate_root_global();
	static String get_vendor_dir();
	static String get_cargo_manifest_global();
	static String get_crate_name();
	static String get_library_file_name();
	static String get_library_path_global(const String &p_profile);

	static void register_settings();
	// Makes sure every src/*.rs module is declared (and its scripts registered)
	// in the crate root; safe to call repeatedly and heals older projects.
	static void sync_crate_modules();
	// Adds missing `godot` / `godot-script` dependencies for older projects.
	static void sync_cargo_manifest();
	// Runs both sync steps (manifest + module declarations).
	static void sync_crate();
	static void register_editor_settings();
	static bool handle_cmdline();

	static Error scaffold_project(const String &p_project_dir_global, const String &p_project_name);
	static Error ensure_extension_config(const String &p_library_path_global = String());
	static String find_existing_library();
	static bool is_library_stale(const String &p_library_path_global);

private:
	static String _sanitize_crate_name(const String &p_name);
	static Error _write_file(const String &p_path, const String &p_content);
	static void _collect_rust_sources(const String &p_dir, Vector<String> &r_files, int p_depth = 0);
	static Error _write_extension_config(const String &p_config_global, const String &p_crate_name, const String &p_library_path_global);
};
