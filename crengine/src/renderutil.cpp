/*
 * Shared rendering helpers.
 *
 * This file currently centralizes the engine's CSS initial-letter support so
 * layout (lvtextfm.cpp), rendering (lvrend.cpp) and rect / hit-testing
 * (lvtinydom.cpp) keep using the exact same geometry rules.
 *
 * The important model is:
 *   1. We build a dedicated used font for the ::first-letter pseudo from the
 *      parent line metrics.
 *   2. We measure the real glyph ink of that enlarged initial.
 *   3. When possible, the pseudo's own inner final block is tightened to that
 *      ink band, then allowed to grow back toward the parent line grid when a
 *      purely ink-tight band would make ordinary char rects look too rugged.
 *   4. The outer inline-box still reserves the full sunk shape on the first
 *      line and for later-line exclusion.
 *
 * The trickiest part is baseline handling. Three different baselines matter:
 *   - the style-adjusted baseline: the font baseline corrected by the used
 *     line-height; this is the reference used when deriving glyph ink bounds
 *   - the tightened-band strut baseline: the baseline requested for the
 *     pseudo's tightened inner one-line box
 *   - the rendered baseline: the actual baseline returned by
 *     renderBlockElement() once that inner line has been formatted
 *
 * Those last two are often close, but they are not guaranteed to match. Low
 * glyphs showed that using the requested strut baseline for outer placement is
 * wrong: the inline-box must align against the rendered baseline, while the
 * tightened band still needs the strut baseline to stay inside the ink band.
 */

#include "../include/fb2def.h"
#include "../include/lvrend.h"
#include "../include/renderutil.h"

struct InitialLetterInkBounds
{
    int min_left;
    int min_top;
    int max_right;
    int max_bottom;
};

struct InitialLetterInnerTextMetrics
{
    bool valid;
    int line_height;
    int strut_baseline;
    LVFontRef font;

    InitialLetterInnerTextMetrics()
        : valid(false), line_height(0), strut_baseline(0), font()
    {
    }
};

static bool getInitialLetterInkBoundsPx(ldomNode * enode, css_style_rec_t * style, LVFontRef font,
        InitialLetterInkBounds & bounds, int computed_em);
static bool getInitialLetterNaturalBandPx(ldomNode * enode, css_style_rec_t * style, LVFontRef font,
        const InitialLetterInkBounds & ink_bounds, int & expand_top, int & natural_height);

// Shared predicate used throughout the engine to answer a very specific
// question: "does this computed style represent an actually applied
// initial-letter effect?".
//
// We intentionally exclude display:none here so callers do not have to keep
// repeating that guard before doing extra initial-letter work.
bool hasAppliedInitialLetter(css_style_rec_t * style) {
    return style && style->display != css_d_none
            && style->initial_letter.type == css_val_unspecified
            && style->initial_letter.value >= 0;
}

// Applied initial-letter is always carried by a ::first-letter pseudo-element.
// We use effective-node accessors so the same helper works both on the
// original pseudo-element and on ::first-line clones that proxy back to it.
static bool isAppliedInitialLetterPseudoElem(ldomNode * enode, css_style_rec_t * style) {
    return enode && enode->getEffectiveNodeId() == el_pseudoElem
            && enode->hasEffectiveAttribute(attr_FirstLetter)
            && hasAppliedInitialLetter(style);
}

// Initial-letter rides on top of the existing inline-box pipeline.
// This helper recognizes the wrapper shape produced by that pipeline:
// the inlineBox host, whose first child is the ::first-letter pseudo-element.
//
// Having a shared recognizer avoids three copies of subtle structural checks,
// especially once ::first-line clones are involved.
ldomNode * getInitialLetterInlineBoxPseudoElem(ldomNode * inline_box)
{
    if ( !inline_box || inline_box->getChildCount() <= 0 ) {
        return NULL;
    }
    ldomNode * node = inline_box->getChildNode(0);
    if ( !node || node->getEffectiveNodeId() != el_pseudoElem || !node->hasEffectiveAttribute(attr_FirstLetter) ) {
        return NULL;
    }
    css_style_ref_t style_ref = node->getStyle();
    if ( style_ref.isNull() || !hasAppliedInitialLetter(style_ref.get()) ) {
        return NULL;
    }
    return node;
}

