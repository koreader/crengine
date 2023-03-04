/*******************************************************

   CoolReader Engine

   lvstsheet.cpp:  style sheet implementation

   (c) Vadim Lopatin, 2000-2006

   This source code is distributed under the terms of
   GNU General Public License.

   See LICENSE file for details.

*******************************************************/

#include "../include/lvstsheet.h"
#include "../include/lvtinydom.h"
#include "../include/fb2def.h"
#include "../include/lvstream.h"
#include "../include/lvrend.h"   // for -cr-only-if:

// define to dump all tokens
//#define DUMP_CSS_PARSING

// Helper to debug string parsing, showing current position and context
static void dbg_str_pos(const char * prefix, const char * str) {
    printf("/----------|---------- %s\n", prefix);
    printf("\\%.*s\n", 20, str - 10);
}

#define IMPORTANT_DECL_HIGHER   ((lUInt32)0x80000000U) // | to prop_code
#define IMPORTANT_DECL_SET      ((lUInt32)0x40000000U) // | to prop_code
#define IMPORTANT_DECL_REMOVE   ((lUInt32)0x3FFFFFFFU) // & to prop_code
#define IMPORTANT_DECL_SHIFT    30 // >> from prop_code to get 2 bits (importance, is_important)

enum css_decl_code {
    cssd_unknown,
    cssd_display,
    cssd_white_space,
    cssd_text_align,
    cssd_text_align_last,
    cssd_text_align_last2, // -epub-text-align-last (mentioned in early versions of EPUB3)
    cssd_text_decoration,
    cssd_text_decoration2, // -epub-text-decoration (WebKit css extension)
    cssd_text_transform,
    cssd_hyphenate,  // hyphens (proper css property name)
    cssd_hyphenate2, // -webkit-hyphens (used by authors as an alternative to adobe-hyphenate)
    cssd_hyphenate3, // adobe-hyphenate (used by late Adobe RMSDK)
    cssd_hyphenate4, // adobe-text-layout (used by earlier Adobe RMSDK)
    cssd_hyphenate5, // hyphenate (fb2? used in obsoleted css files))
    cssd_hyphenate6, // -epub-hyphens (mentioned in early versions of EPUB3)
    cssd_color,
    cssd_border_top_color,
    cssd_border_right_color,
    cssd_border_bottom_color,
    cssd_border_left_color,
    cssd_background_color,
    cssd_vertical_align,
    cssd_font_family, // id families like serif, sans-serif
    cssd_font_names,   // string font name like Arial, Courier
    cssd_font_size,
    cssd_font_style,
    cssd_font_weight,
    cssd_font_features,           // font-feature-settings (not yet parsed)
    cssd_font_variant,            // all these are parsed specifically and mapped into
    cssd_font_variant_ligatures,  // the same style->font_features 31 bits bitmap
    cssd_font_variant_ligatures2, // -webkit-font-variant-ligatures (former Webkit property)
    cssd_font_variant_caps,
    cssd_font_variant_position,
    cssd_font_variant_numeric,
    cssd_font_variant_east_asian,
    cssd_font_variant_alternates,
    cssd_text_indent,
    cssd_line_height,
    cssd_letter_spacing,
    cssd_width,
    cssd_height,
    cssd_min_width,
    cssd_min_height,
    cssd_max_width,
    cssd_max_height,
    cssd_margin_left,
    cssd_margin_right,
    cssd_margin_top,
    cssd_margin_bottom,
    cssd_margin,
    cssd_padding_left,
    cssd_padding_right,
    cssd_padding_top,
    cssd_padding_bottom,
    cssd_padding,
    cssd_page_break_before, // Historical, but common, page break properties names
    cssd_page_break_after,
    cssd_page_break_inside,
    cssd_break_before, // Newest page break properties names
    cssd_break_after,
    cssd_break_inside,
    cssd_list_style,
    cssd_list_style_type,
    cssd_list_style_position,
    cssd_list_style_image,
    cssd_border_top_style,
    cssd_border_top_width,
    cssd_border_right_style,
    cssd_border_right_width,
    cssd_border_bottom_style,
    cssd_border_bottom_width,
    cssd_border_left_style,
    cssd_border_left_width,
    cssd_border_style,
    cssd_border_width,
    cssd_border_color,
    cssd_border,
    cssd_border_top,
    cssd_border_right,
    cssd_border_bottom,
    cssd_border_left,
    cssd_background,
    cssd_background_image,
    cssd_background_repeat,
    cssd_background_position,
    cssd_background_size,
    cssd_background_size2, // -webkit-background-size (former Webkit property)
    cssd_border_collapse,
    cssd_border_spacing,
    cssd_orphans,
    cssd_widows,
    cssd_float,
    cssd_clear,
    cssd_direction,
    cssd_visibility,
    cssd_line_break,
    cssd_line_break2,
    cssd_line_break3,
    cssd_word_break,
    cssd_box_sizing,
    cssd_content,
    cssd_cr_ignore_if_dom_version_greater_or_equal,
    cssd_cr_hint,
    cssd_cr_only_if,
    cssd_stop
};

static const char * css_decl_name[] = {
    "",
    "display",
    "white-space",
    "text-align",
    "text-align-last",
    "-epub-text-align-last",
    "text-decoration",
    "-epub-text-decoration",
    "text-transform",
    "hyphens",
    "-webkit-hyphens",
    "adobe-hyphenate",
    "adobe-text-layout",
    "hyphenate",
    "-epub-hyphens",
    "color",
    "border-top-color",
    "border-right-color",
    "border-bottom-color",
    "border-left-color",
    "background-color",
    "vertical-align",
    "font-family",
    "$dummy-for-font-names$",
    "font-size",
    "font-style",
    "font-weight",
    "font-feature-settings",
    "font-variant",
    "font-variant-ligatures",
    "-webkit-font-variant-ligatures",
    "font-variant-caps",
    "font-variant-position",
    "font-variant-numeric",
    "font-variant-east-asian",
    "font-variant-alternates",
    "text-indent",
    "line-height",
    "letter-spacing",
    "width",
    "height",
    "min-width",
    "min-height",
    "max-width",
    "max-height",
    "margin-left",
    "margin-right",
    "margin-top",
    "margin-bottom",
    "margin",
    "padding-left",
    "padding-right",
    "padding-top",
    "padding-bottom",
    "padding",
    "page-break-before",
    "page-break-after",
    "page-break-inside",
    "break-before",
    "break-after",
    "break-inside",
    "list-style",
    "list-style-type",
    "list-style-position",
    "list-style-image",
    "border-top-style",
    "border-top-width",
    "border-right-style",
    "border-right-width",
    "border-bottom-style",
    "border-bottom-width",
    "border-left-style",
    "border-left-width",
    "border-style",
    "border-width",
    "border-color",
    "border",
    "border-top",
    "border-right",
    "border-bottom",
    "border-left",
    "background",
    "background-image",
    "background-repeat",
    "background-position",
    "background-size",
    "-webkit-background-size",
    "border-collapse",
    "border-spacing",
    "orphans",
    "widows",
    "float",
    "clear",
    "direction",
    "visibility",
    "line-break",
    "-epub-line-break",
    "-webkit-line-break",
    "word-break",
    "box-sizing",
    "content",
    "-cr-ignore-if-dom-version-greater-or-equal",
    "-cr-hint",
    "-cr-only-if",
    NULL
};

// See https://developer.mozilla.org/en-US/docs/Web/CSS/Pseudo-classes
enum LVCssSelectorPseudoClass
{
    csspc_root,             // :root
    csspc_dir,              // :dir(rtl), :dir(ltr)
    csspc_first_child,      // :first-child
    csspc_first_of_type,    // :first-of-type
    csspc_nth_child,        // :nth-child(even), :nth-child(3n+4)
    csspc_nth_of_type,      // :nth-of-type()
    // Those after this won't be valid when checked in the initial
    // document loading phase when the XML is being parsed, as at
    // this point, the checked node is always the last node as we
    // haven't yet parsed its following siblings. When meeting one,
    // we'll need to re-render and re-check styles after load with
    // a fully built DOM.
    csspc_last_child,       // :last-child
    csspc_last_of_type,     // :last-of-type
    csspc_nth_last_child,   // :nth-last-child()
    csspc_nth_last_of_type, // :nth-last-of-type()
    csspc_only_child,       // :only-child
    csspc_only_of_type,     // :only-of-type
    csspc_empty,            // :empty
};

static const char * css_pseudo_classes[] =
{
    "root",
    "dir",
    "first-child",
    "first-of-type",
    "nth-child",
    "nth-of-type",
    "last-child",
    "last-of-type",
    "nth-last-child",
    "nth-last-of-type",
    "only-child",
    "only-of-type",
    "empty",
    NULL
};

// https://developer.mozilla.org/en-US/docs/Web/CSS/Pseudo-elements
enum LVCssSelectorPseudoElement
{
    csspe_before = 1,   // ::before
    csspe_after  = 2,   // ::after
};

static const char * css_pseudo_elements[] =
{
    "before",
    "after",
    NULL
};

inline bool css_is_alpha( char ch )
{
    return ( (ch>='A' && ch<='Z') || ( ch>='a' && ch<='z' ) || (ch=='-') || (ch=='_') );
}

inline bool css_is_alnum( char ch )
{
    return ( css_is_alpha(ch) || ( ch>='0' && ch<='9' ) );
}

static int substr_compare( const char * sub, const char * & str )
{
    int j;
    for ( j=0; sub[j] == str[j] && sub[j] && str[j]; j++)
        ;
    if (!sub[j])
    {
        //bool last_alpha = css_is_alpha( sub[j-1] );
        //bool next_alnum = css_is_alnum( str[j] );
        if ( !css_is_alpha( sub[j-1] ) || !css_is_alnum( str[j] ) )
        {
            str+=j;
            return j;
        }
    }
    return 0;
}

inline char toLower( char c )
{
    if ( c>='A' && c<='Z' )
        return c - 'A' + 'a';
    return c;
}

static int substr_icompare( const char * sub, const char * & str )
{
    int j;

    // Small optimisation: don't toLower() 'sub', as all the harcoded values
    // we compare with are lowercase in this file.
    // for ( j=0; toLower(sub[j]) == toLower(str[j]) && sub[j] && str[j]; j++)
    for ( j=0; sub[j] == toLower(str[j]) && sub[j] && str[j]; j++)
        ;
    if (!sub[j])
    {
        //bool last_alpha = css_is_alpha( sub[j-1] );
        //bool next_alnum = css_is_alnum( str[j] );
        if ( !css_is_alpha( sub[j-1] ) || !css_is_alnum( str[j] ) )
        {
            str+=j;
            return j;
        }
    }
    return 0;
}

static bool skip_spaces( const char * & str )
{
    const char * oldpos = str;
    for (;;) {
        while (*str==' ' || *str=='\t' || *str=='\n' || *str == '\r')
            str++;
        if ( *str=='/' && str[1]=='*' ) {
            // comment found
            while ( *str && str[1] && (str[0]!='*' || str[1]!='/') )
                str++;
            if ( *str=='*' && str[1]=='/' )
                str +=2;
        }
        while (*str==' ' || *str=='\t' || *str=='\n' || *str == '\r')
            str++;
        if (oldpos == str)
            break;
        if (*str == 0)
            return false;
        oldpos = str;
    }
    return *str != 0;
}

static bool parse_ident( const char * &str, char * ident, size_t maxsize, bool skip_namespace=false )
{
    // Note: skipping any space before or after should be ensured by caller if needed
    *ident = 0;
    if ( !css_is_alpha( *str ) ) {
        if ( !skip_namespace )
            return false;
        // By checking for the char after to be alpha, we avoid considering the '|'
        // in cssrt_attrstarts_word ([attr|=foo]) as a namespace separator.
        if ( str[0] == '|' && css_is_alpha(str[1]) )
            str++;
        else if ( str[0] == '*' && str[1] == '|' && css_is_alpha(str[2]) )
            str+=2;
        else
            return false;
    }
    int i;
    int max_i = maxsize - 1;
    for (i=0; css_is_alnum(str[i]); i++) {
        if ( i < max_i )
            ident[i] = str[i];
        // Keep parsing/skipping even if not accumulated in ident
    }
    if ( skip_namespace && str[i] == '|' && css_is_alpha(str[i+1]) ) {
        str += i+1;
        *ident = 0;
        for (i=0; css_is_alnum(str[i]); i++) {
            if ( i < max_i )
                ident[i] = str[i];
        }
    }
    ident[i < max_i ? i : max_i] = 0;
    str += i;
    return true;
}

static css_decl_code parse_property_name( const char * & res )
{
    const char * str = res;
    for (int i=1; css_decl_name[i]; i++)
    {
        if (substr_icompare( css_decl_name[i], str )) // css property case should not matter (eg: "Font-Weight:")
        {
            // found!
            skip_spaces(str);
            if ( substr_compare( ":", str )) {
#ifdef DUMP_CSS_PARSING
                CRLog::trace("property name: %s", lString8(res, str-res).c_str() );
#endif
                skip_spaces(str);
                res = str;
                return (css_decl_code)i;
            }
        }
    }
    return cssd_unknown;
}

static int parse_name( const char * & str, const char * * names, int def_value )
{
    for (int i=0; names[i]; i++)
    {
        if (substr_icompare( names[i], str )) // css named value case should not matter (eg: "BOLD")
        {
            // found!
            return i;
        }
    }
    return def_value;
}

static lUInt32 parse_important( const char *str ) // does not advance the original *str
{
    skip_spaces( str );
    // "!  important", with one or more spaces in between, is valid
    if (*str == '!') {
        str++;
        skip_spaces( str );
        if (substr_icompare( "important", str )) {
            // returns directly what should be | to prop_code
            return IMPORTANT_DECL_SET;
        }
    }
    return 0;
}

static inline bool skip_to_next( const char * & str, char stop_char_to_skip, char stop_char_no_skip, char token_sep_char=0 )
{
    // https://www.w3.org/TR/CSS2/syndata.html#parsing-errors
    //  "User agents must handle unexpected tokens encountered while
    //  parsing a declaration by reading until the end of the
    //  declaration, while observing the rules for matching pairs
    //  of (), [], {}, "", and '', and correctly handling escapes."
    // We handle quotes, and one pair of parens (which should allow
    // nested balanced pairs of different types of parens).
    char quote_ch = 0;
    char closing_paren_ch = 0;
    while (*str) {
        if ( *str == '\\' ) {
            str++; // skip '\', so the escaped char will be skipped instead
        }
        else if ( quote_ch ) {
            if ( *str == quote_ch ) {
                quote_ch = 0;
            }
            // skip closing quote, or anything not this quote when inside quote
        }
        else if ( closing_paren_ch ) {
            if ( *str == closing_paren_ch ) {
                closing_paren_ch = 0;
            }
            // skip closing paren, or anything not this closing paren when
            // inside parens and not inside quotes (handled above)
        }
        else if ( *str == stop_char_to_skip ) {
            // i.e. ';' after "property:value;" if not inside quotes/parens nor escaped
            str++; // skip it
            break;
        }
        else if ( *str == stop_char_no_skip ) {
            // i.e. '}' or ')', skipping handled by callers
            break;
        }
        else if ( *str == token_sep_char ) {
            // token_sep_char provided and met
            if ( skip_spaces( str ) ) {
                if ( *str != stop_char_to_skip && *str != stop_char_no_skip ) {
                    // Something else before any stop char (before next property or end of declaration)
                    return true;
                }
            }
            return false; // no next token
        }
        // These could be used as stop_chars, so check only after we have checked stop_chars
        else if ( *str == '\'' || *str=='\"' ) {
            // Quote met while not yet inside quotes
            quote_ch = *str;
        }
        else if ( *str == '(' ) {
            closing_paren_ch = ')';
        }
        else if ( *str == '[' ) {
            closing_paren_ch = ']';
        }
        else if ( *str == '{' ) {
            closing_paren_ch = '}';
        }
        str++;
    }
    return skip_spaces( str );
}

// Give more explicite names to classic usages of skip_to_next()
static inline bool next_property( const char * & str )
{
    return skip_to_next( str, ';', '}' );
}

static inline bool next_token( const char * & str, char stop_char='}')
{
    return skip_to_next( str, ';', stop_char, ' ' );
}

static bool parse_integer( const char * & str, int & value)
{
    skip_spaces( str );
    if (*str<'0' || *str>'9') {
        return false; // not a number
    }
    value = 0;
    while (*str>='0' && *str<='9') {
        value = value*10 + (*str - '0');
        str++;
    }
    return true;
}

bool parse_number_value( const char * & str, css_length_t & value,
                                    bool accept_percent,    // Defaults to true
                                    bool accept_negative,   // This and next ones default to false
                                    bool accept_auto,
                                    bool accept_none,
                                    bool accept_normal,
                                    bool accept_unspecified,
                                    bool accept_contain_cover,
                                    bool is_font_size )
{
    const char * orig_pos = str;
    value.type = css_val_unspecified;
    skip_spaces( str );
    // Here and below: named values and unit case should not matter
    if ( substr_icompare( "inherit", str ) ) {
        value.type = css_val_inherited;
        value.value = 0;
        return true;
    }
    if ( accept_auto && substr_icompare( "auto", str ) ) {
        value.type = css_val_unspecified;
        value.value = css_generic_auto;
        return true;
    }
    if ( accept_none && substr_icompare( "none", str ) ) {
        value.type = css_val_unspecified;
        value.value = css_generic_none;
        return true;
    }
    if ( accept_normal && substr_icompare( "normal", str ) ) {
        value.type = css_val_unspecified;
        value.value = css_generic_normal;
        return true;
    }
    if ( accept_contain_cover ) {
        if ( substr_icompare( "contain", str ) ) {
            value.type = css_val_unspecified;
            value.value = css_generic_contain;
            return true;
        }
        if ( substr_icompare( "cover", str ) ) {
            value.type = css_val_unspecified;
            value.value = css_generic_cover;
            return true;
        }
    }
    if ( is_font_size ) {
        // Absolute-size keywords, based on the default font size (which is medium)
        // Factors as suggested in https://drafts.csswg.org/css-fonts-3/#absolute-size-value
        if ( substr_icompare( "medium", str ) ) {
            value.type = css_val_rem;
            value.value = 256;
            return true;
        }
        else if ( substr_icompare( "small", str ) ) {
            value.type = css_val_rem;
            value.value = (int)(256 * 8/9);
            return true;
        }
        else if ( substr_icompare( "x-small", str ) ) {
            value.type = css_val_rem;
            value.value = (int)(256 * 3/4);
            return true;
        }
        else if ( substr_icompare( "xx-small", str ) ) {
            value.type = css_val_rem;
            value.value = (int)(256 * 3/5);
            return true;
        }
        else if ( substr_icompare( "large", str ) ) {
            value.type = css_val_rem;
            value.value = (int)(256 * 6/5);
            return true;
        }
        else if ( substr_icompare( "x-large", str ) ) {
            value.type = css_val_rem;
            value.value = (int)(256 * 3/2);
            return true;
        }
        else if ( substr_icompare( "xx-large", str ) ) {
            value.type = css_val_rem;
            value.value = (int)(256 * 2);
            return true;
        }
        // Approximate the (usually uneven) gaps between named sizes.
        else if ( substr_icompare( "smaller", str ) ) {
            value.type = css_val_percent;
            value.value = 80 << 8;
            return true;
        }
        else if ( substr_icompare( "larger", str ) ) {
            value.type = css_val_percent;
            value.value = 125 << 8;
            return true;
        }
    }
    bool negative = false;
    if (accept_negative) {
        if ( *str == '-' ) {
            str++;
            negative = true;
        }
    }
    int n = 0;
    if (*str != '.') {
        if (*str<'0' || *str>'9') {
            str = orig_pos; // revert our possible str++
            return false; // not a number
        }
        while (*str>='0' && *str<='9') {
            n = n*10 + (*str - '0');
            str++;
        }
    }
    int frac = 0;
    int frac_div = 1;
    if (*str == '.') {
        str++;
        while (*str>='0' && *str<='9')
        {
            // don't process more than 6 digits after decimal point
            // to avoid overflow in case of very long values
            if (frac_div < 1000000) {
                frac = frac*10 + (*str - '0');
                frac_div *= 10;
            }
            str++;
        }
    }
    if ( substr_icompare( "em", str ) )
        value.type = css_val_em;
    else if ( substr_icompare( "pt", str ) )
        value.type = css_val_pt;
    else if ( substr_icompare( "ex", str ) )
        value.type = css_val_ex;
    else if ( substr_icompare( "ch", str ) )
        value.type = css_val_ch;
    else if ( substr_icompare( "rem", str ) )
        value.type = css_val_rem;
    else if ( substr_icompare( "px", str ) )
        value.type = css_val_px;
    else if ( substr_icompare( "in", str ) )
        value.type = css_val_in;
    else if ( substr_icompare( "cm", str ) )
        value.type = css_val_cm;
    else if ( substr_icompare( "mm", str ) )
        value.type = css_val_mm;
    else if ( substr_icompare( "pc", str ) )
        value.type = css_val_pc;
    else if ( substr_icompare( "%", str ) ) {
        if (accept_percent)
            value.type = css_val_percent;
        else {
            str = orig_pos; // revert our possible str++
            return false;
        }
    }
    else if ( substr_icompare( "vw", str ) )
        value.type = css_val_vw;
    else if ( substr_icompare( "vh", str ) )
        value.type = css_val_vh;
    else if ( substr_icompare( "vmin", str ) )
        value.type = css_val_vmin;
    else if ( substr_icompare( "vmax", str ) )
        value.type = css_val_vmax;
    else if ( css_is_alpha(*str) ) { // some other unit we don't support
        str = orig_pos; // revert our possible str++
        return false;
    }
    else if (n == 0 && frac == 0)
        value.type = css_val_px;
    else if ( !accept_unspecified ) {
        str = orig_pos; // revert our possible str++
        return false;
    }

    // The largest frac here is 999999, limited above, with a frac_div of
    // 1000000, and even scaling it by 256 it does not overflow a 32 bit
    // integer. The frac_div is a power of 10 so always divisible by 2 without
    // loss when frac is non-zero.
    value.value = n * 256 + (256 * frac + frac_div / 2 ) / frac_div; // *256
    if (negative)
        value.value = -value.value;
    return true;
}

static lString32 parse_nth_value( const lString32 value )
{
    // https://developer.mozilla.org/en-US/docs/Web/CSS/:nth-child
    // Parse "even", "odd", "5", "5n", "5n+2", "-n"...
    // Pack 3 numbers, enough to check if match, into another lString32
    // for quicker checking:
    // - a tuple of 3 lChar32: (negative, n-step, offset)
    // - or the empty string when invalid or if it would never match
    // (Note that we get the input already trimmed and lowercased.)
    lString32 ret = lString32(); // empty string = never match
    if ( value == "even" ) { //  = "2n"
        ret << lChar32(0) <<lChar32(2) << lChar32(0);
        return ret;
    }
    if ( value == "odd" ) {  // = "2n+1"
        ret << lChar32(0) <<lChar32(2) << lChar32(1);
        return ret;
    }
    int len = value.length();
    if (len == 0) // empty value
        return ret; // invalid
    bool negative = false;
    int first = 0;
    int second = 0;
    int i = 0;
    lChar32 c;
    c = value[i];
    if ( c == '-' ) {
        negative = true;
        i++;
    }
    if ( i==len ) // no follow up content
        return ret; // invalid
    c = value[i];
    if ( c == 'n' ) { // 'n' or '-n' without a leading number
        first = 1;
    }
    else {
        // Parse first number
        if ( c < '0' || c > '9') // not a digit
            return ret; // invalid
        while (true) { // grab digit(s)
            first = first * 10 + ( c - '0' );
            i++; // pass by this digit
            if ( i==len ) { // single number seen: this parsed number is actually the offset
                if ( negative ) // "-4"
                    return ret; // never match
                ret << lChar32(0) << lChar32(0) << lChar32(first);
                return ret;
            }
            c = value[i];
            if ( c < '0' || c > '9') // done grabbing first digits
                break;
        }
        if ( c != 'n' ) // invalid char after first number
            return ret; // invalid
    }
    i++; // pass by that 'n'
    if ( i==len ) { // ends with that 'n'
        if ( negative || first == 0) // valid, but would never match anything
            return ret; // never match
        ret << lChar32(0) << lChar32(first) << lChar32(0);
        return ret;
    }
    c = value[i];
    if ( c != '+' ) // follow up content must start with a '+'
        return ret; // invalid
    i++; // pass b y that '+'
    if ( i==len ) // ends with that '+'
        return ret; // invalid
    // Parse second number
    c = value[i];
    if ( c < '0' || c > '9') // not a digit
        return ret; // invalid
    while (true) { // grab digit(s)
        second = second * 10 + ( c - '0' );
        i++; // pass by this digit
        if ( i==len ) // end of string, fully valid
            break;
        c = value[i];
        if ( c < '0' || c > '9') // expected a digit (invalid stuff at end of value)
            return ret; // invalid
    }
    // Valid, and we parsed everything
    ret << lChar32(negative) << lChar32(first) << lChar32(second);
    return ret;
}

static bool match_nth_value( const lString32 value, int n)
{
    // Apply packed parsed value (parsed by above function) to n
    if ( value.empty() ) // invalid, or never match
        return false;
    bool negative = value[0];
    int step = value[1];
    int offset = value[2];
    if ( step == 0 )
        return n == offset;
    if ( negative )
        n = offset - n;
    else
        n = n - offset;
    if ( n < 0 )
        return false;
    return n % step == 0;
}

// For some expensive LVCssSelectorRule::check() checks, that might
// be done on a node for multiple rules and would give the same
// result, we can cache the result in the node's RenderRectAccessor(),
// which is not used at this point and will be reset and cleared after
// all styles have been applied, before rendering methods are set.
// This is mostly useful for the :zzz-child pseudoclasses checks
// that involve using the expensive getUnboxedSibling methods; we
// are sure that no boxing is done when applying stylesheets, so
// the position among the parent children collection is stable
// and can be cached.
// Note that we can't cache the value 0: a field with value 0 means
// it has not yet been cached (we could tweak it before caching if
// storing 0 is needed).
static void cache_node_checked_property( const ldomNode * node, int property, int value )
{
    RenderRectAccessor fmt( (ldomNode*)node );
    if ( !RENDER_RECT_HAS_FLAG(fmt, TEMP_USED_AS_CSS_CHECK_CACHE) ) {
        // Clear it from past rendering stuff: we're processing stylesheets,
        // which means we will soon re-render the whole DOM and have it cleared
        // and updated. We can use it for caching other stuff until then.
        fmt.clear();
        RENDER_RECT_SET_FLAG(fmt, TEMP_USED_AS_CSS_CHECK_CACHE);
    }
    switch ( property ) {
        // Positive integer >= 1: needs a int field
        case csspc_nth_child:
            fmt.setY(value);
            break;
        case csspc_nth_of_type:
            fmt.setHeight(value);
            break;
        case csspc_nth_last_child:
            fmt.setTopOverflow(value);
            break;
        case csspc_nth_last_of_type:
            fmt.setBottomOverflow(value);
            break;
        // Boolean (1 means false, 2 means true): fine in a short int field
        case csspc_first_child:
            fmt.setX(value);
            break;
        case csspc_first_of_type:
            fmt.setWidth(value);
            break;
        case csspc_last_child:
            fmt.setInnerWidth(value);
            break;
        case csspc_last_of_type:
            fmt.setInnerX(value);
            break;
        case csspc_only_child:
            fmt.setInnerY(value);
            break;
        case csspc_only_of_type:
            fmt.setBaseline(value);
            break;
        default:
            break;
    }
}

