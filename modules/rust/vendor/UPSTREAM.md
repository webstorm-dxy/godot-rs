# Vendored godot-rust (gdext)

This directory contains an **unmodified** copy of the upstream godot-rust
("gdext") workspace, vendored into the engine so that Rust projects can build
against bindings that always match *this* engine build.

- Upstream: https://github.com/godot-rust/gdext
- Version: `v0.5.5` (tag commit `e7b3a4a7dc9678a5502dc8432c258c702ff89429`)
- License: MPL-2.0 (see `License.txt` / `godot/Cargo.toml`)

## Why vendored

The engine module `modules/rust` never patches these crates. Bindings are
generated from the running engine instead (`extension_api.json` and
`gdextension_interface.json`, dumped by `RustBindings` into
`<project>/.godot/rust/bindings/`) and handed to cargo through the
`api-custom-json` feature plus the `GDRUST_GODOT_API_JSON` /
`GDRUST_GODOT_INTERFACE_JSON` environment variables.

Keeping the copy unmodified means MPL-2.0's file-level copyleft never applies to
changes we would otherwise have to publish. If a patch ever becomes
unavoidable:

1. Put it in `vendor/patches/*.patch` (never edit the vendored files in place).
2. Record it below together with the modified file list, so the modified MPL
   sources stay available as required by the license.

## Local patches

None.

## Updating

```sh
python3 modules/rust/tools/sync_gdext.py --tag v0.5.6
```

The script re-clones the requested tag, replaces `vendor/gdext`, drops VCS
metadata, and re-applies `vendor/patches/*.patch` if any exist.
