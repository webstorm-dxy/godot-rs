#include "rust_project.h"

#include "rust_bindings.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/variant/variant.h"
#include "core/version.h"

#ifdef TOOLS_ENABLED
#include "editor/settings/editor_settings.h"
#endif

static String _rust_cwd() {
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	return da.is_valid() ? da->get_current_dir() : String();
}

static String _darwin_lib(const String &p_crate) { return "lib" + p_crate + ".dylib"; }
static String _windows_lib(const String &p_crate) { return p_crate + ".dll"; }
static String _linux_lib(const String &p_crate) { return "lib" + p_crate + ".so"; }

bool RustProject::has_project() {
	return !ProjectSettings::get_singleton()->get_resource_path().is_empty();
}

bool RustProject::is_enabled() {
	return has_project() && (bool)GLOBAL_GET("rust/enabled");
}

bool RustProject::project_uses_rust() {
	return is_enabled() || FileAccess::exists(get_cargo_manifest_global());
}

String RustProject::get_crate_root_res() {
	return has_project() ? (String)GLOBAL_GET("rust/crate_root") : String();
}

String RustProject::get_crate_root_global() {
	return ProjectSettings::get_singleton()->globalize_path(get_crate_root_res());
}

String RustProject::get_vendor_dir() {
#ifdef TOOLS_ENABLED
	if (EditorSettings::get_singleton() != nullptr) {
		String override_dir = EditorSettings::get_singleton()->get_setting("rust/vendor_dir");
		if (!override_dir.is_empty()) {
			return override_dir;
		}
	}
#endif
	String env_dir = OS::get_singleton()->get_environment("GODOT_RUST_VENDOR_DIR");
	if (!env_dir.is_empty()) {
		return env_dir;
	}
	// Locally built editors live in <engine>/bin, so the vendored crates shipped
	// with this source tree can be found relative to the executable.
	String relative = OS::get_singleton()->get_executable_path().get_base_dir().path_join("../modules/rust/vendor/gdext").simplify_path();
	if (DirAccess::dir_exists_absolute(relative.path_join("godot"))) {
		return relative;
	}
	return String();
}

String RustProject::get_cargo_manifest_global() {
	return has_project() ? get_crate_root_global().path_join("Cargo.toml") : String();
}

String RustProject::get_crate_name() {
	Error err;
	Ref<FileAccess> file = FileAccess::open(get_cargo_manifest_global(), FileAccess::READ, &err);
	if (err != OK) {
		return String();
	}

	bool in_package = false;
	while (!file->eof_reached()) {
		String line = file->get_line().strip_edges();
		if (line.begins_with("[") && line.ends_with("]")) {
			in_package = line == "[package]";
			continue;
		}
		if (!in_package || !line.begins_with("name")) {
			continue;
		}
		int eq = line.find_char('=');
		if (eq == -1) {
			continue;
		}
		String value = line.substr(eq + 1).strip_edges().trim_prefix("\"").trim_suffix("\"");
		if (!value.is_empty()) {
			return value;
		}
	}
	return String();
}

String RustProject::get_library_file_name() {
	String crate = get_crate_name();
	if (crate.is_empty()) {
		return String();
	}
#if defined(WINDOWS_ENABLED)
	return _windows_lib(crate);
#elif defined(MACOS_ENABLED)
	return _darwin_lib(crate);
#else
	return _linux_lib(crate);
#endif
}

String RustProject::get_library_path_global(const String &p_profile) {
	String lib = get_library_file_name();
	if (lib.is_empty()) {
		return String();
	}
	return get_target_dir_global().path_join(p_profile == "release" ? "release" : "debug").path_join(lib);
}

String RustProject::find_existing_library() {
	for (const String &profile : { String("debug"), String("release") }) {
		String path = get_library_path_global(profile);
		if (!path.is_empty() && FileAccess::exists(path)) {
			return path;
		}
	}
	return String();
}

