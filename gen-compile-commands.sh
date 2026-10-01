#!/usr/bin/env bash

# Local helper that (re)generates compile_commands.json for IDE tooling
# (clangd, VS Code C/C++, CLion, ...).
# Not part of upstream Godot; kept in this fork as a build convenience.
#
# A compilation database is only valid for the platform/arch/target combination
# it was generated with, so the flags below must stay in sync with
# build-macos.sh. Regenerate after changing them, and after adding or removing
# source files.
#
# The compilation database is written next to SConstruct (repo root), which is
# where clangd and most IDEs look for it. It is already covered by .gitignore.

set -euo pipefail

# Same four machine-specific workarounds as build-macos.sh -- see that script
# for the full explanation. They have to be repeated here because SCons
# otherwise configures a different (older) toolchain and the generated commands
# would not match a real build.

readonly repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

readonly developer_dir="${DEVELOPER_DIR:-/Library/Developer/CommandLineTools}"
readonly module_cache_dir="${CLANG_MODULE_CACHE_PATH:-/tmp/clang-modules}"
readonly target="${TARGET:-editor}"

export PATH="/Library/Developer/CommandLineTools/usr/bin:$PATH"
export DEVELOPER_DIR="$developer_dir"
export TMPDIR="${TMPDIR:-/tmp}"
export CLANG_MODULE_CACHE_PATH="$module_cache_dir"

mkdir -p "$CLANG_MODULE_CACHE_PATH"

cd "$repo_dir"

# compiledb_gen_only makes SCons write the database and exit before compiling,
# so this is safe to run against an already built tree.
exec scons \
	platform=macos \
	arch=arm64 \
	target="$target" \
	module_mono_enabled=no \
	module_rust_enabled="${MODULE_RUST_ENABLED:-yes}" \
	import_env_vars=DEVELOPER_DIR,TMPDIR,CLANG_MODULE_CACHE_PATH \
	compiledb=yes \
	compiledb_gen_only=yes \
	"$@"