// Return the used line-height that initial-letter sizing should be based on.
//
// The important subtlety is that we do *not* want ordinary document interline
// scaling to distort the pseudo-element's own line-height math. The raised/
// sunk initial is sized from the parent line box, but the pseudo-element's own
// used line-height should stay anchored to its font metrics and authored CSS.
//
// This is why we skip the document-scale adjustment for the initial-letter
// pseudo itself, while still keeping the usual behavior for ordinary styles.
// (This helper is used both for the pseudo-element itself and for ordinary
// parent paragraph line metrics.)
static int getInitialLetterStyleLineHeightPx(ldomDocument * doc, ldomNode * enode,
        css_style_rec_t * style, LVFontRef font, int computed_em)
{
    bool is_initial_letter = isAppliedInitialLetterPseudoElem(enode, style);
    int line_h;
    if ( style->line_height.type == css_val_unspecified &&
            style->line_height.value == css_generic_normal ) {
        line_h = font->getHeight();
    }
    else {
        int em = computed_em >= 0 ? computed_em : font->getSize();
        line_h = lengthToPx(enode, style->line_height, em, em, true);
    }
    if ( style->line_height.type != css_val_screen_px &&
            doc->getInterlineScaleFactor() != INTERLINE_SCALE_FACTOR_NO_SCALE &&
            !is_initial_letter ) {
        line_h = (line_h * doc->getInterlineScaleFactor()) >> INTERLINE_SCALE_FACTOR_SHIFT;
    }
    if ( is_initial_letter && line_h < font->getHeight() ) {
        line_h = font->getHeight();
    }
    return line_h;
}

// Initial-letter geometry is aligned against the line-box baseline, not the raw
// font baseline. With enlarged initials, using the font baseline directly would
// make the glyph ink sit too high or too low whenever the used line-height does
// not match the font height.
static int getInitialLetterStyleAdjustedBaselinePx(ldomDocument * doc, ldomNode * enode,
        css_style_rec_t * style, LVFontRef font, int computed_em)
{
    int line_height = getInitialLetterStyleLineHeightPx(doc, enode, style, font, computed_em);
    return font->getBaseline() + (line_height - font->getHeight()) / 2;
}

// Cap-height is the metric that most closely matches what CSS initial-letter
// sizing is trying to align visually: the height of uppercase glyph bodies,
// not total ascender height. Some font backends do not expose it, so we keep
// the historical 0.7em fallback to stay robust across fonts.
static int getInitialLetterFontCapHeightPx(LVFontRef font)
{
    int cap_height = font->getExtraMetric(font_metric_cap_height);
    if ( cap_height > 0 )
        return cap_height;
    return font->getHeight() * 7 / 10;
}

// Extract the exact text that ::first-letter owns and apply the same
// text-transform rendering will later apply.
//
// Geometry helpers must operate on the post-transform glyph sequence; otherwise
// a case conversion could change glyph metrics and we would compute exclusion
// depth or hit-testing bounds from the wrong characters.
static bool getInitialLetterText(ldomNode * enode, css_style_rec_t * style, lString32 & text)
{
    if ( !isAppliedInitialLetterPseudoElem(enode, style) ) {
        return false;
    }
    int first_letter_end = enode->getEffectiveAttributeValue(attr_FirstLetter).atoi();
    if ( first_letter_end <= 0 )
        return false;
    ldomNode * text_node = enode->getEffectiveFirstLetterTextNode();
    if ( !text_node )
        return false;
    text = text_node->getText();
    if ( text.length() < first_letter_end )
        return false;
    text = text.substr(0, first_letter_end);
    switch (style->text_transform) {
        case css_tt_uppercase: text.uppercase(); break;
        case css_tt_lowercase: text.lowercase(); break;
        case css_tt_capitalize: text.capitalize(); break;
        case css_tt_full_width: break;
        case css_tt_none: break;
        case css_tt_inherit: break;
    }
    return !text.empty();
}

