/** \file cssdef.h
    \brief Cascading Style Sheet definitions

    Defines enums and structures for subset of CSS2 properties.

    CoolReader Engine

    (c) Vadim Lopatin, 2000-2006

    This source code is distributed under the terms of
    GNU General Public License.

    See LICENSE file for details.
*/

#if !defined(__CSS_DEF_H_INCLUDED__)
#define __CSS_DEF_H_INCLUDED__

#include "lvtypes.h"
#include "lvref.h"
#include "lvstring.h"

// The order of items in following enums should match the order in the tables in src/lvstsheet.cpp
/// display property values

// Especially don't change the order of these ones, as we use "style->display > css_d_something" like tests
enum css_display_t {
    css_d_inherit, // Inheritance not implemented: should not be seen, unless specified in CSS, which then behave as inline
    css_d_ruby,    // Behave as inline, but inner content might be wrapped in a table like structure
    css_d_run_in,  // Rarely used display type (but used as a solution to inline FB2 footnotes)
    css_d_inline,  // All elements starts being css_d_inline, unless otherwise specified in fb2def.h
    // Above are those that define a mostly inline container, below those that define a mostly block container
    css_d_block,
    css_d_list_item_legacy, // display: -cr-list-item-final (was used before 20180524 for display: list-item)
    css_d_list_item_block,  // display: list-item
    css_d_inline_block,
    css_d_inline_table, // (needs to be before css_d_table, as we use tests like if: (style->display > css_d_table))
    css_d_table, 
    css_d_table_row_group, 
    css_d_table_header_group, 
    css_d_table_footer_group, 
    css_d_table_row, 
    css_d_table_column_group, 
    css_d_table_column, 
    css_d_table_cell, 
    css_d_table_caption, 
    css_d_none
};

// https://www.w3.org/TR/CSS2/text.html#white-space-prop
// https://florian.rivoal.net/talks/line-breaking/
// https://developer.mozilla.org/en-US/docs/Web/CSS/white-space
//      Behaviors: New lines   Spaces/tabs End-of-line spaces Text wrap
// normal          Collapse    Collapse    Remove             Wrap
// nowrap          Collapse    Collapse    Remove             No wrap
// pre-line        Preserve    Collapse    Remove             Wrap
// pre             Preserve    Preserve    Preserve           No wrap
// pre-wrap        Preserve    Preserve    Hang               Wrap
// break-spaces    Preserve    Preserve    Wrap               Wrap
//
// crengine ensures the 3 first behaviors at XML parsing time, initially only for:
//   'normal' : replace new lines and tabs by spaces, replace consecutive spaces
//              by only one, remove spaces at start and end if "display: block"
//   'pre' : preserve spaces and newlines, expands tabs to 8-spaces tabstops
// A change of the white-space value for a single node will make the DOM stalled,
// and a full reload should be done to get the correct result.
//
// The last behavior (text wrap) happens at text rendering time, and
// we always wrap to fit text into the container or screen width.
//
// We can approximate support for the other values:
// 'nowrap' is mostly like 'normal', but need some additional care:
//   - in lvtextfm, to prevent wrap where it would be allowed, but
//     still accounting for normal wrap points to be used if no other
//     non-nowrap text node on the line provides a wrap opportunity.
//   - in getRenderedWidths(), where it can impact the widths of
//     table cells and floats
// 'pre-line' might be parsed just like 'pre', but rendered just
//     like normal (lvtextfm will collapse spaces and wrap on \n)
// 'pre-wrap' is just like 'pre', as we would always wrap to fit
//     in the container/screen width
// 'break-spaces' is very similar to 'pre-wrap', except that spaces
//     should not be dropped on wrap. We don't ensure that.
//
/// white-space property values: keep them ordered this way for easier checks
enum css_white_space_t {
    css_ws_inherit,
    css_ws_normal,
    css_ws_nowrap,
        /* parse XML as 'normal' before this, as 'pre' after this */
    css_ws_pre_line,
        /* render text as 'normal' before this, as 'pre' after this */
    css_ws_pre,
    css_ws_pre_wrap,
    css_ws_break_spaces
};

