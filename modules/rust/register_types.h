#pragma once

#include "modules/register_module_types.h"

void initialize_rust_module(ModuleInitializationLevel p_level);
void uninitialize_rust_module(ModuleInitializationLevel p_level);

#ifdef TOOLS_ENABLED
// Makes sure the .rs syntax highlighter is known to the script editor. Called from
// the editor init callbacks and again from the editor plugin's startup check.
void rust_register_syntax_highlighter();
#endif