static bool get_cached_node_checked_property( const ldomNode * node, int property, int & value )
{
    RenderRectAccessor fmt( (ldomNode*)node );
    if ( !RENDER_RECT_HAS_FLAG(fmt, TEMP_USED_AS_CSS_CHECK_CACHE) )
        return false; // nothing cached yet
    bool res = false;
    switch ( property ) {
        // Positive integer >= 1
        case csspc_nth_child:
            value = fmt.getY();
            res = value != 0;
            break;
        case csspc_nth_of_type:
            value = fmt.getHeight();
            res = value != 0;
            break;
        case csspc_nth_last_child:
            value = fmt.getTopOverflow();
            res = value != 0;
            break;
        case csspc_nth_last_of_type:
            value = fmt.getBottomOverflow();
            res = value != 0;
            break;
        // Boolean (1 means false, 2 means true)
        case csspc_first_child:
            value = fmt.getX();
            res = value != 0;
            break;
        case csspc_first_of_type:
            value = fmt.getWidth();
            res = value != 0;
            break;
        case csspc_last_child:
            value = fmt.getInnerWidth();
            res = value != 0;
            break;
        case csspc_last_of_type:
            value = fmt.getInnerX();
            res = value != 0;
            break;
        case csspc_only_child:
            value = fmt.getInnerY();
            res = value != 0;
            break;
        case csspc_only_of_type:
            value = fmt.getBaseline();
            res = value != 0;
            break;
        default:
            break;
    }
    return res;
}

struct standard_color_t
{
    const char * name;
    lUInt32 color;
};

standard_color_t standard_color_table[] = {
    {"aliceblue",0xf0f8ff},
    {"antiquewhite",0xfaebd7},
    {"aqua",0x00ffff},
    {"aquamarine",0x7fffd4},
    {"azure",0xf0ffff},
    {"beige",0xf5f5dc},
    {"bisque",0xffe4c4},
    {"black",0x000000},
    {"blanchedalmond",0xffebcd},
    {"blue",0x0000ff},
    {"blueviolet",0x8a2be2},
    {"brown",0xa52a2a},
    {"burlywood",0xdeb887},
    {"cadetblue",0x5f9ea0},
    {"chartreuse",0x7fff00},
    {"chocolate",0xd2691e},
    {"coral",0xff7f50},
    {"cornflowerblue",0x6495ed},
    {"cornsilk",0xfff8dc},
    {"crimson",0xdc143c},
    {"cyan",0x00ffff},
    {"darkblue",0x00008b},
    {"darkcyan",0x008b8b},
    {"darkgoldenrod",0xb8860b},
    {"darkgray",0xa9a9a9},
    {"darkgreen",0x006400},
    {"darkgrey",0xa9a9a9},
    {"darkkhaki",0xbdb76b},
    {"darkmagenta",0x8b008b},
    {"darkolivegreen",0x556b2f},
    {"darkorange",0xff8c00},
    {"darkorchid",0x9932cc},
    {"darkred",0x8b0000},
    {"darksalmon",0xe9967a},
    {"darkseagreen",0x8fbc8f},
    {"darkslateblue",0x483d8b},
    {"darkslategray",0x2f4f4f},
    {"darkslategrey",0x2f4f4f},
    {"darkturquoise",0x00ced1},
    {"darkviolet",0x9400d3},
    {"deeppink",0xff1493},
    {"deepskyblue",0x00bfff},
    {"dimgray",0x696969},
    {"dimgrey",0x696969},
    {"dodgerblue",0x1e90ff},
    {"firebrick",0xb22222},
    {"floralwhite",0xfffaf0},
    {"forestgreen",0x228b22},
    {"fuchsia",0xff00ff},
    {"gainsboro",0xdcdcdc},
    {"ghostwhite",0xf8f8ff},
    {"gold",0xffd700},
    {"goldenrod",0xdaa520},
    {"gray",0x808080},
    {"green",0x008000},
    {"grey",0x808080},
    {"greenyellow",0xadff2f},
    {"honeydew",0xf0fff0},
    {"hotpink",0xff69b4},
    {"indianred",0xcd5c5c},
    {"indigo",0x4b0082},
    {"ivory",0xfffff0},
    {"khaki",0xf0e68c},
    {"lavender",0xe6e6fa},
    {"lavenderblush",0xfff0f5},
    {"lawngreen",0x7cfc00},
    {"lemonchiffon",0xfffacd},
    {"lightblue",0xadd8e6},
    {"lightcoral",0xf08080},
    {"lightcyan",0xe0ffff},
    {"lightgoldenrodyellow",0xfafad2},
    {"lightgray",0xd3d3d3},
    {"lightgreen",0x90ee90},
    {"lightgrey",0xd3d3d3},
    {"lightpink",0xffb6c1},
    {"lightsalmon",0xffa07a},
    {"lightseagreen",0x20b2aa},
    {"lightskyblue",0x87cefa},
    {"lightslategray",0x778899},
    {"lightslategrey",0x778899},
    {"lightsteelblue",0xb0c4de},
    {"lightyellow",0xffffe0},
    {"lime",0x00ff00},
    {"limegreen",0x32cd32},
    {"linen",0xfaf0e6},
    {"magenta",0xff00ff},
    {"maroon",0x800000},
    {"mediumaquamarine",0x66cdaa},
    {"mediumblue",0x0000cd},
    {"mediumorchid",0xba55d3},
    {"mediumpurple",0x9370db},
    {"mediumseagreen",0x3cb371},
    {"mediumslateblue",0x7b68ee},
    {"mediumspringgreen",0x00fa9a},
    {"mediumturquoise",0x48d1cc},
    {"mediumvioletred",0xc71585},
    {"midnightblue",0x191970},
    {"mintcream",0xf5fffa},
    {"mistyrose",0xffe4e1},
    {"moccasin",0xffe4b5},
    {"navajowhite",0xffdead},
    {"navy",0x000080},
    {"oldlace",0xfdf5e6},
    {"olive",0x808000},
    {"olivedrab",0x6b8e23},
    {"orange",0xffa500},
    {"orangered",0xff4500},
    {"orchid",0xda70d6},
    {"palegoldenrod",0xeee8aa},
    {"palegreen",0x98fb98},
    {"paleturquoise",0xafeeee},
    {"palevioletred",0xdb7093},
    {"papayawhip",0xffefd5},
    {"peachpuff",0xffdab9},
    {"peru",0xcd853f},
    {"pink",0xffc0cb},
    {"plum",0xdda0dd},
    {"powderblue",0xb0e0e6},
    {"purple",0x800080},
    {"rebeccapurple",0x663399},
    {"red",0xff0000},
    {"rosybrown",0xbc8f8f},
    {"royalblue",0x4169e1},
    {"saddlebrown",0x8b4513},
    {"salmon",0xfa8072},
    {"sandybrown",0xf4a460},
    {"seagreen",0x2e8b57},
    {"seashell",0xfff5ee},
    {"sienna",0xa0522d},
    {"silver",0xc0c0c0},
    {"skyblue",0x87ceeb},
    {"slateblue",0x6a5acd},
    {"slategray",0x708090},
    {"slategrey",0x708090},
    {"snow",0xfffafa},
    {"springgreen",0x00ff7f},
    {"steelblue",0x4682b4},
    {"tan",0xd2b48c},
    {"teal",0x008080},
    {"thistle",0xd8bfd8},
    {"tomato",0xff6347},
    {"turquoise",0x40e0d0},
    {"violet",0xee82ee},
    {"wheat",0xf5deb3},
    {"white",0xffffff},
    {"whitesmoke",0xf5f5f5},
    {"yellow",0xffff00},
    {"yellowgreen",0x9acd32},
    {NULL, 0}
};

static int hexDigit( char c )
{
    if ( c >= '0' && c <= '9' )
        return c-'0';
    if ( c >= 'A' && c <= 'F' )
        return c - 'A' + 10;
    if ( c >= 'a' && c <= 'f' )
        return c - 'a' + 10;
    return -1;
}

bool parse_color_value( const char * & str, css_length_t & value )
{
    const char * orig_pos = str;
    value.type = css_val_unspecified;
    skip_spaces( str );
    if ( substr_icompare( "transparent", str ) ) {
        // Make it an invalid color, but a valid parsing so it
        // can be inherited or flagged with !important
        value.type = css_val_unspecified;
        value.value = css_generic_transparent;
        return true;
    }
    if ( substr_icompare( "currentcolor", str ) ) {
        value.type = css_val_unspecified;
        value.value = css_generic_currentcolor;
        return true;
    }
    if ( substr_icompare( "inherit", str ) )
    {
        value.type = css_val_inherited;
        value.value = 0;
        return true;
    }
    if ( substr_icompare( "none", str ) )
    {
        value.type = css_val_unspecified;
        value.value = 0;
        return true;
    }
    if (*str=='#') {
        // #rgb or #rrggbb colors
        str++;
        int nDigits = 0;
        for ( ; hexDigit(str[nDigits])>=0; nDigits++ )
            ;
        if ( nDigits==3 || nDigits==4 ) {
            int r = hexDigit( *str++ );
            int g = hexDigit( *str++ );
            int b = hexDigit( *str++ );
            value.type = css_val_color;
            value.value = (((r + r*16) * 256) | (g + g*16)) * 256 | (b + b*16);
            if ( nDigits==4 ) {
                int a = hexDigit( *str++ );
                // cre color upper byte is inverted alpha (0x00=opaque, 0xFF=transparency)
                value.value |= ((a + a*16)^0xFF)<<24;
            }
            return true;
        } else if ( nDigits==6 || nDigits==8 ) {
            int r = hexDigit( *str++ ) * 16;
            r += hexDigit( *str++ );
            int g = hexDigit( *str++ ) * 16;
            g += hexDigit( *str++ );
            int b = hexDigit( *str++ ) * 16;
            b += hexDigit( *str++ );
            value.type = css_val_color;
            value.value = ((r * 256) | g) * 256 | b;
            if ( nDigits==8 ) {
                int a = hexDigit( *str++ ) * 16;
                a += hexDigit( *str++ );
                // cre color upper byte is inverted alpha
                value.value |= (a^0xFF)<<24;
            }
            return true;
        } else {
            str = orig_pos; // revert our possible str++
            return false;
        }
    }
    if ( substr_icompare( "rgb(", str ) || substr_icompare( "rgba(", str ) ) {
        skip_spaces(str);
        bool valid = true;
        int color = 0;
        // Per-specs, separators, and numbers and percentages, can't be mixed: the first type met decides.
        bool allow_unspecified = true;
        bool allow_percent = true;
        bool has_comma_separator = false;
        for ( int i=1; i<=3; i++ ) {
            css_length_t num;
            if ( parse_number_value(str, num, true, true, false, false, false, true) ) { // accept percent, negative, unspecified
                // Firefox actually caps the values (-123 is considered as 0, 300 as 255)
                int n = 0; // defaults to 0 if negative met below
                if ( allow_unspecified && num.type == css_val_unspecified ) {
                    if (num.value >= 0 ) {
                        n = (num.value + 0x7F) >> 8; // round it
                        if (n > 255) {
                            n = 255;
                        }
                    }
                    // else: keep n=0
                    allow_percent = false;
                }
                else if ( allow_unspecified && num.type == css_val_px && num.value == 0 ) { // when parsing "0"
                    // keep n=0
                    allow_percent = false;
                }
                else if ( allow_percent && num.type == css_val_percent ) {
                    if (num.value >= 0 ) {
                        n = ( 255 * num.value / 100 ) >> 8;
                        if (n > 255) {
                            n = 255;
                        }
                    }
                    // else: keep n=0
                    allow_unspecified = false;
                }
                else {
                    valid = false;
                    break;
                }
                color = (color << 8) + n;
            }
            else {
                valid = false;
                break;
            }
            skip_spaces(str);
            // separator may be a space or ',', and it should then always be the same
            if ( i == 1 && *str == ',' ) {
                has_comma_separator = true;
            }
            bool expecting_4th_number = false;
            if ( has_comma_separator ) { // skip it and more spaces
                if ( *str == ',' ) {
                    str++;
                    skip_spaces(str);
                    if ( i == 3 )
                        expecting_4th_number = true;
                }
                else if ( i < 3 ) { // separator non consistent
                    valid = false;
                    break;
                }
            }
            else {
                // If there is a 4th number, and the separator was a space, it
                // should be a '/' before the 4th number.
                if ( i == 3 && *str == '/' ) {
                    str++;
                    skip_spaces(str);
                    expecting_4th_number = true;
                }
            }
            if ( expecting_4th_number ) {
                if ( parse_number_value(str, num, true, true, false, false, false, true) ) {
                    int n = 0;
                    if ( num.type == css_val_unspecified ) {
                        if (num.value >= 0 ) {
                            // "0.0" gives num.value=0, "1.0" gives num.value=256 (that we translate to 255)
                            n = num.value;
                            if (num.value > 255) {
                                n = 255;
                            }
                        }
                        // else: keep n=0
                    }
                    else if ( num.type == css_val_px && num.value == 0 ) { // when parsing "0"
                        // keep n=0
                    }
                    else if ( num.type == css_val_percent ) {
                        if (num.value >= 0 ) {
                            n = ( 255 * num.value / 100 ) >> 8;
                            if (n > 255) {
                                n = 255;
                            }
                        }
                        // else: keep n=0
                    }
                    else {
                        valid = false;
                        break;
                    }
                    // cre color upper byte is inverted alpha (0x00=opaque, 0xFF=transparency)
                    color = ((n^0xFF)<<24) | color;
                }
                else {
                    valid = false;
                    break;
                }
                skip_spaces(str);
            }
        }
        if ( !valid || *str != ')' ) { // invalid
            str = orig_pos; // revert our possible str++
            return false;
        }
        str++;
        value.type = css_val_color;
        value.value = color;
        return true;
    }
    for ( int i=0; standard_color_table[i].name != NULL; i++ ) {
        if ( substr_icompare( standard_color_table[i].name, str ) ) {
            value.type = css_val_color;
            value.value = standard_color_table[i].color;
            return true;
        }
    }
    str = orig_pos; // revert our possible str++
    return false;
}

// Parse a CSS "content:" property into an intermediate format single string.
bool parse_content_property( const char * & str, lString32 & parsed_content, bool & has_unsupported, char stop_char='}')
{
    // https://developer.mozilla.org/en-US/docs/Web/CSS/content
    // The property may have multiple tokens:
    //   p::before { content: "[" attr(n) "]"; }
    //               content: "Qq. " attr(qq)
    //               content: '\201D\ In: ';
    // We can meet some bogus values: content: "&#x2219; ";
    // or values we don't support: Firefox would drop the whole
    // declaration, but, as we don't support all those from the
    // specs, we'll just ignore the tokens we don't support.
    // We parse the original content into a "parsed content" string,
    // consisting of a first letter, indicating its type, and if some
    // data: its length (+1 to avoid NULLs in strings) and that data.
    // parsed_content may contain multiple values, in the format
    //   'X' for 'none' (or 'normal', = none with pseudo elements)
    //   's' + <len> + string32 (string content) for ""
    //   'a' + <len> + string32 (attribute name) for attr()
    //   'Q' for 'open-quote'
    //   'q' for 'close-quote'
    //   'N' for 'no-open-quote'
    //   'n' for 'no-close-quote'
    //   'u' for 'url()', that we don't support
    //   'z' for unsupported tokens, like gradient()...
    //   '$' (at start) this content needs post processing before
    //       being applied to a node's style (needed with quotes,
    //       to get the correct char for the current nested level).
    // Note: this parsing might not be super robust with
    // convoluted declarations...
    parsed_content.clear();
    const char * orig_pos = str;
    // The presence of a single 'none' or 'normal' among multiple
    // values make the whole thing 'none'.
    bool has_none = false;
    bool needs_processing_when_applying = false;
    while ( skip_spaces( str ) && *str!=';' && *str!=stop_char && *str!='!' ) {
        if ( substr_icompare("none", str) ) {
            has_none = true;
            continue; // continue parsing
        }
        else if ( substr_icompare("normal", str) ) {
            // Computes to 'none' for pseudo elements
            has_none = true;
            continue; // continue parsing
        }
        else if ( substr_icompare("open-quote", str) ) {
            parsed_content << U'Q';
            needs_processing_when_applying = true;
            continue;
        }
        else if ( substr_icompare("close-quote", str) ) {
            parsed_content << U'q';
            needs_processing_when_applying = true;
            continue;
        }
        else if ( substr_icompare("no-open-quote", str) ) {
            parsed_content << U'N';
            needs_processing_when_applying = true;
            continue;
        }
        else if ( substr_icompare("no-close-quote", str) ) {
            parsed_content << U'n';
            needs_processing_when_applying = true;
            continue;
        }
        else if ( substr_icompare("attr", str) ) {
            if ( *str == '(' ) {
                str++;
                skip_spaces( str );
                lString8 attr8;
                while ( *str && *str!=')' ) {
                    attr8 << *str;
                    str++;
                }
                if ( *str == ')' ) {
                    str++;
                    lString32 attr = Utf8ToUnicode(attr8);
                    attr.trim();
                    parsed_content << U'a';
                    parsed_content << lChar32(attr.length() + 1); // (+1 to avoid storing \x00)
                    parsed_content << attr;
                    continue;
                }
                // No closing ')': invalid
            }
        }
        else if ( substr_icompare("url", str) ) {
            // Unsupported for now, but parse it
            has_unsupported = true;
            if ( *str == '(' ) {
                str++;
                skip_spaces( str );
                lString8 url8;
                while ( *str && *str!=')' ) {
                    url8 << *str;
                    str++;
                }
                if ( *str == ')' ) {
                    str++;
                    parsed_content << U'u';
                    continue;
                }
                // No closing ')': invalid
            }
        }
        else if ( *str == '"' || *str == '\'' ) {
            // https://developer.mozilla.org/en-US/docs/Web/CSS/string
            // https://www.w3.org/TR/CSS2/syndata.html#strings
            // https://drafts.csswg.org/css-values-3/#strings
            char quote_ch = *str;
            str++;
            lString8 str8; // quoted string content (as UTF8, like original stylesheet)
            while ( *str && *str != quote_ch ) {
                if ( *str == '\\' ) {
                    // https://www.w3.org/TR/CSS2/syndata.html#characters
                    str++;
                    if ( hexDigit(*str) >= 0 ) {
                        lUInt32 codepoint = 0;
                        int num_digits = 0;
                        while ( num_digits < 6 ) {
                            int v = hexDigit(*str);
                            if ( v >= 0 ) {
                                codepoint = (codepoint << 4) + v;
                                num_digits++;
                                str++;
                                continue;
                            }
                            // Not a hex digit
                            break;
                        }
                        if ( num_digits < 6 && *str == ' ' ) // skip space following a non-6-hex-digits
                            str++;
                        if ( codepoint == 0 || codepoint > 0x10FFFF ) {
                            // zero not allowed, and should be under max valid unicode codepoint
                            codepoint = 0xFFFD; // replacement character
                        }
                        // Serialize it as UTF-8
                        lString32 c;
                        c << (lChar32)codepoint;
                        str8 << UnicodeToUtf8(c);
                    }
                    else if ( *str == '\r' && *(str+1) == '\n' ) {
                        // Ignore \ at end of CRLF line
                        str += 2;
                    }
                    else if ( *str == '\n' ) {
                        // Ignore \ at end of line
                        str++;
                    }
                    else {
                        // Accept next char as is
                        str8 << *str;
                        str++;
                    }
                }
                else {
                    str8 << *str;
                    str++;
                }
                // todo:
                // https://www.w3.org/TR/CSS2/syndata.html#parsing-errors
                // "User agents must close strings upon reaching the end
                // of a line (i.e., before an unescaped line feed, carriage
                // return or form feed character), but then drop the construct
                // (declaration or rule) in which the string was found."
            }
            if ( *str == quote_ch ) {
                lString32 str32 = Utf8ToUnicode(str8);
                parsed_content << U's';
                parsed_content << lChar32(str32.length() + 1); // (+1 to avoid storing \x00)
                parsed_content << str32;
                str++;
                continue;
            }
        }
        else {
            // Not supported
            has_unsupported = true;
            parsed_content << U'z';
            next_token(str, stop_char);
        }
    }
    if ( has_none ) {
        // Forget all other tokens parsed
        parsed_content.clear();
        parsed_content << U'X';
    }
    else if ( needs_processing_when_applying ) {
        parsed_content.insert(0, 1, U'$');
    }
    if (*str) // something (;, } or !important) follows
        return true;
    // Restore original position if we reach end of CSS string,
    // as it might just be missing a ')' or closing quote: we'll
    // be skipping up to next ; or }, and might manage with
    // the rest of the string.
    str = orig_pos;
    return false;
}

/// Update a style->content, post processed for its node
void update_style_content_property( css_style_rec_t * style, ldomNode * node ) {
    // We don't want to update too much: styles are hashed and shared by
    // multiple nodes. We don't resolve "attr()" here as attributes are
    // stable (and "attr(id)" would make all style->content different
    // and prevent styles from being shared, increasing the number
    // of styles to cache).
    // But we need to resolve quotes, according to their nesting level,
    // and transform them into a litteral string 's'.

    if ( style->content.empty() || style->content[0] != U'$' ) {
        // No update needed
        return;
    }

    // We need to know if this node is visible: if not, quotes nested
    // level should not be updated. We might want to still include
    // the computed quote (with quote char for level 1) for it to be
    // displayed by writeNodeEx() when displaying the HTML, even if
    // the node is invisible.
    bool visible = style->display != css_d_none;
    if ( visible ) {
        ldomNode * n = node->getParentNode();
        for ( ; !n->isRoot(); n = n->getParentNode() ) {
            if ( n->getStyle()->display == css_d_none ) {
                visible = false;
                break;
            }
        }
    }

    // We do not support specifying quote chars to be used via CSS "quotes":
    //     :root { quotes: '\201c' '\201d' '\2018' '\2019'; }
    // We use the ones hardcoded for the node lang tag language (or default
    // typography language) provided by TextLangCfg.
    // HTML5 default CSS specifies them with:
    //   :root:lang(af), :not(:lang(af)) > :lang(af) { quotes: '\201c' '\201d' '\2018' '\2019' }
    // This might (or not) implies that nested levels are reset when entering
    // text with another language, so this new language first level quote is used.
    // We can actually get that same behaviour by having each TextLangCfg manage
    // its own nesting level (which won't be reset when en>fr>en, though).
    // But all this is quite rare, so don't bother about it much.
    TextLangCfg * lang_cfg = TextLangMan::getTextLangCfg( node );

    // Note: some quote char like (U+201C / U+201D) seem to not be mirrored
    // (when using HarfBuzz) when added to some RTL arabic text. But it
    // appears that way with Firefox too!
    // But if we use another char (U+00AB / U+00BB), it gets mirrored correctly.
    // Might be that HarfBuzz first substitute it with arabic quotes (which
    // happen to look inverted), and then mirror that?

    lString32 res;
    lString32 parsed_content = style->content;
    lString32 quote;
    int i = 1; // skip initial '$'
    int parsed_content_len = parsed_content.length();
    while ( i < parsed_content_len ) {
        lChar32 ctype = parsed_content[i];
        if ( ctype == 's' ) { // literal string: copy as-is
            lChar32 len = parsed_content[i] - 1; // (remove added +1)
            res.append(parsed_content, i, len+2);
            i += len+2;
        }
        else if ( ctype == 'a' ) { // attribute value: copy as-is
            lChar32 len = parsed_content[i] - 1; // (remove added +1)
            res.append(parsed_content, i, len+2);
            i += len+2;
        }
        else if ( ctype == 'Q' ) { // open-quote
            quote = lang_cfg->getOpeningQuote(visible);
            // length+1 as expected with 's' by get_applied_content_property()
            res << U's' << lChar32(quote.length() + 1) << quote;
            i += 1;
        }
        else if ( ctype == 'q' ) { // close-quote
            quote = lang_cfg->getClosingQuote(visible);
            // length+1 as expected with 's' by get_applied_content_property()
            res << U's' << lChar32(quote.length() + 1) << quote;
            i += 1;
        }
        else if ( ctype == 'N' ) { // no-open-quote
            // This should just increment nested quote level and output nothing.
            lang_cfg->getOpeningQuote(visible);
            i += 1;
        }
        else if ( ctype == 'n' ) { // no-close-quote
            // This should just increment nested quote level and output nothing.
            lang_cfg->getClosingQuote(visible);
            i += 1;
        }
        else {
            // All other stuff are single char (u, z, X) or unsupported/bogus char.
            res.append(parsed_content, i, 1);
            i += 1;
        }
    }
    // Replace style->content with what we built
    style->content = res;
}