/// text-align property values
enum css_text_align_t {
    css_ta_inherit,
    css_ta_left,
    css_ta_right,
    css_ta_center,
    css_ta_justify,
    css_ta_start, // = left if LTR, right if RTL
    css_ta_end,   // = right if LTR, left if LTR
    // Non-standard keywords (similar to -moz-center, -webkit-right...), used to implement <div align=...>
    // (this feels like it's not the right way to implement how "aligning descendants" as described in
    // the HTML specs, which sounds orthogonal to text-align, but both Firefox and Chromium implement
    // it this way, with unnatural (but probably expected, and worked around by authors) side effects;
    // we need to implement it the same way to be able to compare our rendering to something...)
    css_ta_html_align_left,   // <div align=left>
    css_ta_html_align_right,  // <div align=right>
    css_ta_html_align_center, // <div align=center>, <center>
    // Next ones are only accepted with text-align-last
    css_ta_auto,
    css_ta_left_if_not_first,    // These non standard keywords allow text-align-last
    css_ta_right_if_not_first,   // to not apply to a single line. The previous normal
    css_ta_center_if_not_first,  // keywords apply to a single line (which is alone,
    css_ta_justify_if_not_first, // so the last) according to the specs.
    css_ta_start_if_not_first,
    css_ta_end_if_not_first
};

/// vertical-align property values
enum css_vertical_align_t {
    css_va_inherit,
    css_va_baseline, 
    css_va_sub,
    css_va_super,
    css_va_top,
    css_va_text_top,
    css_va_middle,
    css_va_bottom,
    css_va_text_bottom
};

/// text-decoration property values
enum css_text_decoration_t {
    // TODO: support multiple flags
    css_td_inherit = 0,
    css_td_none = 1,
    css_td_underline = 2,
    css_td_overline = 3,
    css_td_line_through = 4,
    css_td_blink = 5
};

/// text-transform property values
enum css_text_transform_t {
    css_tt_inherit = 0,
    css_tt_none = 1,
    css_tt_uppercase = 2,
    css_tt_lowercase = 3,
    css_tt_capitalize = 4,
    css_tt_full_width = 5
};

/// hyphenate property values
enum css_hyphenate_t {
    css_hyph_inherit = 0,
    css_hyph_none = 1,
    css_hyph_auto = 2
};

/// font-style property values
enum css_font_style_t {
    css_fs_inherit,
    css_fs_normal,
    css_fs_italic,
    css_fs_oblique
};

/// font-weight property values
enum css_font_weight_t {
    css_fw_inherit,
    css_fw_normal,
    css_fw_bold,
    css_fw_bolder,
    css_fw_lighter,
    css_fw_100,
    css_fw_200,
    css_fw_300,
    css_fw_400,
    css_fw_500,
    css_fw_600,
    css_fw_700,
    css_fw_800,
    css_fw_900
};

/// font-family property values
enum css_font_family_t {
    css_ff_inherit,
    css_ff_serif,
    css_ff_sans_serif,
    css_ff_cursive,
    css_ff_fantasy,
    css_ff_monospace,
    css_ff_math,
    css_ff_emoji,
    css_ff_fangsong
};

/// page split property values
enum css_page_break_t {
    css_pb_inherit,
    css_pb_auto,
    css_pb_avoid, // those after this one are not supported by page-break-inside
    css_pb_always,
    css_pb_left,
    css_pb_right,
    css_pb_page,
    css_pb_recto,
    css_pb_verso
};

/// list-style-type property values
enum css_list_style_type_t {
    css_lst_inherit,
    css_lst_disc,
    css_lst_circle,
    css_lst_square,
    css_lst_decimal,
    css_lst_lower_roman,
    css_lst_upper_roman,
    css_lst_lower_alpha,
    css_lst_upper_alpha,
    css_lst_none
};

/// list-style-position property values
enum css_list_style_position_t {
    css_lsp_inherit,
    css_lsp_inside,
    css_lsp_outside,
    css_lsp_cr_outside // private value "list-style-position: -cr-outside", legacy rendering
                       // of outside, with marker left-aligned instead of right near the content
};