// Given a target cap-height, iteratively find the font size that actually
// produces it with the current font lookup pipeline.
//
// We cannot just scale once proportionally because the final font metrics are
// the result of font selection, hinting and backend rounding. The loop keeps
// re-resolving the font and nudging the size toward the requested cap-height
// until the remaining difference is visually negligible.
static int getInitialLetterFontSizePx(ldomNode * enode, css_style_rec_t * style, int target_cap_height_px)
{
    int font_size_px = 0;
    LVFontRef computed_font = enode ? enode->getFont() : LVFontRef();
    if ( !computed_font.isNull() ) {
        font_size_px = computed_font->getSize();
    }
    if ( font_size_px <= 0 ) {
        font_size_px = lengthToPx(enode, style->font_size, 0, 0);
    }
    if ( font_size_px < 8 )
        font_size_px = 8;
    if ( target_cap_height_px < 8 )
        return 8;
    int doc_id = enode->getDocument()->getFontContextDocIndex();
    css_style_rec_t tmp = *style;
    // Iterate a few times to converge on the requested cap height while keeping the number of getFont()
    // lookups bounded. 6 rounds has proven plenty to settle (usually in 2 or 3) or get within the +/-1px stop.
    for ( int i=0; i<6; i++ ) {
        tmp.font_size.type = css_val_screen_px;
        tmp.font_size.value = font_size_px;
        LVFontRef font = getFont(enode, &tmp, doc_id);
        int font_cap_height = getInitialLetterFontCapHeightPx(font);
        if ( font_cap_height <= 0 )
            break;
        int diff = font_cap_height > target_cap_height_px ? font_cap_height - target_cap_height_px : target_cap_height_px - font_cap_height;
        if ( diff <= 1 )
            break;
        int next_font_size_px = font_size_px * target_cap_height_px / font_cap_height;
        if ( next_font_size_px == font_size_px ) {
            if ( font_cap_height < target_cap_height_px && font_size_px < 340 )
                next_font_size_px++;
            else if ( font_cap_height > target_cap_height_px && font_size_px > 8 )
                next_font_size_px--;
            else
                break;
        }
        if ( next_font_size_px < 8 )
            next_font_size_px = 8;
        else if ( next_font_size_px > 340 )
            next_font_size_px = 340;
        if ( next_font_size_px == font_size_px )
            break;
        font_size_px = next_font_size_px;
    }
    return font_size_px;
}

// Build the used rendering font for the initial-letter pseudo-element.
//
// CSS gives us the initial-letter "size" in lines, not a literal font-size.
// To turn that into an actual font, we:
//   1. look at the parent line-height and parent cap-height
//   2. derive the target cap-height for the initial based on the CSS value
//   3. iteratively search the font-size that reaches that target
//   4. resolve the final LVFont through the normal engine font lookup
//
// Callers may pass the parent style/font when they already have them, which
// keeps layout/render hot paths from doing redundant lookups.
static LVFontRef getUsedInitialLetterFont(ldomNode * enode, css_style_rec_t * style,
        css_style_rec_t * parent_style, LVFontRef parent_font)
{
    if ( !isAppliedInitialLetterPseudoElem(enode, style) ) {
        return LVFontRef();
    }
    if ( parent_style == NULL || parent_font.isNull() ) {
        ldomNode * parent = enode->getParentNode();
        if ( !parent || parent->isNull() )
            return LVFontRef();
        if ( parent_style == NULL ) {
            css_style_ref_t parent_style_ref = parent->getStyle();
            if ( parent_style_ref.isNull() )
                return LVFontRef();
            parent_style = parent_style_ref.get();
        }
        if ( parent_font.isNull() ) {
            parent_font = parent->getFont();
            if ( parent_font.isNull() )
                return LVFontRef();
        }
    }
    int parent_line_height = getInitialLetterStyleLineHeightPx(enode->getDocument(), enode, parent_style, parent_font, -1);
    if ( parent_line_height <= 0 )
        return LVFontRef();
    int initial_letter_size = style->initial_letter.value >> 8;
    int parent_cap_height = getInitialLetterFontCapHeightPx(parent_font);
    int target_height = parent_cap_height + ((initial_letter_size - 256) * parent_line_height) / 256;
    if ( target_height < parent_cap_height )
        target_height = parent_cap_height;
    css_style_rec_t tmp = *style;
    tmp.font_size.type = css_val_screen_px;
    tmp.font_size.value = getInitialLetterFontSizePx(enode, style, target_height);
    int doc_id = enode->getDocument()->getFontContextDocIndex();
    return getFont(enode, &tmp, doc_id);
}

