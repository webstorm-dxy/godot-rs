#pragma once

#include "core/string/ustring.h"
#include "core/typedefs.h"

// Generates the GDExtension bindings description for *this* engine build and
// caches it inside the project. gdext only needs these two JSON files to
// generate bindings that match the engine ABI exactly, which is what makes a
// modified engine usable from Rust without shipping a patched binding crate.
class RustBindings {
public:
	static Error ensure(bool p_force = false);
	static String get_api_json_path();
	static String get_interface_json_path();
};