void RustProject::_collect_rust_sources(const String &p_dir, Vector<String> &r_files, int p_depth) {
	if (p_depth > 16) {
		return;
	}
	Ref<DirAccess> dir = DirAccess::open(p_dir);
	if (dir.is_null()) {
		return;
	}
	dir->list_dir_begin();
	String entry = dir->get_next();
	while (!entry.is_empty()) {
		if (dir->current_is_dir()) {
			if (entry != "." && entry != ".." && !entry.begins_with(".") && entry != "target" && entry != "vendor") {
				_collect_rust_sources(p_dir.path_join(entry), r_files, p_depth + 1);
			}
		} else {
			String ext = entry.get_extension().to_lower();
			if (ext == "rs" || entry == "Cargo.toml") {
				r_files.push_back(p_dir.path_join(entry));
			}
		}
		entry = dir->get_next();
	}
	dir->list_dir_end();
}

bool RustProject::is_library_stale(const String &p_library_path_global) {
	if (p_library_path_global.is_empty() || !FileAccess::exists(p_library_path_global)) {
		return true;
	}
	uint64_t lib_time = FileAccess::get_modified_time(p_library_path_global);
	String crate_root = get_crate_root_global();
	if (crate_root.is_empty()) {
		return true;
	}
	Vector<String> sources;
	_collect_rust_sources(crate_root, sources);
	for (const String &source : sources) {
		if (FileAccess::get_modified_time(source) > lib_time) {
			return true;
		}
	}
	return false;
}

void RustProject::register_settings() {
	GLOBAL_DEF(PropertyInfo(Variant::BOOL, "rust/enabled"), false);
	GLOBAL_DEF(PropertyInfo(Variant::STRING, "rust/crate_root", PROPERTY_HINT_DIR), "res://");
	GLOBAL_DEF(PropertyInfo(Variant::STRING, "rust/build/profile", PROPERTY_HINT_ENUM, "debug,release"), "debug");
	GLOBAL_DEF(PropertyInfo(Variant::BOOL, "rust/build/before_playing"), true);
	GLOBAL_DEF(PropertyInfo(Variant::BOOL, "rust/build/on_editor_startup"), true);
	GLOBAL_DEF(PropertyInfo(Variant::STRING, "rust/build/extra_flags"), "");
	GLOBAL_DEF(PropertyInfo(Variant::STRING, "rust/bindings_source", PROPERTY_HINT_ENUM, "vendored,crates-io"), "vendored");
	GLOBAL_DEF(PropertyInfo(Variant::BOOL, "rust/lsp/enabled"), true);
}

void RustProject::register_editor_settings() {
#ifdef TOOLS_ENABLED
	if (EditorSettings::get_singleton() == nullptr) {
		return;
	}
	if (EditorSettings::get_singleton()->has_setting("rust/cargo_path")) {
		return;
	}
	EDITOR_DEF("rust/cargo_path", "");
	EDITOR_DEF("rust/vendor_dir", "");
	EDITOR_DEF("rust/rust_analyzer_path", "");
	EDITOR_DEF("rust/skip_build_before_playing", false);
	EDITOR_DEF("rust/show_output_panel_on_error", true);
#endif
}

String RustProject::_sanitize_crate_name(const String &p_name) {
	String name = p_name.strip_edges().to_lower();
	String out;
	for (int i = 0; i < name.length(); i++) {
		char32_t c = name[i];
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
			out += String::chr(c);
		} else {
			out += "_";
		}
	}
	if (out.is_empty() || (out[0] >= '0' && out[0] <= '9')) {
		out = "rust_game";
	}
	return out;
}

Error RustProject::_write_file(const String &p_path, const String &p_content) {
	Error err;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
	if (err != OK) {
		return err;
	}
	file->store_string(p_content);
	return OK;
}