// The inline-box outer layout still treats the initial as one object, but the
// pseudo-element's own inner final block can use a tighter one-line box so
// createXPointer()/getRect() can rely on ordinary per-char text geometry.
static bool getInitialLetterInnerTextMetrics(ldomNode * enode, InitialLetterInnerTextMetrics & metrics)
{
    css_style_ref_t style_ref = enode ? enode->getStyle() : css_style_ref_t();
    LVFontRef computed_font = enode ? enode->getFont() : LVFontRef();
    if ( enode == NULL || style_ref.isNull() || computed_font.isNull() ) {
        return false;
    }
    css_style_rec_t * style = style_ref.get();
    if ( !isAppliedInitialLetterPseudoElem(enode, style) ) {
        return false;
    }
    LVFontRef font = getUsedInitialLetterFont(enode, style, NULL, LVFontRef());
    if ( font.isNull() ) {
        return false;
    }
    InitialLetterInkBounds ink_bounds;
    if ( !getInitialLetterInkBoundsPx(enode, style, font, ink_bounds, computed_font->getSize()) ) {
        return false;
    }
    int line_height = ink_bounds.max_bottom - ink_bounds.min_top;
    if ( line_height <= 0 ) {
        return false;
    }
    int adjusted_baseline = getInitialLetterStyleAdjustedBaselinePx(enode->getDocument(), enode, style, font, computed_font->getSize());
    int strut_baseline = adjusted_baseline - ink_bounds.min_top;
    // The tightened band must still contain the baseline used to render the
    // pseudo text. If it would end up fully below that baseline, the glyph may
    // disappear entirely; if it ends exactly on the baseline, some low glyphs
    // (like a sunk lowercase 'o') can also get clipped away. Keep the old
    // metrics in the first case, and just extend the tightened band by 1px in
    // the second.
    if ( strut_baseline <= 0 ) {
        return false;
    }
    if ( strut_baseline >= line_height ) {
        line_height = strut_baseline + 1;
    }
    int expand_top = 0;
    int natural_height = 0;
    if ( getInitialLetterNaturalBandPx(enode, style, font, ink_bounds, expand_top, natural_height) ) {
        if ( expand_top > 0 ) {
            strut_baseline += expand_top;
            line_height += expand_top;
        }
        if ( natural_height > line_height ) {
            line_height = natural_height;
        }
    }
    if ( strut_baseline >= line_height ) {
        line_height = strut_baseline + 1;
    }
    metrics.line_height = line_height;
    metrics.strut_baseline = strut_baseline;
    metrics.font = font;
    metrics.valid = true;
    return true;
}

// Rendering needs one coherent bundle for the pseudo final block and for the
// actual AddSourceLine() text run. The tightened ink-band metrics are optional,
// but callers should not have to open-code the fallback chain when they are not
// available.
bool getInitialLetterTextLayout(ldomNode * enode, int default_line_height, InitialLetterTextLayout & layout)
{
    css_style_ref_t style_ref = enode ? enode->getStyle() : css_style_ref_t();
    LVFontRef computed_font = enode ? enode->getFont() : LVFontRef();
    if ( enode == NULL || style_ref.isNull() || computed_font.isNull() ) {
        return false;
    }
    css_style_rec_t * style = style_ref.get();
    if ( !isAppliedInitialLetterPseudoElem(enode, style) ) {
        return false;
    }

    layout = InitialLetterTextLayout();
    layout.font = computed_font;
    layout.line_height = default_line_height;
    layout.first_letter_line_height = default_line_height;
    int fh = computed_font->getHeight();
    int fb = computed_font->getBaseline();
    int f_half_leading = (default_line_height - fh) / 2;
    layout.strut_baseline = fb + f_half_leading;

    LVFontRef used_font = getUsedInitialLetterFont(enode, style, NULL, LVFontRef());
    if ( !used_font.isNull() ) {
        layout.font = used_font;
        int used_line_height = getInitialLetterStyleLineHeightPx(enode->getDocument(),
                enode, style, used_font, computed_font->getSize());
        if ( used_line_height > 0 ) {
            layout.first_letter_line_height = used_line_height;
        }
    }

    InitialLetterInnerTextMetrics inner_metrics;
    if ( getInitialLetterInnerTextMetrics(enode, inner_metrics) ) {
        layout.tightened = true;
        layout.font = inner_metrics.font;
        layout.line_height = inner_metrics.line_height;
        layout.strut_baseline = inner_metrics.strut_baseline;
        layout.first_letter_line_height = inner_metrics.line_height;
    }
    return true;
}

