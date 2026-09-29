# Rust module

> End-user documentation (Chinese, beginner friendly) lives in
> [`docs/rust/`](../../docs/rust/README.md): installation, project setup, writing
> attachable scripts, editor workflow, troubleshooting and architecture.

Turns Rust into a first-class language in the Godot editor, without a
hand-maintained `.gdextension` file in the project: a `.rs` file is a script you
attach to any node, write with `#[derive(RustScript)]`, and call from GDScript
like a `.gd` file.

## What it does

**Project and build closure**

- Recognizes `.rs` files as `RustScript` resources, so they open in the built-in
  script editor, get syntax highlighting from the active script language and
  participate in EditorFileSystem scans.
- Creates the Rust project when a new Godot project is created ("Rust Project"
  toggle in the project manager) or on demand:

  ```sh
  godot --headless --editor --rust-init-project /path/to/project --quit
  ```

  This writes `Cargo.toml`, `src/lib.rs`, `src/player.rs`, `.cargo/config.toml`,
  `.gitignore` and the generated `.gdextension` file under `.godot/rust/`.
- Keeps the crate healthy: every `src/*.rs` gets its `mod`/`register()` line in
  `src/lib.rs`, and `Cargo.toml` is pointed at this engine's vendored `godot`
  crate plus `godot-script` when they are missing or wrong (a crates.io `godot`
  would pull in a second `godot-core`).
- Builds the crate with `cargo build --message-format=json-render-diagnostics`:
  - automatically before running the project (F5 and `--build-solutions`),
  - on editor startup when the library is missing or older than the sources,
  - from the run-bar *Build* button or the *Rust* bottom dock.
- Shows cargo diagnostics in the *Rust* dock (Problems/Output) and in the editor
  log; double-clicking a problem opens the `.rs` file at the reported line. In
  headless runs cargo's output is mirrored to the console, so
  `--build-solutions` is usable from CI.
- Loads the built library through `GDExtensionManager` directly (in every build
  mode). The configuration lives under `.godot/`, which the engine's own
  extension list cannot track: `extension_list.cfg` is (re)written from the
  `.gdextension` files the editor finds under `res://`. The module therefore
  loads the configuration itself and never writes that list; when no library has
  been built yet it stays quiet instead of logging load errors.
- Generates bindings for *this* engine build: `extension_api.json` and
  `gdextension_interface.json` are dumped by the running editor into
  `.godot/rust/bindings/` and consumed through gdext's `api-custom-json` feature
  (`GDRUST_GODOT_API_JSON` / `GDRUST_GODOT_INTERFACE_JSON`). A modified engine
  therefore works without shipping a patched binding crate.

**Attachable scripts** (`support/godot-script`)

- `#[derive(RustScript)]` reads the struct: `#[script(base = Node2D, tool, name)]`,
  `#[export]` fields (with `default`, `range`, `enum`, `flags`, `file`, `dir`,
  `multiline`, `placeholder`, `node`, `color_no_alpha`, `exp_easing`, `storage`),
  and a field called `owner` that receives the node the script is attached to.
- `#[godot_script_api]` reads the impl block: the five lifecycle hooks by name
  (`ready`, `process`, `physics_process`, `enter_tree`, `exit_tree`), `#[func]`
  methods callable from GDScript and the editor (with `#[opt(default = ...)]`
  optional parameters) and `#[signal]` declarations.
- Exported properties reach the inspector, are saved with the scene, and work
  through a placeholder instance before the library exists.
- Class name, base type, properties, methods and signals are reported to the
  engine registry, so `has_method()`, `call()`, `get_method_list()`,
  `has_signal()`, `get_signal_list()` and `get_script_*_list()` behave like
  GDScript's.
- Samples: [`support/godot-script/src/lib.rs`](support/godot-script/src/lib.rs)
  and [`docs/rust/03-writing-scripts.md`](../../docs/rust/03-writing-scripts.md).
- Tool scripts (`#[script(..., tool)]`) run in the editor; plain scripts only run
  in the game, exactly like GDScript.
- Descriptors are cached in `.godot/rust/script_cache.json` after every build, so
  the editor still knows class name, base type, properties, methods and signals
  when the library has not been loaded (fresh checkout, failed build).

**Language server** (`editor/rust_lsp.*`, `docs/rust/07-language-server.md`)

- Starts `rust-analyzer` for the crate and forwards completion, hover,
  go-to-definition and diagnostics to the built-in script editor.
- Fails fast with the captured stderr when the binary is missing or dies, instead
  of hanging on the first request; `rust/rust_analyzer_path` selects an explicit
  binary.

## Requirements

- A Rust toolchain with `cargo` (MSRV 1.94 for the vendored gdext 0.5.5).
- Network access for the first build of a project: cargo resolves gdext's
  transitive dependencies from crates.io and fetches the `godot4-prebuilt`
  repository that the upstream `gdextension-api` dependency points at (it is
  ignored at runtime because bindings are generated from this engine, but cargo
  still resolves it). Both are cached in `CARGO_HOME` afterwards; pre-warm with
  `cargo fetch` inside the project if you want the editor build to be instant.