Error RustProject::_write_extension_config(const String &p_config_global, const String &p_crate_name, const String &p_library_path_global) {
	String crate = p_crate_name.is_empty() ? String("rust_game") : p_crate_name;
	String base = "res://.godot/rust/target/";
	const String darwin = _darwin_lib(crate);
	const String windows = _windows_lib(crate);
	const String linux = _linux_lib(crate);

	const String content = vformat(R"GDEXT([configuration]

entry_symbol = "gdext_rust_init"
compatibility_minimum = "%d.%d"
reloadable = false

[libraries]

macos.debug = "%sdebug/%s"
macos.release = "%srelease/%s"
windows.debug.x86_64 = "%sdebug/%s"
windows.release.x86_64 = "%srelease/%s"
windows.debug.arm64 = "%sdebug/%s"
windows.release.arm64 = "%srelease/%s"
linux.debug.x86_64 = "%sdebug/%s"
linux.release.x86_64 = "%srelease/%s"
linux.debug.arm64 = "%sdebug/%s"
linux.release.arm64 = "%srelease/%s"
)GDEXT",
			GODOT_VERSION_MAJOR, GODOT_VERSION_MINOR,
			base, darwin, base, darwin,
			base, windows, base, windows,
			base, windows, base, windows,
			base, linux, base, linux,
			base, linux, base, linux);

	Error err = DirAccess::make_dir_recursive_absolute(p_config_global.get_base_dir());
	ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot create the Rust data directory.");
	return _write_file(p_config_global, content);
}


Error RustProject::ensure_extension_config(const String &p_library_path_global) {
	return _write_extension_config(get_extension_config_global(), get_crate_name(), p_library_path_global);
}


Error RustProject::scaffold_project(const String &p_project_dir_global, const String &p_project_name) {
	String project_dir = p_project_dir_global.simplify_path();
	if (!project_dir.is_absolute_path()) {
		project_dir = _rust_cwd().path_join(project_dir).simplify_path();
	}
	String crate_name = _sanitize_crate_name(p_project_name.strip_edges().is_empty() ? project_dir.get_file() : p_project_name);

	const String data_dir = project_dir.path_join(".godot").path_join("rust");
	Error err = DirAccess::make_dir_recursive_absolute(project_dir.path_join("src"));
	ERR_FAIL_COND_V_MSG(err != OK, err, vformat("Cannot create '%s/src'.", project_dir));
	err = DirAccess::make_dir_recursive_absolute(project_dir.path_join(".cargo"));
	ERR_FAIL_COND_V_MSG(err != OK, err, vformat("Cannot create '%s/.cargo'.", project_dir));
	err = DirAccess::make_dir_recursive_absolute(data_dir);
	ERR_FAIL_COND_V_MSG(err != OK, err, vformat("Cannot create '%s'.", data_dir));

	const String vendor_dir = get_vendor_dir();
	String godot_dep;
	String script_dep;
	if (!vendor_dir.is_empty() && DirAccess::dir_exists_absolute(vendor_dir.path_join("godot"))) {
		godot_dep = vformat("godot = { path = \"%s\", features = [\"api-custom-json\"] }", vendor_dir.path_join("godot").replace("\\", "/"));
		// godot-script ships with the engine and provides attachable scripts.
		String support_dir = vendor_dir.get_base_dir().get_base_dir().path_join("support/godot-script");
		if (FileAccess::exists(support_dir.path_join("Cargo.toml"))) {
			script_dep = vformat("godot-script = { path = \"%s\" }", support_dir.replace("\\", "/"));
		}
	} else {
		godot_dep = "godot = { version = \"0.5\", features = [\"api-custom-json\"] }";
	}

	const String cargo_toml = vformat(R"TOML([package]
name = "%s"
version = "0.1.0"
edition = "2024"
rust-version = "1.94"

[lib]
crate-type = ["cdylib"]

[dependencies]
%s
%s)TOML", crate_name, godot_dep, script_dep);

	const String lib_rs = R"RUST(use godot::prelude::*;

mod player;

/// The extension library entry point. A GDExtension can only have one.
struct RustGame;

#[gdextension]
unsafe impl ExtensionLibrary for RustGame {
    fn on_stage_init(stage: InitStage) {
        if stage == InitStage::Scene {
            register_scripts();
        }
    }
}

/// Registers every attachable script of this crate.
///
/// Scripts created from the editor are added here automatically.
pub fn register_scripts() {
    player::register();
}
)RUST";

	const String player_rs = R"RUST(use godot::prelude::*;
use godot_script::prelude::*;

/// Example script. Attach it to any Node2D in the editor, or delete it.
pub struct Player {
    owner: Gd<Node2D>,
    speed: f32,
}