/// Returns the computed value for a node from its parsed CSS "content:" value
lString32 get_applied_content_property( ldomNode * node ) {
    lString32 res;
    css_style_ref_t style = node->getStyle();
    lString32 parsed_content = style->content;
    if ( parsed_content.empty() )
        return res;
    int i = 0;
    int parsed_content_len = parsed_content.length();
    while ( i < parsed_content_len ) {
        lChar32 ctype = parsed_content[i++];
        if ( ctype == 's' ) { // literal string
            lChar32 len = parsed_content[i++] - 1; // (remove added +1)
            res << parsed_content.substr(i, len);
            i += len;
        }
        else if ( ctype == 'a' ) { // attribute value
            lChar32 len = parsed_content[i++] - 1; // (remove added +1)
            lString32 attr_name = parsed_content.substr(i, len);
            i += len;
            ldomNode * attrNode = node;
            if ( node->getNodeId() == el_pseudoElem ) {
                // For attributes, we should pick them from the parent of the added pseudo element
                attrNode = node->getUnboxedParent();
            }
            if ( attrNode )
                res << attrNode->getAttributeValue(attr_name.c_str());
        }
        else if ( ctype == 'u' ) { // url
            // Url to image: we can't easily support that, as our
            // image support needs a reference to a node, and we
            // don't have a node here.
            // Show a small square so one can see there's something
            // that is missing, something different enough from the
            // classic tofu char so we can distinguish it.
            // res << 0x25FD; // WHITE MEDIUM SMALL SQUARE
            res << 0x2B26; // WHITE MEDIUM DIAMOND
        }
        else if ( ctype == 'X' ) { // 'none'
            res.clear(); // should be standalone, but let's be sure
            break;
        }
        else if ( ctype == 'z' ) { // unsupported token
            // Just ignore it, don't show anything
        }
        else if ( ctype == 'Q' ) { // open-quote
            // Shouldn't happen: replaced earlier by update_style_content_property()
        }
        else if ( ctype == 'q' ) { // close-quote
            // Shouldn't happen: replaced earlier by update_style_content_property()
        }
        else if ( ctype == 'N' ) { // no-open-quote
            // Shouldn't happen: replaced earlier by update_style_content_property()
        }
        else if ( ctype == 'n' ) { // no-close-quote
            // Shouldn't happen: replaced earlier by update_style_content_property()
        }
        else { // unexpected
            break;
        }
    }
    if ( style->white_space < css_ws_pre_line ) {
        // Remove consecutive spaces (although this might be handled well by
        // lvtextfm) and '\n' - but we should keep leading and trailing spaces.
        res.trimDoubleSpaces(true, true, false);
    }
    return res;
}

static void resolve_url_path( lString8 & str, lString32 codeBase ) {
    // A URL (path to local or container's file) must be resolved
    // at parsing time, as it is related to this stylesheet file
    // path (and not to the HTML files that are linking to this
    // stylesheet) - it wouldn't be possible to resolve it later.
    lString32 path = Utf8ToUnicode(str);
    path.trim();
    if (path.startsWithNoCase(lString32("url"))) path = path.substr(3);
    path.trim();
    if (path.startsWith(U"(")) path = path.substr(1);
    if (path.endsWith(U")")) path = path.substr(0, path.length() - 1);
    path.trim();
    if (path.startsWith(U"\"") || path.startsWith(U"'")) path = path.substr(1);
    if (path.endsWith(U"\"") || path.endsWith(U"'")) path = path.substr(0, path.length() - 1);
    path.trim();
    if (path.startsWith(lString32("data:image"))) {
        // base64 encoded image: leave as-is
    }
    else {
        // We assume it's a path to a local file in the container, so we don't try
        // to check if it's a remote url (as we can't fetch its content anyway).
        if ( !codeBase.empty() ) {
            path = LVCombinePaths( codeBase, path );
        }
    }
    // printf("url: [%s]+%s => %s\n", UnicodeToLocal(codeBase).c_str(), str.c_str(), UnicodeToUtf8(path).c_str());
    str = UnicodeToUtf8(path);
}

enum css_atrule_keyword {
    css_atkw_and,
    css_atkw_or,
    css_atkw_not,
    css_atkw_unknown
};
static const char * css_atrule_keyword_name[] = {
    "and",
    "or",
    "not",
    NULL
};

// Base class for AtSupportsLogicalConditionParser and AtMediaLogicalConditionParser
class AtRuleLogicalConditionParser {
protected:
    // Nested levels' current result and operand (and, or)
    LVArray<bool> result;
    LVArray<bool> negated;
    LVArray<int> operand;
    int level;
    bool malformed;
    lxmlDocBase * doc;
    lChar32 stop_char;
    lChar32 stop_char2;
    AtRuleLogicalConditionParser(lxmlDocBase * d, char stopchar='{', char stopchar2=0)
    : doc(d), stop_char(stopchar), stop_char2(stopchar2) {
        malformed = false;
        level = -1;
        enterLevel(); // first level
    }
    ~AtRuleLogicalConditionParser() {}
    void setResult(bool res) {
        result[level] = res;
    }
    void enterLevel() {
        level++;
        result.add(true); // True if nothing
        negated.add(false);
        operand.add(css_atkw_unknown); // not known until after first element
    }
    void leaveLevel() {
        bool res = result.remove(level);
        bool neg = negated.remove(level);
        if (neg)
            res = !res;
        level--;
        if (level >= 0) {
            // Still an active level: update it with result of sublevel
            int op = operand[level];
            if ( op == css_atkw_unknown ) {
                // No operand = first element = use its result
                result[level] = res;
            }
            else if ( op == css_atkw_and ) {
                if (!res)
                    result[level] = false;
            }
            else if ( op == css_atkw_or ) {
                if (res)
                    result[level] = true;
            }
        }
    }
    virtual void parseCondition(const char * &str) {
        // Dummy implementation to be overidden by subclasses
        setResult( true );
        while ( *str && *str != ')' && *str != stop_char )
            str++;
    }
public:
    bool getResult() {
        if ( level != 0 ) // invalid if we didn't get back to 0
            return false;
        if ( malformed )
            return false;
        bool res = result.remove(0);
        bool neg = negated.remove(0);
        if (neg)
            res = !res;
        return res;
    }
    void parse(const char * &str) {
        // This is a generic parser which should work with @supports and @media.
        // We don't check the full grammar validity (we'll wrongly accept "and and not and"),
        // but we should handle properly grammatically correct expressions.
        while (true) {
            skip_spaces(str);
            if ( !*str || *str == stop_char || *str == stop_char2 )
                break;
            if ( *str == '(' ) {
                enterLevel();
                str++;
                continue;
            }
            if ( *str == ')' ) {
                leaveLevel();
                str++;
                continue;
            }
            int name = parse_name( str, css_atrule_keyword_name, css_atkw_unknown);
            if ( name == css_atkw_not ) {
                negated[level] = true;
                continue;
            }
            else if ( name == css_atkw_and ) {
                if ( operand[level] == css_atkw_unknown )
                    operand[level] = css_atkw_and;
                else if ( operand[level] != css_atkw_and )
                    malformed = true;
                continue;
            }
            else if ( name == css_atkw_or ) {
                if ( operand[level] == css_atkw_unknown )
                    operand[level] = css_atkw_or;
                else if ( operand[level] != css_atkw_or )
                    malformed = true;
                continue;
            }
            else if ( name != css_atkw_unknown ) {
                // Skip unhandled known keyword
                continue;
            }
            // Uknown keyword: must be what we need to validate
            parseCondition(str);
        }
    }
};

// https://drafts.csswg.org/css-conditional/#at-supports
class AtSupportsLogicalConditionParser : public AtRuleLogicalConditionParser {
public:
    AtSupportsLogicalConditionParser(lxmlDocBase * d)
        : AtRuleLogicalConditionParser(d, '{', 0) {}
protected:
    virtual void parseCondition(const char * &str) {
        // Use our regular declaration parser to see if supported
        LVCssDeclaration tmp_decl;
        setResult( tmp_decl.parseAndCheckIfSupported( str, doc ) );
    }
};

static bool check_at_supports_condition( const char * &str, lxmlDocBase * doc )
{
    AtSupportsLogicalConditionParser parser(doc);
    parser.parse(str);
    return parser.getResult();
}

// https://developer.mozilla.org/en-US/docs/Web/CSS/Media_Queries/Using_media_queries
// https://www.w3.org/TR/css3-mediaqueries/#media0
enum css_atmedia_code {
    css_atmedia_width,
    css_atmedia_min_width,
    css_atmedia_max_width,
    css_atmedia_height,
    css_atmedia_min_height,
    css_atmedia_max_height,
    css_atmedia_aspect_ratio,
    css_atmedia_min_aspect_ratio,
    css_atmedia_max_aspect_ratio,
    css_atmedia_orientation,
    css_atmedia_device_width,
    css_atmedia_min_device_width,
    css_atmedia_max_device_width,
    css_atmedia_device_height,
    css_atmedia_min_device_height,
    css_atmedia_max_device_height,
    css_atmedia_device_aspect_ratio,
    css_atmedia_min_device_aspect_ratio,
    css_atmedia_max_device_aspect_ratio,
    css_atmedia_resolution,
    css_atmedia_min_resolution,
    css_atmedia_max_resolution,
    css_atmedia_color,
    css_atmedia_min_color,
    css_atmedia_max_color,
    css_atmedia_monochrome,
    css_atmedia_grid,
    css_atmedia_scripting,
    css_atmedia_update,
    css_atmedia_overflow_inline,
    css_atmedia_overflow_block,
    css_atmedia_unknown
};

static const char * css_atmedia_name[] = {
    // These are related to the size of the page (in CSS pixels, which may
    // not be screen pixels), excluding crengine margins. They will change
    // a lot when switching between single and 2-pages mode).
    "width",
    "min-width",
    "max-width",
    "height",
    "min-height",
    "max-height",
    "aspect-ratio",
    "min-aspect-ratio",
    "max-aspect-ratio",
    "orientation",

    // These are related to the size of the screen (in CSS pixels, which may
    // not be screen pixels)
    "device-width",
    "min-device-width",
    "max-device-width",
    "device-height",
    "min-device-height",
    "max-device-height",
    "device-aspect-ratio",
    "min-device-aspect-ratio",
    "max-device-aspect-ratio",

    // These are to be compared to gRenderDPI
    "resolution",
    "min-resolution",
    "max-resolution",

    "color",            // We don't know the buffer type when loading a document,
    "min-color",        // so we can't be accurate: we'll pretend for now we have 16M colors
    "max-color",
    "monochrome",       // For now, we'll say we're not monochrome, even on eInk
    "grid",             // false, we're not a tty terminal with a fixed font
    "scripting",        // "none", we don't support scripting
    "update",           // "none", "Once it has been rendered, the layout can no longer be updated"
    "overflow-inline",  // "none", no horizontal scrolling
    "overflow-block",   // "paged", which is our main purpose (even if we have a scroll mode)

    // Not implemented:
    // "color-index",
    // "min-color-index",
    // "max-color-index",
    NULL
};

class AtMediaLogicalConditionParser : public AtRuleLogicalConditionParser {
public:
    AtMediaLogicalConditionParser(lxmlDocBase * d, char stop_char='{')
        : AtRuleLogicalConditionParser(d, stop_char, ',') {}
            // stop on a ',', which is another independant media condition (to be OR'ed)
protected:
    virtual void parseCondition(const char * &str) {
        setResult( false ); // until proven guilty of truth
        skip_spaces( str );
        int name = parse_name(str, css_atmedia_name, css_atmedia_unknown);
        if ( name == css_atmedia_unknown ) {
            // Property unknown
            skip_to_next( str, 0, ')' );
            return;
        }
        skip_spaces( str );
        bool use_default_value = false;
        if ( *str == ':' ) { // some value follows
            str++; // skip it
            skip_spaces( str );
        }
        else if ( *str == ')' ) {
            // For some media features, a value is optional
            switch ( name ) {
                case css_atmedia_width:
                case css_atmedia_height:
                case css_atmedia_device_width:
                case css_atmedia_device_height:
                case css_atmedia_resolution:
                case css_atmedia_color:
                case css_atmedia_monochrome:
                case css_atmedia_grid:
                    use_default_value = true;
                    break;
                default: // invalid
                    return;
            }
        }
        else { // unexpected
            skip_to_next( str, 0, ')' );
            return;
        }
        // Property known, parse value and check
        switch ( name ) {
            case css_atmedia_width:
            case css_atmedia_min_width:
            case css_atmedia_max_width:
            case css_atmedia_height:
            case css_atmedia_min_height:
            case css_atmedia_max_height:
            case css_atmedia_device_width:
            case css_atmedia_min_device_width:
            case css_atmedia_max_device_width:
            case css_atmedia_device_height:
            case css_atmedia_min_device_height:
            case css_atmedia_max_device_height:
                {
                    if ( ! doc )
                        break; // No doc provided: not able to check
                    if ( use_default_value ) {
                        setResult( true ); // these are always > 0
                        break;
                    }
                    css_length_t val;
                    if ( parse_number_value(str, val, false) ) {
                        int size = lengthToPx(doc->getRootNode(), val, 0);
                        // This CSS length has been scaled according to gRenderDPI from CSS pixels to screen pixels,
                        // so we can compare it with the viewport or screen width/height which are in screen pixels
                        if ( name <= css_atmedia_max_width ) {
                            int viewport_width = ((ldomDocument*)doc)->getPageWidth();
                            if ( name == css_atmedia_width && viewport_width == size )
                                setResult( true );
                            if ( name == css_atmedia_min_width && viewport_width >= size )
                                setResult( true );
                            if ( name == css_atmedia_max_width && viewport_width <= size )
                                setResult( true );
                        }
                        else if ( name <= css_atmedia_max_height ) {
                            int viewport_height = ((ldomDocument*)doc)->getPageHeight();
                            if ( name == css_atmedia_height && viewport_height == size )
                                setResult( true );
                            if ( name == css_atmedia_min_height && viewport_height >= size )
                                setResult( true );
                            if ( name == css_atmedia_max_height && viewport_height <= size )
                                setResult( true );
                        }
                        else if ( name <= css_atmedia_max_device_width ) {
                            int screen_width = ((ldomDocument*)doc)->getScreenWidth();
                            if ( name == css_atmedia_device_width && screen_width == size )
                                setResult( true );
                            if ( name == css_atmedia_min_device_width && screen_width >= size )
                                setResult( true );
                            if ( name == css_atmedia_max_device_width && screen_width <= size )
                                setResult( true );
                        }
                        else {
                            int screen_height = ((ldomDocument*)doc)->getScreenHeight();
                            if ( name == css_atmedia_device_height && screen_height == size )
                                setResult( true );
                            if ( name == css_atmedia_min_device_height && screen_height >= size )
                                setResult( true );
                            if ( name == css_atmedia_max_device_height && screen_height <= size )
                                setResult( true );
                        }
                    }
                }
                break;
            case css_atmedia_aspect_ratio:
            case css_atmedia_min_aspect_ratio:
            case css_atmedia_max_aspect_ratio:
            case css_atmedia_device_aspect_ratio:
            case css_atmedia_min_device_aspect_ratio:
            case css_atmedia_max_device_aspect_ratio:
                {
                    if ( ! doc )
                        break; // No doc provided: not able to check
                    // The value may be a fraction like "4/3"
                    int num;
                    int den = 1;
                    if ( parse_integer(str, num) && num > 0 ) {
                        skip_spaces(str);
                        if ( *str == '/' ) {
                            str++;
                            skip_spaces(str);
                            if ( parse_integer(str, den) ) {
                                if ( den <= 0 )
                                    den = 1;
                            }
                        }
                        int ratio = 1000 * num / den;
                        if ( name <= css_atmedia_max_aspect_ratio ) {
                            int viewport_ratio = 1000 * ((ldomDocument*)doc)->getPageWidth() / ((ldomDocument*)doc)->getPageHeight();
                            if ( name == css_atmedia_aspect_ratio && viewport_ratio == ratio )
                                setResult( true );
                            if ( name == css_atmedia_min_aspect_ratio && viewport_ratio >= ratio )
                                setResult( true );
                            if ( name == css_atmedia_max_aspect_ratio && viewport_ratio <= ratio )
                                setResult( true );
                        }
                        else {
                            int screen_ratio = 1000 * ((ldomDocument*)doc)->getScreenWidth() / ((ldomDocument*)doc)->getScreenHeight();
                            if ( name == css_atmedia_device_aspect_ratio && screen_ratio == ratio )
                                setResult( true );
                            if ( name == css_atmedia_min_device_aspect_ratio && screen_ratio >= ratio )
                                setResult( true );
                            if ( name == css_atmedia_max_device_aspect_ratio && screen_ratio <= ratio )
                                setResult( true );
                        }
                    }
                }
                break;
            case css_atmedia_orientation:
                {
                    if ( ! doc )
                        break; // No doc provided: not able to check
                    char ident[16];
                    if ( parse_ident(str, ident, 16) ) {
                        bool is_portrait = ((ldomDocument*)doc)->getPageHeight() >= ((ldomDocument*)doc)->getPageWidth();
                        lString8 orientation(ident);
                        orientation.lowercase();
                        if ( orientation == "portrait" && is_portrait )
                            setResult( true );
                        else if ( orientation == "landscape" && !is_portrait )
                            setResult( true );
                    }
                }
                break;
            case css_atmedia_resolution:
            case css_atmedia_min_resolution:
            case css_atmedia_max_resolution:
                {
                    if ( ! doc )
                        break; // No doc provided: not able to check
                    if ( use_default_value ) {
                        setResult( true ); // resolution is always > 0
                        break;
                    }
                    int num;
                    if ( parse_integer(str, num) ) {
                        int dpi = -1;
                        if ( substr_icompare( "dpi", str ) ) {
                            dpi = num;
                        }
                        else if ( substr_icompare( "dpcm", str ) ) {
                            dpi = num * 2.54;
                        }
                        if ( dpi >= 0 ) {
                            if ( name == css_atmedia_resolution && gRenderDPI == dpi )
                                setResult( true );
                            if ( name == css_atmedia_min_resolution && gRenderDPI >= dpi )
                                setResult( true );
                            if ( name == css_atmedia_max_resolution && gRenderDPI <= dpi )
                                setResult( true );
                        }
                    }
                }
                break;
            case css_atmedia_color:
            case css_atmedia_min_color:
            case css_atmedia_max_color:
            case css_atmedia_monochrome:
                {
                    // For now, pretend we have 8 bits per color (24/32 bpp)
                    int colors = 8;
                    int value;
                    if ( use_default_value ) { // "other than zero" per specs
                        if ( name == css_atmedia_color )
                            setResult( true );
                        else if ( name == css_atmedia_monochrome )
                            setResult( false );
                    }
                    else if ( parse_integer(str, value) ) {
                        if ( name == css_atmedia_color && colors == value )
                            setResult( true );
                        if ( name == css_atmedia_min_color && colors >= value )
                            setResult( true );
                        if ( name == css_atmedia_max_color && colors <= value )
                            setResult( true );
                        if ( name == css_atmedia_monochrome )
                            setResult( false );
                    }
                }
                break;
            case css_atmedia_grid:
                {
                    int value;
                    if ( use_default_value ) // "other than zero" per specs
                        setResult( false );
                    else if ( parse_integer(str, value) && value == 0 )
                        setResult( true );
                }
                break;
            case css_atmedia_scripting:
            case css_atmedia_update:
            case css_atmedia_overflow_inline:
                {
                    char ident[8];
                    if ( parse_ident(str, ident, 8) && lString8(ident).lowercase() == "none" )
                        setResult( true );
                }
                break;
            case css_atmedia_overflow_block:
                {
                    char ident[8];
                    if ( parse_ident(str, ident, 8) && lString8(ident).lowercase() == "paged" )
                        setResult( true );
                }
                break;
            default:
                break;
        }
        skip_to_next( str, 0, ')' );
    }
};

static bool check_at_media_condition( const char * &str, lxmlDocBase * doc, char stop_char='{' )
{
    bool final_res = false;
    // Multiple conditions can be separated by ',' (which means 'or')
    while (true) {
        skip_spaces(str);
        bool res = true; // if no media type condition
        bool negated = false;
        // First, parse the initial media "type"
        char ident[8];
        while ( parse_ident( str, ident, 8 ) ) { // parse keywords
            lString8 keyword(ident);
            keyword.lowercase();
            if ( keyword == "not" ) {
                negated = true;
            }
            else if ( keyword == "only" || keyword == "and" ) {
                // No meaning, or (condition) follows
            }
            else if ( keyword == "all" || keyword == "screen" ) {
                // Supported media type: "screen" only
                // "print" is "intended for paged material and documents viewed on a screen
                // in print preview mode" which could be nice in our eReader context, but
                // some may conflict/override what is set in "screen"
            }
            else {
                res = false; // unsupported media type
            }
            skip_spaces(str);
        }
        // Then, parse any condition(s) that follows
        if ( res && *str == '(' ) {
            AtMediaLogicalConditionParser parser(doc, stop_char);
            parser.parse(str);
            res = parser.getResult();
        }
        if ( negated )
            res = !res;
        if ( !final_res && res ) // multiple conditions are OR'ed
            final_res = true;
        if ( *str == ',' ) { // there is a next OR'ed condition
            str++;
            continue;
        }
        break;
    }
    return final_res;
}

enum css_atrule_code {
    css_at_charset,
    css_at_import,
    css_at_namespace,
    css_at_custom_selector,
    css_at_custom_media,
    css_at_media,
    css_at_supports,
    css_at_scope,
    css_at_document,
    css_at_keyframes,
    css_at_font_face,
    css_at_page,
    css_at_bottom_center,
    css_at_bottom_left,
    css_at_bottom_left_corner,
    css_at_bottom_right,
    css_at_bottom_right_corner,
    css_at_left_bottom,
    css_at_left_center,
    css_at_left_middle,
    css_at_left_top,
    css_at_right_bottom,
    css_at_right_bottom_corner,
    css_at_right_middle,
    css_at_right_top,
    css_at_top_center,
    css_at_top_left,
    css_at_top_left_corner,
    css_at_top_right,
    css_at_top_right_corner,
    css_at_font_feature_values,
    css_at_annotation,
    css_at_character_variant,
    css_at_ornaments,
    css_at_styleset,
    css_at_stylistic,
    css_at_swash,
    css_at_color_profile,
    css_at_counter_style,
    css_at_property,
    css_at_viewport,
    css_at_unknown
};

static const char * css_atrule_name[] = {
    // Supported ones are marked with '// +', unsupported ones with '// -'
    // These ones are followed by some text up to ';'. No followup CSS block
    "charset",   // - we expect UTF-8
    "import",    // - (@import at start of stylesheet are handled elsewhere, @import after start is invalid and should be ignored)
    "namespace", // - we don't handle prefixes and namespaces, all elements and CSS selectors are/target a single global namespace
    "custom-selector", // - not supported
    "custom-media",    // - not supported

    // These ones are followed by some prelude and then a CSS block which is a full stylesheet
    "media",     // + media query
    "supports",  // + query for CSS property name:value support
    "scope",     // - Scoping (followup stylesheet applies only in a scope): not supported
    "document",  // - (deprecated) match url or filename
    "keyframes", // - CSS animations (not really a stylesheet, but looks like one with 'from' and 'to' selectors)

    // These ones may or may not have a prelude, and then a CSS block which is only a declaration (and
    // not a full stylesheet as a list of selectors+declarations) which may contain other at-rules
    // All of these are unsupported
    "font-face", // - already quickly parsed in epubfmt.cpp, ignored at this point
    "page",      // - CSS properties used for printing (usually to set larger margins)
        "bottom-center",      // - sub-at-rules allowed inside @page { }
        "bottom-left",        //   The list of names differ among specifications,
        "bottom-left-corner", //   and some are redundant, but let's have them all.
        "bottom-right",
        "bottom-right-corner",
        "left-bottom",
        "left-center",
        "left-middle",
        "left-top",
        "right-bottom",
        "right-bottom-corner",
        "right-middle",
        "right-top",
        "top-center",
        "top-left",
        "top-left-corner",
        "top-right",
        "top-right-corner",
    "font-feature-values", // - used to set nickname for font-variant-alternates
        "annotation",      // - sub-at-rules allowed inside @font-feature-values { }
        "character-variant",
        "ornaments",
        "styleset",
        "stylistic",
        "swash",
    "color-profile", // - for CSS color() function
    "counter-style", // - custom styling of UL list items
    "property",      // - CSS custom properties definitions
    "viewport",      // - (deprecated) specifies viewport to use
    NULL
};

// Uncomment for debugging @rules processing
// #define DEBUG_AT_RULES_PROCESSING

/// Parse (or skip) @keyword rule
static bool parse_or_skip_at_rule( const char * &str, lxmlDocBase * doc )
{
    // https://developer.mozilla.org/en-US/docs/Web/CSS/At-rule
    // We only handle a few of them, and we may not parse according to the full complex
    // rules, but we hope the CSS we meet is not too twisted and hope for the best...
    if (!str || *str != '@')
        return false;
    #ifdef DEBUG_AT_RULES_PROCESSING
        const char * start = str;
    #endif
    str++; // skip '@'
    int name = parse_name( str, css_atrule_name, css_at_unknown);
    skip_spaces(str);

    // At-rules can have different followup content and kind of CSS blocks
    // See https://github.com/tabatkins/parse-css for a list
    bool skip_to_next_semi_colon = false;
    bool has_nested_stylesheet = false;
    bool has_nested_declaration = false;
    if ( name == css_at_import ) {
        // We could check the condition, and read and include the file here,
        // but @import, per specs, is only valid at start of CSS content, which
        // is handled (with media conditions checked) by LVProcessStyleSheetImport()
        // and its callers. So, just skip any one met out of the allowed context.
        skip_to_next_semi_colon = true;
    }
    else if ( name <= css_at_custom_media ) { // @charset, @namespace, @custom-selector, @custom-media
        skip_to_next_semi_colon = true;
    }
    else if ( name <= css_at_keyframes ) { // @media, @supports, @scope, @document, @keyframes
        has_nested_stylesheet = true;
    }
    else if ( name < css_at_unknown ) { // @font-face, @page, and all others
        has_nested_declaration = true;
    }
    else {
        // Not known, we don't know what to expect, try to guess
        const char * tmp = str;
        skip_to_next( tmp, ';', '{');
        if ( *tmp != '{' ) { // probably stop_char_to_skip=';' met
            skip_to_next_semi_colon = true;
        }
        else { // on '{'
            tmp++;
            skip_to_next( tmp, ';', '{');
            if ( *tmp != '{' ) // probably stop_char_to_skip=';' met before a '{'
                has_nested_declaration = true;
            else // '{' + some selectors without any semicolon + '{': looks like a stylesheet
                has_nested_stylesheet = true;
        }
    }

    if ( skip_to_next_semi_colon ) {
        skip_to_next( str, ';', 0);
        #ifdef DEBUG_AT_RULES_PROCESSING
            printf("-; %.*s\n", str-start > 60 ? 60 : str-start, start);
        #endif
        return true;
    }

    // We don't support mostly anything but @media and @supports, so skip their block
    bool process_nested_block = false;
    if ( name == css_at_media ) {
        process_nested_block = check_at_media_condition( str, doc );
    }
    else if ( name == css_at_supports ) {
        process_nested_block = check_at_supports_condition( str, doc );
    }

    // Whether checked or not, move to next '{'
    skip_to_next( str, ';', '{' );
    if ( *str != '{' ) // not the expected block start
        return false;

    if ( process_nested_block ) {
        #ifdef DEBUG_AT_RULES_PROCESSING
            printf("++ %.*s\n", str-start, start);
        #endif
        str++; // skip opening '{'
        // Just go on reading normally: we'll be erroring on the unexpected closing '}'
        // but we'll just skip it with skip_until_end_of_rule()
        return true;
    }

    // Skip nested block
    #ifdef DEBUG_AT_RULES_PROCESSING
        printf("-%c %.*s\n", (has_nested_stylesheet ? 's' : has_nested_declaration ? 'd' : '?'), str-start, start);
    #endif
    // We can't just skip until the next '}' as the block may have nested
    // declarations with '{ }'. We also don't want to count nested '{' and '}',
    // as some of them may be inside "::before { content: '{'; }".
    // The safest way to handle all the edge cases is to just parse what's coming
    // with our regular stylesheet and declaration parsers, and trash what we parsed.
    if ( has_nested_stylesheet ) {
        LVStyleSheet tmp_stylesheet(doc, true); // nested=true :
        tmp_stylesheet.parseAndAdvance( str );  // will stop after first unbalanced '}'
        tmp_stylesheet.clear(); // trash what was parsed
    }
    else if ( has_nested_declaration ) {
        LVCssDeclaration tmp_decl;
        tmp_decl.parse( str, false, doc );
    }
    #ifdef DEBUG_AT_RULES_PROCESSING
        printf("     done at [...%.*s]%.*s...\n", 10, str-10, 10, str);
    #endif
    skip_spaces(str);
    return true;
}


