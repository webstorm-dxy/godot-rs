#!/usr/bin/env bash
# End-to-end smoke test for the Rust module (M1 scope).
#
# Usage: tests/rust/e2e.sh [path/to/godot-binary]
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [ -n "${1:-}" ]; then
  BIN="$1"
elif [ -x "$ROOT_DIR/bin/godot.macos.editor.arm64.rust" ]; then
  # Modules add their name to the binary, like the mono module does.
  BIN="$ROOT_DIR/bin/godot.macos.editor.arm64.rust"
else
  BIN="$ROOT_DIR/bin/godot.macos.editor.arm64"
fi
WORK_DIR="${WORK_DIR:-$(mktemp -d)}"
mkdir -p "$WORK_DIR"
PROJ="$WORK_DIR/rust_proj"

fail() { echo "FAIL: $*"; exit 1; }

echo "== godot binary: $BIN"
"$BIN" --headless --version || fail "cannot run godot"

echo "== scaffold Rust project"
"$BIN" --headless --editor --rust-init-project "$PROJ" --quit || fail "scaffolding failed"
for f in Cargo.toml src/lib.rs .cargo/config.toml .godot/rust/rust.gdextension .gitignore; do
  [ -e "$PROJ/$f" ] || fail "missing $f"
done
grep -q 'gdext_rust_init' "$PROJ/.godot/rust/rust.gdextension" || fail "extension config looks wrong"

cat > "$PROJ/project.godot" <<'EOF'
config_version=5

[application]

config/name="RustE2E"
run/main_scene="res://main.tscn"

[rust]

enabled=true
EOF

cat > "$PROJ/main.tscn" <<'EOF'
[gd_scene format=3]

[node name="Player" type="Player"]
EOF

echo "== cargo build through --build-solutions"
"$BIN" --headless --path "$PROJ" --build-solutions --quit || fail "cargo build failed"
LIB_COUNT=$(find "$PROJ/.godot/rust/target/debug" -maxdepth 1 -name 'lib*.dylib' -o -maxdepth 1 -name '*.so' -o -maxdepth 1 -name '*.dll' | wc -l | tr -d ' ')
[ "$LIB_COUNT" != "0" ] || fail "no cdylib produced"

echo "== run the project (expects the Rust example class to load)"
OUTPUT=$("$BIN" --headless --path "$PROJ" --quit-after 120 2>&1)
echo "$OUTPUT" | tail -20
echo "$OUTPUT" | grep -q 'Rust Player ready' || fail "Rust class did not run"

echo "== regenerate bindings"
"$BIN" --headless --editor --path "$PROJ" --rust-regenerate-bindings --quit || fail "bindings regeneration failed"
[ -e "$PROJ/.godot/rust/bindings/extension_api.json" ] || fail "extension_api.json missing"
[ -e "$PROJ/.godot/rust/bindings/gdextension_interface.json" ] || fail "gdextension_interface.json missing"

echo "== all good (project kept at $PROJ)"
