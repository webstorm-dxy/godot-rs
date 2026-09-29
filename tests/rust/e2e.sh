#!/usr/bin/env bash
# End-to-end smoke test for the Rust module: scaffold -> build -> run -> bindings.
#
# Covers the attachable-script surface (properties, methods, default arguments,
# signals, descriptor cache) through a GDScript that drives a generated crate.
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
grep -q 'derive(RustScript)' "$PROJ/src/player.rs" || fail "scaffolded script should use the derive macros"

cat > "$PROJ/project.godot" <<'EOF'
config_version=5

[application]

config/name="RustE2E"
run/main_scene="res://main.tscn"

[rust]

enabled=true
lsp/enabled=false
EOF

# A second script file: the module has to add its mod/register lines by itself.
cat > "$PROJ/src/emitter.rs" <<'EOF'
use godot::prelude::*;
use godot_script::prelude::*;

#[derive(RustScript)]
#[script(base = Node2D)]
pub struct Emitter {
    owner: Gd<Node2D>,
    #[export(default = 5)]
    hits: i64,
}

#[godot_script_api]
impl Emitter {
    #[func]
    fn emit_ping(&mut self, value: i64) {
        self.owner.clone().emit_signal("pinged", &[value.to_variant()]);
    }

    #[signal]
    fn pinged(value: i64) {}
}

godot_script::register_script!(Emitter);
EOF

cat > "$PROJ/check.gd" <<'EOF'
extends Node

func _on_pinged(value: int) -> void:
	print("E2E signal ", value)

func _ready() -> void:
	var player: Node2D = $Player
	print("E2E has_method ", player.has_method("jump"))
	player.set("speed", 7.0)
	print("E2E jump ", player.call("jump", 5.0))
	print("E2E speed ", player.get("speed"))
	print("E2E has_signal ", player.has_signal("jumped"))
	var properties: Array = []
	for property in player.get_script().get_script_property_list():
		properties.append(String(property.name))
	print("E2E properties ", properties)
	var emitter: Node2D = $Emitter
	print("E2E revert ", emitter.property_can_revert("hits"), " ", emitter.property_get_revert("hits"))
	emitter.connect("pinged", Callable(self, "_on_pinged"))
	emitter.call("emit_ping", 42)
EOF

cat > "$PROJ/main.tscn" <<'EOF'
[gd_scene load_steps=4 format=3]

[ext_resource type="Script" path="res://check.gd" id="1_check"]
[ext_resource type="Script" path="res://src/player.rs" id="2_player"]
[ext_resource type="Script" path="res://src/emitter.rs" id="3_emitter"]

[node name="Root" type="Node"]
script = ExtResource("1_check")

[node name="Player" type="Node2D" parent="."]
script = ExtResource("2_player")

[node name="Emitter" type="Node2D" parent="."]
script = ExtResource("3_emitter")
EOF

echo "== cargo build through --build-solutions"
"$BIN" --headless --editor --path "$PROJ" --build-solutions --quit || fail "cargo build failed"
LIB_COUNT=$(find "$PROJ/.godot/rust/target/debug" -maxdepth 1 -name 'lib*.dylib' -o -maxdepth 1 -name '*.so' -o -maxdepth 1 -name '*.dll' | wc -l | tr -d ' ')
[ "$LIB_COUNT" != "0" ] || fail "no cdylib produced"
grep -q 'mod emitter;' "$PROJ/src/lib.rs" || fail "the module did not register the new source file"

echo "== descriptor cache"
CACHE="$PROJ/.godot/rust/script_cache.json"
[ -e "$CACHE" ] || fail "script_cache.json missing"
grep -q 'res://src/emitter.rs' "$CACHE" || fail "the cache does not know the new script"
grep -q '"defaults"' "$CACHE" || fail "method defaults are not part of the descriptor"

echo "== run the project (properties, methods, signals)"
OUTPUT=$("$BIN" --headless --path "$PROJ" --quit-after 120 2>&1)
echo "$OUTPUT" | tail -20
echo "$OUTPUT" | grep -q 'Player ready' || fail "Rust script did not run"
echo "$OUTPUT" | grep -q 'E2E has_method true' || fail "has_method() did not see the #[func] method"
echo "$OUTPUT" | grep -q 'E2E jump 12' || fail "call() did not reach the method"
echo "$OUTPUT" | grep -q 'E2E speed 7' || fail "the exported property did not round-trip"
echo "$OUTPUT" | grep -q 'E2E has_signal true' || fail "has_signal() did not see the #[signal] declaration"
echo "$OUTPUT" | grep -q 'E2E properties \["speed"\]' || fail "the script property list is wrong"
echo "$OUTPUT" | grep -q 'E2E signal 42' || fail "the emitted signal did not reach GDScript"
echo "$OUTPUT" | grep -q 'E2E revert true 5' || fail "the #[export(default = ...)] value is not offered as revert"

echo "== regenerate bindings"
"$BIN" --headless --editor --path "$PROJ" --rust-regenerate-bindings --quit || fail "bindings regeneration failed"
[ -e "$PROJ/.godot/rust/bindings/extension_api.json" ] || fail "extension_api.json missing"
[ -e "$PROJ/.godot/rust/bindings/gdextension_interface.json" ] || fail "gdextension_interface.json missing"

echo "== all good (project kept at $PROJ)"