/// css length value types, see:
//  https://developer.mozilla.org/en-US/docs/Web/CSS/length
//  https://www.w3.org/Style/Examples/007/units.en.html
enum css_value_type_t {
    css_val_inherited,
    css_val_unspecified,
    css_val_px,   // css px (1 css px = 1 screen px at 96 DPI)
    css_val_in,   // 2.54 cm   1in = 96 css px
    css_val_cm,   //        2.54cm = 96 css px
    css_val_mm,   //        25.4mm = 96 css px
    css_val_pt,   // 1/72 in  72pt = 96 css px
    css_val_pc,   // 12 pt     6pc = 96 css px
    css_val_em,   // relative to font size of the current element
    css_val_ex,   // 1ex =~ 0.5em in many fonts (https://developer.mozilla.org/en-US/docs/Web/CSS/length)
    css_val_ch,   // 1ch assumed to be 0.5em for widths when impractical to determine (same url)
    css_val_rem,  // 'root em', relative to font-size of the root element (typically <html>)
    css_val_vw,   // 1 percent of the screen width
    css_val_vh,   // 1 percent of the screen height
    css_val_vmin, // maximum of 1 percent of the screen height or width
    css_val_vmax, // minimum of 1 percent of the screen height or width
    css_val_percent,
    css_val_screen_px, // screen px, for already scaled values
    css_val_color
};

/// css border style values
enum css_border_style_type_t {
    css_border_inherit,
    css_border_none,
    css_border_hidden,
    css_border_solid,
    css_border_dotted,
    css_border_dashed,
    css_border_double,
    css_border_groove,
    css_border_ridge,
    css_border_inset,
    css_border_outset
};
/// css background property values
enum css_background_repeat_value_t {
    css_background_repeat,
    css_background_repeat_x,
    css_background_repeat_y,
    css_background_no_repeat,
    css_background_r_inherit
};
enum css_background_position_value_t {
    css_background_left_top = 0,
    css_background_left_center,
    css_background_left_bottom,
    css_background_right_top,
    css_background_right_center,
    css_background_right_bottom,
    css_background_center_top,
    css_background_center_center,
    css_background_center_bottom = 8,
    css_background_p_inherit = 9
};

enum css_border_collapse_value_t {
    css_border_c_inherit,
    css_border_c_separate,
    css_border_c_collapse
};

enum css_orphans_widows_value_t { // supported only if in range 1-9
    css_orphans_widows_inherit,
    css_orphans_widows_1,
    css_orphans_widows_2,
    css_orphans_widows_3,
    css_orphans_widows_4,
    css_orphans_widows_5,
    css_orphans_widows_6,
    css_orphans_widows_7,
    css_orphans_widows_8,
    css_orphans_widows_9
};

/// float property values
enum css_float_t {
    css_f_inherit,
    css_f_none,
    css_f_left,
    css_f_right
};

/// clear property values
enum css_clear_t {
    css_c_inherit,
    css_c_none,
    css_c_left,
    css_c_right,
    css_c_both
};

/// direction property values
enum css_direction_t {
    css_dir_inherit,
    css_dir_unset,
    css_dir_ltr,
    css_dir_rtl
};

/// visibility property values
enum css_visibility_t {
    css_v_inherit,
    css_v_visible,
    css_v_hidden,
    css_v_collapse // handled as "hidden" (but it should behave differently on tables, which we don't ensure)
};

/// line-break property values
enum css_line_break_t {
    css_lb_inherit,
    css_lb_auto,
    css_lb_normal,
    css_lb_loose,
    css_lb_strict,
    css_lb_anywhere,
    css_lb_cr_loose // private value "line-break: -cr-loose" to ignore &nbsp;
};

/// word-break property values
enum css_word_break_t {
    css_wb_inherit,
    css_wb_normal,
    css_wb_break_word, // legacy, handled as "normal" (and "overflow-wrap: anywhere", which is our default behaviour)
    css_wb_break_all,
    css_wb_keep_all
};

/// box-sizing property values
enum css_box_sizing_t {
    css_bs_inherit,
    css_bs_content_box,
    css_bs_border_box
};

