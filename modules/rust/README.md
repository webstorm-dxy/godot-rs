# Rust module

> End-user documentation (Chinese, beginner friendly) lives in
> [`docs/rust/`](../../docs/rust/README.md): installation, project setup, writing
> attachable scripts, editor workflow, troubleshooting and architecture.

Turns Rust into a first-class language in the Godot editor, without a
hand-maintained `.gdextension` file in the project.

## What it does (M1: project + build closure)

- Recognizes `.rs` files as `RustScript` resources, so they open in the built-in
  script editor, get syntax highlighting from the active script language and
  participate in EditorFileSystem scans.
- Creates the Rust project when a new Godot project is created ("Rust Project"
  toggle in the project manager) or on demand:

  ```sh
  godot --headless --editor --rust-init-project /path/to/project --quit
  ```

  This writes `Cargo.toml`, `src/lib.rs`, `.cargo/config.toml` and the generated
  `.gdextension` file under `.godot/rust/`.
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
- Refreshes open scenes that have no unsaved changes after the library loads, so
  nodes using freshly built Rust types appear without reopening the scene.
- Generates bindings for *this* engine build: `extension_api.json` and
  `gdextension_interface.json` are dumped by the running editor into
  `.godot/rust/bindings/` and consumed through gdext's `api-custom-json` feature
  (`GDRUST_GODOT_API_JSON` / `GDRUST_GODOT_INTERFACE_JSON`). A modified engine
  therefore works without shipping a patched binding crate.

## Requirements

- A Rust toolchain with `cargo` (MSRV 1.94 for the vendored gdext 0.5.5).
- Network access for the first build of a project: cargo resolves gdext's
  transitive dependencies from crates.io and fetches the `godot4-prebuilt`
  repository that the upstream `gdextension-api` dependency points at (it is
  ignored at runtime because bindings are generated from this engine, but cargo
  still resolves it). Both are cached in `CARGO_HOME` afterwards; pre-warm with
  `cargo fetch` inside the project if you want the editor build to be instant.
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

## Editor settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `rust/cargo_path` | `""` | Explicit `cargo` executable; otherwise `PATH` and `~/.cargo/bin` are probed. |
| `rust/vendor_dir` | `""` | Explicit path to the vendored gdext workspace. |
| `rust/skip_build_before_playing` | `false` | Debug escape hatch: never build before running. |
| `GODOT_RUST_CARGO` (env) | unset | Explicit `cargo` executable; overrides both the setting and `PATH`. |
| `rust/show_output_panel_on_error` | `true` | Expand the dock when a build fails. |

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

## Scope of this milestone

Implemented: project layout, build integration, diagnostics, extension loading,
minimal language/resource layer.

Not implemented yet (planned, see the module plan document):

- `#[derive(RustScript)]` + a Rust-side `ScriptInstance` bridge so a `.rs` file
  can be attached to any node (GDScript-like semantics).
- rust-analyzer based completion/hover/goto/formatting in the built-in editor.
- Hot reload of class definitions inside the editor (`reloadable` is currently
  disabled in the generated configuration).
- Native breakpoint debugging (DAP client -> `lldb-dap`/CodeLLDB).
- Export support (shipping the `cdylib` with exported games).

## Layout

```
modules/rust/
├── rust_language.*            # ScriptLanguage (M1: highlighting, validation)
├── rust_script.*              # Script resource for .rs files
├── rust_script_resource_format.*
├── rust_diagnostics.*         # thread-safe diagnostic store
├── editor/
│   ├── rust_project.*         # paths, settings, scaffolding, extension list
│   ├── rust_bindings.*        # in-process extension API dump + cache
│   ├── rust_build.*           # cargo invocation, JSON diagnostics, threading
│   ├── rust_build_panel.*     # Rust dock (Problems/Output)
│   └── rust_editor_plugin.*   # run-bar button, build callback, extension load
├── vendor/gdext/              # vendored godot-rust 0.5.5 (MPL-2.0)
└── tools/sync_gdext.py        # re-vendor a newer upstream tag
```
