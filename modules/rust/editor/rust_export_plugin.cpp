#include "rust_export_plugin.h"

#include "rust_build.h"
#include "rust_project.h"

#include "core/io/file_access.h"
#include "core/os/os.h"

void RustExportPlugin::_export_begin(const HashSet<String> &p_features, bool p_is_debug, const String &p_path, int p_flags) {
	if (!RustProject::project_uses_rust()) {
		return;
	}

	const String platform = RustProject::export_platform(p_features);
	const String host = RustProject::host_platform();
	if (platform.is_empty()) {
		WARN_PRINT("Rust: this export platform is not supported yet, so the exported game will not load the Rust scripts. Build the library yourself and add it to the preset to package it.");
		return;
	}
	if (host.is_empty() || platform != host) {
		WARN_PRINT(vformat("Rust: exporting for '%s' from a '%s' editor needs a cross-compiled library; the exported game will not load the Rust scripts unless you add one.", platform, host));
		return;
	}

	const String profile = p_is_debug ? "debug" : "release";
	RustBuild *build = RustBuild::get_singleton();
	if (build == nullptr || !build->build_blocking(profile)) {
		WARN_PRINT("Rust: the cargo build for this export failed; the exported game will not load the Rust scripts.");
		return;
	}

	const String library_global = RustProject::get_library_path_global(profile);
	if (!FileAccess::exists(library_global)) {
		WARN_PRINT(vformat("Rust: '%s' is missing after the build; skipping the Rust files in this export.", library_global));
		return;
	}

	// The game loads this configuration itself (see RustPaths); the library sits
	// next to the executable (inside the bundle on macOS), which is where a res://
	// path points to in an exported project.
	const String library_res = "res://" + library_global.get_file();
	const String config = RustProject::make_export_config_text(platform, library_res);
	add_file(RustProject::get_extension_config_res(), config.to_utf8_buffer(), false);

	Vector<String> tags;
	tags.push_back(platform);
#ifdef __APPLE__
	add_shared_object(library_global, tags, "Contents/Resources");
#else
	add_shared_object(library_global, tags);
#endif

	print_line(vformat("Rust: packaged %s (%s, %s) into the export.", library_global.get_file(), platform, profile));
}