// The order of items in following tables should match the order in the enums in include/cssdef.h
static const char * css_d_names[] = 
{
    "inherit",
    "ruby",
    "run-in",
    "inline",
    "block",
    "-cr-list-item-final", // non-standard, legacy crengine rendering of list items as final: css_d_list_item_legacy
    "list-item",           // correct rendering of list items as block: css_d_list_item_block
    "inline-block",
    "inline-table",
    "table", 
    "table-row-group", 
    "table-header-group", 
    "table-footer-group", 
    "table-row", 
    "table-column-group", 
    "table-column", 
    "table-cell", 
    "table-caption", 
    "none", 
    NULL
};

static const char * css_ws_names[] = 
{
    "inherit",
    "normal",
    "nowrap",
    "pre-line",
    "pre",
    "pre-wrap",
    "break-spaces",
    NULL
};

static const char * css_ta_names[] = 
{
    "inherit",
    "left",
    "right",
    "center",
    "justify",
    "start",
    "end",
    "auto",
    "-cr-left-if-not-first",
    "-cr-right-if-not-first",
    "-cr-center-if-not-first",
    "-cr-justify-if-not-first",
    "-cr-start-if-not-first",
    "-cr-end-if-not-first",
    NULL
};

static const char * css_td_names[] = 
{
    "inherit",
    "none",
    "underline",
    "overline",
    "line-through",
    "blink",
    NULL
};

static const char * css_tt_names[] =
{
    "inherit",
    "none",
    "uppercase",
    "lowercase",
    "capitalize",
    "full-width",
    NULL
};

// All these css_hyph_names* should map original properties in this order:
//  1st value: inherit
//  2nd value: no hyphenation
//  3rd value: use the hyphenation method set to HyphMan
// See https://github.com/readium/readium-css/blob/master/docs/CSS21-epub_compat.md
// for documentation about the obscure properties.
//
// For "hyphens:", "hyphenate:" (fb2? also used by obsoleted css files)
// No support for "hyphens: manual" (this would involve toggling the hyphenation
// method from what it is set to SoftHyphensHyph locally)
static const char * css_hyph_names[] = 
{
    "inherit",
    "none",
    "auto",
    NULL
};
// For "adobe-text-layout:" (for documents made for Adobe RMSDK)
static const char * css_hyph_names2[] =
{
    "inherit",
    "optimizespeed",
    "optimizequality",
    NULL
};
// For "adobe-hyphenate:"
static const char * css_hyph_names3[] =
{
    "inherit",
    "none",
    "explicit", // this may wrong, as it's supposed to be like "hyphens: manual"
    NULL
};

static const char * css_pb_names[] =
{
    "inherit",
    "auto",
    "avoid", // those after this one are not supported by page-break-inside
    "always",
    "left",
    "right",
    "page",
    "recto",
    "verso",
    NULL
};

static const char * css_fs_names[] = 
{
    "inherit",
    "normal",
    "italic",
    "oblique",
    NULL
};

static const char * css_fw_names[] = 
{
    "inherit",
    "normal",
    "bold",
    "bolder",
    "lighter",
    "100",
    "200",
    "300",
    "400",
    "500",
    "600",
    "700",
    "800",
    "900",
    NULL
};
static const char * css_va_names[] = 
{
    "inherit",
    "baseline", 
    "sub",
    "super",
    "top",
    "text-top",
    "middle",
    "bottom",
    "text-bottom",
    NULL
};

static const char * css_ti_attribute_names[] =
{
    "hanging",
    NULL
};

static const char * css_ff_names[] =
{
    "inherit",
    "serif",
    "sans-serif",
    "cursive",
    "fantasy",
    "monospace",
    "math",
    "emoji",
    "fangsong",
    NULL
};

static const char * css_lst_names[] =
{
    "inherit",
    "disc",
    "circle",
    "square",
    "decimal",
    "lower-roman",
    "upper-roman",
    "lower-alpha",
    "upper-alpha",
    "none",
    NULL
};

static const char * css_lsp_names[] =
{
    "inherit",
    "inside",
    "outside",
    "-cr-outside",
    NULL
};
///border style names
static const char * css_bst_names[]={
  "solid",
  "dotted",
  "dashed",
  "double",
  "groove",
  "ridge",
  "inset",
  "outset",
  "none",
  NULL
};
///border width value names
static const char * css_bw_names[]={
        "thin",
        "medium",
        "thick",
        "initial",
        "inherit",
        NULL
};

//background repeat names
static const char * css_bg_repeat_names[]={
        "repeat",
        "repeat-x",
        "repeat-y",
        "no-repeat",
        "initial",
        "inherit",
        NULL
};
//background attachment names
static const char * css_bg_attachment_names[]={
        "scroll",
        "fixed",
        "local",
        "initial",
        "inherit",
        NULL
};
//background position names
static const char * css_bg_position_names[]={
        "left top", // 0
        "left center",
        "left bottom",
        "right top",
        "right center",
        "right bottom",
        "center top",
        "center center",
        "center bottom", // 8
        "top left", // 9
        "center left",
        "bottom left",
        "top right",
        "center right",
        "bottom right",
        "top center",
        "center center",
        "bottom center", // 17
        "center", // 18
        "left",
        "right",
        "top",
        "bottom",
        "initial", // 23
        "inherit", // 24
        NULL
};

//border-collpase names
static const char * css_bc_names[]={
        "separate",
        "collapse",
        "initial",
        "inherit",
        NULL
};

// orphans and widows values (supported only if in range 1-9)
// https://drafts.csswg.org/css-break-3/#widows-orphans
//   "Negative values and zero are invalid and must cause the declaration to be ignored."
static const char * css_orphans_widows_names[]={
        "inherit",
        "1",
        "2",
        "3",
        "4",
        "5",
        "6",
        "7",
        "8",
        "9",
        NULL
};

// float value names
static const char * css_f_names[] =
{
    "inherit",
    "none",
    "left",
    "right",
    NULL
};

// clear value names
static const char * css_c_names[] =
{
    "inherit",
    "none",
    "left",
    "right",
    "both",
    NULL
};

// direction value names
static const char * css_dir_names[] =
{
    "inherit",
    "unset",
    "ltr",
    "rtl",
    NULL
};

// visibility value names
static const char * css_v_names[] =
{
    "inherit",
    "visible",
    "hidden",
    "collapse",
    NULL
};

// line-break value names
static const char * css_lb_names[] =
{
    "inherit",
    "auto",
    "normal",
    "loose",
    "strict",
    "anywhere",
    "-cr-loose",
    NULL
};

// word-break value names
static const char * css_wb_names[] =
{
    "inherit",
    "normal",
    "break-word",
    "break-all",
    "keep-all",
    NULL
};

// box-sizing value names
static const char * css_bs_names[] =
{
    "inherit",
    "content-box",
    "border-box",
    NULL
};

static const char * css_cr_only_if_names[]={
        "any",
        "always",
        "never",
        "legacy",
        "enhanced",
        "float-floatboxes",
        "box-inlineboxes",
        "ensure-style-width",
        "ensure-style-height",
        "allow-style-w-h-absolute-units",
        "full-featured",
        "epub-document",
        "fb2-document",
        "html-document",
        "txt-document",
        "rtf-document",
        "chm-document",
        "doc-document",
        "docx-document",
        "odt-document",
        "pdb-document",
        "inline",
        "not-inline",
        "inpage-footnote",
        "not-inpage-footnote",
        "inside-inpage-footnote",
        "not-inside-inpage-footnote",
        NULL
};
enum cr_only_if_t {
    cr_only_if_any,    // always true, don't ignore
    cr_only_if_always, // always true, don't ignore
    cr_only_if_never,  // always false, do ignore
    cr_only_if_legacy,
    cr_only_if_enhanced,
    cr_only_if_float_floatboxes,
    cr_only_if_box_inlineboxes,
    cr_only_if_ensure_style_width,
    cr_only_if_ensure_style_height,
    cr_only_if_allow_style_w_h_absolute_units,
    cr_only_if_full_featured,
    cr_only_if_epub_document,
    cr_only_if_fb2_document, // fb2 or fb3
    cr_only_if_html_document,
    cr_only_if_txt_document,
    cr_only_if_rtf_document,
    cr_only_if_chm_document,
    cr_only_if_doc_document,
    cr_only_if_docx_document,
    cr_only_if_odt_document,
    cr_only_if_pdb_document,
    // The following ones are non-static: unlike previous ones which depend on the document
    // or rendering options and so we can only ignore the CSS declaration, the next ones
    // depend on the style of the node, and need to be saved and checked on each node (for
    // the rest of the declaration to be applied to this node, or skipped as a whole).
    // Note: the "not_xyz" ones are equivalent to "-xyz", and need to be just after
    // the one they negate/invert.
    // When mixing static and non-static -cr-only-if, put the static ones first.
    cr_only_if_inline,
    cr_only_if_not_inline,
    cr_only_if_inpage_footnote,
    cr_only_if_not_inpage_footnote,
    cr_only_if_inside_inpage_footnote,
    cr_only_if_not_inside_inpage_footnote,
};

