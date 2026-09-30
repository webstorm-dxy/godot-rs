#pragma once

#include "editor/export/editor_export_plugin.h"

// Packs the built Rust library next to the exported game and puts the matching
// .gdextension configuration into the pack, so an exported project loads exactly
// the same scripts as the editor does.
class RustExportPlugin : public EditorExportPlugin {
	GDCLASS(RustExportPlugin, EditorExportPlugin);

protected:
	virtual void _export_begin(const HashSet<String> &p_features, bool p_is_debug, const String &p_path, int p_flags) override;

public:
	virtual String get_name() const override { return "RustScript"; }
};
