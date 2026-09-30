#pragma once

#include "editor/script/syntax_highlighters.h"

#include "scene/resources/syntax_highlighter.h"

// Syntax coloring for .rs files in the built-in script editor.
//
// The script editor picks a highlighter by script language name, and a language
// without one keeps its files plain, so this one answers to "Rust". It mirrors
// the editor's standard highlighter (same theme colors, keywords taken from
// RustLanguage) and adds what Rust needs on top: macros, attributes, character
// literals, engine classes and the script classes of the project.
class RustSyntaxHighlighter : public EditorSyntaxHighlighter {
	GDCLASS(RustSyntaxHighlighter, EditorSyntaxHighlighter);

	Ref<CodeHighlighter> highlighter;

public:
	virtual void _update_cache() override;
	virtual Dictionary _get_line_syntax_highlighting_impl(int p_line) override { return highlighter->get_line_syntax_highlighting(p_line); }

	virtual String _get_name() const override { return "Rust"; }
	virtual PackedStringArray _get_supported_languages() const override;
	virtual Ref<EditorSyntaxHighlighter> _create() const override;

	RustSyntaxHighlighter();
};