// Measure the union of the actual glyph ink boxes for the rendered initial.
//
// We intentionally compute *ink* bounds rather than advance-width bounds:
// initial-letter exclusion, drawing overflow and selection rectangles should
// follow what is visibly painted, not the logical pen position alone.
//
// The helper first measures cumulative advances for the transformed text, then
// asks the font backend for each glyph's origin and black-box dimensions, and
// finally unions those rectangles in line-box coordinates.
static bool getInitialLetterInkBoundsPx(ldomNode * enode, css_style_rec_t * style, LVFontRef font,
        InitialLetterInkBounds & bounds, int computed_em)
{
    lString32 text;
    if ( !getInitialLetterText(enode, style, text) )
        return false;
    int adjusted_baseline = getInitialLetterStyleAdjustedBaselinePx(enode->getDocument(), enode, style, font, computed_em);
    int letter_spacing = lengthToPx(enode, style->letter_spacing, computed_em);
    lUInt16 widths[64];
    lUInt8 flags[64];
    int len = text.length();
    if ( len > 64 ) {
        len = 64;
    }
    // (We measure ink per-codepoint, so this may differ from what will be shaped by Harfbuzz and drawn.
    // This is probably acceptable as initial-letter is typically one grapheme or a few simple chars.)
    font->measureText(text.c_str(), len, widths, flags, 0x7FFF, '?', NULL, letter_spacing, false, 0);
    bounds.min_left = 0;
    bounds.min_top = 0;
    bounds.max_right = 0;
    bounds.max_bottom = 0;
    bool found = false;
    for ( int i=0; i<len; i++ ) {
        LVFont::glyph_info_t glyph;
        if ( font->getGlyphInfo(text[i], &glyph, '?' ) ) {
            int pen_x = i > 0 ? widths[i - 1] : 0;
            int left = pen_x + glyph.originX;
            int top = adjusted_baseline - glyph.originY;
            int right = left + glyph.blackBoxX;
            int bottom = top + glyph.blackBoxY;
            if ( !found || left < bounds.min_left )
                bounds.min_left = left;
            if ( !found || top < bounds.min_top )
                bounds.min_top = top;
            if ( right > bounds.max_right )
                bounds.max_right = right;
            if ( bottom > bounds.max_bottom )
                bounds.max_bottom = bottom;
            found = true;
        }
    }
    return found;
}

// Expand the pseudo's own one-line band back toward the parent line grid when
// the visually tightened ink band would otherwise be noticeably smaller than
// the surrounding natural lines.
static bool getInitialLetterNaturalBandPx(ldomNode * enode, css_style_rec_t * style, LVFontRef font,
        const InitialLetterInkBounds & ink_bounds, int & expand_top, int & natural_height)
{
    expand_top = 0;
    natural_height = 0;
    ldomNode * parent = enode ? enode->getParentNode() : NULL;
    if ( !parent || parent->isNull() || !style || font.isNull() ) {
        return false;
    }
    css_style_ref_t parent_style_ref = parent->getStyle();
    LVFontRef parent_font = parent->getFont();
    if ( parent_style_ref.isNull() || parent_font.isNull() ) {
        return false;
    }
    css_style_rec_t * parent_style = parent_style_ref.get();
    int parent_line_height = getInitialLetterStyleLineHeightPx(parent->getDocument(), parent,
            parent_style, parent_font, -1);
    if ( parent_line_height <= 0 ) {
        return false;
    }
    int sink = style->initial_letter.value & 0xFF;
    if ( sink <= 0 ) {
        sink = 1;
    }
    int parent_adjusted_baseline = getInitialLetterStyleAdjustedBaselinePx(parent->getDocument(), parent,
            parent_style, parent_font, -1);
    int initial_baseline = font->getBaseline();
    int initial_letter_size = style->initial_letter.value >> 8;
    int top_offset;
    if ( initial_letter_size >= sink * 256 ) {
        top_offset = parent_adjusted_baseline + (sink - 1) * parent_line_height - initial_baseline;
    }
    else {
        int parent_cap_height = getInitialLetterFontCapHeightPx(parent_font);
        int initial_cap_height = getInitialLetterFontCapHeightPx(font);
        top_offset = parent_adjusted_baseline - parent_cap_height
                    - (initial_baseline - initial_cap_height);
    }
    int actual_top = top_offset + ink_bounds.min_top;
    if ( actual_top > 0 ) {
        expand_top = actual_top;
        natural_height = sink * parent_line_height;
    }
    else {
        // Raised initials can already extend above the parent line grid. If we
        // keep that higher top, the natural bottom has to be measured from that
        // real top, not from the grid top, otherwise the last sunk line stays
        // a bit too short.
        natural_height = sink * parent_line_height - actual_top;
    }
    return true;
}

