// Shared rendering helpers used across lvtextfm.cpp, lvrend.cpp and
// lvtinydom.cpp.
//
// Keep this header narrow: only expose helpers that need to be shared across
// those translation units. At the moment, that public surface is entirely
// about CSS initial-letter support.

#ifndef __RENDERUTIL_H_INCLUDED__
#define __RENDERUTIL_H_INCLUDED__

#include "lvtinydom.h"

struct InitialLetterInlineBoxMetrics
{
    // placement_baseline is the inline-box baseline to use on the first line.
    // exclusion_height is how far the sunk shape must exclude following lines,
    // and top_overflow is the real ink that sticks above the first line top.
    bool valid;
    int placement_baseline;
    int exclusion_height;
    int top_overflow;

    InitialLetterInlineBoxMetrics()
        : valid(false), placement_baseline(0), exclusion_height(0), top_overflow(0)
    {
    }
};

struct InitialLetterTextLayout
{
    // line_height/strut_baseline are for the pseudo-element final block and its
    // strut. first_letter_line_height is the line-height to use for the actual
    // AddSourceLine() run that draws the initial. tightened tells whether those
    // values come from the ink-tight band or from the regular fallback path.
    bool tightened;
    int line_height;
    int strut_baseline;
    int first_letter_line_height;
    LVFontRef font;

    InitialLetterTextLayout()
        : tightened(false), line_height(0), strut_baseline(0), first_letter_line_height(0), font()
    {
    }
};

// Returns true when this style represents an actually displayed applied
// initial-letter. We keep this predicate shared so style, layout and
// hit-testing all agree on when initial-letter logic should kick in.
bool hasAppliedInitialLetter(css_style_rec_t * style);

// Inline-box-backed initials are represented as an inline box whose first child
// is the ::first-letter pseudo-element. This helper validates that structure
// and returns the pseudo-element when present.
ldomNode * getInitialLetterInlineBoxPseudoElem(ldomNode * inline_box);

// Build the rendering bundle for an applied initial-letter pseudo-element.
// This gives renderFinalBlock() one always-usable place to fetch the enlarged
// font, the pseudo block strut/band metrics, and the line-height for the
// actual AddSourceLine() run, while hiding the fallback chain between the
// tightened ink-band path and the regular line-height path.
bool getInitialLetterTextLayout(ldomNode * enode, int default_line_height,
        InitialLetterTextLayout & layout);

// Compute the placement and later-line exclusion geometry for an inline-box
// that hosts an applied initial-letter. Layout uses this to keep the enlarged
// first letter aligned with the parent line grid while reserving enough depth
// for the actual glyph ink below the first line.
bool getInitialLetterInlineBoxMetrics(ldomNode * inline_box, int rendered_baseline,
        InitialLetterInlineBoxMetrics & metrics);

// Return the ink-tight rectangle for an inline-box-backed initial-letter.
// This keeps the horizontal footprint reserved by layout, but trims the
// vertical bounds to the actual glyph ink for hit-testing/highlight purposes.
bool getInitialLetterInlineBoxInkRect(ldomNode * inline_box, lvRect & rect);

#endif // __RENDERUTIL_H_INCLUDED__