/// caption-side property values
enum css_caption_side_t {
    css_cs_inherit,
    css_cs_top,
    css_cs_bottom
};

enum css_generic_value_t {
    css_generic_auto = -1,         // (css_val_unspecified, css_generic_auto), for "margin: auto"
    css_generic_normal = -2,       // (css_val_unspecified, css_generic_normal), for "line-height: normal"
    // css_generic_transparent = -3, // replaced by (css_val_color, CSS_COLOR_TRANSPARENT)
    css_generic_currentcolor = -4, // (css_val_unspecified, css_generic_currentcolor), for "color: currentcolor"
    css_generic_contain = -5,      // (css_val_unspecified, css_generic_contain), for "background-size: contain"
    css_generic_cover = -6,        // (css_val_unspecified, css_generic_cover), for "background-size: cover"
    css_generic_none = -7,         // (css_val_unspecified, css_generic_none), for "max-width: none"
    css_generic_cr_special = -8,   // (css_val_unspecified, css_generic_cr_special), for "padding-left: -cr-special"
                                             //  (could be used with other properties for other special behaviours)
};

// color 'transparent' is transparent black rgba(0,0,0,0) per specs
#define CSS_COLOR_TRANSPARENT     0xFF000000
#define IS_COLOR_FULLY_TRANSPARENT(color_value)  ( (bool)((color_value & 0xFF000000) == 0xFF000000) )
#define IS_COLOR_FULLY_OPAQUE(color_value)       ( (bool)((color_value & 0xFF000000) == 0x00000000) )

// -cr-hint is a non standard property for providing hints to crengine via style tweaks
// Handled as a bitmap, with a flag for each hint, as we might set multiple on a same node (max 31 bits)
#define CSS_CR_HINT_NONE                    0x00000000 // default value

// Reset any hint previously set and don't inherit any from parent
#define CSS_CR_HINT_NONE_NO_INHERIT         0x00000001 // -cr-hint: none

// Text and images should not overflow/modify their paragraph strut baseline and height
// (it could have been a non-standard named value for line-height:, but we want to be
// able to not override existing line-height: values)
#define CSS_CR_HINT_STRUT_CONFINED          0x00000002 // -cr-hint: strut-confined (inheritable)
// Glyphs should not overflow line edges, and across text nodes or over images
// (it can also be used to disable hanging punctuation on the targeted nodes).
#define CSS_CR_HINT_FIT_GLYPHS              0x00000004 // -cr-hint: fit-glyphs (inheritable)
// Paragraphs ("final" blocks') width and text-indent is to be ajusted to an
// integer multiple of their font size (so text justification of lines made of
// only squared CJK glyphs get less chance to need to spread the glyphs).
#define CSS_CR_HINT_CJK_TAILORED            0x00000008 // -cr-hint: cjk-tailored (inheritable)

// Skip/ignore CSS selectors flagged as "presentational hints"
// Will be forced on all styles when embedded styles are disabled.
// Can also be set explicitely on elements, but it Will reduce the specificity of selectors
// containing it in their declaration so they get applied at the earliest.
#define CSS_CR_HINT_NO_PRESENTATIONAL_CSS   0x00000010 // -cr-hint: no-presentational

// A node with these should be considered as TOC item of level N when building alternate TOC
#define CSS_CR_HINT_TOC_LEVEL1              0x00000100 // -cr-hint: toc-level1
#define CSS_CR_HINT_TOC_LEVEL2              0x00000200 // -cr-hint: toc-level2
#define CSS_CR_HINT_TOC_LEVEL3              0x00000400 // -cr-hint: toc-level3
#define CSS_CR_HINT_TOC_LEVEL4              0x00000800 // -cr-hint: toc-level4
#define CSS_CR_HINT_TOC_LEVEL5              0x00001000 // -cr-hint: toc-level5
#define CSS_CR_HINT_TOC_LEVEL6              0x00002000 // -cr-hint: toc-level6
#define CSS_CR_HINT_TOC_LEVELS_MASK         0x00003F00
// Ignore H1...H6 that have this when building alternate TOC
#define CSS_CR_HINT_TOC_IGNORE              0x00004000 // -cr-hint: toc-ignore