- `rust-analyzer` for IDE features (`rustup component add rust-analyzer`).
- The `godot` crate is taken from `modules/rust/vendor/gdext` (vendored upstream
  copy, MPL-2.0, see `vendor/UPSTREAM.md`); `GODOT_RUST_VENDOR_DIR` or the editor
  setting `rust/vendor_dir` override the location, and if the vendored copy is
  missing the scaffolder falls back to the crates.io release.

## Project settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `rust/enabled` | `false` | Marks the project as Rust-enabled (adds the `Rust` feature tag). |
| `rust/crate_root` | `res://` | Where `Cargo.toml` lives; used to map cargo paths to `res://`. |
| `rust/build/profile` | `debug` | `debug` or `release`. |
| `rust/build/before_playing` | `true` | Build before running the project. |
| `rust/build/on_editor_startup` | `true` | Build on editor startup when the library is stale. |
| `rust/build/extra_flags` | `""` | Extra arguments appended to `cargo build`. |
| `rust/bindings_source` | `vendored` | `vendored` or `crates-io`. |
| `rust/lsp/enabled` | `true` | Start `rust-analyzer` for the crate while the editor runs. |

## Editor settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `rust/cargo_path` | `""` | Explicit `cargo` executable; otherwise `PATH` and `~/.cargo/bin` are probed. |
| `rust/vendor_dir` | `""` | Explicit path to the vendored gdext workspace. |
| `rust/rust_analyzer_path` | `""` | Explicit `rust-analyzer` executable. |
| `rust/skip_build_before_playing` | `false` | Debug escape hatch: never build before running. |
| `rust/show_output_panel_on_error` | `true` | Expand the dock when a build fails. |
| `GODOT_RUST_CARGO` (env) | unset | Explicit `cargo` executable; overrides both the setting and `PATH`. |
| `GODOT_RUST_ANALYZER` (env) | unset | Explicit `rust-analyzer` executable. |

## Command line

- `--rust-init-project <dir>`: scaffold (or re-scaffold) the Rust project.
- `--rust-regenerate-bindings`: refresh `extension_api.json` /
  `gdextension_interface.json` for the current project.

Both are handled during module initialization and do not quit the engine, so
pass `--editor` and `--quit` (or `--quit-after N`) when running headless:

```sh
godot --headless --editor --path /path/to/project --rust-regenerate-bindings --quit
godot --headless --editor --path /path/to/project --rust-init-project /path/to/project --quit
```

The same applies to `--build-solutions`, which builds and then keeps the editor
running:

```sh
godot --headless --path /path/to/project --build-solutions --quit
```

## Status

Implemented (see `docs/rust/06-architecture.md` for the internals): project
layout, build integration, diagnostics, extension loading, attachable scripts
with derive macros, methods/signals, descriptor cache, tool scripts,
rust-analyzer integration.

Not implemented yet (planned):

- Hot reload of class definitions inside the editor (`reloadable` is currently
  disabled in the generated configuration; a rebuild needs an editor restart).
- RPCs (`@rpc` style multiplayer synchronization).
- Native breakpoint debugging (DAP client -> `lldb-dap`/CodeLLDB).
- Export support (shipping the `cdylib` with exported games).

## Tests

`tests/rust/e2e.sh` scaffolds a project, builds it through `--build-solutions`,
runs it and regenerates the bindings. It expects a built engine binary and a
warm cargo cache; set `WORK_DIR` to keep the generated project.

## Layout

```
modules/rust/
├── rust_language.*            # ScriptLanguage: highlighting, validation, LSP hooks
├── rust_script.*              # Script resource for .rs files (descriptor backed)
├── rust_script_registry.*     # RustScriptRegistry engine singleton
├── rust_script_resource_format.*
├── rust_paths.*               # .godot/rust paths (bindings, target, cache)
├── rust_diagnostics.*         # thread-safe diagnostic store
├── editor/
│   ├── rust_project.*         # paths, settings, scaffolding, script cache
│   ├── rust_bindings.*        # in-process extension API dump + cache
│   ├── rust_build.*           # cargo invocation, JSON diagnostics, threading
│   ├── rust_build_panel.*     # Rust dock (Problems/Output)
│   ├── rust_lsp.*             # rust-analyzer client (completion/hover/goto)
│   └── rust_editor_plugin.*   # run-bar button, build callback, extension load
├── icons/                     # editor icons shipped with the theme
├── support/godot-script/      # runtime crate: RustScript trait, ScriptInstance
├── support/godot-script-derive/ # proc macros: RustScript, godot_script_api
├── vendor/gdext/              # vendored godot-rust 0.5.5 (MPL-2.0)
└── tools/sync_gdext.py        # re-vendor a newer upstream tag
```
