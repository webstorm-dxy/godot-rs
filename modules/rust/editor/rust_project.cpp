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

static bool _is_valid_rust_module_name(const String &p_name) {
	if (p_name.is_empty()) {
		return false;
	}
	if (p_name[0] >= '0' && p_name[0] <= '9') {
		return false;
	}
	for (int i = 0; i < p_name.length(); i++) {
		const char32_t c = p_name[i];
		const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
		if (!ok) {
			return false;
		}
	}
	static const char *const keywords[] = { "as", "break", "const", "continue", "crate", "dyn", "else", "enum", "extern", "false", "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return", "self", "static", "struct", "super", "trait", "true", "type", "unsafe", "use", "where", "while", nullptr };
	for (int i = 0; keywords[i] != nullptr; i++) {
		if (p_name == keywords[i]) {
			return false;
		}
	}
	return true;
}

void RustProject::sync_crate_modules() {
	const String src_dir = get_crate_root_global().path_join("src");
	const String crate_file = src_dir.path_join("lib.rs");
	if (!FileAccess::exists(crate_file)) {
		return;
	}

	Vector<String> modules;
	Vector<String> scripts;
	Ref<DirAccess> dir = DirAccess::open(src_dir);
	if (dir.is_valid()) {
		dir->list_dir_begin();
		String entry = dir->get_next();
		while (!entry.is_empty()) {
			if (!dir->current_is_dir() && entry.get_extension().to_lower() == "rs" && entry != "lib.rs") {
				const String stem = entry.get_basename();
				if (_is_valid_rust_module_name(stem)) {
					modules.push_back(stem);
					Ref<FileAccess> module_file = FileAccess::open(src_dir.path_join(entry), FileAccess::READ);
					if (module_file.is_valid() && module_file->get_as_utf8_string().contains("register_script!")) {
						scripts.push_back(stem);
					}
				}
			}
			entry = dir->get_next();
		}
		dir->list_dir_end();
	}
	if (modules.is_empty()) {
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

	bool changed = false;
	for (const String &module : modules) {
		const String mod_line = "mod " + module + ";";
		bool found = false;
		for (const String &line : lines) {
			if (line.strip_edges() == mod_line) {
				found = true;
				break;
			}
		}
		if (found) {
			continue;
		}
		int insert_at = 0;
		for (int i = 0; i < lines.size(); i++) {
			if (lines[i].begins_with("mod ")) {
				insert_at = i + 1;
			}
		}
		lines.insert(insert_at, mod_line);
		changed = true;
	}

	for (const String &module : scripts) {
		const String register_line = module + "::register();";
		bool found = false;
		for (const String &line : lines) {
			if (line.strip_edges() == register_line) {
				found = true;
				break;
			}
		}
		if (found) {
			continue;
		}
		for (int i = 0; i < lines.size(); i++) {
			if (!lines[i].contains("fn register_scripts()")) {
				continue;
			}
			for (int j = i + 1; j < lines.size(); j++) {
				if (lines[j].strip_edges() == "}") {
					lines.insert(j, "\t" + register_line);
					changed = true;
					break;
				}
			}
			break;
		}
	}

	if (!changed) {
		return;
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

// Where the vendored crates live (empty when they cannot be found).
static String _vendored_godot_path() {
	const String vendor_dir = RustProject::get_vendor_dir();
	if (vendor_dir.is_empty() || !DirAccess::dir_exists_absolute(vendor_dir.path_join("godot"))) {
		return String();
	}
	return vendor_dir.path_join("godot").replace("\\", "/");
}

static String _vendored_script_path() {
	const String vendor_dir = RustProject::get_vendor_dir();
	if (vendor_dir.is_empty()) {
		return String();
	}
	const String support_dir = vendor_dir.get_base_dir().get_base_dir().path_join("support/godot-script");
	if (!FileAccess::exists(support_dir.path_join("Cargo.toml"))) {
		return String();
	}
	return support_dir.replace("\\", "/");
}

// Finds an array value such as `features = [ ... ]` and reports its brackets.
static bool _find_array_value(const String &p_line, const String &p_key, int &r_open, int &r_close) {
	int at = p_line.find(p_key);
	while (at >= 0) {
		// "default-features" also contains "features", so the key must start a word.
		const char32_t before = at > 0 ? p_line[at - 1] : ' ';
		int i = at + p_key.length();
		if (before != '-' && before != '_' && before != '"') {
			while (i < p_line.length() && p_line[i] == ' ') {
				i++;
			}
			if (i < p_line.length() && p_line[i] == '=') {
				i++;
				while (i < p_line.length() && p_line[i] == ' ') {
					i++;
				}
				if (i < p_line.length() && p_line[i] == '[') {
					const int close = p_line.find("]", i);
					if (close > i) {
						r_open = i;
						r_close = close;
						return true;
					}
				}
			}
		}
		at = p_line.find(p_key, at + 1);
	}
	return false;
}

// Rewrites a godot dependency so the bindings come from this engine build,
// keeping the features and flags the author had asked for.
static String _godot_dependency_line(const String &p_existing = String()) {
	const String path = _vendored_godot_path();
	if (path.is_empty()) {
		return "godot = { version = \"0.5\", features = [\"api-custom-json\"] }";
	}
	String list = "\"api-custom-json\"";
	int open = 0;
	int close = 0;
	if (_find_array_value(p_existing, "features", open, close)) {
		for (const String &feature : p_existing.substr(open + 1, close - open - 1).split(",")) {
			const String name = feature.strip_edges().trim_prefix("\"").trim_suffix("\"");
			if (!name.is_empty() && name != "api-custom-json") {
				list += ", \"" + name + "\"";
			}
		}
	}
	String extras;
	for (const String &key : { String("default-features"), String("optional") }) {
		const int at = p_existing.find(key);
		if (at < 0) {
			continue;
		}
		const int eq = p_existing.find("=", at);
		if (eq < 0) {
			continue;
		}
		int end = p_existing.find(",", eq);
		const int brace = p_existing.find("}", eq);
		if (end < 0 || (brace >= 0 && brace < end)) {
			end = brace;
		}
		if (end < 0) {
			continue;
		}
		const String value = p_existing.substr(eq + 1, end - eq - 1).strip_edges();
		if (!value.is_empty()) {
			extras += vformat(", %s = %s", key, value);
		}
	}
	return vformat("godot = { path = \"%s\", features = [%s]%s }", path, list, extras);
}

static String _script_dependency_line() {
	const String path = _vendored_script_path();
	if (path.is_empty()) {
		return String();
	}
	return vformat("godot-script = { path = \"%s\" }", path);
}

void RustProject::sync_cargo_manifest() {
	const String crate_root = get_crate_root_global();
	const String src_dir = crate_root.path_join("src");
	const String manifest = crate_root.path_join("Cargo.toml");
	if (!FileAccess::exists(manifest)) {
		return;
	}

	// Which dependencies do the sources actually use?
	bool uses_godot = false;
	bool uses_script = false;
	Ref<DirAccess> dir = DirAccess::open(src_dir);
	if (dir.is_valid()) {
		dir->list_dir_begin();
		String entry = dir->get_next();
		while (!entry.is_empty()) {
			if (!dir->current_is_dir() && entry.get_extension().to_lower() == "rs") {
				Ref<FileAccess> source = FileAccess::open(src_dir.path_join(entry), FileAccess::READ);
				if (source.is_valid()) {
					const String text = source->get_as_utf8_string();
					uses_godot = uses_godot || text.contains("godot::");
					uses_script = uses_script || text.contains("godot_script::");
				}
			}
			entry = dir->get_next();
		}
		dir->list_dir_end();
	}
	if (!uses_godot && !uses_script) {
		return;
	}

	Error err;
	Ref<FileAccess> file = FileAccess::open(manifest, FileAccess::READ, &err);
	if (err != OK) {
		return;
	}
	Vector<String> lines;
	while (!file->eof_reached()) {
		lines.push_back(file->get_line());
	}
	file->close();

	const String godot_path = _vendored_godot_path();
	const String script_path = _vendored_script_path();

	bool has_godot = false;
	bool has_script = false;
	int dependencies_at = -1;
	int godot_at = -1;
	int script_at = -1;
	for (int i = 0; i < lines.size(); i++) {
		const String line = lines[i].strip_edges();
		if (line == "[dependencies]") {
			dependencies_at = i;
			continue;
		}
		if (line.begins_with("[dependencies.")) {
			// Table style: [dependencies.godot] / [dependencies.godot-script].
			const String name = line.trim_prefix("[dependencies.").trim_suffix("]");
			has_godot = has_godot || name == "godot";
			has_script = has_script || name == "godot-script";
			continue;
		}
		if (line.begins_with("godot-script")) {
			has_script = true;
			script_at = i;
		} else if (line.begins_with("godot")) {
			has_godot = true;
			godot_at = i;
		}
	}

	bool changed = false;
	Vector<String> additions;

	// Bindings must come from this engine build: a crates.io dependency pulls in a
	// second copy of godot-core, which breaks trait matching for scripts.
	if (uses_godot && has_godot && !godot_path.is_empty() && godot_at >= 0 && !lines[godot_at].contains(godot_path)) {
		lines.set(godot_at, _godot_dependency_line(lines[godot_at]));
		changed = true;
		print_line("Rust: pointed the godot dependency at this engine's bindings.");
	}
	if (uses_script && has_script && !script_path.is_empty() && script_at >= 0 && !lines[script_at].contains(script_path)) {
		lines.set(script_at, _script_dependency_line());
		changed = true;
		print_line("Rust: pointed the godot-script dependency at this engine's support crate.");
	}

	if (uses_godot && has_godot && !godot_path.is_empty() && godot_at < 0) {
		WARN_PRINT(vformat("Rust: the godot dependency is declared as a table, so it may not point at this engine's bindings. Use `godot = { path = \"%s\", features = [\"api-custom-json\"] }` instead.", godot_path));
	}

	if (uses_script && !has_script) {
		const String script_dep = _script_dependency_line();
		if (script_dep.is_empty()) {
			WARN_PRINT("Rust: these sources use godot_script, but no godot-script dependency was found and the module's support crate could not be located. Add it manually, e.g. godot-script = { path = \"<engine>/modules/rust/support/godot-script\" }.");
		} else {
			additions.push_back(script_dep);
		}
	}
	if (uses_godot && !has_godot) {
		additions.push_back(_godot_dependency_line());
	}
	if (!changed && additions.is_empty()) {
		return;
	}

	if (!additions.is_empty()) {
		if (dependencies_at < 0) {
			lines.push_back(String());
			lines.push_back("[dependencies]");
			dependencies_at = lines.size() - 1;
		}
		int insert_at = dependencies_at + 1;
		for (const String &addition : additions) {
			lines.insert(insert_at, addition);
			insert_at++;
		}
	}

	String content;
	for (const String &line : lines) {
		content += line + "\n";
	}
	Ref<FileAccess> out = FileAccess::open(manifest, FileAccess::WRITE, &err);
	if (err == OK) {
		out->store_string(content);
		if (!additions.is_empty()) {
			print_line("Rust: added the missing dependencies to Cargo.toml.");
		}
	}
}

void RustProject::sync_crate() {
	sync_cargo_manifest();
	sync_crate_modules();
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

	const String godot_dep = _godot_dependency_line();
	const String script_dep = _script_dependency_line();

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
