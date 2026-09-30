#!/usr/bin/env bash
# End-to-end check for the lldb-dap bridge (M5).
#
# A headless editor session drives RustDebug exactly like the Rust dock does: it
# starts lldb-dap, launches the game under it and sets a breakpoint in a .rs file.
# The game has to stop there, report the frame and its locals, and finish after
# being continued.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
GODOT="${GODOT:-$REPO/bin/godot.macos.editor.arm64.rust}"
WORK_DIR="${WORK_DIR:-$REPO/.tmp-rust-debug}"

if [ ! -x "$GODOT" ]; then
	echo "missing engine binary: $GODOT" >&2
	exit 2
fi

export RUSTUP_HOME="${RUSTUP_HOME:-$HOME/.rustup}"
export CARGO_NET_OFFLINE="${CARGO_NET_OFFLINE:-true}"
export GODOT_RUST_CARGO="${GODOT_RUST_CARGO:-$HOME/.cargo/bin/cargo}"

PROJ="$WORK_DIR/proj"
rm -rf "$WORK_DIR"
mkdir -p "$PROJ"

echo "== engine: $GODOT"
echo "== scaffold"
"$GODOT" --headless --editor --rust-init-project "$PROJ" --quit > "$WORK_DIR/scaffold.log" 2>&1 || true
if [ ! -f "$PROJ/Cargo.toml" ]; then
	echo "scaffold failed, see $WORK_DIR/scaffold.log" >&2
	exit 1
fi

cat > "$PROJ/project.godot" <<'EOF'
config_version=5

[application]

config/name="DebugProj"
run/main_scene="res://main.tscn"

[rust]

enabled=true
lsp/enabled=false
EOF

# The breakpoint goes inside `helper`, which `ready` calls.
cat > "$PROJ/src/player.rs" <<'EOF'
use godot::prelude::*;
use godot_script::prelude::*;

#[derive(RustScript)]
#[script(base = Node2D, tool)]
pub struct Player {
    owner: Gd<Node2D>,
}

fn helper(input: i64) -> i64 {
    let doubled = input * 2;
    godot_print!("M5 game helper {}", doubled);
    doubled
}

#[godot_script_api]
impl Player {
    fn ready(&mut self) {
        let value = helper(21);
        godot_print!("M5 game ready value={}", value);
    }
}

godot_script::register_script!(Player);
EOF

# Line of `let doubled = input * 2;` (1-based).
BREAKPOINT_LINE="$(grep -n 'let doubled' "$PROJ/src/player.rs" | cut -d: -f1)"
echo "== breakpoint at src/player.rs:$BREAKPOINT_LINE"

cat > "$PROJ/main.tscn" <<'EOF'
[gd_scene load_steps=2 format=3]

[ext_resource type="Script" path="res://src/player.rs" id="1_player"]

[node name="Player" type="Node2D"]
script = ExtResource("1_player")
EOF

cat > "$PROJ/driver.gd" <<EOF
@tool
extends Node

var dbg
var phase := 0
var frames := 0

func _ready() -> void:
	dbg = Engine.get_singleton("RustDebug")
	var project_dir: String = ProjectSettings.globalize_path("res://")
	var args := PackedStringArray(["--headless", "--path", project_dir, "--quit-after", "240"])
	var breakpoints := {"res://src/player.rs": [$BREAKPOINT_LINE]}
	var ok: bool = dbg.start_gd(OS.get_executable_path(), args, project_dir, breakpoints)
	print("M5 start ", ok, " state=", dbg.get_state(), " adapter=", dbg.get_adapter_path())
	if not ok:
		print("M5 FAIL start: ", dbg.get_error())
		phase = 2

func _process(_delta: float) -> void:
	if dbg == null or phase == 2:
		return
	frames += 1
	var state: String = dbg.get_state()
	if phase == 0 and state == "stopped":
		var loc: Dictionary = dbg.get_stop_location_gd()
		print("M5 stopped ", loc.get("file", ""), ":", loc.get("line", 0), " in ", loc.get("function", ""))
		var stack: Array = dbg.get_stack_gd()
		print("M5 frames ", stack.size())
		if stack.size() > 0:
			print("M5 top ", stack[0].get("name", ""), " file=", stack[0].get("file", ""))
		var variables: Array = dbg.get_variables_gd(0)
		var names := PackedStringArray()
		for variable in variables:
			names.append(str(variable.get("name", "")) + "=" + str(variable.get("value", "")))
		print("M5 variables ", variables.size(), " ", ", ".join(names))
		dbg.continue_()
		phase = 1
	elif phase == 1 and state == "terminated":
		print("M5 terminated")
		phase = 2
	if frames > 900 and phase == 0:
		print("M5 FAIL timeout, state=", state)
		phase = 2
		dbg.stop()
EOF

cat > "$PROJ/driver.tscn" <<'EOF'
[gd_scene load_steps=2 format=3]

[ext_resource type="Script" path="res://driver.gd" id="1_driver"]

[node name="Driver" type="Node"]
script = ExtResource("1_driver")
EOF

echo "== build the project"
"$GODOT" --headless --editor --path "$PROJ" --build-solutions --quit > "$WORK_DIR/build.log" 2>&1 || true

echo "== drive the debug session"
timeout 300 "$GODOT" --headless --editor --path "$PROJ" res://driver.tscn --quit-after 1200 > "$WORK_DIR/debug.log" 2>&1 || true

grep -E '^M5 ' "$WORK_DIR/debug.log" || true

if grep -q '^M5 FAIL' "$WORK_DIR/debug.log"; then
	echo "---- lldb-dap output ----"
	grep -iE 'M5|error|lldb' "$WORK_DIR/debug.log" | tail -20
	exit 1
fi
if ! grep -q '^M5 stopped res://src/player.rs:' "$WORK_DIR/debug.log"; then
	echo "the session never stopped at the breakpoint" >&2
	tail -30 "$WORK_DIR/debug.log" >&2
	exit 1
fi
if ! grep -q '^M5 terminated' "$WORK_DIR/debug.log"; then
	echo "the session never terminated after continue" >&2
	exit 1
fi

echo "================================================================"
echo "== debug session ok (project kept at $PROJ)"