bool LVCssDeclaration::parse( const char * &decl, bool higher_importance, lxmlDocBase * doc, lString32 codeBase )
{
    if ( !decl )
        return false;
    skip_spaces( decl );

    if ( !_check_if_supported ) {
        if ( *decl != '{' )
            return false;
        decl++;
    }
    // Normal declarations end with '}', while @supports checks end with ')'
    // (we keep checking for ';' with @supports, but we shouldn't meet any,
    // and we return after the first declaration checked)
    char stop_char = _check_if_supported ? ')' : '}';

    SerialBuf buf(512, true);

    bool ignoring = false;
    while ( *decl && *decl != stop_char ) {
        skip_spaces( decl );
        css_decl_code prop_code = parse_property_name( decl );
        if ( ignoring && prop_code != cssd_cr_only_if ) {
            // Skip until next -cr-only-if:
            next_property( decl );
            continue;
        }
        skip_spaces( decl );
        lString8 strValue;
        lUInt32 importance = higher_importance ? IMPORTANT_DECL_HIGHER : 0;
        lUInt32 parsed_important = 0; // for !important that may be parsed along the way
        bool skip_to_next_property = true; // to do when done (except if we've parsed a @rule)
        if (prop_code != cssd_unknown) {
            // parsed ok
            int n = -1;
            switch ( prop_code )
            {
            // non standard property to ignore declaration depending on gDOMVersionRequested
            case cssd_cr_ignore_if_dom_version_greater_or_equal:
                {
                    int dom_version;
                    if ( parse_integer( decl, dom_version ) ) {
                        if ( !doc || doc->getDOMVersionRequested() >= dom_version ) {
                            return false; // ignore the whole declaration
                        }
                    }
                    else { // ignore the whole declaration too if not an integer
                        return false;
                    }
                }
                break;
            // non standard property to only apply next properties if rendering option enabled
            case cssd_cr_only_if:
                {
                    // We may have multiple names, and they must all match
                    ignoring = false;
                    while ( *decl != ';' ) {
                        skip_spaces( decl );
                        bool invert = false;
                        if ( *decl == '-' ) {
                            invert = true;
                            decl++;
                        }
                        bool match = false;
                        int name = parse_name( decl, css_cr_only_if_names, -1 );
                        if ( name == cr_only_if_any || name == cr_only_if_always ) {
                            match = !invert;
                        }
                        else if ( name == cr_only_if_never ) {
                            match = invert;
                        }
                        else if ( name >= cr_only_if_inline ) {
                            // Non-static ones
                            match = true; // this one will be check on each node, and next properties need to be saved
                            if (invert) {
                                name++; // "-inline" is the same as "non-inline" which follows "inline"
                            }
                            if ( !ignoring ) {
                                // No static -cr-only-if prevents this non-static one to be checked
                                buf<<(lUInt32) (prop_code | importance | parsed_important | parse_important(decl));
                                buf<<(lUInt32) name;
                            }
                        }
                        else if ( !doc ) {
                            // Without a doc, we don't have access to any of the following properties
                            match = false;
                        }
                        else if ( name == cr_only_if_legacy ) {
                            match = BLOCK_RENDERING_D(doc, ENHANCED) == invert;
                        }
                        else if ( name == cr_only_if_enhanced ) {
                            match = BLOCK_RENDERING_D(doc, ENHANCED) != invert;
                        }
                        else if ( name == cr_only_if_float_floatboxes ) {
                            match = BLOCK_RENDERING_D(doc, FLOAT_FLOATBOXES) != invert;
                        }
                        else if ( name == cr_only_if_box_inlineboxes ) {
                            match = BLOCK_RENDERING_D(doc, BOX_INLINE_BLOCKS) != invert;
                        }
                        else if ( name == cr_only_if_ensure_style_width ) {
                            match = BLOCK_RENDERING_D(doc, ENSURE_STYLE_WIDTH) != invert;
                        }
                        else if ( name == cr_only_if_ensure_style_height ) {
                            match = BLOCK_RENDERING_D(doc, ENSURE_STYLE_HEIGHT) != invert;
                        }
                        else if ( name == cr_only_if_allow_style_w_h_absolute_units ) {
                            match = BLOCK_RENDERING_D(doc, ALLOW_STYLE_W_H_ABSOLUTE_UNITS) != invert;
                        }
                        else if ( name == cr_only_if_full_featured ) {
                            match = (doc->getRenderBlockRenderingFlags() == BLOCK_RENDERING_FULL_FEATURED) != invert;
                        }
                        else if ( name == cr_only_if_epub_document ) {
                            match = doc->getProps()->getIntDef(DOC_PROP_FILE_FORMAT_ID, doc_format_none) == doc_format_epub;
                            if (invert) {
                                match = !match;
                            }
                        }
                        else if ( name == cr_only_if_fb2_document ) {
                            int doc_format = doc->getProps()->getIntDef(DOC_PROP_FILE_FORMAT_ID, doc_format_none);
                            match = (doc_format == doc_format_fb2) || (doc_format == doc_format_fb3);
                            if (invert) {
                                match = !match;
                            }
                        }
                        else if ( name == cr_only_if_html_document ) {
                            match = doc->getProps()->getIntDef(DOC_PROP_FILE_FORMAT_ID, doc_format_none) == doc_format_html;
                            if (invert) {
                                match = !match;
                            }
                        }
                        else if ( name == cr_only_if_txt_document ) {
                            match = doc->getProps()->getIntDef(DOC_PROP_FILE_FORMAT_ID, doc_format_none) == doc_format_txt;
                            if (invert) {
                                match = !match;
                            }
                        }
                        else if ( name == cr_only_if_rtf_document ) {
                            match = doc->getProps()->getIntDef(DOC_PROP_FILE_FORMAT_ID, doc_format_none) == doc_format_rtf;
                            if (invert) {
                                match = !match;
                            }
                        }
                        else if ( name == cr_only_if_chm_document ) {
                            match = doc->getProps()->getIntDef(DOC_PROP_FILE_FORMAT_ID, doc_format_none) == doc_format_chm;
                            if (invert) {
                                match = !match;
                            }
                        }
                        else if ( name == cr_only_if_doc_document ) {
                            match = doc->getProps()->getIntDef(DOC_PROP_FILE_FORMAT_ID, doc_format_none) == doc_format_doc;
                            if (invert) {
                                match = !match;
                            }
                        }
                        else if ( name == cr_only_if_docx_document ) {
                            match = doc->getProps()->getIntDef(DOC_PROP_FILE_FORMAT_ID, doc_format_none) == doc_format_docx;
                            if (invert) {
                                match = !match;
                            }
                        }
                        else if ( name == cr_only_if_odt_document ) {
                            match = doc->getProps()->getIntDef(DOC_PROP_FILE_FORMAT_ID, doc_format_none) == doc_format_odt;
                            if (invert) {
                                match = !match;
                            }
                        }
                        else if ( name == cr_only_if_pdb_document ) {
                            match = doc->getProps()->getIntDef(DOC_PROP_FILE_FORMAT_ID, doc_format_none) == doc_format_pdb;
                            if (invert) {
                                match = !match;
                            }
                        }
                        else { // unknown option: ignore
                            match = false;
                        }
                        if ( !match ) {
                            ignoring = true;
                            break; // no need to look at others
                        }
                        skip_spaces( decl );
                    }
                }
                break;
            // non standard property for providing hints via style tweaks
            case cssd_cr_hint:
                {
                    // All values are mapped into a single style->cr_hint 31 bits bitmap
                    int hints = 0; // "none" = no hint
                    int nb_parsed = 0;
                    int nb_invalid = 0;
                    while ( *decl && *decl !=';' && *decl!=stop_char) {
                        // Details in crengine/include/cssdef.h (checks ordered by most likely to be seen)
                        if ( substr_icompare("none", decl) ) {
                            // Forget everything parsed previously, and prevent inheritance
                            hints = CSS_CR_HINT_NONE_NO_INHERIT;
                        }
                        else if ( substr_icompare("footnote-inpage", decl) )        hints |= CSS_CR_HINT_FOOTNOTE_INPAGE|CSS_CR_HINT_INSIDE_FOOTNOTE_INPAGE;
                        else if ( substr_icompare("non-linear", decl) )             hints |= CSS_CR_HINT_NON_LINEAR;
                        else if ( substr_icompare("non-linear-combining", decl) )   hints |= CSS_CR_HINT_NON_LINEAR_COMBINING;
                        else if ( substr_icompare("strut-confined", decl) )         hints |= CSS_CR_HINT_STRUT_CONFINED;
                        else if ( substr_icompare("cjk-tailored", decl) )           hints |= CSS_CR_HINT_CJK_TAILORED;
                        else if ( substr_icompare("fit-glyphs", decl) )             hints |= CSS_CR_HINT_FIT_GLYPHS;
                        else if ( substr_icompare("text-selection-skip", decl) )    hints |= CSS_CR_HINT_TEXT_SELECTION_SKIP;
                        else if ( substr_icompare("text-selection-inline", decl) )  hints |= CSS_CR_HINT_TEXT_SELECTION_INLINE;
                        else if ( substr_icompare("text-selection-block", decl) )   hints |= CSS_CR_HINT_TEXT_SELECTION_BLOCK;
                        else if ( substr_icompare("toc-level1", decl) )             hints |= CSS_CR_HINT_TOC_LEVEL1;
                        else if ( substr_icompare("toc-level2", decl) )             hints |= CSS_CR_HINT_TOC_LEVEL2;
                        else if ( substr_icompare("toc-level3", decl) )             hints |= CSS_CR_HINT_TOC_LEVEL3;
                        else if ( substr_icompare("toc-level4", decl) )             hints |= CSS_CR_HINT_TOC_LEVEL4;
                        else if ( substr_icompare("toc-level5", decl) )             hints |= CSS_CR_HINT_TOC_LEVEL5;
                        else if ( substr_icompare("toc-level6", decl) )             hints |= CSS_CR_HINT_TOC_LEVEL6;
                        else if ( substr_icompare("toc-ignore", decl) )             hints |= CSS_CR_HINT_TOC_IGNORE;
                        else if ( substr_icompare("noteref", decl) )                hints |= CSS_CR_HINT_NOTEREF;
                        else if ( substr_icompare("noteref-ignore", decl) )         hints |= CSS_CR_HINT_NOTEREF_IGNORE;
                        else if ( substr_icompare("footnote", decl) )               hints |= CSS_CR_HINT_FOOTNOTE;
                        else if ( substr_icompare("footnote-ignore", decl) )        hints |= CSS_CR_HINT_FOOTNOTE_IGNORE;
                        //
                        else if ( substr_icompare("late", decl) ) {
                            // "-cr-hint: late", unlike previous ones, does not set a flag to the nodes matched
                            // by the selector(s): it just sets a huge specifity to the selectors preceeeding
                            // this declaration. These selectors will then be checked late, after all the other
                            // ones that don't have this hint.
                            // This can be used to have "* {...}" (very low specificity, so checked and applied
                            // very early) checked after all others, to easily override styles. Using "!important"
                            // with the properties to be overridden styles is still required.
                            setExtraWeighted(true);
                            nb_invalid++; // as it is not to be saved into 'buf'
                        }
                        //
                        else if ( parse_important(decl) ) {
                            parsed_important = IMPORTANT_DECL_SET;
                            break; // stop looking for more
                        }
                        else { // unsupported or invalid named value
                            nb_invalid++;
                            // Walk over unparsed value, and continue checking
                            while (*decl && *decl !=' ' && *decl !=';' && *decl!=stop_char)
                                decl++;
                        }
                        nb_parsed++;
                        skip_spaces( decl );
                    }
                    if ( nb_parsed - nb_invalid > 0 ) { // at least one valid named value seen
                        buf<<(lUInt32) (prop_code | importance | parsed_important);
                        buf<<(lUInt32) css_val_unspecified; // len.type
                        buf<<(lUInt32) hints; // len.value
                        // (css_val_unspecified just says this value has no unit)
                    }
                }
                break;
            case cssd_display:
                n = parse_name( decl, css_d_names, -1 );
                if (n == css_d_list_item_block && doc && doc->getDOMVersionRequested() < 20180524) {
                    n = css_d_list_item_legacy; // legacy rendering of list-item
                }
                break;
            case cssd_white_space:
                n = parse_name( decl, css_ws_names, -1 );
                break;
            case cssd_text_align:
                n = parse_name( decl, css_ta_names, -1 );
                if ( n >= css_ta_auto ) // only accepted with text-align-last
                    n = -1;
                break;
            case cssd_text_align_last:
            case cssd_text_align_last2:
                prop_code = cssd_text_align_last;
                n = parse_name( decl, css_ta_names, -1 );
                break;
            case cssd_text_decoration:
            case cssd_text_decoration2:
                prop_code = cssd_text_decoration;
                n = parse_name( decl, css_td_names, -1 );
                break;
            case cssd_text_transform:
                n = parse_name( decl, css_tt_names, -1 );
                break;
            case cssd_hyphenate:
            case cssd_hyphenate2:
            case cssd_hyphenate3:
            case cssd_hyphenate4:
            case cssd_hyphenate5:
            case cssd_hyphenate6:
                prop_code = cssd_hyphenate;
                n = parse_name( decl, css_hyph_names, -1 );
                if ( n==-1 )
                    n = parse_name( decl, css_hyph_names2, -1 );
                if ( n==-1 )
                    n = parse_name( decl, css_hyph_names3, -1 );
                break;
            case cssd_page_break_before:
            case cssd_break_before:
                prop_code = cssd_page_break_before;
                n = parse_name( decl, css_pb_names, -1 );
                break;
            case cssd_page_break_inside:
            case cssd_break_inside:
                prop_code = cssd_page_break_inside;
                n = parse_name( decl, css_pb_names, -1 );
                // Only a subset of css_pb_names are accepted
                if (n > css_pb_avoid)
                    n = -1;
                break;
            case cssd_page_break_after:
            case cssd_break_after:
                prop_code = cssd_page_break_after;
                n = parse_name( decl, css_pb_names, -1 );
                break;
            case cssd_list_style_type:
                n = parse_name( decl, css_lst_names, -1 );
                break;
            case cssd_list_style_position:
                n = parse_name( decl, css_lsp_names, -1 );
                break;
            case cssd_list_style:
                {
                    // The list-style property is specified as one, two, or three keywords in any order,
                    // the keywords being those of list-style-type, list-style-position and list-style-image.
                    // We don't support (and will fail parsing the declaration) a list-style-image url(...)
                    // component, but we can parse the declaration when it contains a type (square, decimal) and/or
                    // a position (inside, outside) in any order.
                    int ntype=-1;
                    int nposition=-1;
                    // check order "type position"
                    ntype = parse_name( decl, css_lst_names, -1 );
                    skip_spaces( decl );
                    nposition = parse_name( decl, css_lsp_names, -1 );
                    skip_spaces( decl );
                    if (ntype == -1) { // check again if order was "position type"
                        ntype = parse_name( decl, css_lst_names, -1 );
                        skip_spaces( decl );
                    }
                    parsed_important = parse_important(decl);
                    if (ntype != -1) {
                        buf<<(lUInt32) (cssd_list_style_type | importance | parsed_important);
                        buf<<(lUInt32) ntype;
                    }
                    if (nposition != -1) {
                        buf<<(lUInt32) (cssd_list_style_position | importance | parsed_important);
                        buf<<(lUInt32) nposition;
                    }
                }
                break;
            case cssd_vertical_align:
                {
                    css_length_t len;
                    int n1 = parse_name( decl, css_va_names, -1 );
                    if (n1 != -1) {
                        len.type = css_val_unspecified;
                        len.value = n1;
                        buf<<(lUInt32) (prop_code | importance | parse_important(decl));
                        buf<<(lUInt32) len.type;
                        buf<<(lUInt32) len.value;
                    }
                    else {
                        if ( parse_number_value( decl, len, true, true ) ) { // accepts a negative value
                            buf<<(lUInt32) (prop_code | importance | parse_important(decl));
                            buf<<(lUInt32) len.type;
                            buf<<(lUInt32) len.value;
                        }
                    }
                }
                break;
            case cssd_font_family:
                {
                    lString8Collection list;
                    int processed = splitPropertyValueList( decl, list );
                    // printf("font-family: %s\n", lString8(decl, processed).c_str());
                    decl += processed;
                    n = -1;
                    bool check_for_important = true;
                    bool ignore_font_names = false;
                    if ( list.length() ) {
                        for (int i=list.length()-1; i>=0; i--) {
                            const char * name = list[i].c_str();
                            // printf("  %d: #%s#\n", i, name);
                            if ( check_for_important ) {
                                check_for_important = false;
                                // The last item from splitPropertyValueList may be or include '!important':
                                //   "serif !important"      (when generic family name)
                                //   "Bitter !important"     (when unquoted font name)
                                //   "Noto Sans !important"  (when unquoted font name)
                                //   "!important"            (when preceded by a quoted font name)
                                // We want to notice it and clean the previous part
                                const char * str = name;
                                bool drop_item = false;
                                while (*str) {
                                    parsed_important = parse_important(str);
                                    if ( parsed_important ) {
                                        // Found it
                                        if ( name == str ) {
                                            // No advance: standalone "!important"
                                            // remove it from the list of font names
                                            drop_item = true;
                                        }
                                        else {
                                            // Otherwise, truncate name up to where we
                                            // were when finding !important
                                            list[i] = lString8(name, str - name);
                                            name = list[i].c_str();
                                        }
                                        break;
                                    }
                                    else { // skip current char that might be a space
                                        str++;
                                    }
                                    // skip next char until we find a space or '!', that would start
                                    // a new token or '!important" itself if stuck to previous token
                                    while (*str && *str != ' ' && *str != '!') {
                                        str++;
                                    }
                                }
                                if ( drop_item ) {
                                    list.erase( i, 1 );
                                    continue;
                                }
                            }
                            int nn = parse_name( name, css_ff_names, -1 );
                            if ( nn != -1 ) {
                                if ( nn == css_ff_inherit ) {
                                    // "inherit" is invalid when not standalone
                                    // We have seen books with 'font-family: "Some Font", inherit',
                                    // that Calibre 3.x would render with "Some Font", while Firefox
                                    // and Calibre 5.x would consider the whole declaration invalid.
                                    // So, best to just ignore any non-standalone "inherit", and
                                    // keep parsing the font names.
                                    if ( i == 0 && list.length() == 1) {
                                        // At start and single (we have removed "!important" from the list
                                        // above, as well as any other generic name that was after)
                                        n = css_ff_inherit;
                                    }
                                }
                                else {
                                    // As we browse list from the right, keep replacing
                                    // the generic family name with the left most one
                                    n = nn;
                                }
                                // Remove generic family name from the font list, or replace it with
                                // the font name associated to this family if any.
                                // (Note: this could be handled another way, by doing the replacement
                                // in lvrend.cpp's getFont(), but it would need another css_ff_unset
                                // and more tweaks elsewhere. Doing it here also avoid possibly messing
                                // other platforms that use a font infrastructure better than FreeType
                                // at categorizing font families.)
                                lString8 family_font;
                                bool ignore_font_names_if_family_set = false;
                                if ( doc && nn != css_ff_inherit ) {
                                    family_font = ((ldomDocument*)doc)->getFontForFamily((css_font_family_t)nn, ignore_font_names_if_family_set);
                                }
                                if ( !family_font.empty() ) {
                                    list[i] = family_font;
                                    if ( ignore_font_names_if_family_set )
                                        ignore_font_names = true; // ignore next font names (we are parsing from right to left)
                                }
                                else {
                                    list.erase( i, 1 );
                                }
                            }
                            else { // Not a generic family name
                                if ( ignore_font_names ) {
                                    // We have seen and replaced a generic family name, and requested to ignore font names
                                    list.erase( i, 1 );
                                }
                            }
                        }
                        strValue = joinPropertyValueList( list );
                    }
                    // printf("  n=%d imp=%x strValue=%s\n", n, parsed_important, strValue.c_str());
                    // Default to sans-serif generic font-family (the default
                    // in lvfntman.cpp, as FreeType can't know the family of
                    // a font)
                    if (n == -1)
                        n = css_ff_sans_serif;
                }
                break;
            case cssd_font_style:
                n = parse_name( decl, css_fs_names, -1 );
                break;
            case cssd_font_weight:
                n = parse_name( decl, css_fw_names, -1 );
                break;
            case cssd_font_features: // font-feature-settings
                // Not (yet) implemented.
                // We map font-variant(|-*) values into the style->font_features bitmap,
                // that is associated to cssd_font_features, as "font-feature-settings" looks
                // nearer (than font-variant) to how we handle internally OpenType feature tags.
                // But font-variant and font-feature-settings, even if they enable the same
                // OpenType feature tags, should have each a life (inheritance) of their own,
                // which we won't really ensure by mapping all of them into style->font_features.
                // Also, font-feature-settings is quite more complicated to parse (optional
                // arguments, 0|1|2|3|on|off...), and we would only support up to the 31 tags
                // that can be stored in the bitmap, so ignoring all possible others.
                // As font-feature-settings is quite new, let's not support it (quite
                // often, publishers will include both font-variant and font-feature-settings
                // in a same declaration, so we should be fine).
                break;
            case cssd_font_variant:
            case cssd_font_variant_ligatures:
            case cssd_font_variant_ligatures2:
            case cssd_font_variant_caps:
            case cssd_font_variant_position:
            case cssd_font_variant_numeric:
            case cssd_font_variant_east_asian:
            case cssd_font_variant_alternates:
                {
                    // https://drafts.csswg.org/css-fonts-3/#propdef-font-variant
                    // https://developer.mozilla.org/en-US/docs/Web/CSS/font-variant
                    bool parse_ligatures =  prop_code == cssd_font_variant || prop_code == cssd_font_variant_ligatures
                                                                           || prop_code == cssd_font_variant_ligatures2;
                    bool parse_caps =       prop_code == cssd_font_variant || prop_code == cssd_font_variant_caps;
                    bool parse_position =   prop_code == cssd_font_variant || prop_code == cssd_font_variant_position;
                    bool parse_numeric =    prop_code == cssd_font_variant || prop_code == cssd_font_variant_numeric;
                    bool parse_eastasian =  prop_code == cssd_font_variant || prop_code == cssd_font_variant_east_asian;
                    bool parse_alternates = prop_code == cssd_font_variant || prop_code == cssd_font_variant_alternates;
                    // All values are mapped into a single style->font_features 31 bits bitmap
                    prop_code = cssd_font_features;
                    int features = 0; // "normal" = no extra feature
                    int nb_parsed = 0;
                    int nb_invalid = 0;
                    while ( *decl && *decl !=';' && *decl!=stop_char) {
                        if ( substr_icompare("normal", decl) ) {
                            features = 0;
                        }
                        else if ( substr_icompare("none", decl) ) {
                            features = 0;
                        }
                        // Details in crengine/include/lvfntman.h
                        else if ( parse_ligatures  && substr_icompare("no-common-ligatures", decl) )        features |= LFNT_OT_FEATURES_M_LIGA;
                        else if ( parse_ligatures  && substr_icompare("no-contextual", decl) )              features |= LFNT_OT_FEATURES_M_CALT;
                        else if ( parse_ligatures  && substr_icompare("discretionary-ligatures", decl) )    features |= LFNT_OT_FEATURES_P_DLIG;
                        else if ( parse_ligatures  && substr_icompare("no-discretionary-ligatures", decl) ) features |= LFNT_OT_FEATURES_M_DLIG;
                        else if ( parse_ligatures  && substr_icompare("historical-ligatures", decl) )       features |= LFNT_OT_FEATURES_P_HLIG;
                        else if ( parse_ligatures  && substr_icompare("no-historical-ligatures", decl) )    features |= LFNT_OT_FEATURES_M_HLIG;
                        else if ( parse_alternates && substr_icompare("historical-forms", decl) )           features |= LFNT_OT_FEATURES_P_HIST;
                        else if ( parse_eastasian  && substr_icompare("ruby", decl) )                       features |= LFNT_OT_FEATURES_P_RUBY;
                        else if ( parse_caps       && substr_icompare("small-caps", decl) )                 features |= LFNT_OT_FEATURES_P_SMCP;
                        else if ( parse_caps       && substr_icompare("all-small-caps", decl) )             features |= LFNT_OT_FEATURES_P_C2SC;
                        else if ( parse_caps       && substr_icompare("petite-caps", decl) )                features |= LFNT_OT_FEATURES_P_PCAP;
                        else if ( parse_caps       && substr_icompare("all-petite-caps", decl) )            features |= LFNT_OT_FEATURES_P_C2PC;
                        else if ( parse_caps       && substr_icompare("unicase", decl) )                    features |= LFNT_OT_FEATURES_P_UNIC;
                        else if ( parse_caps       && substr_icompare("titling-caps", decl) )               features |= LFNT_OT_FEATURES_P_TITL;
                        else if ( parse_position   && substr_icompare("super", decl) )                      features |= LFNT_OT_FEATURES_P_SUPS;
                        else if ( parse_position   && substr_icompare("sub", decl) )                        features |= LFNT_OT_FEATURES_P_SUBS;
                        else if ( parse_numeric    && substr_icompare("lining-nums", decl) )                features |= LFNT_OT_FEATURES_P_LNUM;
                        else if ( parse_numeric    && substr_icompare("oldstyle-nums", decl) )              features |= LFNT_OT_FEATURES_P_ONUM;
                        else if ( parse_numeric    && substr_icompare("proportional-nums", decl) )          features |= LFNT_OT_FEATURES_P_PNUM;
                        else if ( parse_numeric    && substr_icompare("tabular-nums", decl) )               features |= LFNT_OT_FEATURES_P_TNUM;
                        else if ( parse_numeric    && substr_icompare("slashed-zero", decl) )               features |= LFNT_OT_FEATURES_P_ZERO;
                        else if ( parse_numeric    && substr_icompare("ordinal", decl) )                    features |= LFNT_OT_FEATURES_P_ORDN;
                        else if ( parse_numeric    && substr_icompare("diagonal-fractions", decl) )         features |= LFNT_OT_FEATURES_P_FRAC;
                        else if ( parse_numeric    && substr_icompare("stacked-fractions", decl) )          features |= LFNT_OT_FEATURES_P_AFRC;
                        else if ( parse_eastasian  && substr_icompare("simplified", decl) )                 features |= LFNT_OT_FEATURES_P_SMPL;
                        else if ( parse_eastasian  && substr_icompare("traditional", decl) )                features |= LFNT_OT_FEATURES_P_TRAD;
                        else if ( parse_eastasian  && substr_icompare("full-width", decl) )                 features |= LFNT_OT_FEATURES_P_FWID;
                        else if ( parse_eastasian  && substr_icompare("proportional-width", decl) )         features |= LFNT_OT_FEATURES_P_PWID;
                        else if ( parse_eastasian  && substr_icompare("jis78", decl) )                      features |= LFNT_OT_FEATURES_P_JP78;
                        else if ( parse_eastasian  && substr_icompare("jis83", decl) )                      features |= LFNT_OT_FEATURES_P_JP83;
                        else if ( parse_eastasian  && substr_icompare("jis04", decl) )                      features |= LFNT_OT_FEATURES_P_JP04;

                        else if ( parse_important(decl) ) {
                            parsed_important = IMPORTANT_DECL_SET;
                            break; // stop looking for more
                        }
                        else { // unsupported or invalid named value
                            nb_invalid++;
                            // Firefox would ignore the whole declaration if it contains a non-standard named value.
                            // As we don't parse all valid values (eg. styleset(user-defined-ident)), we just skip
                            // them without failing the whole.
                            // Walk over unparsed value
                            while (*decl && *decl !=' ' && *decl !=';' && *decl!=stop_char)
                                decl++;
                        }
                        nb_parsed++;
                        skip_spaces( decl );
                    }
                    if ( nb_parsed - nb_invalid > 0 ) { // at least one valid named value seen
                        buf<<(lUInt32) (prop_code | importance | parsed_important);
                        buf<<(lUInt32) css_val_unspecified; // len.type
                        buf<<(lUInt32) features; // len.value
                        // css_val_unspecified just says this value has no unit
                        // For cssd_font_features, it actually means there is a value specified.
                        // The default of (css_val_inherited, 0) is what means there was no
                        // value specified, and that it should be inherited, from possibly
                        // the root note that has (css_val_unspecified, 0).
                    }
                }
                break;
            case cssd_text_indent:
                {
                    // read length
                    css_length_t len;
                    const char * orig_pos = decl;
                    if ( parse_number_value( decl, len, true, true ) ) { // accepts % and negative values
                        // Read optional "hanging" flag
                        // Note: "1em hanging" is not the same as "-1em"; the former shifts
                        // all other but first line by 1em to the right, while the latter
                        // shifts the first  by 1em to the left. Visually, lines would
                        // look the same relative to each other, but the whole block would
                        // appear shifted to the left with the latter.
                        // Little hack here: to be able to store the presence of "hanging" as
                        // a flag in the css_length_t, we reset the lowest bit to 0, which
                        // shouldn't really have a visual impact on the computed value (as
                        // the parsed number is stored *256 to allow fractional value, so
                        // we're losing 0.004em, 0.004px, 0.004%...)
                        len.value &= 0xFFFFFFFE; // set lowest bit to 0
                            // printf("3: %x -3: %x => %x %x %d\n", (lInt16)(3), (lInt16)(-3),
                            //    (lInt16)(3&0xFFFFFFFE), (lInt16)((-3)&0xFFFFFFFE), (lInt16)((-3)&0xFFFFFFFE));
                            // outputs: 3: 3 -3: fffffffd => 2 fffffffc -4
                        skip_spaces( decl );
                        int attr = parse_name( decl, css_ti_attribute_names, -1 );
                        if ( attr == 0 ) { // "hanging" found
                            len.value |= 0x00000001; // set lowest bit to 1
                        }
                        // Note: if needed, we could parse the "each-line" keyword to be able
                        // to bring back the legacy behaviour (where indent was applied after
                        // a <br>) with CSS, and put this fact in the 2nd lowest bit.
                        buf<<(lUInt32) (prop_code | importance | parse_important(decl));
                        buf<<(lUInt32) len.type;
                        buf<<(lUInt32) len.value;
                    }
                    else {
                        decl = orig_pos; // revert any decl++
                    }
                }
                break;

            // Next ones accept 1 length value (with possibly named values for borders
            // that we map to a length)
            case cssd_border_bottom_width:
            case cssd_border_top_width:
            case cssd_border_left_width:
            case cssd_border_right_width:
                {
                    int n1 = parse_name( decl, css_bw_names, -1 );
                    if (n1 != -1) {
                        buf<<(lUInt32) (prop_code | importance | parse_important(decl));
                        switch (n1) {
                            case 0: // thin
                                buf<<(lUInt32) css_val_px;
                                buf<<(lUInt32) (1*256);
                                break;
                            case 1: // medium
                                buf<<(lUInt32) css_val_px;
                                buf<<(lUInt32) (3*256);
                                break;
                            case 2: // thick
                                buf<<(lUInt32) css_val_px;
                                buf<<(lUInt32) (5*256);
                                break;
                            case 3: // initial
                                buf<<(lUInt32) css_val_px;
                                buf<<(lUInt32) (3*256);
                                break;
                            case 4: // inherit
                            default:
                                buf<<(lUInt32) css_val_inherited;
                                buf<<(lUInt32) 0;
                                break;
                        }
                        break; // We found a named border-width, we're done
                    }
                }
                // no named value found, don't break: continue checking if value is a number
            case cssd_line_height:
            case cssd_letter_spacing:
            case cssd_font_size:
            case cssd_width:
            case cssd_height:
            case cssd_min_width:
            case cssd_min_height:
            case cssd_max_width:
            case cssd_max_height:
            case cssd_margin_left:
            case cssd_margin_right:
            case cssd_margin_top:
            case cssd_margin_bottom:
            case cssd_padding_left:
            case cssd_padding_right:
            case cssd_padding_top:
            case cssd_padding_bottom:
                {
                    // borders don't accept length in %
                    bool accept_percent = true;
                    if ( prop_code==cssd_border_bottom_width || prop_code==cssd_border_top_width ||
                            prop_code==cssd_border_left_width || prop_code==cssd_border_right_width )
                        accept_percent = false;
                    // only margin accepts negative values
                    bool accept_negative = false;
                    if ( prop_code==cssd_margin_bottom || prop_code==cssd_margin_top ||
                            prop_code==cssd_margin_left || prop_code==cssd_margin_right )
                        accept_negative = true;
                    // only margin, width, height, min-width, min-height accept keyword "auto"
                    // (also accept it with max-width, max-height for style tweaks user sake)
                    bool accept_auto = false;
                    if ( prop_code==cssd_margin_bottom || prop_code==cssd_margin_top ||
                            prop_code==cssd_margin_left || prop_code==cssd_margin_right ||
                            prop_code==cssd_width || prop_code==cssd_height ||
                            prop_code==cssd_min_width || prop_code==cssd_min_height ||
                            prop_code==cssd_max_width || prop_code==cssd_max_height )
                        accept_auto = true;
                    // only max-width, max-height accept keyword "none"
                    // (also accepts it with min-width, min-height for style tweaks user sake)
                    bool accept_none = false;
                    if ( prop_code==cssd_max_width || prop_code==cssd_max_height ||
                            prop_code==cssd_min_width || prop_code==cssd_min_height )
                        accept_none = true;
                    // only line-height and letter-spacing accept keyword "normal"
                    bool accept_normal = false;
                    if ( prop_code==cssd_line_height || prop_code==cssd_letter_spacing )
                        accept_normal = true;
                    // only line-height accepts numbers with unspecified unit
                    bool accept_unspecified = false;
                    if ( prop_code==cssd_line_height )
                        accept_unspecified = true;
                    // only font-size is... font-size
                    bool is_font_size = false;
                    if ( prop_code==cssd_font_size )
                        is_font_size = true;
                    css_length_t len;
                    if ( parse_number_value( decl, len, accept_percent, accept_negative, accept_auto, accept_none, accept_normal, accept_unspecified, false, is_font_size) ) {
                        buf<<(lUInt32) (prop_code | importance | parse_important(decl));
                        buf<<(lUInt32) len.type;
                        buf<<(lUInt32) len.value;
                    }
                }
                break;
            // Done with those that accept 1 length value.

            // Next ones accept 1 to 4 length values (with possibly named values for borders
            // that we map to a length)
            case cssd_border_width:
                {
                    int n1 = parse_name( decl, css_bw_names, -1 );
                    if (n1!=-1) {
                        buf<<(lUInt32) (prop_code | importance | parse_important(decl));
                        switch (n1) {
                            case 0: // thin
                                for (int i = 0; i < 4; i++) {
                                    buf<<(lUInt32) css_val_px;
                                    buf<<(lUInt32) (1*256);
                                }
                                break;
                            case 1: // medium
                                for (int i = 0; i < 4; i++) {
                                    buf<<(lUInt32) css_val_px;
                                    buf<<(lUInt32) (3*256);
                                }
                                break;
                            case 2: // thick
                                for (int i = 0; i < 4; i++) {
                                    buf<<(lUInt32) css_val_px;
                                    buf<<(lUInt32) (5*256);
                                }
                                break;
                            case 3: // initial
                                for (int i = 0; i < 4; i++) {
                                    buf<<(lUInt32) css_val_px;
                                    buf<<(lUInt32) (3*256);
                                }
                                break;
                            case 4: // inherit
                            default:
                                for (int i = 0; i < 4; i++) {
                                    buf<<(lUInt32) css_val_inherited;
                                    buf<<(lUInt32) 0;
                                }
                                break;
                        }
                        break; // We found a named border-width, we're done
                    }
                }
                // no named value found, don't break: continue checking if value is a number
            case cssd_margin:
            case cssd_padding:
                {
                    bool accept_percent = true;
                    if ( prop_code==cssd_border_width )
                        accept_percent = false;
                    bool accept_auto = false;
                    bool accept_negative = false;
                    if ( prop_code==cssd_margin ) {
                        accept_auto = true;
                        accept_negative = true;
                    }
                    css_length_t len[4];
                    int i;
                    for (i = 0; i < 4; i++) {
                        if (!parse_number_value( decl, len[i], accept_percent, accept_negative, accept_auto ))
                            break;
                    }
                    if (i) {
                        // If we found 1, it applies to 4 edges
                        // If we found 2, 1st one apply to top and bottom, 2nd to right and left
                        // If we found 3, 1st one apply to top 2nd to right and left, 3rd to bottom
                        switch (i) {
                            case 1: len[1] = len[0]; /* fall through */
                            case 2: len[2] = len[0]; /* fall through */
                            case 3: len[3] = len[1];
                        }
                        buf<<(lUInt32) (prop_code | importance | parse_important(decl));
                        for (i = 0; i < 4; i++) {
                            buf<<(lUInt32) len[i].type;
                            buf<<(lUInt32) len[i].value;
                        }
                    }
                }
                break;
            // Done with those that accept 1 to 4 length values.

            case cssd_color:
            case cssd_background_color:
            case cssd_border_top_color:
            case cssd_border_right_color:
            case cssd_border_bottom_color:
            case cssd_border_left_color:
                {
                    css_length_t len;
                    if ( parse_color_value( decl, len ) ) {
                        buf<<(lUInt32) (prop_code | importance | parse_important(decl));
                        buf<<(lUInt32) len.type;
                        buf<<(lUInt32) len.value;
                    }
                }
                break;
            case cssd_border_color:
                {
                    // Accepts 1 to 4 color values
                    css_length_t len[4];
                    int i;
                    for (i = 0; i < 4; i++)
                        if (!parse_color_value( decl, len[i]))
                            break;
                    if (i) {
                        switch (i) {
                            case 1: len[1] = len[0]; /* fall through */
                            case 2: len[2] = len[0]; /* fall through */
                            case 3: len[3] = len[1];
                        }
                        buf<<(lUInt32) (prop_code | importance | parse_important(decl));
                        for (i = 0; i < 4; i++) {
                            buf<<(lUInt32) len[i].type;
                            buf<<(lUInt32) len[i].value;
                        }
                    }
                }
                break;
            case cssd_border_top_style:
            case cssd_border_right_style:
            case cssd_border_bottom_style:
            case cssd_border_left_style:
                n = parse_name( decl, css_bst_names, -1 );
                break;
            case cssd_border_style:
                {
                    // Accepts 1 to 4 named values
                    int name[4];
                    int i;
                    for (i = 0; i < 4; i++) {
                        int n1 = parse_name( decl, css_bst_names, -1 );
                        if ( n1 != -1 ) {
                            name[i] = n1;
                            skip_spaces(decl);
                            continue;
                        }
                        break;
                    }
                    if (i) {
                        switch (i) {
                            case 1: name[1] = name[0]; /* fall through */
                            case 2: name[2] = name[0]; /* fall through */
                            case 3: name[3] = name[1];
                        }
                        buf<<(lUInt32) (prop_code | importance | parse_important(decl));
                        for (i = 0; i < 4; i++) {
                            buf<<(lUInt32) name[i];
                        }
                    }
                }
                break;

            // Next ones accept a triplet (possibly incomplete) like "2px solid blue".
            // Borders don't accept length in %, and Firefox ignores the whole
            // individual declaration when that happens (with "10% dotted blue", and
            // later a style="border-width: 5px", Firefox shows it solid and black).
            case cssd_border:
            case cssd_border_top:
            case cssd_border_right:
            case cssd_border_bottom:
            case cssd_border_left:
                {
                    bool found_style = false;
                    bool found_width = false;
                    bool found_color = false;
                    int style_val = -1;
                    css_length_t width;
                    css_length_t color;
                    // https://developer.mozilla.org/en-US/docs/Web/CSS/border-right
                    // We look for 3 values at most, which are allowed to be in any order
                    // and be missing.
                    // If they are missing, we should set them to the default value:
                    //   width: medium, style: none, color: currentColor
                    // Note that the parse_* functions only advance the string when they
                    // match. When they don't match, we stay at the position we were.
                    for (int i=0; i<3; i++) {
                        skip_spaces(decl);
                        if ( !found_width ) {
                            if ( parse_number_value( decl, width, false ) ) { // accept_percent=false
                                found_width = true;
                                continue;
                            }
                            else {
                                int num = parse_name( decl, css_bw_names, -1 );
                                if ( num != -1 ) {
                                    width.type = css_val_px;
                                    switch (num){
                                        case 0: // thin
                                            width.value = 1*256;
                                            break;
                                        case 1: // medium
                                            width.value = 3*256;
                                            break;
                                        case 2: // thick
                                            width.value = 5*256;
                                            break;
                                        case 3: // initial
                                            width.value = 3*256;
                                            break;
                                        case 4: // inherit
                                        default:
                                            width.type = css_val_inherited;
                                            width.value = 0;
                                            break;
                                    }
                                    found_width = true;
                                    continue;
                                }
                            }
                        }
                        if ( !found_style ) {
                            style_val = parse_name( decl, css_bst_names, -1 );
                            if ( style_val != -1 ) {
                                found_style = true;
                                continue;
                            }
                        }
                        if ( !found_color ) {
                            if( parse_color_value( decl, color ) ){
                                found_color = true;
                                continue;
                            }
                        }
                        // We have not found any usable name/color/width
                        // in this loop: no need for more
                        break;
                    }
                    parsed_important = parse_important(decl);

                    // We expect to have at least found one of them
                    if ( found_style || found_width || found_color ) {
                        // We must set the not found properties to their default values
                        if ( !found_style ) {
                            // Default to "none"
                            style_val = css_border_none;
                        }
                        if ( !found_width ) {
                            // Default to "medium"
                            width.type = css_val_px;
                            width.value = 3*256;
                        }
                        if ( !found_color ) {
                            color.type = css_val_unspecified;
                            color.value = css_generic_currentcolor;
                        }
                        if ( prop_code==cssd_border ) {
                            buf<<(lUInt32) (cssd_border_style | importance | parsed_important);
                            for (int i = 0; i < 4; i++) {
                                buf<<(lUInt32) style_val;
                            }
                            buf<<(lUInt32) (cssd_border_width | importance | parsed_important);
                            for (int i = 0; i < 4; i++) {
                                buf<<(lUInt32) width.type;
                                buf<<(lUInt32) width.value;
                            }
                            buf<<(lUInt32) (cssd_border_color | importance | parsed_important);
                            for (int i = 0; i < 4; i++) {
                                buf<<(lUInt32) color.type;
                                buf<<(lUInt32) color.value;
                            }
                        }
                        else {
                            css_decl_code prop_style, prop_width, prop_color;
                            switch (prop_code) {
                                case cssd_border_top:
                                    prop_style = cssd_border_top_style;
                                    prop_width = cssd_border_top_width;
                                    prop_color = cssd_border_top_color;
                                    break;
                                case cssd_border_right:
                                    prop_style = cssd_border_right_style;
                                    prop_width = cssd_border_right_width;
                                    prop_color = cssd_border_right_color;
                                    break;
                                case cssd_border_bottom:
                                    prop_style = cssd_border_bottom_style;
                                    prop_width = cssd_border_bottom_width;
                                    prop_color = cssd_border_bottom_color;
                                    break;
                                case cssd_border_left:
                                default:
                                    prop_style = cssd_border_left_style;
                                    prop_width = cssd_border_left_width;
                                    prop_color = cssd_border_left_color;
                                    break;
                            }
                            buf<<(lUInt32) (prop_style | importance | parsed_important);
                            buf<<(lUInt32) style_val;
                            buf<<(lUInt32) (prop_width | importance | parsed_important);
                            buf<<(lUInt32) width.type;
                            buf<<(lUInt32) width.value;
                            buf<<(lUInt32) (prop_color | importance | parsed_important);
                            buf<<(lUInt32) color.type;
                            buf<<(lUInt32) color.value;
                        }
                    }
                }
                break;
            // Done with those that accepts a triplet.

            case cssd_background_image:
                {
                    lString8 str;
                    const char *tmp = decl;
                    int len=0;
                    while (*tmp && *tmp!=';' && *tmp!=stop_char && *tmp!='!') {
                        if ( *tmp == '(' && *(tmp-3) == 'u' && *(tmp-2) == 'r' && *(tmp-1) == 'l') {
                            // Accepts everything until ')' after 'url(', including ';'
                            // needed when parsing: url("data:image/png;base64,abcd...")
                            tmp++; len++;
                            while ( *tmp && *tmp!=')' ) {
                                tmp++; len++;
                            }
                        }
                        else {
                            tmp++; len++;
                        }
                    }
                    str.append(decl,len);
                    decl += len;
                    resolve_url_path(str, codeBase);
                    len = str.length();
                    buf<<(lUInt32) (prop_code | importance | parse_important(decl));
                    buf<<(lUInt32) len;
                    for (int i=0; i<len; i++)
                        buf<<(lUInt32) str[i];
                }
                break;
            case cssd_background_repeat:
                n = parse_name( decl, css_bg_repeat_names, -1 );
                break;
            case cssd_background_position:
                n = parse_name( decl, css_bg_position_names, -1 );
                // Only values between 0 and 8 will be checked by the background drawing code
                if ( n>8 ) {
                    if ( n<18 ) n=n-9;       // "top left" = "left top"
                    else if ( n==18 ) n=7;   // "center" = "center center"
                    else if ( n==19 ) n=1;   // "left" = "left center"
                    else if ( n==20 ) n=4;   // "right" = "right center"
                    else if ( n==21 ) n=6;   // "top" = "center top"
                    else if ( n==22 ) n=8;   // "bottom" = "center bottom"
                    else if ( n==23 ) n=0;   // "initial" = "left top"
                    else if ( n==24 ) n=0;   // "inherit" = "left top"
                }
                break;
            case cssd_background:
                {
                    // Limited parsing of this possibly complex property
                    // We only support a single layer in these orders:
                    //   - color
                    //   - url(...) repeat position
                    //   - color url(...) repeat position
                    //   - color url(...) position repeat
                    // (with repeat and position possibly absent or re-ordered)
                    css_length_t color;
                    bool has_color = parse_color_value(decl, color);
                    skip_spaces(decl);
                    const char *tmp = decl;
                    int len = 0;
                    while (*tmp && *tmp!=';' && *tmp!=stop_char && *tmp!='!') {
                        if ( *tmp == '(' && *(tmp-3) == 'u' && *(tmp-2) == 'r' && *(tmp-1) == 'l') {
                            // Accepts everything until ')' after 'url(', including ';'
                            // needed when parsing: url("data:image/png;base64,abcd...")
                            tmp++; len++;
                            while ( *tmp && *tmp!=')' ) {
                                tmp++; len++;
                            }
                        }
                        else {
                            tmp++; len++;
                        }
                    }
                    lString8 str;
                    str.append(decl,len);
                    if ( Utf8ToUnicode(str).lowercase().startsWith("url(") ) {
                        tmp = str.c_str();
                        len = 0;
                        while (*tmp && *tmp!=')') {
                            tmp++; len++;
                        }
                        len = len + 1;
                        str.clear();
                        str.append(decl, len);
                        decl += len;
                        resolve_url_path(str, codeBase);
                        len = str.length();
                        // Try parsing following repeat and position
                        skip_spaces(decl);
                        int repeat = parse_name( decl, css_bg_repeat_names, -1 );
                        if( repeat != -1 ) {
                            skip_spaces(decl);
                        }
                        int position = parse_name( decl, css_bg_position_names, -1 );
                        if ( position != -1 ) {
                            // Only values between 0 and 8 will be checked by the background drawing code
                            if ( position>8 ) {
                                if ( position<18 ) position -= 9;    // "top left" = "left top"
                                else if ( position==18 ) position=7; // "center" = "center center"
                                else if ( position==19 ) position=1; // "left" = "left center"
                                else if ( position==20 ) position=4; // "right" = "right center"
                                else if ( position==21 ) position=6; // "top" = "center top"
                                else if ( position==22 ) position=8; // "bottom" = "center bottom"
                                else if ( position==23 ) position=0; // "initial" = "left top"
                                else if ( position==24 ) position=0; // "inherit" = "left top"
                            }
                        }
                        if( repeat == -1 ) { // Try parsing repeat after position
                            skip_spaces(decl);
                            repeat = parse_name( decl, css_bg_repeat_names, -1 );
                        }
                        parsed_important = parse_important(decl);
                        buf<<(lUInt32) (cssd_background_image | importance | parsed_important);
                        buf<<(lUInt32) len;
                        for (int i = 0; i < len; i++)
                            buf<<(lUInt32) str[i];
                        if(repeat != -1) {
                            buf<<(lUInt32) (cssd_background_repeat | importance | parsed_important);
                            buf<<(lUInt32) repeat;
                        }
                        if (position != -1) {
                            buf<<(lUInt32) (cssd_background_position | importance | parsed_important);
                            buf<<(lUInt32) position;
                        }
                    }
                    else { // no url, only color
                        decl += len; // skip any unsupported stuff until !
                        parsed_important = parse_important(decl);
                    }
                    if ( has_color ) {
                        buf<<(lUInt32) (cssd_background_color | importance | parsed_important);
                        buf<<(lUInt32) color.type;
                        buf<<(lUInt32) color.value;
                    }
                }
                break;
            case cssd_background_size:
            case cssd_background_size2:
                {
                    prop_code = cssd_background_size;
                    // https://developer.mozilla.org/en-US/docs/Web/CSS/background-size
                    css_length_t len[2];
                    int i;
                    for (i = 0; i < 2; i++) {
                        if ( !parse_number_value( decl, len[i], true, false, true, false, false, false, true ) )
                            break;
                    }
                    if (i) {
                        if (i == 1) { // Only 1 value parsed
                            if ( len[0].type == css_val_unspecified ) { // "auto", "contain" or "cover"
                                len[1].type = css_val_unspecified;
                                len[1].value = len[0].value;
                            }
                            else { // first value is a length: second value should be "auto"
                                len[1].type = css_val_unspecified;
                                len[1].value = css_generic_auto;
                            }
                        }
                        buf<<(lUInt32) (prop_code | importance | parse_important(decl));
                        for (i = 0; i < 2; i++) {
                            buf<<(lUInt32) len[i].type;
                            buf<<(lUInt32) len[i].value;
                        }
                    }
                }
                break;
            case cssd_border_spacing:
                {
                    css_length_t len[2];
                    int i;
                    for (i = 0; i < 2; i++) {
                        // border-spacing doesn't accept values in %
                        if ( !parse_number_value( decl, len[i], false ) )
                            break;
                    }
                    if (i) {
                        if (i==1)
                            len[1] = len[0];
                        buf<<(lUInt32) (prop_code | importance | parse_important(decl));
                        for (i = 0; i < 2; i++) {
                            buf<<(lUInt32) len[i].type;
                            buf<<(lUInt32) len[i].value;
                        }
                    }
                }
                break;
            case cssd_border_collapse:
                n = parse_name( decl, css_bc_names, -1 );
                break;
            case cssd_orphans:
                n = parse_name( decl, css_orphans_widows_names, -1 );
                break;
            case cssd_widows:
                n = parse_name( decl, css_orphans_widows_names, -1 );
                break;
            case cssd_float:
                n = parse_name( decl, css_f_names, -1 );
                break;
            case cssd_clear:
                n = parse_name( decl, css_c_names, -1 );
                break;
            case cssd_direction:
                n = parse_name( decl, css_dir_names, -1 );
                break;
            case cssd_visibility:
                n = parse_name( decl, css_v_names, -1 );
                break;
            case cssd_line_break:
            case cssd_line_break2:
            case cssd_line_break3:
                prop_code = cssd_line_break;
                n = parse_name( decl, css_lb_names, -1 );
                break;
            case cssd_word_break:
                n = parse_name( decl, css_wb_names, -1 );
                break;
            case cssd_box_sizing:
                n = parse_name( decl, css_bs_names, -1 );
                break;
            case cssd_content:
                {
                    lString32 parsed_content;
                    bool has_unsupported = false;
                    if ( parse_content_property( decl, parsed_content, has_unsupported, stop_char ) ) {
                        if ( _check_if_supported && has_unsupported ) {
                            // When checking @supports (content: "foo" bar), any unsupported token makes
                            // the whole content unsupported - so don't add to buf if any unsupported
                        }
                        else {
                            buf<<(lUInt32) (cssd_content | importance | parsed_important | parse_important(decl));
                            buf<<(lUInt32) parsed_content.length();
                            for (int i=0; i < parsed_content.length(); i++) {
                                buf<<(lUInt32) parsed_content[i];
                            }
                        }
                    }
                }
                break;
            case cssd_stop:
            case cssd_unknown:
            default:
                break;
            }
            if ( n!= -1) {
                // add enum property
                buf<<(lUInt32) (prop_code | importance | parsed_important | parse_important(decl));
                buf<<(lUInt32) n;
            }
            if ( !strValue.empty() ) {
                // add string property
                if ( prop_code==cssd_font_family ) {
                    // font names
                    buf<<(lUInt32) (cssd_font_names | importance | parsed_important | parse_important(decl));
                    buf<<(lUInt32) strValue.length();
                    for (int i=0; i < strValue.length(); i++)
                        buf<<(lUInt32) strValue[i];
                }
            }
        }
        else {
            // skip unknown property
            if ( *decl == '@' ) {
                // Unless it's an atrule (ie. "@top-left {}" inside "@page {}"),
                // and we need to parse or skip it properly
                parse_or_skip_at_rule(decl, doc);
                // This will have properly skipped any closing ';' or '}' - the latter
                // does not require a followup ';', so avoid skipping what follows it
                skip_to_next_property = false;
            }
        }
        if ( _check_if_supported ) {
            // If no data added to buf: unknown property, or known property but unsupported or invalid value
            bool res = buf.pos() > 0;
            // Our parsing code above grabs what it can understand, but may leave followup
            // unsupported stuff (that would be skipped by next_property() below)
            // Here, we want to be sure there's nothing left not handled.
            skip_spaces( decl );
            if ( *decl != ')' )
                res = false;
            skip_to_next( decl, 0, ')' );
            return res;
        }
        if ( skip_to_next_property ) {
            next_property( decl );
        }
    }

    // store parsed result
    if (buf.pos()) {
        buf<<(lUInt32) cssd_stop; // add end marker
        int sz = buf.pos()/4;
        _data = new int[sz];
        // Could that cause problem with different endianess?
        buf.copyTo( (lUInt8*)_data, buf.pos() );
        // Alternative:
        //   buf.setPos(0);
        //   for (int i=0; i<sz; i++)
        //      buf >> _data[i];
    }

    // skip '}' (we don't check stop_char, as with @supports (),
    // we shouldn't see any - and we must have returned above)
    skip_spaces( decl );
    if (*decl == '}') {
        decl++;
        return true;
    }
    return false;
}