impl RustScript for Player {
    type Base = Node2D;
    const CLASS_NAME: &'static str = "Player";
    const BASE_NAME: &'static str = "Node2D";

    fn new(owner: Gd<Self::Base>) -> Self {
        Self { owner, speed: 100.0 }
    }

    fn properties() -> Vec<PropertyInfo> {
        vec![PropertyInfo::new_export::<f32>("speed")]
    }

    fn get_property(&self, name: &str) -> Option<Variant> {
        match name {
            "speed" => Some(self.speed.to_variant()),
            _ => None,
        }
    }

    fn set_property(&mut self, name: &str, value: &Variant) -> bool {
        match name {
            "speed" => {
                self.speed = f32::from_variant(value);
                true
            }
            _ => false,
        }
    }

    fn ready(&mut self) {
        godot_print!("Player ready, speed = {}", self.speed);
    }

    fn process(&mut self, delta: f64) {
        let distance = self.speed as f64 * delta;
        let _ = (&self.owner, distance);
    }
}

godot_script::register_script!(Player);
)RUST";

	const String bindings_dir = data_dir.path_join("bindings").replace("\\", "/");
	const String target_dir = data_dir.path_join("target").replace("\\", "/");
	const String cargo_config = vformat(R"TOML([build]
target-dir = "%s"

[env]
GDRUST_GODOT_API_JSON = { value = "%s/extension_api.json", force = true }
GDRUST_GODOT_INTERFACE_JSON = { value = "%s/gdextension_interface.json", force = true }
)TOML", target_dir, bindings_dir, bindings_dir);

	err = _write_file(project_dir.path_join("Cargo.toml"), cargo_toml);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot write Cargo.toml.");
	err = _write_file(project_dir.path_join("src").path_join("lib.rs"), lib_rs);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot write src/lib.rs.");
	err = _write_file(project_dir.path_join("src").path_join("player.rs"), player_rs);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot write src/player.rs.");
	err = _write_file(project_dir.path_join(".cargo").path_join("config.toml"), cargo_config);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot write .cargo/config.toml.");

	const String gitignore = project_dir.path_join(".gitignore");
	if (!FileAccess::exists(gitignore)) {
		_write_file(gitignore, "# Godot 4+ specific ignores\n.godot/\n");
	}

	err = _write_extension_config(data_dir.path_join("rust.gdextension"), crate_name, String());
	ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot write the GDExtension configuration.");
	const String project_godot = project_dir.path_join("project.godot");
	if (FileAccess::exists(project_godot)) {
		Ref<ConfigFile> cfg;
		cfg.instantiate();
		if (cfg->load(project_godot) == OK) {
			cfg->set_value("rust", "enabled", true);
			cfg->save(project_godot);
		}
	}

	print_line(vformat("Rust project created at '%s' (crate: %s).", project_dir, crate_name));
	return OK;
}

bool RustProject::handle_cmdline() {
	Vector<String> args;
	for (const String &arg : OS::get_singleton()->get_cmdline_args()) {
		args.push_back(arg);
	}

	String init_project_dir;
	bool regenerate_bindings = false;
	for (int i = 0; i < args.size(); i++) {
		if (args[i] == "--rust-init-project") {
			if (i + 1 < args.size() && !args[i + 1].begins_with("--")) {
				init_project_dir = args[i + 1];
			} else {
				init_project_dir = ".";
			}
		} else if (args[i] == "--rust-regenerate-bindings") {
			regenerate_bindings = true;
		}
	}

	if (!init_project_dir.is_empty()) {
		Error err = scaffold_project(init_project_dir, init_project_dir.simplify_path().get_file());
		if (err != OK) {
			OS::get_singleton()->set_exit_code(1);
		}
		print_line("Rust: command finished. Pass --quit to exit the engine after this command.");
		return true;
	}

	if (regenerate_bindings) {
		Error err = RustBindings::ensure(true);
		if (err != OK) {
			OS::get_singleton()->set_exit_code(1);
		}
		print_line("Rust: command finished. Pass --quit to exit the engine after this command.");
		return true;
	}

	return false;
}
