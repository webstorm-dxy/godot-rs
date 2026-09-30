#!/usr/bin/env bash

# Local helper for building this Godot checkout on this machine.
# Not part of upstream Godot; kept in this fork as a build convenience.

set -euo pipefail

# Four machine-specific workarounds are applied below:
#
# 1. `xcode-select` points at Xcode 15.3 (Apple Clang 15), but SConstruct aborts
#    on anything older than Apple Clang 16. The Command Line Tools 26.6
#    toolchain (Apple Clang 21 + MacOSX26.5 SDK) is used instead, which only
#    needs DEVELOPER_DIR -- no `sudo xcode-select -s` required.
#
# 2. SConstruct builds its SCons environment with `Environment(tools=[])`,
#    which drops the outer environment. DEVELOPER_DIR therefore has to be
#    handed over explicitly through the `import_env_vars` option, otherwise
#    SCons silently falls back to the Xcode 15.3 compiler and the build fails
#    inside the SDK's libc++ headers.
#
# 3. Clang keeps its module cache in the per-user Darwin cache directory, which
#    is not writable here. CLANG_MODULE_CACHE_PATH redirects it.
#
# 4. Some shells put Homebrew LLVM (llvm@20) first in PATH. Its libc++ headers
#    get mixed with the Apple SDK ones and the build dies with
#    "could not build module 'std_float_h'". Prepend the Command Line Tools
#    bin directory so clang/clang++ resolve to the Apple toolchain.

readonly repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

readonly developer_dir="${DEVELOPER_DIR:-/Library/Developer/CommandLineTools}"
readonly module_cache_dir="${CLANG_MODULE_CACHE_PATH:-/tmp/clang-modules}"
readonly jobs="${JOBS:-$(sysctl -n hw.ncpu)}"
readonly target="${TARGET:-editor}"

export PATH="/Library/Developer/CommandLineTools/usr/bin:$PATH"
export DEVELOPER_DIR="$developer_dir"
export TMPDIR="${TMPDIR:-/tmp}"
export CLANG_MODULE_CACHE_PATH="$module_cache_dir"

mkdir -p "$CLANG_MODULE_CACHE_PATH"

cd "$repo_dir"

exec scons \
	platform=macos \
	arch=arm64 \
	target="$target" \
	module_mono_enabled=no \
	module_rust_enabled="${MODULE_RUST_ENABLED:-yes}" \
	import_env_vars=DEVELOPER_DIR,TMPDIR,CLANG_MODULE_CACHE_PATH \
	-j"$jobs" \
	"$@"