// Small convenience wrapper for callers that only care about the deepest ink
// extent. We keep it as a separate helper because some layout paths only need
// the lower bound to extend exclusion or overflow, and recomputing the full
// rectangle at each call site would just duplicate boilerplate.
static int getInitialLetterInkBottomPx(ldomNode * enode, css_style_rec_t * style, LVFontRef font, int computed_em)
{
    lString32 text;
    if ( !getInitialLetterText(enode, style, text) )
        return -1;
    int adjusted_baseline = getInitialLetterStyleAdjustedBaselinePx(enode->getDocument(), enode, style, font, computed_em);
    int max_bottom = 0;
    bool found = false;
    for ( int i=0; i<text.length(); i++ ) {
        LVFont::glyph_info_t glyph;
        if ( font->getGlyphInfo(text[i], &glyph, '?' ) ) {
            int bottom = adjusted_baseline - glyph.originY + glyph.blackBoxY;
            if ( bottom > max_bottom )
                max_bottom = bottom;
            found = true;
        }
    }
    return found ? max_bottom : -1;
}

// Inline-box-backed initial-letter layout needs one more derived layer on top
// of the raw font/ink helpers above: where should the box baseline sit on the
// first line, and how much depth should later lines exclude below it?
//
// This helper converts the CSS size/sink pair into concrete formatter values.
// It uses the parent paragraph metrics as the reference grid, then adjusts that
// grid with the actual used font and actual ink extents of the enlarged glyph.
bool getInitialLetterInlineBoxMetrics(ldomNode * inline_box, int rendered_baseline,
        InitialLetterInlineBoxMetrics & metrics)
{
    ldomNode * pseudo = getInitialLetterInlineBoxPseudoElem(inline_box);
    if ( !pseudo ) {
        return false;
    }
    ldomNode * parent = inline_box->getParentNode();
    if ( !parent || parent->isNull() ) {
        return false;
    }
    css_style_ref_t style_ref = pseudo->getStyle();
    css_style_ref_t parent_style_ref = parent->getStyle();
    LVFontRef computed_font = pseudo->getFont();
    LVFontRef parent_font = parent->getFont();
    if ( style_ref.isNull() || parent_style_ref.isNull() || computed_font.isNull() || parent_font.isNull() ) {
        return false;
    }
    css_style_rec_t * style = style_ref.get();
    css_style_rec_t * parent_style = parent_style_ref.get();
    LVFontRef font = getUsedInitialLetterFont(pseudo, style, parent_style, parent_font);
    if ( font.isNull() ) {
        font = computed_font;
    }
    int computed_em = computed_font->getSize();
    int parent_line_height = getInitialLetterStyleLineHeightPx(parent->getDocument(), parent, parent_style, parent_font, -1);
    if ( parent_line_height <= 0 ) {
        return false;
    }
    int sink = style->initial_letter.value & 0xFF;
    if ( sink <= 0 ) {
        sink = 1;
    }
    int parent_adjusted_baseline = getInitialLetterStyleAdjustedBaselinePx(parent->getDocument(), parent, parent_style, parent_font, -1);
    int initial_baseline = font->getBaseline();
    int initial_letter_size = style->initial_letter.value >> 8;
    int top_offset;
    if ( initial_letter_size >= sink * 256 ) {
        top_offset = parent_adjusted_baseline + (sink - 1) * parent_line_height - initial_baseline;
    }
    else {
        int parent_cap_height = getInitialLetterFontCapHeightPx(parent_font);
        int initial_cap_height = getInitialLetterFontCapHeightPx(font);
        top_offset = parent_adjusted_baseline - parent_cap_height
                    - (initial_baseline - initial_cap_height);
    }
    metrics.exclusion_height = sink * parent_line_height;
    InitialLetterInkBounds ink_bounds;
    bool have_ink_bounds = getInitialLetterInkBoundsPx(pseudo, style, font, ink_bounds, computed_em);
    if ( have_ink_bounds ) {
        InitialLetterInnerTextMetrics inner_metrics;
        if ( getInitialLetterInnerTextMetrics(pseudo, inner_metrics) ) {
            int adjusted_baseline = getInitialLetterStyleAdjustedBaselinePx(pseudo->getDocument(), pseudo, style, font, computed_em);
            // renderBlockElement() returns the actual baseline used by the
            // pseudo final block after it has formatted its single tightened
            // line. The outer inline-box must align against that rendered
            // baseline, not directly against the tightened band baseline: low
            // glyphs showed that the formatter can keep the rendered baseline a
            // little below the requested strut baseline when the font metrics are
            // taller than the tightened band.
            metrics.placement_baseline = parent_adjusted_baseline - top_offset
                    + rendered_baseline - adjusted_baseline;
        }
        else {
            // The inline-box keeps its own baseline, but must visually sit on the
            // parent paragraph grid defined by initial-letter size and sink.
            metrics.placement_baseline = rendered_baseline + parent_adjusted_baseline - top_offset - initial_baseline;
        }
        int actual_top = top_offset + ink_bounds.min_top;
        if ( actual_top < 0 ) {
            metrics.top_overflow = -actual_top;
        }
    }
    else {
        metrics.placement_baseline = rendered_baseline + parent_adjusted_baseline - top_offset - initial_baseline;
    }
    int visual_bottom = getInitialLetterInkBottomPx(pseudo, style, font, computed_em);
    if ( visual_bottom <= 0 ) {
        visual_bottom = getInitialLetterStyleLineHeightPx(pseudo->getDocument(), pseudo, style, font, computed_em);
    }
    int actual_bottom = top_offset + visual_bottom;
    if ( actual_bottom > metrics.exclusion_height ) {
        metrics.exclusion_height = actual_bottom;
    }
    metrics.valid = true;
    return true;
}