// Tweak text selection behaviour when traversing a node with these hints
#define CSS_CR_HINT_TEXT_SELECTION_INLINE   0x00010000 // -cr-hint: text-selection-inline  don't add a '\n' before inner text
                                                       //                                  (even if the node happens to be block)
#define CSS_CR_HINT_TEXT_SELECTION_BLOCK    0x00020000 // -cr-hint: text-selection-block   add a '\n' before inner text (even
                                                       //                                  if the node happens to be inline)
#define CSS_CR_HINT_TEXT_SELECTION_SKIP     0x00040000 // -cr-hint: text-selection-skip    don't include inner text in selection

// To be set on a block element: this block makes a non-linear flow that can be hidden from normal flow by frontends.
// Consecutive siblings all hinted with 'non-linear-combining' combine all into a single flow.
#define CSS_CR_HINT_NON_LINEAR              0x00100000 // -cr-hint: non-linear
#define CSS_CR_HINT_NON_LINEAR_COMBINING    0x00300000 // -cr-hint: non-linear-combining  (2 bits flag, includes previous one)

// To be set on a block element: it is a footnote (must be a full footnote block container),
// and to be displayed at the bottom of all pages that contain a link to it.
#define CSS_CR_HINT_FOOTNOTE_INPAGE         0x00400000 // -cr-hint: footnote-inpage
#define CSS_CR_HINT_INSIDE_FOOTNOTE_INPAGE  0x00800000 // (internal: set by previous one, and inherited early by children)


// For footnote popup detection by koreader-base/cre.cpp
// 'noteref-ignore' also prevents in-page footnotes from being added to the pages of links with that hint
#define CSS_CR_HINT_NOTEREF                 0x01000000 // -cr-hint: noteref         link is to a footnote
#define CSS_CR_HINT_NOTEREF_IGNORE          0x02000000 // -cr-hint: noteref-ignore  link is not to a footnote (even if
                                                       //                           everything else indicates it is)
#define CSS_CR_HINT_FOOTNOTE                0x04000000 // -cr-hint: footnote        block is a footnote (must be a full
                                                       //                           footnote block container)
#define CSS_CR_HINT_FOOTNOTE_IGNORE         0x08000000 // -cr-hint: footnote-ignore block is not a footnote (even if
                                                       //                           everything else indicates it is)

// To be set on a block element: it may be considered a continuation of an inpage footnote
// and will be displayed as part of it on the bottom of pages that contain a link to it
// (see `-cr-hint: footnote-inpage` above)
#define CSS_CR_HINT_EXTEND_FOOTNOTE_INPAGE  0x10000000 // -cr-hint: extend-footnote-inpage

// A few of them are inheritable, most are not.
#define CSS_CR_HINT_INHERITABLE_MASK        0x0000000E
#define CSS_CR_HINT_INHERITABLE_EARLY_MASK  0x00800000 // inherited before stylesheets are applied

// Macro for easier checking
#define STYLE_HAS_CR_HINT(s, h)     ( (bool)(s->cr_hint.value & CSS_CR_HINT_##h) )

/// css length value
typedef struct css_length_tag {
    css_value_type_t type;  ///< type of value
    int         value;      ///< value: *256 for all types (to allow for fractional px and %), except css_val_screen_px
                            // allow for values -/+ 524288.0 (32bits -8 for fraction -4 for pack -1 for sign)
    css_length_tag()
        : type (css_val_screen_px), value (0)
    {
    }
    css_length_tag( int px_value )
        : type (css_val_screen_px), value (px_value)
    {
    }
    css_length_tag(css_value_type_t n_type, int n_value) // expects caller to do << 8
        : type(n_type), value(n_value)
    {
    }
    bool operator == ( const css_length_tag & v ) const
    {
        return type == v.type 
            && value == v.value;
    }
    // used only in hash calculation
    lUInt32 pack() { return (lUInt32)type + (((lUInt32)value)<<4); }
} css_length_t;

#endif // __CSS_DEF_H_INCLUDED__