static css_length_t read_length( int * &data )
{
    css_length_t len;
    len.type = (css_value_type_t) (*data++);
    len.value = (*data++);
    return len;
}

void LVCssDeclaration::apply( css_style_rec_t * style )
{
    if (!_data)
        return;
    int * p = _data;
    for (;;)
    {
        lUInt32 prop_code = *p++;
        lUInt8 is_important = prop_code >> IMPORTANT_DECL_SHIFT; // 2 bits (importance, is_important)
        prop_code = prop_code & IMPORTANT_DECL_REMOVE;
        switch (prop_code)
        {
        case cssd_display:
            style->Apply( (css_display_t) *p++, &style->display, imp_bit_display, is_important );
            break;
        case cssd_white_space:
            style->Apply( (css_white_space_t) *p++, &style->white_space, imp_bit_white_space, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_text_align:
            style->Apply( (css_text_align_t) *p++, &style->text_align, imp_bit_text_align, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_text_align_last:
            style->Apply( (css_text_align_t) *p++, &style->text_align_last, imp_bit_text_align_last, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_text_decoration:
            style->Apply( (css_text_decoration_t) *p++, &style->text_decoration, imp_bit_text_decoration, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_text_transform:
            style->Apply( (css_text_transform_t) *p++, &style->text_transform, imp_bit_text_transform, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_hyphenate:
            style->Apply( (css_hyphenate_t) *p++, &style->hyphenate, imp_bit_hyphenate, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_list_style_type:
            style->Apply( (css_list_style_type_t) *p++, &style->list_style_type, imp_bit_list_style_type, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_list_style_position:
            style->Apply( (css_list_style_position_t) *p++, &style->list_style_position, imp_bit_list_style_position, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_page_break_before:
            style->Apply( (css_page_break_t) *p++, &style->page_break_before, imp_bit_page_break_before, is_important );
            break;
        case cssd_page_break_after:
            style->Apply( (css_page_break_t) *p++, &style->page_break_after, imp_bit_page_break_after, is_important );
            break;
        case cssd_page_break_inside:
            style->Apply( (css_page_break_t) *p++, &style->page_break_inside, imp_bit_page_break_inside, is_important );
            break;
        case cssd_vertical_align:
            style->Apply( read_length(p), &style->vertical_align, imp_bit_vertical_align, is_important );
            break;
        case cssd_font_family:
            style->Apply( (css_font_family_t) *p++, &style->font_family, imp_bit_font_family, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_font_names:
            {
                lString8 names;
                names.reserve(64);
                int len = *p++;
                for (int i=0; i<len; i++)
                    names << (lChar8)(*p++);
                names.pack();
                style->Apply( names, &style->font_name, imp_bit_font_name, is_important );
                style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            }
            break;
        case cssd_font_style:
            style->Apply( (css_font_style_t) *p++, &style->font_style, imp_bit_font_style, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_font_weight:
            style->Apply( (css_font_weight_t) *p++, &style->font_weight, imp_bit_font_weight, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_font_size:
            style->Apply( read_length(p), &style->font_size, imp_bit_font_size, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_font_features:
            // We want to 'OR' the bitmap from any declaration that is to be applied to this node
            // (while still ensuring !important).
            {
                css_length_t font_features = read_length(p);
                if ( font_features.value == 0 && font_features.type == css_val_unspecified ) {
                    // except if "font-variant: normal/none", which resets all previously set bits
                    style->Apply( font_features, &style->font_features, imp_bit_font_features, is_important );
                }
                else {
                    style->ApplyAsBitmapOr( font_features, &style->font_features, imp_bit_font_features, is_important );
                }
                style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            }
            break;
        case cssd_text_indent:
            style->Apply( read_length(p), &style->text_indent, imp_bit_text_indent, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_line_height:
            style->Apply( read_length(p), &style->line_height, imp_bit_line_height, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_letter_spacing:
            style->Apply( read_length(p), &style->letter_spacing, imp_bit_letter_spacing, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_color:
            style->Apply( read_length(p), &style->color, imp_bit_color, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_background_color:
            style->Apply( read_length(p), &style->background_color, imp_bit_background_color, is_important );
            break;
        case cssd_width:
            style->Apply( read_length(p), &style->width, imp_bit_width, is_important );
            break;
        case cssd_height:
            style->Apply( read_length(p), &style->height, imp_bit_height, is_important );
            break;
        case cssd_min_width:
            style->Apply( read_length(p), &style->min_width, imp_bit_min_width, is_important );
            break;
        case cssd_min_height:
            style->Apply( read_length(p), &style->min_height, imp_bit_min_height, is_important );
            break;
        case cssd_max_width:
            style->Apply( read_length(p), &style->max_width, imp_bit_max_width, is_important );
            break;
        case cssd_max_height:
            style->Apply( read_length(p), &style->max_height, imp_bit_max_height, is_important );
            break;
        case cssd_margin_left:
            style->Apply( read_length(p), &style->margin[0], imp_bit_margin_left, is_important );
            break;
        case cssd_margin_right:
            style->Apply( read_length(p), &style->margin[1], imp_bit_margin_right, is_important );
            break;
        case cssd_margin_top:
            style->Apply( read_length(p), &style->margin[2], imp_bit_margin_top, is_important );
            break;
        case cssd_margin_bottom:
            style->Apply( read_length(p), &style->margin[3], imp_bit_margin_bottom, is_important );
            break;
        case cssd_margin:
            style->Apply( read_length(p), &style->margin[2], imp_bit_margin_top, is_important );
            style->Apply( read_length(p), &style->margin[1], imp_bit_margin_right, is_important );
            style->Apply( read_length(p), &style->margin[3], imp_bit_margin_bottom, is_important );
            style->Apply( read_length(p), &style->margin[0], imp_bit_margin_left, is_important );
            break;
        case cssd_padding_left:
            style->Apply( read_length(p), &style->padding[0], imp_bit_padding_left, is_important );
            break;
        case cssd_padding_right:
            style->Apply( read_length(p), &style->padding[1], imp_bit_padding_right, is_important );
            break;
        case cssd_padding_top:
            style->Apply( read_length(p), &style->padding[2], imp_bit_padding_top, is_important );
            break;
        case cssd_padding_bottom:
            style->Apply( read_length(p), &style->padding[3], imp_bit_padding_bottom, is_important );
            break;
        case cssd_padding:
            style->Apply( read_length(p), &style->padding[2], imp_bit_padding_top, is_important );
            style->Apply( read_length(p), &style->padding[1], imp_bit_padding_right, is_important );
            style->Apply( read_length(p), &style->padding[3], imp_bit_padding_bottom, is_important );
            style->Apply( read_length(p), &style->padding[0], imp_bit_padding_left, is_important );
            break;
        case cssd_border_top_color:
            style->Apply( read_length(p), &style->border_color[0], imp_bit_border_color_top, is_important );
            break;
        case cssd_border_right_color:
            style->Apply( read_length(p), &style->border_color[1], imp_bit_border_color_right, is_important );
            break;
        case cssd_border_bottom_color:
            style->Apply( read_length(p), &style->border_color[2], imp_bit_border_color_bottom, is_important );
            break;
        case cssd_border_left_color:
            style->Apply( read_length(p), &style->border_color[3], imp_bit_border_color_left, is_important );
            break;
        case cssd_border_top_width:
            style->Apply( read_length(p), &style->border_width[0], imp_bit_border_width_top, is_important );
            break;
        case cssd_border_right_width:
            style->Apply( read_length(p), &style->border_width[1], imp_bit_border_width_right, is_important );
            break;
        case cssd_border_bottom_width:
            style->Apply( read_length(p), &style->border_width[2], imp_bit_border_width_bottom, is_important );
            break;
        case cssd_border_left_width:
            style->Apply( read_length(p), &style->border_width[3], imp_bit_border_width_left, is_important );
            break;
        case cssd_border_top_style:
            style->Apply( (css_border_style_type_t) *p++, &style->border_style_top, imp_bit_border_style_top, is_important );
            break;
        case cssd_border_right_style:
            style->Apply( (css_border_style_type_t) *p++, &style->border_style_right, imp_bit_border_style_right, is_important );
            break;
        case cssd_border_bottom_style:
            style->Apply( (css_border_style_type_t) *p++, &style->border_style_bottom, imp_bit_border_style_bottom, is_important );
            break;
        case cssd_border_left_style:
            style->Apply( (css_border_style_type_t) *p++, &style->border_style_left, imp_bit_border_style_left, is_important );
            break;
        case cssd_border_color:
            style->Apply( read_length(p), &style->border_color[0], imp_bit_border_color_top, is_important );
            style->Apply( read_length(p), &style->border_color[1], imp_bit_border_color_right, is_important );
            style->Apply( read_length(p), &style->border_color[2], imp_bit_border_color_bottom, is_important );
            style->Apply( read_length(p), &style->border_color[3], imp_bit_border_color_left, is_important );
            break;
        case cssd_border_width:
            style->Apply( read_length(p), &style->border_width[0], imp_bit_border_width_top, is_important );
            style->Apply( read_length(p), &style->border_width[1], imp_bit_border_width_right, is_important );
            style->Apply( read_length(p), &style->border_width[2], imp_bit_border_width_bottom, is_important );
            style->Apply( read_length(p), &style->border_width[3], imp_bit_border_width_left, is_important );
            break;
        case cssd_border_style:
            style->Apply( (css_border_style_type_t) *p++, &style->border_style_top, imp_bit_border_style_top, is_important );
            style->Apply( (css_border_style_type_t) *p++, &style->border_style_right, imp_bit_border_style_right, is_important );
            style->Apply( (css_border_style_type_t) *p++, &style->border_style_bottom, imp_bit_border_style_bottom, is_important );
            style->Apply( (css_border_style_type_t) *p++, &style->border_style_left, imp_bit_border_style_left, is_important );
            break;
        case cssd_background_image:
            {
                lString8 imagefile;
                imagefile.reserve(64);
                int l = *p++;
                for (int i=0; i<l; i++)
                    imagefile << (lChar8)(*p++);
                imagefile.pack();
                style->Apply( imagefile, &style->background_image, imp_bit_background_image, is_important );
            }
            break;
        case cssd_background_repeat:
            style->Apply( (css_background_repeat_value_t) *p++, &style->background_repeat, imp_bit_background_repeat, is_important );
            break;
        case cssd_background_position:
            style->Apply( (css_background_position_value_t) *p++, &style->background_position, imp_bit_background_position, is_important );
            break;
        case cssd_background_size:
            style->Apply( read_length(p), &style->background_size[0], imp_bit_background_size_h, is_important );
            style->Apply( read_length(p), &style->background_size[1], imp_bit_background_size_v, is_important );
            break;
        case cssd_border_spacing:
            style->Apply( read_length(p), &style->border_spacing[0], imp_bit_border_spacing_h, is_important );
            style->Apply( read_length(p), &style->border_spacing[1], imp_bit_border_spacing_v, is_important );
            break;
        case cssd_border_collapse:
            style->Apply( (css_border_collapse_value_t) *p++, &style->border_collapse, imp_bit_border_collapse, is_important );
            break;
        case cssd_orphans:
            style->Apply( (css_orphans_widows_value_t) *p++, &style->orphans, imp_bit_orphans, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_widows:
            style->Apply( (css_orphans_widows_value_t) *p++, &style->widows, imp_bit_widows, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_float:
            style->Apply( (css_float_t) *p++, &style->float_, imp_bit_float, is_important );
            break;
        case cssd_clear:
            style->Apply( (css_clear_t) *p++, &style->clear, imp_bit_clear, is_important );
            break;
        case cssd_direction:
            style->Apply( (css_direction_t) *p++, &style->direction, imp_bit_direction, is_important );
            // inherited in CSS specs, but not needed for us as we handle it at rendering time
            break;
        case cssd_visibility:
            style->Apply( (css_visibility_t) *p++, &style->visibility, imp_bit_visibility, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_line_break:
            style->Apply( (css_line_break_t) *p++, &style->line_break, imp_bit_line_break, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_word_break:
            style->Apply( (css_word_break_t) *p++, &style->word_break, imp_bit_word_break, is_important );
            style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
            break;
        case cssd_box_sizing:
            style->Apply( (css_box_sizing_t) *p++, &style->box_sizing, imp_bit_box_sizing, is_important );
            break;
        case cssd_cr_hint:
            {
                // We want to 'OR' the bitmap from any declaration that is to be applied to this node
                // (while still ensuring !important) - unless this declaration had "-cr-hint: none"
                // in which case we should reset previously set bits
                css_length_t cr_hint = read_length(p);
                if ( cr_hint.value & CSS_CR_HINT_NONE_NO_INHERIT ) {
                    style->Apply( cr_hint, &style->cr_hint, imp_bit_cr_hint, is_important );
                    style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED; // this cancels inheritance, this cancelling is then inherited
                }
                else {
                    style->ApplyAsBitmapOr( cr_hint, &style->cr_hint, imp_bit_cr_hint, is_important );
                    if ( cr_hint.value & (CSS_CR_HINT_INHERITABLE_MASK|CSS_CR_HINT_INHERITABLE_EARLY_MASK) ) {
                        style->flags |= STYLE_REC_FLAG_INHERITABLE_APPLIED;
                    }
                }
            }
            break;
        case cssd_cr_only_if:
            {
                // This -cr-only-if depends on some style as it is set at this point.
                // When the condition is not met, we return, skipping the whold declaration.
                cr_only_if_t only_if = (cr_only_if_t) *p++;
                if ( only_if == cr_only_if_inline || only_if == cr_only_if_not_inline ) {
                    css_display_t display = style->display;
                    if ( display == css_d_none ) {
                        // Let's have "display:none" not matched by neither inline nor not-inline
                        return;
                    }
                    if ( display <= css_d_inline || display == css_d_inline_block || display == css_d_inline_table ) {
                        // It is inline-like
                        if ( only_if == cr_only_if_not_inline )
                            return; // don't apply anything more of this declaration to this style
                    }
                    else {
                        // It is not inline-like
                        if ( only_if == cr_only_if_inline )
                            return; // don't apply anything more of this declaration to this style
                    }
                }
                else if ( only_if == cr_only_if_inpage_footnote || only_if == cr_only_if_not_inpage_footnote ) {
                    if ( STYLE_HAS_CR_HINT(style, FOOTNOTE_INPAGE) ) {
                        if ( only_if == cr_only_if_not_inpage_footnote )
                            return; // don't apply anything more of this declaration to this style
                    }
                    else {
                        if ( only_if == cr_only_if_inpage_footnote )
                            return; // don't apply anything more of this declaration to this style
                    }
                }
                else if ( only_if == cr_only_if_inside_inpage_footnote || only_if == cr_only_if_not_inside_inpage_footnote ) {
                    if ( STYLE_HAS_CR_HINT(style, INSIDE_FOOTNOTE_INPAGE) ) {
                        if ( only_if == cr_only_if_not_inside_inpage_footnote )
                            return; // don't apply anything more of this declaration to this style
                    }
                    else {
                        if ( only_if == cr_only_if_inside_inpage_footnote )
                            return; // don't apply anything more of this declaration to this style
                    }
                }
            }
            break;
        case cssd_content:
            {
                int l = *p++;
                lString32 content;
                if ( l > 0 ) {
                    content.reserve(l);
                    for (int i=0; i<l; i++)
                        content << (lChar32)(*p++);
                }
                style->Apply( content, &style->content, imp_bit_content, is_important );
            }
            break;
        case cssd_stop:
            return;
        }
    }
}

lUInt32 LVCssDeclaration::getHash() {
    if (!_data)
        return 0;
    int * p = _data;
    lUInt32 hash = 0;
    for (;*p != cssd_stop;p++)
        hash = hash * 31 + *p;
    return hash;
}

// We are storing specificity/weight in a lUInt32.
// We also want to include in it the order in which we have
// seen/parsed the selectors, so we store in the lower bits
// of this lUInt32 some sequence number to ensure selectors
// with the same specificity are applied in the order we've
// seen them when parsing.
// So, apply the real CSS specificity in higher bits, allowing
// for the following number of such rules in a single selector
// (we're not checking for overflow thus...)
#define WEIGHT_SPECIFICITY_EXTRA    1<<31 // extra huge weight (set by declaration when containing "-cr-hint: late")
#define WEIGHT_SPECIFICITY_ID       1<<29 // allow for 8 #id (b in comment below)
#define WEIGHT_SPECIFICITY_ATTRCLS  1<<24 // allow for 32 .class and [attr...] (c)
#define WEIGHT_SPECIFICITY_ELEMENT  1<<19 // allow for 32 element names div > p span (d)
#define WEIGHT_SELECTOR_ORDER       1     // allow for counting 524288 selectors

lUInt32 LVCssSelectorRule::getWeight() {
    /* Each LVCssSelectorRule will add its own weight to
       its LVCssSelector container specifity.

    Following https://www.w3.org/TR/CSS2/cascade.html#specificity

    A selector's specificity is calculated as follows:

    - count 1 if the declaration is from is a 'style' attribute rather
    than a rule with a selector, 0 otherwise (= a) (In HTML, values
    of an element's "style" attribute are style sheet rules. These
    rules have no selectors, so a=1, b=0, c=0, and d=0.)
    - count the number of ID attributes in the selector (= b) => 1 << 16
    - count the number of other attributes and pseudo-classes in the
    selector (= c) => 1 << 8
    - count the number of element names and pseudo-elements in the
    selector (= d) => 1

    The specificity is based only on the form of the selector. In
    particular, a selector of the form "[id=p33]" is counted as an
    attribute selector (a=0, b=0, c=1, d=0), even if the id attribute is
    defined as an "ID" in the source document's DTD.
    */

    // declaration from a style="" attribute (a) are always applied last,
    // and don't have a selector here.
    // LVCssSelector._specificity will be added 1 by LVCssSelector when it
    // has itself an elementName  // E
    //
    switch (_type) {
        case cssrt_id:            // E#id
            return WEIGHT_SPECIFICITY_ID;
            break;
        case cssrt_attrset:           // E[foo]
        case cssrt_attreq:            // E[foo="value"]
        case cssrt_attreq_i:          // E[foo="value" i]
        case cssrt_attrhas:           // E[foo~="value"]
        case cssrt_attrhas_i:         // E[foo~="value" i]
        case cssrt_attrstarts_word:   // E[foo|="value"]
        case cssrt_attrstarts_word_i: // E[foo|="value" i]
        case cssrt_attrstarts:        // E[foo^="value"]
        case cssrt_attrstarts_i:      // E[foo^="value" i]
        case cssrt_attrends:          // E[foo$="value"]
        case cssrt_attrends_i:        // E[foo$="value" i]
        case cssrt_attrcontains:      // E[foo*="value"]
        case cssrt_attrcontains_i:    // E[foo*="value" i]
        case cssrt_class:             // E.class
        case cssrt_pseudoclass:       // E:pseudo-class
            return WEIGHT_SPECIFICITY_ATTRCLS;
            break;
        case cssrt_parent:        // E > F
        case cssrt_ancessor:      // E F
        case cssrt_predecessor:   // E + F
        case cssrt_predsibling:   // E ~ F
            // These don't contribute to specificity. If they
            // come with an element name, WEIGHT_SPECIFICITY_ELEMENT
            // has already been added in LVCssSelector::parse().
            return 0;
            break;
        case cssrt_universal:     // *
            return 0;
    }
    return 0;
}

bool LVCssSelectorRule::check( const ldomNode * & node )
{
    if (!node || node->isNull() || node->isRoot())
        return false;
    // For most checks, while navigating nodes, we must ignore sibling text nodes.
    // We also ignore crengine internal boxing elements (inserted for rendering
    // purpose) by using the getUnboxedParent/Sibling(true) methods (providing
    // 'true' make them skip text nodes).
    // Note that if we are returnging 'true', the provided 'node' must stay
    // or be updated to the node on which next selectors (on the left in the
    // chain) must be checked against. When returning 'false', we can let
    // node be in any state, even messy.

    // We allow internal/boxing elements element names in selectors,
    // so if one is specified, we should not skip it with getUnboxed*().
    // We expect to find only a single kind of them per selector though.
    lUInt16 exceptBoxingNodeId = 0;
    if ( _id <= EL_BOXING_END && _id >= EL_BOXING_START ) { // _id from rule
        exceptBoxingNodeId = _id;
    }
    else {
        // Also check current node: if we stopped on it from a previous
        // rule, it's because the previous rule was checking for this
        // boxing element name
        lUInt16 curNodeId = node->getNodeId();
        if ( curNodeId <= EL_BOXING_END && curNodeId >= EL_BOXING_START )
            exceptBoxingNodeId = curNodeId;
    }
    switch (_type)
    {
    case cssrt_parent:        // E > F (child combinator)
        {
            if ( node->getNodeId() == el_DocFragment ) {
                // Don't go check the parent of DocFragment (which crengine made it
                // unfortunately be a <body> element, but it shouldn't match any CSS)
                return false;
            }
            node = node->getUnboxedParent(exceptBoxingNodeId);
            if (!node || node->isNull())
                return false;
            // If _id=0, we are the parent and we match
            if (!_id || node->getNodeId() == _id)
                return true;
            return false;
        }
        break;
    case cssrt_ancessor:      // E F (descendant combinator)
        {
            for (;;) {
                if ( node->getNodeId() == el_DocFragment ) {
                    // Don't go check the parent of DocFragment (which crengine made it
                    // unfortunately be a <body> element, but it shouldn't match any CSS)
                    return false;
                }
                node = node->getUnboxedParent(exceptBoxingNodeId);
                if (!node || node->isNull())
                    return false;
                // cssrt_ancessor is a non-deterministic rule: next rules
                // could fail when checked against this parent that matches
                // current rule, but could succeed when checked against
                // another parent that matches.
                // So, we need to check the full next rules chain on each
                // of our parent that matches current rule.
                // As we check the whole selector rules chain here,
                // LVCssSelector::check() won't have to: so it will trust
                // our return value.
                // Note: this is quite expensive compared to other combinators.
                if ( !_id || node->getNodeId() == _id ) {
                    // No element name to match against, or this element name
                    // matches: check next rules starting from there.
                    const ldomNode * n = node;
                    if (checkNextRules(n))
                        // We match all next rules (possibly including other
                        // cssrt_ancessor)
                        return true;
                    // Next rules didn't match: continue with next parent
                }
            }
        }
        break;
    case cssrt_predecessor:   // E + F (adjacent sibling combinator)
        {
            node = node->getUnboxedPrevSibling(true, exceptBoxingNodeId); // skip text nodes
            if (!node || node->isNull())
                return false;
            if (!_id || node->getNodeId() == _id) {
                // No element name to match against, or this element name matches
                return true;
            }
            return false;
        }
        break;
    case cssrt_predsibling:   // E ~ F (preceding sibling / general sibling combinator)
        {
            for (;;) {
                node = node->getUnboxedPrevSibling(true, exceptBoxingNodeId); // skip text nodes
                if (!node || node->isNull())
                    return false;
                if ( !_id || node->getNodeId() == _id ) {
                    // No element name to match against, or this element name
                    // matches: check next rules starting from there.
                    // Same as what is done in cssrt_ancessor above: we may have
                    // to check next rules on all preceeding matching siblings.
                    const ldomNode * n = node;
                    if (checkNextRules(n))
                        // We match all next rules (possibly including other
                        // cssrt_ancessor or cssrt_predsibling)
                        return true;
                    // Next rules didn't match: continue with next prev sibling
                }
            }
        }
        break;
    case cssrt_attrset:       // E[foo]
        {
            if ( !node->hasAttributes() )
                return false;
            return node->hasAttribute(_attrid);
        }
        break;
    case cssrt_attreq:        // E[foo="value"]
    case cssrt_attreq_i:      // E[foo="value" i]
        {
            if ( !node->hasAttribute(_attrid) )
                return false;
            lString32 val = node->getAttributeValue(_attrid);
            if (_type == cssrt_attreq_i)
                val.lowercase();
            return val == _value;
        }
        break;
    case cssrt_attrhas:       // E[foo~="value"]
    case cssrt_attrhas_i:     // E[foo~="value" i]
        // one of space separated values
        {
            if ( !node->hasAttribute(_attrid) )
                return false;
            lString32 val = node->getAttributeValue(_attrid);
            if (_type == cssrt_attrhas_i)
                val.lowercase();
            int p = val.pos( lString32(_value.c_str()) );            
            if (p<0)
                return false;
            if ( (p>0 && val[p-1]!=' ') 
                    || (p+_value.length()<val.length() && val[p+_value.length()]!=' ') )
                return false;
            return true;
        }
        break;
    case cssrt_attrstarts_word:    // E[foo|="value"]
    case cssrt_attrstarts_word_i:  // E[foo|="value" i]
        {
            if ( !node->hasAttribute(_attrid) )
                return false;
            // value can be exactly value or can begin with value
            // immediately followed by a hyphen
            lString32 val = node->getAttributeValue(_attrid);
            int val_len = val.length();
            int value_len = _value.length();
            if (value_len > val_len)
                return false;
            if (_type == cssrt_attrstarts_i)
                val.lowercase();
            if (value_len == val_len) {
                return val == _value;
            }
            if (val[value_len] != '-')
                return false;
            val = val.substr(0, value_len);
            return val == _value;
        }
        break;
    case cssrt_attrstarts:    // E[foo^="value"]
    case cssrt_attrstarts_i:  // E[foo^="value" i]
        {
            if ( !node->hasAttribute(_attrid) )
                return false;
            lString32 val = node->getAttributeValue(_attrid);
            int val_len = val.length();
            int value_len = _value.length();
            if (value_len > val_len)
                return false;
            val = val.substr(0, value_len);
            if (_type == cssrt_attrstarts_i)
                val.lowercase();
            return val == _value;
        }
        break;
    case cssrt_attrends:    // E[foo$="value"]
    case cssrt_attrends_i:  // E[foo$="value" i]
        {
            if ( !node->hasAttribute(_attrid) )
                return false;
            lString32 val = node->getAttributeValue(_attrid);
            int val_len = val.length();
            int value_len = _value.length();
            if (value_len > val_len)
                return false;
            val = val.substr(val_len-value_len, value_len);
            if (_type == cssrt_attrends_i)
                val.lowercase();
            return val == _value;
        }
        break;
    case cssrt_attrcontains:    // E[foo*="value"]
    case cssrt_attrcontains_i:  // E[foo*="value" i]
        {
            if ( !node->hasAttribute(_attrid) )
                return false;
            lString32 val = node->getAttributeValue(_attrid);
            if (_value.length()>val.length())
                return false;
            if (_type == cssrt_attrcontains_i)
                val.lowercase();
            return val.pos(_value, 0) >= 0;
        }
        break;
    case cssrt_id:            // E#id
        {
            const lString32 val = node->getAttributeValue(attr_id);
            if ( val.empty() )
                return false;
            // With EPUBs and CHMs, using ldomDocumentFragmentWriter,
            // we get the codeBasePrefix (+ a space) prepended to the
            // original id, ie: id="_doc_fragment_7_ origId"
            if ( !val.endsWith(_value) )
                return false;
            int prefix_len = val.length() - _value.length();
            if ( prefix_len == 0 )
                return true; // exact match (non EPUB/CHM)
            if ( val[prefix_len - 1] != U' ' )
                return false; // not a space, can't be a match
            // Ensure this prefix looks enough like a codeBasePrefix so
            // that we can consider it a match
            if ( prefix_len >= 2 && val[prefix_len - 2] != U'_' )
                return false; // not the trailing '_' of a "_doc_fragment_7_"
            if ( val.startsWith(U"_doc_fragment_") )
                return true;
            return false;
        }
        break;
    case cssrt_class:         // E.class
        {
            const lString32 val = node->getAttributeValue(attr_class);
            if ( val.empty() )
                return false;
            // val.lowercase(); // className should be case sensitive
            // we have appended a space when there was some inner space, meaning
            // this class attribute contains multiple class names, which needs
            // more complex checks
            if ( val[val.length()-1] == ' ' ) {
                lString32 value_w_space_after = _value + " ";
                if (val.pos(value_w_space_after) == 0)
                    return true; // at start
                lString32 value_w_spaces_before_after = " " + _value + " ";
                if (val.pos(value_w_spaces_before_after) != -1)
                    return true; // in between or at end
                return false;
            }
            return val == _value;
        }
        break;
    case cssrt_universal:     // *
        return true; // should it be: return !node->isBoxingNode(); ?
    case cssrt_pseudoclass:   // E:pseudo-class
        {
            int nodeId;
            switch (_attrid) {
                case csspc_root:
                {
                    // We never have any CSS when meeting the crengine root node.
                    // Only when using :root in our cr3gui/data/*.css we get a chance
                    // to meet the root node's first child node, which is not always <html>.
                    // The elements hierarchy may be:
                    //   <html> <body> with plain HTML files.
                    //   <body> <DocFragment> <body> with EPUB documents.
                    // The embedded stylesheets, being stored as attribute/child of <body>
                    // or <DocFragment> are not yet there when metting the <html> or the
                    // first <body> node.
                    // So, we can only try to match the <body> that is a child of
                    // <html> or <DocFragment>, and apply this style to it.
                    // If we were to use :root in our cr3gui/data/*.css, we would meet
                    // the first <body> or the <html> node, but to avoid applyng the
                    // style twice (to the 2 <body>s), we want to NOT match the first
                    // node.
                    ldomNode * parent = node->getUnboxedParent(exceptBoxingNodeId);
                    if ( !parent || parent->isRoot() )
                        return false; // we do not want to return true;
                    lUInt16 parentNodeId = parent->getNodeId();
                    return parentNodeId == el_DocFragment || parentNodeId == el_html;
                    // Note: to override, with style tweaks, styles set with :root,
                    // it should be enough to use body { ... !important }.
                    // The '!important' is needed because :root has a higher
                    // specificity that a simple body {}.
                }
                break;
                case csspc_empty:
                    return node->getChildCount() == 0;
                break;
                case csspc_dir:
                {
                    // We're looking at parents, but we don't want to update 'node'
                    const ldomNode * elem = node;
                    while (elem) {
                        if ( !elem->hasAttribute( attr_dir ) ) {
                            // No need to use getUnboxedParent(), boxes don't have this attribute
                            elem = elem->getParentNode();
                            continue;
                        }
                        lString32 dir = elem->getAttributeValue( attr_dir );
                        dir = dir.lowercase(); // (no need for trim(), it's done by the XMLParser)
                        if ( dir == _value )
                            return true;
                        // We could ignore invalide values, but for now, just stop looking.
                        return false;
                    }
                    return false;
                }
                break;
                case csspc_first_child:
                case csspc_first_of_type:
                {
                    int n; // 1 = false, 2 = true (should not be 0 for caching)
                    if ( !get_cached_node_checked_property(node, _attrid, n) ) {
                        n = 2; // true
                        if ( _attrid == csspc_first_of_type )
                            nodeId = node->getNodeId();
                        const ldomNode * elem = node;
                        for (;;) {
                            elem = elem->getUnboxedPrevSibling(true, exceptBoxingNodeId); // skip text nodes
                            if (!elem)
                                break;
                            // We have a previous sibling
                            if (_attrid == csspc_first_child || elem->getNodeId() == nodeId) {
                                n = 1; // false, we're not the first
                                break;
                            }
                        }
                        cache_node_checked_property(node, _attrid, n);
                    }
                    return n == 2;
                }
                break;
                case csspc_last_child:
                case csspc_last_of_type:
                {
                    int n; // 1 = false, 2 = true (should not be 0 for caching)
                    if ( !get_cached_node_checked_property(node, _attrid, n) ) {
                        n = 2; // true
                        if ( _attrid == csspc_last_of_type )
                            nodeId = node->getNodeId();
                        const ldomNode * elem = node;
                        for (;;) {
                            elem = elem->getUnboxedNextSibling(true, exceptBoxingNodeId); // skip text nodes
                            if (!elem)
                                break;
                            // We have a next sibling
                            if (_attrid == csspc_last_child || elem->getNodeId() == nodeId) {
                                n = 1; // false, we're not the last
                                break;
                            }
                        }
                        cache_node_checked_property(node, _attrid, n);
                    }
                    return n == 2;
                }
                break;
                case csspc_nth_child:
                case csspc_nth_of_type:
                {
                    int n;
                    if ( !get_cached_node_checked_property(node, _attrid, n) ) {
                        if ( _attrid == csspc_nth_of_type )
                            nodeId = node->getNodeId();
                        const ldomNode * elem = node;
                        n = 1;
                        for (;;) {
                            elem = elem->getUnboxedPrevSibling(true, exceptBoxingNodeId); // skip text nodes
                            if (!elem)
                                break;
                            if (_attrid == csspc_nth_child || elem->getNodeId() == nodeId) {
                                int nprev;
                                if ( get_cached_node_checked_property(elem, _attrid, nprev) ) {
                                    // Computed and cached on a previous sibling: stop counting
                                    n += nprev;
                                    break;
                                }
                                n++;
                            }
                        }
                        cache_node_checked_property(node, _attrid, n);
                    }
                    return match_nth_value(_value, n);
                }
                break;
                case csspc_nth_last_child:
                case csspc_nth_last_of_type:
                {
                    int n;
                    if ( !get_cached_node_checked_property(node, _attrid, n) ) {
                        if ( _attrid == csspc_nth_last_of_type )
                            nodeId = node->getNodeId();
                        const ldomNode * elem = node;
                        n = 1;
                        for (;;) {
                            elem = elem->getUnboxedNextSibling(true, exceptBoxingNodeId); // skip text nodes
                            if (!elem)
                                break;
                            if (_attrid == csspc_nth_last_child || elem->getNodeId() == nodeId) {
                                n++;
                                // Unlike csspc_nth_child/csspc_nth_of_type just above, there's
                                // little luck for next sibling nodes to have their value cached
                                // if this node did not have it cached (as we apply styles from
                                // first to last), so, no need to check for it.
                            }
                        }
                        cache_node_checked_property(node, _attrid, n);
                        // As this may be expensive if checked again on all next siblings, and
                        // as we just computed the value for this node, we can update the cache
                        // for all next siblings.
                        elem = node;
                        int nnext = n - 1;
                        for (;;) {
                            elem = elem->getUnboxedNextSibling(true, exceptBoxingNodeId); // skip text nodes
                            if (!elem)
                                break;
                            if (_attrid == csspc_nth_last_child || elem->getNodeId() == nodeId) {
                                cache_node_checked_property(elem, _attrid, nnext);
                                nnext--;
                            }
                        }
                    }
                    return match_nth_value(_value, n);
                }
                break;
                case csspc_only_child:
                case csspc_only_of_type:
                {
                    int n; // 1 = false, 2 = true (should not be 0 for caching)
                    if ( !get_cached_node_checked_property(node, _attrid, n) ) {
                        n = 2; // true
                        if ( _attrid == csspc_only_of_type )
                            nodeId = node->getNodeId();
                        const ldomNode * elem = node->getUnboxedParent(exceptBoxingNodeId)->getUnboxedFirstChild(true, exceptBoxingNodeId);
                        while (elem) {
                            if (elem != node) {
                                if (_attrid == csspc_only_child || elem->getNodeId() == nodeId) {
                                    n = 1; // false, we're not alone
                                    break;
                                }
                            }
                            elem = elem->getUnboxedNextSibling(true, exceptBoxingNodeId);
                        }
                        cache_node_checked_property(node, _attrid, n);
                    }
                    return n == 2;
                }
                break;
            }
        }
        return false;
    }
    return true;
}

bool LVCssSelectorRule::checkNextRules( const ldomNode * node )
{
    // Similar to LVCssSelector::check() just below, but
    // invoked from a rule
    LVCssSelectorRule * rule = getNext();
    if (!rule)
        return true;
    const ldomNode * n = node;
    do {
        if ( !rule->check(n) )
            return false;
        if ( rule->isFullChecking() )
            return true;
        rule = rule->getNext();
    } while (rule!=NULL);
    return true;
}

bool LVCssSelector::check( const ldomNode * node ) const
{
    lUInt16 nodeId = node->getNodeId();
    if ( nodeId == el_pseudoElem ) {
        if ( !_pseudo_elem ) { // not a ::before/after rule
            // Our added pseudoElem element should not match any other rules
            // (if we added it as a child of a P element, it should not match P > *)
            return false;
        }
        else {
            // We might be the pseudoElem that was created by this selector.
            // Start checking the rules starting from the real parent
            // (except if this selector target a boxing element: we should
            // stop unboxing at that boxing element).
            if ( _id <= EL_BOXING_END && _id >= EL_BOXING_START )
                node = node->getUnboxedParent(_id);
            else
                node = node->getUnboxedParent();
            nodeId = node->getNodeId();
        }
    }
    else if ( _id==0 && node->isBoxingNode() ) {
        // Don't apply "... *" or '.classname' selectors to boxing nodes
        // (but let those with our internal element names ("... autoBoxing") be applied)
        return false;
    }
    // check main Id
    if (_id!=0 && nodeId != _id)
        return false;
    if (!_rules)
        return true;
    // check additional rules
    const ldomNode * n = node;
    LVCssSelectorRule * rule = _rules;
    do {
        if ( !rule->check(n) )
            return false;
        // cssrt_ancessor or cssrt_predsibling rules will have checked next
        // rules on each parent or sibling. If it didn't return false, it
        // found one on which next rules match: no need to check them again
        if ( rule->isFullChecking() )
            return true;
        rule = rule->getNext();
    } while (rule!=NULL);
    return true;
}

bool parse_attr_value( const char * &str, char * buf, bool &parse_trailing_i, char stop_char=']' )
{
    int pos = 0;
    skip_spaces( str );
    if (*str=='\"' || *str=='\'')
    {
        char quote_ch = *str;
        str++;
        for ( ; str[pos] && str[pos]!=quote_ch; pos++)
        {
            if (pos>=64)
                return false;
        }
        if (str[pos]!=quote_ch)
            return false;
        for (int i=0; i<pos; i++)
            buf[i] = str[i];
        buf[pos] = 0;
        str += pos+1;
        skip_spaces( str );
        // The trailing ' i' must be outside the quotes
        if (parse_trailing_i) {
            parse_trailing_i = false;
            if (*str == 'i' || *str == 'I') {
                parse_trailing_i = true;
                str++;
                skip_spaces( str );
            }
        }
        if (*str != stop_char)
            return false;
        str++;
        return true;
    }
    else
    {
        for ( ; str[pos] && str[pos]!=' ' && str[pos]!='\t' && str[pos]!=stop_char; pos++)
        {
            if (pos>=64)
                return false;
        }
        int end_pos = pos;
        if (parse_trailing_i) {
            parse_trailing_i = false;
            if (end_pos == 0) // Empty value, or some leading space: this is invalid
                return false;
            if (str[pos] && str[pos]==' ' && str[pos+1] && (str[pos+1]=='i' || str[pos+1]=='I')) {
                parse_trailing_i = true;
                pos+=2;
            }
        }
        if (str[pos]!=stop_char)
            return false;
        for (int i=0; i<end_pos; i++)
            buf[i] = str[i];
        buf[end_pos] = 0;
        str+=pos;
        str++;
        return true;
    }
}

bool parse_attr_value( const char * &str, char * buf, char stop_char=']' )
{
    bool parse_trailing_i = false;
    return parse_attr_value( str, buf, parse_trailing_i, stop_char );
}

LVCssSelectorRule * parse_attr( const char * &str, lxmlDocBase * doc )
{
    // We should not skip_spaces() here: it's invalid just after one of '.#:'
    // and we should keep the one after the parsed value as its presence or not
    // has a different meaning (no space: multiple attributes or classnames
    // selector - space: descendant combinator)
    char attrname[512];
    char attrvalue[512];
    LVCssSelectorRuleType st = cssrt_universal;
    if (*str=='.') {
        // E.class
        str++;
        if (!parse_ident( str, attrvalue, 512 ))
            return NULL;
        LVCssSelectorRule * rule = new LVCssSelectorRule(cssrt_class);
        const lString32 s( attrvalue );
        // s.lowercase(); // className should be case sensitive
        rule->setAttr(attr_class, s);
        return rule;
    } else if ( *str=='#' ) {
        // E#id
        str++;
        if (!parse_ident( str, attrvalue, 512 ))
            return NULL;
        LVCssSelectorRule * rule = new LVCssSelectorRule(cssrt_id);
        const lString32 s( attrvalue );
        rule->setAttr(attr_id, s);
        return rule;
    } else if ( *str==':' ) {
        // E:pseudo-class (eg: E:first-child)
        str++;
        if (*str==':') {
            // pseudo element (double ::, eg: E::first-line) are not supported,
            // except ::before/after which are handled in LVCssSelector::parse()
            str--;
            return NULL;
        }
        int n = parse_name( str, css_pseudo_classes, -1 );
        if (n == -1) { // not one of out supported pseudo classes
            str--; // LVCssSelector::parse() will also check for :before/after with a single ':'
            return NULL;
        }
        attrvalue[0] = 0;
        if (*str=='(') { // parse () content
            str++;
            if ( !parse_attr_value( str, attrvalue, ')') )
                return NULL;
            // We don't parse the value here, it may have specific meaning
            // per pseudo-class type
            // But for the ones we handle, we only compare strings to a fixed set of target
            // values, so trim() and lowercase() below to avoid doing it on each check.
        }
        LVCssSelectorRule * rule = new LVCssSelectorRule(cssrt_pseudoclass);
        lString32 s( attrvalue );
        s.trim().lowercase();
        if ( n == csspc_nth_child || n == csspc_nth_of_type || n == csspc_nth_last_child || n == csspc_nth_last_of_type ) {
            // Parse "even", "odd", "5", "5n", "5n+2", "-n" into a few
            // numbers packed into a lString32, for quicker checking.
            s = parse_nth_value(s);
        }
        rule->setAttr(n, s);
        // printf("made pseudo class rule %d with %s\n", n, UnicodeToLocal(s).c_str());
        if ( n >= csspc_last_child ) {
            // Pseudoclasses after csspc_last_child can't be accurately checked
            // in the initial loading phase: a re-render will be needed.
            doc->setNodeStylesInvalidIfLoading(NODE_STYLES_INVALID_PECULIAR_CSS_PSEUDOCLASSES);
            // There might still be some issues if CSS would set some display: property
            // as, when re-rendering, a cache might be present and prevent modifying
            // the DOM for some needed autoBoxing - or the invalid styles set now
            // while loading would have created some autoBoxing that we won't be
            // able to remove...
        }
        return rule;
    } else if (*str != '[') // We're looking for an attribute selector after here
        return NULL;
    str++;
    // We may find and skip spaces inside [...]
    skip_spaces( str );
    if (!parse_ident( str, attrname, 512, true ))
        return NULL;
    skip_spaces( str );
    attrvalue[0] = 0;
    bool parse_trailing_i = false;
    if (*str==']')
    {
        st = cssrt_attrset;
        str++;
    }
    else if (*str=='=')
    {
        str++;
        parse_trailing_i = true; // reset to false if value does not end with " i"
        if (!parse_attr_value( str, attrvalue, parse_trailing_i))
            return NULL;
        if (parse_trailing_i)
            st = cssrt_attreq_i;
        else
            st = cssrt_attreq;
    }
    else if (*str=='~' && str[1]=='=')
    {
        str+=2;
        parse_trailing_i = true;
        if (!parse_attr_value( str, attrvalue, parse_trailing_i))
            return NULL;
        if (parse_trailing_i)
            st = cssrt_attrhas_i;
        else
            st = cssrt_attrhas;
    }
    else if (*str=='|' && str[1]=='=')
    {
        str+=2;
        parse_trailing_i = true;
        if (!parse_attr_value( str, attrvalue, parse_trailing_i))
            return NULL;
        if (parse_trailing_i)
            st = cssrt_attrstarts_word_i;
        else
            st = cssrt_attrstarts_word;
    }
    else if (*str=='^' && str[1]=='=')
    {
        str+=2;
        parse_trailing_i = true;
        if (!parse_attr_value( str, attrvalue, parse_trailing_i))
            return NULL;
        if (parse_trailing_i)
            st = cssrt_attrstarts_i;
        else
            st = cssrt_attrstarts;
    }
    else if (*str=='$' && str[1]=='=')
    {
        str+=2;
        parse_trailing_i = true;
        if (!parse_attr_value( str, attrvalue, parse_trailing_i))
            return NULL;
        if (parse_trailing_i)
            st = cssrt_attrends_i;
        else
            st = cssrt_attrends;
    }
    else if (*str=='*' && str[1]=='=')
    {
        str+=2;
        parse_trailing_i = true;
        if (!parse_attr_value( str, attrvalue, parse_trailing_i))
            return NULL;
        if (parse_trailing_i)
            st = cssrt_attrcontains_i;
        else
            st = cssrt_attrcontains;
    }
    else
    {
        return NULL;
    }
    LVCssSelectorRule * rule = new LVCssSelectorRule(st);
    lString32 s( attrvalue );
    if (parse_trailing_i) { // cssrt_attr*_i met
        s.lowercase();
    }
    lUInt16 id = doc->getAttrNameIndex( lString32(attrname).c_str() );
    rule->setAttr(id, s);
    return rule;
}

void LVCssSelector::insertRuleStart( LVCssSelectorRule * rule )
{
    rule->setNext( _rules );
    _rules = rule;
}

void LVCssSelector::insertRuleAfterStart( LVCssSelectorRule * rule )
{
    if ( !_rules ) {
        _rules = rule;
        return;
    }
    rule->setNext( _rules->getNext() );
    _rules->setNext( rule );
}

bool LVCssSelector::parse( const char * &str, lxmlDocBase * doc )
{
    if (!str || !*str)
        return false;
    for (;;)
    {
        skip_spaces( str );
        // We need to skip spaces in the generic parsing, but we need to
        // NOT check for attributes (.class, [attr=val]...) if we did skip
        // some spaces (because "DIV .classname" has a different meaning
        // (ancestor/descendant combinator) than "DIV.classname" (element
        // with classname)).
        bool check_attribute_rules = true;
        if ( *str == '*' ) // universal selector
        {
            str++;
            if (*str==' ' || *str=='\t' || *str=='\n' || *str == '\r')
                check_attribute_rules = false;
            skip_spaces( str );
            _id = 0;
        }
        else if ( *str == '.' ) // classname follows
        {
            _id = 0;
        }
        else if ( *str == ':' ) // pseudoclass follows
        {
            _id = 0;
        }
        else if ( *str == '[' ) // attribute selector follows
        {
            _id = 0;
        }
        else if ( *str == '#' ) // node Id follows
        {
            _id = 0; // (elementName internal id)
        }
        else if ( css_is_alpha( *str ) ) // element name follows
        {
            // ident
            char ident[64];
            if (!parse_ident( str, ident, 64, true ))
                return false;
            // All element names have been lowercased by HTMLParser (except
            // a few ones that are added explicitely by crengine): we need
            // to lowercase them here too to expect a match.
            lString32 element(ident);
            if ( element.length() < 7 ) {
                // Avoid following string comparisons if element name string
                // is shorter than the shortest of them (rubyBox)
                element = element.lowercase();
            }
            else if ( element != "DocFragment" &&
                      element != "autoBoxing"  && element != "tabularBox" &&
                      element != "rubyBox"     && element != "mathBox"    &&
                      element != "floatBox"    && element != "inlineBox"  &&
                      element != "pseudoElem"  && element != "FictionBook" ) {
                element = element.lowercase();
            }
            _id = doc->getElementNameIndex( element.c_str() );
                // Note: non standard element names (not listed in fb2def.h) in
                // selectors (eg: blah {font-style: italic}) may have different values
                // returned by getElementNameIndex() across book loadings, and cause:
                // "cached rendering is invalid (style hash mismatch): doing full rendering"
            if ( _id == el_html ) {
                int doc_format = doc->getProps()->getIntDef(DOC_PROP_FILE_FORMAT_ID, doc_format_none);
                if ( doc_format == doc_format_epub || doc_format == doc_format_chm ) {
                    // When building DOM from EPUB or CHM files, the <html> element is skipped
                    // and its <body> child is wrapped in a <DocFragment> element (so, taking
                    // the place of the skipped <html>).
                    // So, make a selector for "html" actually match "DocFragment".
                    _id = el_DocFragment;
                    // For embedded styles, this will help here with descendants selectors
                    // like "html div {}". For CSS targetting the HTML element, we'll re-initNodeStyle()
                    // the parent <DocFragment> when meeting the <body> (as at the time we meet
                    // this document stylesheet, we've already past/entered the DocFragment.)
                }
            }
            _specificity += WEIGHT_SPECIFICITY_ELEMENT; // we have an element: update specificity
            if (*str==' ' || *str=='\t' || *str=='\n' || *str == '\r')
                check_attribute_rules = false;
            skip_spaces( str );
        }
        else
        {
            return false;
        }
        if ( *str == ',' || *str == '{' )
            return true;
        // one or more attribute rules
        bool attr_rule = false;
        if (check_attribute_rules) {
            while ( *str == '[' || *str=='.' || *str=='#' || *str==':' )
            {
                LVCssSelectorRule * rule = parse_attr( str, doc );
                if (!rule) {
                    // Might be one of our supported pseudo elements, which should
                    // start with "::" but might start with a single ":".
                    // These pseudo element do not add a LVCssSelectorRule.
                    if ( *str==':' ) {
                        str++;
                        if ( *str==':' ) // skip double ::
                            str++;
                        int n = parse_name( str, css_pseudo_elements, -1 );
                        if (n != -1) {
                            _pseudo_elem = n+1; // starts at 1
                            _specificity += WEIGHT_SPECIFICITY_ELEMENT;
                            // Done with this selector: we expect ::before and ::after
                            // to come always last, and are not followed by other rules.
                            // ("x::before::before" seems not ensured by Firefox - if we
                            // stop between them, the 2nd "::before" will make the parsing
                            // of the declaration invalid, and so this rule.)
                            return true;
                        }
                    }
                    return false;
                }
                insertRuleStart( rule ); //insertRuleAfterStart
                //insertRuleAfterStart( rule ); //insertRuleAfterStart
                _specificity += rule->getWeight();

                /*
                if ( _id!=0 ) {
                    LVCssSelectorRule * rule = new LVCssSelectorRule(cssrt_parent);
                    rule->setId(_id);
                    insertRuleStart( rule );
                    _id=0;
                }
                */

                // We should not skip spaces here: combining multiple classnames or
                // attributes is to be done only when there is no space in between
                // them. Otherwise, it's a descendant combinator (cssrt_ancessor).

                attr_rule = true;
                //continue;
            }
            // Skip any space now after all combining attributes or classnames have been parsed
            skip_spaces( str );
        }
        // element relation
        if (*str == '>')
        {
            str++;
            LVCssSelectorRule * rule = new LVCssSelectorRule(cssrt_parent);
            rule->setId(_id);
            insertRuleStart( rule );
            _specificity += rule->getWeight();
            _id=0;
            continue;
        }
        else if (*str == '+')
        {
            str++;
            LVCssSelectorRule * rule = new LVCssSelectorRule(cssrt_predecessor);
            rule->setId(_id);
            insertRuleStart( rule );
            _specificity += rule->getWeight();
            _id=0;
            continue;
        }
        else if (*str == '~')
        {
            str++;
            LVCssSelectorRule * rule = new LVCssSelectorRule(cssrt_predsibling);
            rule->setId(_id);
            insertRuleStart( rule );
            _specificity += rule->getWeight();
            _id=0;
            continue;
        }
        else if (css_is_alpha( *str ) || (*str == '.') || (*str == '#') || (*str == '*') )
        {
            LVCssSelectorRule * rule = new LVCssSelectorRule(cssrt_ancessor);
            rule->setId(_id);
            insertRuleStart( rule );
            _specificity += rule->getWeight();
            _id=0;
            continue;
        }
        if ( !attr_rule )
            return false;
        else if ( *str == ',' || *str == '{' )
            return true;
    }
}

static bool skip_until_end_of_rule( const char * &str )
{
    while ( *str && *str!='}' )
        str++;
    if ( *str == '}' )
        str++;
    return *str != 0;
}

void LVCssSelector::applyToPseudoElement( const ldomNode * node, css_style_rec_t * style ) const
{
    // This might be called both on the node that match the selector (we should
    // not apply to the style of this node), and on the actual pseudo element
    // once it has been created as a child (to which we should apply).
    css_style_rec_t * target_style = NULL;
    if ( node->getNodeId() == el_pseudoElem ) {
        if (    ( _pseudo_elem == csspe_before && node->hasAttribute(attr_Before) )
             || ( _pseudo_elem == csspe_after  && node->hasAttribute(attr_After)  ) ) {
            target_style = style;
        }
    }
    else {
        // For the matching node, we create two style slots to which we apply
        // the declaration. This is just to have all styles applied and see
        // at the end if the pseudo element is display:none or not, and if
        // it should be skipped or created.
        // These css_style_rec_t are just temp slots to gather what's applied,
        // they are not the ones that will be associated to the pseudo element.
        if ( _pseudo_elem == csspe_before ) {
            if ( !style->pseudo_elem_before_style ) {
                style->pseudo_elem_before_style = new css_style_rec_t;
            }
            target_style = style->pseudo_elem_before_style;
        }
        else if ( _pseudo_elem == csspe_after ) {
            if ( !style->pseudo_elem_after_style ) {
                style->pseudo_elem_after_style = new css_style_rec_t;
            }
            target_style = style->pseudo_elem_after_style;
        }
    }

    if ( target_style ) {
        if ( !(target_style->flags & STYLE_REC_FLAG_MATCHED ) ) {
            // pseudoElem starts with "display: none" (in case they were created and
            // inserted in the DOM by a CSS selector that can later disappear).
            // Switch them to "display: inline" when we meet such a selector.
            // (The coming up _decl->apply() may not update ->display, or it may set
            // it explicitely to css_d_none, that we don't want reset to inline.)
            target_style->display = css_d_inline;
            target_style->flags |= STYLE_REC_FLAG_MATCHED;
        }
        // And apply this selector styling.
        _decl->apply(target_style);
    }
    return;
}

LVCssSelectorRule::LVCssSelectorRule( LVCssSelectorRule & v )
: _type(v._type), _id(v._id), _attrid(v._attrid)
, _next(NULL)
, _value( v._value )
{
    if ( v._next )
        _next = new LVCssSelectorRule( *v._next );
}

LVCssSelector::LVCssSelector( LVCssSelector & v )
: _id(v._id), _decl(v._decl), _specificity(v._specificity), _pseudo_elem(v._pseudo_elem), _next(NULL), _rules(NULL)
{
    if ( v._next )
        _next = new LVCssSelector( *v._next );
    if ( v._rules )
        _rules = new LVCssSelectorRule( *v._rules );
}

void LVStyleSheet::set(LVPtrVector<LVCssSelector> & v  )
{
    _selectors.clear();
    if ( !v.size() )
        return;
    _selectors.reserve( v.size() );
    for ( int i=0; i<v.size(); i++ ) {
        LVCssSelector * selector = v[i];
        if ( selector )
            _selectors.add( new LVCssSelector( *selector ) );
        else
            _selectors.add( NULL );
    }
}

LVStyleSheet::LVStyleSheet( LVStyleSheet & sheet )
:   _doc( sheet._doc )
,   _nested( sheet._nested )
{
    set( sheet._selectors );
    _selector_count = sheet._selector_count;
}

void LVStyleSheet::apply( const ldomNode * node, css_style_rec_t * style )
{
    if (!_selectors.length())
        return; // no rules!
        
    lUInt16 id = node->getNodeId();
    if ( id == el_body && node->getParentNode()->isRoot() ) {
        // Don't apply anything to the <body> container of <DocFragment>
        // (other normal <body> have a non-root parent: <html>)
        return;
    }
    if ( id == el_pseudoElem ) { // get the id chain from the parent element
        // Note that a "div:before {float:left}" will result in: <div><floatBox><pseudoElem>
        // There is just one kind of boxing element that is explicitely
        // added when parsing MathML (and can't be implicitely added) that
        // could generate pseudo elements: <mathBox>. So, don't skip them
        id = node->getUnboxedParent(el_mathBox)->getNodeId();
    }
    
    // _selectors[0] holds the ordered chain of selectors starting (from
    // the right of the selector) with a rule with no element name attached
    // (eg. "div p .quote1", class name .quote1 should be checked against
    // all elements' classnames before continuing checking for ancestors).
    // _selectors[element_name_id] holds the ordered chain of selector starting
    // with that element name (eg. ".body div.chapter > p" should be
    // first checked agains all <p>).
    // To see which selectors apply to a <p>, we must iterate thru both chains,
    // checking and applying them in the order of specificity/parsed position.
    LVCssSelector * selector_0 = _selectors[0];
    LVCssSelector * selector_id = id>0 && id<_selectors.length() ? _selectors[id] : NULL;

    for (;;)
    {
        if (selector_0!=NULL)
        {
            if (selector_id==NULL || selector_0->getSpecificity() < selector_id->getSpecificity() )
            {
                // step by sel_0
                selector_0->apply( node, style );
                selector_0 = selector_0->getNext();
            }
            else
            {
                // step by sel_id
                selector_id->apply( node, style );
                selector_id = selector_id->getNext();
            }
        }
        else if (selector_id!=NULL)
        {
            // step by sel_id
            selector_id->apply( node, style );
            selector_id = selector_id->getNext();
        }
        else
        {
            break; // end of chains
        }
    }
}

lUInt32 LVCssSelectorRule::getHash()
{
    lUInt32 hash = 0;
    hash = ( ( ( (lUInt32)_type * 31
        + (lUInt32)_id ) *31 )
        + (lUInt32)_attrid * 31 )
        + ::getHash(_value);
    return hash;
}

lUInt32 LVCssSelector::getHash() const
{
    lUInt32 hash = 0;
    lUInt32 nextHash = 0;

    if (_next)
        nextHash = _next->getHash();
    for (LVCssSelectorRule * p = _rules; p; p = p->getNext()) {
        lUInt32 ruleHash = p->getHash();
        hash = hash * 31 + ruleHash;
    }
    hash = hash * 31 + nextHash;
    hash = hash * 31 + _specificity;
    hash = hash * 31 + _pseudo_elem;
    if (!_decl.isNull())
        hash = hash * 31 + _decl->getHash();
    return hash;
}

/// calculate hash
lUInt32 LVStyleSheet::getHash()
{
    lUInt32 hash = 0;
    for ( int i=0; i<_selectors.length(); i++ ) {
        if ( _selectors[i] )
            hash = hash * 31 + _selectors[i]->getHash() + i*15324;
    }
    return hash;
}

// insert with specificity sorting
static void insert_into_selectors(LVCssSelector *item, LVPtrVector<LVCssSelector> &selectors) {
    lUInt16 id = item->getElementNameId();
    if (id >= selectors.length() || !selectors[id]) {
        item->setNext(NULL);
        selectors.set(id, item);
        return;
    }
    LVCssSelector *prev = NULL;
    LVCssSelector *next = selectors[id];
    lUInt32 specificity = item->getSpecificity();
    while (specificity >= next->getSpecificity()) {
        prev = next;
        next = next->getNext();
        if (!next)
            break;
    }
    item->setNext(next);
    if (prev)
        prev->setNext(item);
    else
        selectors[id] = item;
}

bool LVStyleSheet::parseAndAdvance( const char * &str, bool higher_importance, lString32 codeBase )
{
    if ( !_doc ) {
        // We can't parse anything if no _doc to get element name ids from
        return false;
    }
    LVCssSelector * selector = NULL;
    LVCssSelector * prev_selector;
    int err_count = 0;
    int rule_count = 0;
    for (;*str;)
    {
        // new rule
        prev_selector = NULL;
        bool err = false;
        for (;*str;)
        {
            // parse selector(s)
            // Have selector count number make the initial value
            // of _specificity, so order of selectors is preserved
            // when applying selectors with the same CSS specificity.
            selector = new LVCssSelector(_selector_count);
            _selector_count += 1; // = +WEIGHT_SELECTOR_ORDER
            selector->setNext( prev_selector );
            if ( !selector->parse(str, _doc) )
            {
                err = true;
                break;
            }
            else
            {
                if ( *str == ',' )
                {
                    str++;
                    prev_selector = selector;
                    continue; // next selector
                }
            }
            // parse declaration
            LVCssDeclRef decl( new LVCssDeclaration );
            if ( !decl->parse( str, higher_importance, _doc, codeBase ) )
            {
                err = true;
                err_count++;
            }
            else
            {
                // set decl to selectors
                for (LVCssSelector * p = selector; p; p=p->getNext()) {
                    p->setDeclaration( decl );
                    if ( decl->isExtraWeighted() )
                        p->addSpecificity(WEIGHT_SPECIFICITY_EXTRA);
                }
                rule_count++;
            }
            break;
        }
        if (err)
        {
            // error:
            // delete chain of selectors
            delete selector;
            // We may have stumbled on a @ rule (@namespace, @media...): parse or skip it properly
            if ( *str == '@' ) {
                parse_or_skip_at_rule(str, _doc);
            }
            else if ( *str == '}' && _nested ) {
                // We're done parsing this nested CSS block
                str++; // skip it so upper stylesheet can continue from there
                return true;
            }
            else {
                // ignore current rule: skip to block closing '}'
                skip_until_end_of_rule( str );
            }
        }
        else
        {
            // Ok:
            // place rules to sheet
            for (LVCssSelector * next, * p = selector; p; p = next) {
                next = p->getNext();
                insert_into_selectors(p, _selectors);
            }
        }
    }
    return _selectors.length() > 0;
}

/// Gather snippets in the provided CSS that the provided node would match
bool LVStyleSheet::gatherNodeMatchingRulesets(ldomNode * node, const char * str, lString8Collection & matches) {
    // Parsing as in parseAndAdvance() but simplified as we don't need to build anything
    bool ret = false;
    if ( !_doc ) {
        return ret;
    }
    lUInt16 id = node->getNodeId();
    if ( id == el_body && node->getParentNode()->isRoot() ) {
        return ret;
    }
    if ( id == el_pseudoElem ) { // get the id chain from the parent element
        id = node->getUnboxedParent(el_mathBox)->getNodeId();
    }
    for (;*str;) {
        // new section
        bool match = false;
        const char * start;
        const char * end;
        bool err = false;
        for (;*str;) {
            if ( !match ) {
                // We will truncate the start of the snippet to the first matching selector
                start = str;
            }
            LVCssSelector selector;
            if ( !selector.parse(str, _doc) ) {
                err = true;
                break;
            }
            else {
                if ( !match ) {
                    lUInt16 selector_id = selector.getElementNameId();
                    if ( selector_id == 0 || selector_id == id ) {
                        if ( selector.check(node) ) {
                            match = true;
                        }
                    }
                }
                if ( *str == ',' ) {
                    str++;
                    continue; // next selector
                }
            }
            // parse declaration
            LVCssDeclaration decl;
            if ( !decl.parse( str, false, _doc) ) {
                err = true;
            }
            end = str;
            break;
        }
        if (err) {
            // We may have stumbled on a @ rule (@namespace, @media...): parse or skip it properly
            if ( *str == '@' ) {
                parse_or_skip_at_rule(str, _doc);
            }
            else {
                // ignore current rule: skip to block closing '}'
                skip_until_end_of_rule( str );
            }
        }
        else {
            if ( match ) {
                skip_spaces(start); // cleanup up \n and spaces at start (end should already be clean)
                matches.add(lString8(start, end-start).trim());
                ret = true;
            }
        }
    }
    return ret;
}

void LVStyleSheet::merge(const LVStyleSheet &other) {
    int length = other._selectors.length();
    if (length > _selectors.length())
        _selectors.set(length - 1, nullptr);
    for (int i = 0; i < length; ++i) {
        if (!other._selectors[i])
            continue;
        LVCssSelector *prev = NULL;
        LVCssSelector *next = _selectors[i];
        for (LVCssSelector *p = other._selectors[i]; p; p = p->getNext()) {
            LVCssSelector *item = p->getCopy();
            item->addSpecificity(_selector_count);
            while (next && next->getSpecificity() <= item->getSpecificity()) {
                prev = next;
                next = next->getNext();
            }
            item->setNext(next);
            if (prev)
                prev->setNext(item);
            else
                _selectors[i] = item;
            prev = item;
        }
    }
    _selector_count += other._selector_count;
}

/// extract @import filename from beginning of CSS
bool LVProcessStyleSheetImport( const char * &str, lString8 & import_file, lxmlDocBase * doc )
{
    // @import are only valid at the top of a stylesheet
    const char * p = str;
    import_file.clear();
    skip_spaces( p );
    if ( *p !='@' )
        return false;
    p++;
    // The only thing that can happen before @import is @charset, so skip it
    if (strncmp(p, "charset", 7) == 0) {
        skip_to_next( p, ';', 0);
        skip_spaces( p );
        if ( *p !='@' )
            return false;
        p++;
    }
    if (strncmp(p, "import", 6) != 0)
        return false;
    p+=6;
    skip_spaces( p );
    bool in_url = false;
    char quote_ch = 0;
    if ( !strncmp(p, "url", 3) ) {
        p+=3;
        skip_spaces( p );
        if ( *p != '(' )
            return false;
        p++;
        skip_spaces( p );
        in_url = true;
    }
    if ( *p == '\'' || *p=='\"' )
        quote_ch = *p++;
    while (*p) {
        if ( quote_ch && *p==quote_ch ) {
            p++;
            break;
        }
        if ( !quote_ch ) {
            if ( in_url && *p==')' ) {
                break;
            }
            if ( *p==' ' || *p=='\t' || *p=='\r' || *p=='\n' )
                break;
        }
        import_file << *p++;
    }
    skip_spaces( p );
    if ( in_url ) {
        if ( *p!=')' )
            return false;
        p++;
    }
    skip_spaces( p );

    if ( *p!=';' ) {
        // A media query is allowed before the ';', and ends with the ';'
        bool ok = check_at_media_condition( p, doc , ';');
        if ( !ok ) {
            // Condition not met: skip it, look for next @import
            // (This could have been better handled by a while loop surrounding
            // all this function content - but let's do it via recursive calls
            // just to limit the amount of diff)
            import_file.clear();
            skip_spaces( p );
            if ( *p==';' )
                p++;
            if ( LVProcessStyleSheetImport(p, import_file, doc) ) {
                str = p;
                return true;
            }
            return false;
        }
    }
    skip_spaces( p );

    // Remove trailing ';' at end of "@import url(..);"
    if ( *p==';' )
        p++;
    if ( import_file.empty() )
        return false;
    str = p;
    return true;
}

/// load stylesheet from file, with processing of first @import only
bool LVLoadStylesheetFile( lString32 pathName, lString8 & css )
{
    LVStreamRef file = LVOpenFileStream( pathName.c_str(), LVOM_READ );
    if ( file.isNull() )
        return false;
    lString8 txt = UnicodeToUtf8( LVReadTextFile( file ) );
    lString8 txt2;
    const char * s = txt.c_str();
    lString8 import_file;
    if ( LVProcessStyleSheetImport( s, import_file ) ) {
        lString32 importFilename = LVMakeRelativeFilename( pathName, Utf8ToUnicode(import_file) );
        //lString8 ifn = UnicodeToLocal(importFilename);
        //const char * ifns = ifn.c_str();
        if ( !importFilename.empty() ) {
            LVStreamRef file2 = LVOpenFileStream( importFilename.c_str(), LVOM_READ );
            if ( !file2.isNull() )
                txt2 = UnicodeToUtf8( LVReadTextFile( file2 ) );
        }
    }
    if ( !txt2.empty() )
        txt2 << "\r\n";
    css = txt2 + s;
    return !css.empty();
}