// Hit-testing wants the visual footprint of the enlarged initial-letter, not
// the full inline-box line-height. Keep the horizontal footprint reserved by
// layout, but vertically stay on the actual painted ink rather than the
// pseudo's smoothed internal line box.
bool getInitialLetterInlineBoxInkRect(ldomNode * inline_box, lvRect & rect)
{
    ldomNode * pseudo = getInitialLetterInlineBoxPseudoElem(inline_box);
    if ( !pseudo ) {
        return false;
    }
    ldomNode * parent = inline_box->getParentNode();
    if ( !parent || parent->isNull() ) {
        return false;
    }
    css_style_ref_t style_ref = pseudo->getStyle();
    css_style_ref_t parent_style_ref = parent->getStyle();
    LVFontRef computed_font = pseudo->getFont();
    LVFontRef parent_font = parent->getFont();
    if ( style_ref.isNull() || parent_style_ref.isNull() || computed_font.isNull() || parent_font.isNull() ) {
        return false;
    }
    LVFontRef font = getUsedInitialLetterFont(pseudo, style_ref.get(), parent_style_ref.get(), parent_font);
    if ( font.isNull() ) {
        font = computed_font;
    }
    int computed_em = computed_font->getSize();
    InitialLetterInkBounds ink_bounds;
    if ( !getInitialLetterInkBoundsPx(pseudo, style_ref.get(), font, ink_bounds, computed_em) ) {
        return false;
    }
    lvRect box_rect;
    inline_box->getAbsRect(box_rect);
    // Keep at least the inline-box footprint reserved by layout, but extend it
    // when actual ink hangs outside it.
    rect.left = box_rect.left;
    int ink_left = box_rect.left + ink_bounds.min_left;
    if ( ink_left < rect.left ) {
        rect.left = ink_left;
    }
    rect.right = box_rect.right;
    int ink_right = box_rect.left + ink_bounds.max_right;
    if ( ink_right > rect.right ) {
        rect.right = ink_right;
    }
    int expand_top = 0;
    int natural_height = 0;
    if ( getInitialLetterNaturalBandPx(pseudo, style_ref.get(), font, ink_bounds, expand_top, natural_height) ) {
        rect.top = box_rect.top + expand_top;
        rect.bottom = rect.top + (ink_bounds.max_bottom - ink_bounds.min_top);
    }
    else {
        rect.top = box_rect.top + ink_bounds.min_top;
        rect.bottom = box_rect.top + ink_bounds.max_bottom;
    }
    if ( rect.height() == 0 && rect.width() > 0 ) {
        rect.bottom++;
    }
    return true;
}
