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
    cssd_text_decoration,
    cssd_text_transform,
    cssd_hyphenate,  // hyphens (proper css property name)
    cssd_hyphenate2, // -webkit-hyphens (used by authors as an alternative to adobe-hyphenate)
    cssd_hyphenate3, // adobe-hyphenate (used by late Adobe RMSDK)
    cssd_hyphenate4, // adobe-text-layout (used by earlier Adobe RMSDK)
    cssd_hyphenate5, // hyphenate (fb2? used in obsoleted css files))
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
    cssd_text_indent,
    cssd_line_height,
    cssd_letter_spacing,
    cssd_width,
    cssd_height,
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
    cssd_background_attachment,
    cssd_background_position,
    cssd_border_collapse,
    cssd_border_spacing,
    cssd_orphans,
    cssd_widows,
    cssd_float,
    cssd_clear,
    cssd_direction,
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
    "text-decoration",
    "text-transform",
    "hyphens",
    "-webkit-hyphens",
    "adobe-hyphenate",
    "adobe-text-layout",
    "hyphenate",
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
    "text-indent",
    "line-height",
    "letter-spacing",
    "width",
    "height",
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
    "background-attachment",
    "background-position",
    "border-collapse",
    "border-spacing",
    "orphans",
    "widows",
    "float",
    "clear",
    "direction",
    "-cr-ignore-if-dom-version-greater-or-equal",
    "-cr-hint",
    "-cr-only-if",
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

static bool next_property( const char * & str )
{
    while (*str && *str !=';' && *str!='}')
        str++;
    if (*str == ';')
        str++;
    return skip_spaces( str );
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

static bool parse_number_value( const char * & str, css_length_t & value,
                                    bool accept_percent=true,
                                    bool accept_negative=false,
                                    bool accept_auto=false,
                                    bool accept_normal=false,
                                    bool is_font_size=false )
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
    if ( accept_normal && substr_icompare( "normal", str ) ) {
        value.type = css_val_unspecified;
        value.value = css_generic_normal;
        return true;
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
    else if (n == 0 && frac == 0)
        value.type = css_val_px;
    // allow unspecified unit (for line-height)
    // else
    //    return false;
    value.value = n * 256 + 256 * frac / frac_div; // *256
    if (negative)
        value.value = -value.value;
    return true;
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
    {"darkturquoise",0x00ced1},
    {"darkviolet",0x9400d3},
    {"deeppink",0xff1493},
    {"deepskyblue",0x00bfff},
    {"dimgray",0x696969},
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
    {"lightpink",0xffb6c1},
    {"lightsalmon",0xffa07a},
    {"lightseagreen",0x20b2aa},
    {"lightskyblue",0x87cefa},
    {"lightslategray",0x778899},
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
    // Does not support "rgb(0, 127, 255)" nor "rgba(0,127,255)"
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
    if ( substr_compare( "inherit", str ) )
    {
        value.type = css_val_inherited;
        value.value = 0;
        return true;
    }
    if ( substr_compare( "none", str ) )
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
        if ( nDigits==3 ) {
            int r = hexDigit( *str++ );
            int g = hexDigit( *str++ );
            int b = hexDigit( *str++ );
            value.type = css_val_color;
            value.value = (((r + r*16) * 256) | (g + g*16)) * 256 | (b + b*16);
            return true;
        } else if ( nDigits==6 ) {
            int r = hexDigit( *str++ ) * 16;
            r += hexDigit( *str++ );
            int g = hexDigit( *str++ ) * 16;
            g += hexDigit( *str++ );
            int b = hexDigit( *str++ ) * 16;
            b += hexDigit( *str++ );
            value.type = css_val_color;
            value.value = ((r * 256) | g) * 256 | b;
            return true;
        } else {
            str = orig_pos; // revert our possible str++
            return false;
        }
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

static void resolve_url_path( lString8 & str, lString16 codeBase ) {
    // A URL (path to local or container's file) must be resolved
    // at parsing time, as it is related to this stylesheet file
    // path (and not to the HTML files that are linking to this
    // stylesheet) - it wouldn't be possible to resolve it later.
    lString16 path = Utf8ToUnicode(str);
    path.trim();
    if (path.startsWithNoCase(lString16("url"))) path = path.substr(3);
    path.trim();
    if (path.startsWith(L"(")) path = path.substr(1);
    if (path.endsWith(L")")) path = path.substr(0, path.length() - 1);
    path.trim();
    if (path.startsWith(L"\"") || path.startsWith(L"'")) path = path.substr(1);
    if (path.endsWith(L"\"") || path.endsWith(L"'")) path = path.substr(0, path.length() - 1);
    path.trim();
    // We assume it's a path to a local file in the container, so we don't try
    // to check if it's a remote url (as we can't fetch its content anyway).
    if ( !codeBase.empty() ) {
        path = LVCombinePaths( codeBase, path );
    }
    // printf("url: [%s]+%s => %s\n", UnicodeToLocal(codeBase).c_str(), str.c_str(), UnicodeToUtf8(path).c_str());
    str = UnicodeToUtf8(path);
}

static const char * css_d_names[] = 
{
    "inherit",
    "inline",
    "block",
    "-cr-list-item-final", // non-standard, legacy crengine rendering of list items as final: css_d_list_item
    "list-item",           // correct rendering of list items as block: css_d_list_item_block
    "run-in", 
    "compact", 
    "marker", 
    "table", 
    "inline-table", 
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
    "pre",
    "nowrap",
    NULL
};

static const char * css_ta_names[] = 
{
    "inherit",
    "left",
    "right",
    "center",
    "justify",
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

// -cr-hint names (non standard property for providing hints to crengine via style tweaks)
static const char * css_cr_hint_names[]={
        "inherit",
        "none",
                            // For footnote popup detection:
        "noteref",          // link is to a footnote
        "noteref-ignore",   // link is not to a footnote (even if everything else indicates it is)
        "footnote",         // block is a footnote (must be a full footnote block container)
        "footnote-ignore",  // block is not a footnote (even if everything else indicates it is)
        "footnote-inpage",  // block is a footnote (must be a full footnote block container), and to be
                            // displayed at the bottom of all pages that contain a link to it.
        "toc-level1",       // to be considered as TOC item of level N when building alternate TOC
        "toc-level2",
        "toc-level3",
        "toc-level4",
        "toc-level5",
        "toc-level6",
        "toc-ignore",       // ignore these H1...H6 when building alternate TOC
        NULL
};

static const char * css_cr_only_if_names[]={
        "any",
        "always",
        "never",
        "legacy",
        "enhanced",
        "float-floatboxes",
        "ensure-style-width",
        "ensure-style-height",
        "allow-style-w-h-absolute-units",
        "full-featured",
        "epub-document",
        NULL
};
enum cr_only_if_t {
    cr_only_if_any,    // always true, don't ignore
    cr_only_if_always, // always true, don't ignore
    cr_only_if_never,  // always false, do ignore
    cr_only_if_legacy,
    cr_only_if_enhanced,
    cr_only_if_float_floatboxes,
    cr_only_if_ensure_style_width,
    cr_only_if_ensure_style_height,
    cr_only_if_allow_style_w_h_absolute_units,
    cr_only_if_full_featured,
    cr_only_if_epub_document,
};

bool LVCssDeclaration::parse( const char * &decl, bool higher_importance, lxmlDocBase * doc, lString16 codeBase )
{
    if ( !decl )
        return false;
    skip_spaces( decl );
    if ( *decl != '{' )
        return false;
    decl++;
    SerialBuf buf(512, true);

    bool ignoring = false;
    while ( *decl && *decl != '}' ) {
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
                        if ( gDOMVersionRequested >= dom_version ) {
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
                        else if ( name == cr_only_if_legacy ) {
                            match = ((bool)BLOCK_RENDERING_G(ENHANCED)) == invert;
                        }
                        else if ( name == cr_only_if_enhanced ) {
                            match = ((bool)BLOCK_RENDERING_G(ENHANCED)) != invert;
                        }
                        else if ( name == cr_only_if_float_floatboxes ) {
                            match = ((bool)BLOCK_RENDERING_G(FLOAT_FLOATBOXES)) != invert;
                        }
                        else if ( name == cr_only_if_ensure_style_width ) {
                            match = ((bool)BLOCK_RENDERING_G(ENSURE_STYLE_WIDTH)) != invert;
                        }
                        else if ( name == cr_only_if_ensure_style_height ) {
                            match = ((bool)BLOCK_RENDERING_G(ENSURE_STYLE_HEIGHT)) != invert;
                        }
                        else if ( name == cr_only_if_allow_style_w_h_absolute_units ) {
                            match = ((bool)BLOCK_RENDERING_G(ALLOW_STYLE_W_H_ABSOLUTE_UNITS)) != invert;
                        }
                        else if ( name == cr_only_if_full_featured ) {
                            match = (gRenderBlockRenderingFlags == BLOCK_RENDERING_FULL_FEATURED) != invert;
                        }
                        else if ( name == cr_only_if_epub_document ) {
                            // 'doc' is NULL when parsing elements style= attribute,
                            // but we don't expect to see -cr-only-if: in them.
                            if (doc) {
                                match = doc->getProps()->getIntDef(DOC_PROP_FILE_FORMAT_ID, doc_format_none) == doc_format_epub;
                                if (invert) {
                                    match = !match;
                                }
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
                n = parse_name( decl, css_cr_hint_names, -1 );
                break;
            case cssd_display:
                n = parse_name( decl, css_d_names, -1 );
                if (gDOMVersionRequested < 20180524 && n == 4) { // css_d_list_item_block
                    n = 3; // use css_d_list_item (legacy rendering of list-item)
                }
                break;
            case cssd_white_space:
                n = parse_name( decl, css_ws_names, -1 );
                break;
            case cssd_text_align:
                n = parse_name( decl, css_ta_names, -1 );
                break;
            case cssd_text_align_last:
                n = parse_name( decl, css_ta_names, -1 );
                break;
            case cssd_text_decoration:
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
                    decl += processed;
                    n = -1;
                    if ( list.length() ) {
                        for (int i=list.length()-1; i>=0; i--) {
                            const char * name = list[i].c_str();
                            int nn = parse_name( name, css_ff_names, -1 );
                            // Ignore "inherit" (nn=0) in font-family, as its the default
                            // behaviour, and it may prevent (the way we handle
                            // it in setNodeStyle()) the use of the font names
                            // specified alongside.
                            if (n==-1 && nn!=-1 && nn!=0) {
                                n = nn;
                            }
                            if (nn!=-1) {
                                // remove family name from font list
                                list.erase( i, 1 );
                            }
                            else if ( substr_icompare( "!important", name ) ) {
                                // !important may be caught by splitPropertyValueList()
                                list.erase( i, 1 );
                                parsed_important = IMPORTANT_DECL_SET;
                            }
                        }
                        strValue = joinPropertyValueList( list );
                    }
                    // default to serif generic font-family
                    if (n == -1)
                        n = 1;
                }
                break;
            case cssd_font_style:
                n = parse_name( decl, css_fs_names, -1 );
                break;
            case cssd_font_weight:
                n = parse_name( decl, css_fw_names, -1 );
                break;
            case cssd_text_indent:
                {
                    // read length
                    css_length_t len;
                    const char * orig_pos = decl;
                    bool negative = false;
                    if ( *decl == '-' ) {
                        decl++;
                        negative = true;
                    }
                    if ( parse_number_value( decl, len ) ) {
                        // read optional "hanging" flag
                        skip_spaces( decl );
                        int attr = parse_name( decl, css_ti_attribute_names, -1 );
                        if ( attr==0 || negative ) {
                            len.value = -len.value;
                        }
                        // save result
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
                    // only margin, width and height accept keyword "auto"
                    bool accept_auto = false;
                    if ( prop_code==cssd_margin_bottom || prop_code==cssd_margin_top ||
                            prop_code==cssd_margin_left || prop_code==cssd_margin_right ||
                            prop_code==cssd_width || prop_code==cssd_height )
                        accept_auto = true;
                    // only line-height and letter-spacing accept keyword "normal"
                    bool accept_normal = false;
                    if ( prop_code==cssd_line_height || prop_code==cssd_letter_spacing )
                        accept_normal = true;
                    // only font-size is... font-size
                    bool is_font_size = false;
                    if ( prop_code==cssd_font_size )
                        is_font_size = true;
                    css_length_t len;
                    if ( parse_number_value( decl, len, accept_percent, accept_negative, accept_auto, accept_normal, is_font_size) ) {
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
                            // We don't support "currentColor": fallback to black
                            color.type = css_val_color;
                            color.value = 0x000000;
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
                    while (*tmp && *tmp!=';' && *tmp!='}' && *tmp!='!') {
                        tmp++; len++;
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
            case cssd_background_attachment:
                n = parse_name( decl, css_bg_attachment_names, -1 );
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
                    while (*tmp && *tmp!=';' && *tmp!='}' && *tmp!='!') {
                        tmp++; len++;
                    }
                    lString8 str;
                    str.append(decl,len);
                    if ( Utf8ToUnicode(str).lowercase().startsWith("url") ) {
                        tmp = str.c_str();
                        len = 0;
                        while (*tmp && *tmp!=';' && *tmp!='}' && *tmp!=')') {
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
        }
        next_property( decl );
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

    // skip }
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
            break;
        case cssd_text_align:
            style->Apply( (css_text_align_t) *p++, &style->text_align, imp_bit_text_align, is_important );
            break;
        case cssd_text_align_last:
            style->Apply( (css_text_align_t) *p++, &style->text_align_last, imp_bit_text_align_last, is_important );
            break;
        case cssd_text_decoration:
            style->Apply( (css_text_decoration_t) *p++, &style->text_decoration, imp_bit_text_decoration, is_important );
            break;
        case cssd_text_transform:
            style->Apply( (css_text_transform_t) *p++, &style->text_transform, imp_bit_text_transform, is_important );
            break;
        case cssd_hyphenate:
            style->Apply( (css_hyphenate_t) *p++, &style->hyphenate, imp_bit_hyphenate, is_important );
            break;
        case cssd_list_style_type:
            style->Apply( (css_list_style_type_t) *p++, &style->list_style_type, imp_bit_list_style_type, is_important );
            break;
        case cssd_list_style_position:
            style->Apply( (css_list_style_position_t) *p++, &style->list_style_position, imp_bit_list_style_position, is_important );
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
            }
            break;
        case cssd_font_style:
            style->Apply( (css_font_style_t) *p++, &style->font_style, imp_bit_font_style, is_important );
            break;
        case cssd_font_weight:
            style->Apply( (css_font_weight_t) *p++, &style->font_weight, imp_bit_font_weight, is_important );
            break;
        case cssd_font_size:
            style->Apply( read_length(p), &style->font_size, imp_bit_font_size, is_important );
            break;
        case cssd_text_indent:
            style->Apply( read_length(p), &style->text_indent, imp_bit_text_indent, is_important );
            break;
        case cssd_line_height:
            style->Apply( read_length(p), &style->line_height, imp_bit_line_height, is_important );
            break;
        case cssd_letter_spacing:
            style->Apply( read_length(p), &style->letter_spacing, imp_bit_letter_spacing, is_important );
            break;
        case cssd_color:
            style->Apply( read_length(p), &style->color, imp_bit_color, is_important );
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
        case cssd_background_attachment:
            style->Apply( (css_background_attachment_value_t) *p++, &style->background_attachment, imp_bit_background_attachment, is_important );
            break;
        case cssd_background_position:
            style->Apply( (css_background_position_value_t) *p++, &style->background_position, imp_bit_background_position, is_important );
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
            break;
        case cssd_widows:
            style->Apply( (css_orphans_widows_value_t) *p++, &style->widows, imp_bit_widows, is_important );
            break;
        case cssd_float:
            style->Apply( (css_float_t) *p++, &style->float_, imp_bit_float, is_important );
            break;
        case cssd_clear:
            style->Apply( (css_clear_t) *p++, &style->clear, imp_bit_clear, is_important );
            break;
        case cssd_direction:
            style->Apply( (css_direction_t) *p++, &style->direction, imp_bit_direction, is_important );
            break;
        case cssd_cr_hint:
            style->Apply( (css_cr_hint_t) *p++, &style->cr_hint, imp_bit_cr_hint, is_important );
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

static bool parse_ident( const char * &str, char * ident )
{
    // Note: skipping any space before or after should be ensured by caller if needed
    *ident = 0;
    if ( !css_is_alpha( *str ) )
        return false;
    int i;
    for (i=0; css_is_alnum(str[i]); i++)
        ident[i] = str[i];
    ident[i] = 0;
    str += i;
    return true;
}

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
            return 1 << 16;
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
            return 1 << 8;
            break;
        case cssrt_parent:        // E > F
        case cssrt_ancessor:      // E F
        case cssrt_predecessor:   // E + F
        case cssrt_predsibling:   // E ~ F
            // But not when they don't have an element (_id=0)
            return _id != 0 ? 1 : 0;
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
    // We also ignore <autoBoxing> and <floatBox> (crengine internal block element,
    // inserted for rendering purpose) when looking at parent(s).
    // TODO: for cssrt_predecessor and cssrt_pseudoclass, we should
    // also deal with <autoBoxing> nodes when navigating siblings,
    // by iterating up and down the autoBoxing nodes met on our path while
    // under real parent. These could take wrong decisions in the meantime...
    switch (_type)
    {
    case cssrt_parent:        // E > F (child combinator)
        {
            node = node->getParentNode();
            while (node && !node->isNull() && (node->getNodeId() == el_autoBoxing || node->getNodeId() == el_floatBox))
                node = node->getParentNode();
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
            for (;;)
            {
                node = node->getParentNode();
                if (!node || node->isNull())
                    return false;
                if (node->getNodeId() == el_autoBoxing || node->getNodeId() == el_floatBox)
                    continue;
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
            int index = node->getNodeIndex();
            if (index>0) {
                ldomNode * parent = node->getParentNode();
                for (int i=index-1; i>=0; i--) {
                    ldomNode * elem = parent->getChildElementNode(i);
                    // we get NULL when a child is a text node, that we should ignore
                    if ( elem ) { // this is the preceeding element node
                        if (!_id || elem->getNodeId() == _id) {
                            // No element name to match against, or this element name matches
                            node = elem;
                            return true;
                        }
                        return false;
                    }
                }
            }
            return false;
        }
        break;
    case cssrt_predsibling:   // E ~ F (preceding sibling / general sibling combinator)
        {
            int index = node->getNodeIndex();
            if (index>0) {
                ldomNode * parent = node->getParentNode();
                for (int i=index-1; i>=0; i--) {
                    const ldomNode * elem = parent->getChildElementNode(i);
                    // we get NULL when a child is a text node, that we should ignore
                    if ( elem ) { // this is an element node
                        if ( !_id || elem->getNodeId() == _id ) {
                            // No element name to match against, or this element name
                            // matches: check next rules starting from there.
                            // Same as what is done in cssrt_ancessor above: we may
                            // have to check next rules on all preceeding siblings.
                            if (checkNextRules(elem))
                                // We match all next rules (possibly including other
                                // cssrt_ancessor or cssrt_predsibling)
                                return true;
                            // Next rules didn't match: continue with next parent
                        }
                    }
                }
            }
            return false;
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
            lString16 val = node->getAttributeValue(_attrid);
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
            lString16 val = node->getAttributeValue(_attrid);
            if (_type == cssrt_attrhas_i)
                val.lowercase();
            int p = val.pos( lString16(_value.c_str()) );            
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
            lString16 val = node->getAttributeValue(_attrid);
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
            lString16 val = node->getAttributeValue(_attrid);
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
            lString16 val = node->getAttributeValue(_attrid);
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
            lString16 val = node->getAttributeValue(_attrid);
            if (_value.length()>val.length())
                return false;
            if (_type == cssrt_attrcontains_i)
                val.lowercase();
            return val.pos(_value, 0) >= 0;
        }
        break;
    case cssrt_id:            // E#id
        {
            lString16 val = node->getAttributeValue(attr_id);
            if ( val.empty() )
                return false;
            /*lString16 ldomDocumentFragmentWriter::convertId( lString16 id ) adds codeBasePrefix to
             *original id name, I can not get codeBasePrefix from here so I add a space to identify the
             *real id name.*/
            int pos = val.pos(" ");
            if (pos != -1) {
                val = val.substr(pos + 1, val.length() - pos - 1);
            }
            if (_value.length()>val.length())
                return false;
            return val == _value;
        }
        break;
    case cssrt_class:         // E.class
        {
            lString16 val = node->getAttributeValue(attr_class);
            if ( val.empty() )
                return false;
            // val.lowercase(); // className should be case sensitive
            // if ( val.length() != _value.length() )
            //     return false;
            //CRLog::trace("attr_class: %s %s", LCSTR(val), LCSTR(_value) );
            /*As I have eliminated leading and ending spaces in the attribute value, any space in
             *val means there are more than one classes */
            if (val.pos(" ") != -1) {
                lString16 value_w_space_after = _value + " ";
                if (val.pos(value_w_space_after) == 0)
                    return true; // at start
                lString16 value_w_space_before = " " + _value;
                int pos = val.pos(value_w_space_before);
                if (pos != -1 && pos + value_w_space_before.length() == val.length())
                    return true; // at end
                lString16 value_w_spaces_before_after = " " + _value + " ";
                if (val.pos(value_w_spaces_before_after) != -1)
                    return true; // in between
                return false;
            }
            return val == _value;
        }
        break;
    case cssrt_universal:     // *
        return true;
    case cssrt_pseudoclass:   // E:pseudo-class
        {
            int nodeId = node->getNodeId();
            int index = node->getNodeIndex();
            ldomNode * parent = node->getParentNode();
            switch (_attrid) {
                case csspc_first_child:
                case csspc_first_of_type:
                {
                    if (index>0) {
                        for (int i=index-1; i>=0; i--) {
                            ldomNode * elem = parent->getChildElementNode(i);
                            if ( elem ) // child before us
                                if (_attrid == csspc_first_child || elem->getNodeId() == nodeId)
                                    return false;
                        }
                    }
                    return true;
                }
                break;
                case csspc_last_child:
                case csspc_last_of_type:
                {
                    for (int i=index+1; i<parent->getChildCount(); i++) {
                        ldomNode * elem = parent->getChildElementNode(i);
                        if ( elem ) // child after us
                            if (_attrid == csspc_last_child || elem->getNodeId() == nodeId)
                                return false;
                    }
                    return true;
                }
                break;
                case csspc_nth_child:
                case csspc_nth_of_type:
                {
                    int n = 0;
                    for (int i=0; i<index; i++) {
                        ldomNode * elem = parent->getChildElementNode(i);
                        if ( elem )
                            if (_attrid == csspc_nth_child || elem->getNodeId() == nodeId)
                                n++;
                    }
                    n++; // this is our position
                    if (_value == "even" && (n & 1)==0)
                        return true;
                    if (_value == "odd" && (n & 1)==1)
                        return true;
                    // other values ( 5, 5n3...) not supported (yet)
                    return false;
                }
                break;
                case csspc_nth_last_child:
                case csspc_nth_last_of_type:
                {
                    int n = 0;
                    for (int i=parent->getChildCount()-1; i>index; i--) {
                        ldomNode * elem = parent->getChildElementNode(i);
                        if ( elem )
                            if (_attrid == csspc_nth_last_child || elem->getNodeId() == nodeId)
                                n++;
                    }
                    n++; // this is our position
                    if (_value == "even" && (n & 1)==0)
                        return true;
                    if (_value == "odd" && (n & 1)==1)
                        return true;
                    // other values ( 5, 5n3...) not supported (yet)
                    return false;
                }
                break;
                case csspc_only_child:
                case csspc_only_of_type:
                {
                    int n = 0;
                    for (int i=0; i<parent->getChildCount(); i++) {
                        ldomNode * elem = parent->getChildElementNode(i);
                        if ( elem )
                            if (_attrid == csspc_only_child || elem->getNodeId() == nodeId) {
                                n++;
                                if (n > 1)
                                    break;
                            }
                    }
                    if (n > 1)
                        return false;
                    return true;
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
    // check main Id
    if (_id!=0 && node->getNodeId() != _id)
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
    if (*str=='\"')
    {
        str++;
        for ( ; str[pos] && str[pos]!='\"'; pos++)
        {
            if (pos>=64)
                return false;
        }
        if (str[pos]!='\"')
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
        if (!parse_ident( str, attrvalue ))
            return NULL;
        LVCssSelectorRule * rule = new LVCssSelectorRule(cssrt_class);
        lString16 s( attrvalue );
        // s.lowercase(); // className should be case sensitive
        rule->setAttr(attr_class, s);
        return rule;
    } else if ( *str=='#' ) {
        // E#id
        str++;
        if (!parse_ident( str, attrvalue ))
            return NULL;
        LVCssSelectorRule * rule = new LVCssSelectorRule(cssrt_id);
        lString16 s( attrvalue );
        rule->setAttr(attr_id, s);
        return rule;
    } else if ( *str==':' ) {
        // E:pseudo-class (eg: E:first-child)
        str++;
        if (*str==':')   // pseudo element (double ::, eg: E::first-line) are not supported
            return NULL;
        int n = parse_name( str, css_pseudo_classes, -1 );
        if (n == -1) // not one of out supported pseudo classes
            return NULL;
        attrvalue[0] = 0;
        if (*str=='(') { // parse () content
            str++;
            if ( !parse_attr_value( str, attrvalue, ')') )
                return NULL;
            // we don't parse the value here, it may have specific meaning
            // per pseudo-class type
        }
        LVCssSelectorRule * rule = new LVCssSelectorRule(cssrt_pseudoclass);
        lString16 s( attrvalue );
        rule->setAttr(n, s);
        // printf("made pseudo class rule %d with %s\n", n, UnicodeToLocal(s).c_str());
        if ( n >= csspc_last_child ) {
            // Pseudoclasses after csspc_last_child can't be accurately checked
            // in the initial loading phase: a re-render will be needed.
            doc->setNodeStylesInvalidIfLoading();
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
    if (!parse_ident( str, attrname ))
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
    lString16 s( attrvalue );
    if (parse_trailing_i) { // cssrt_attr*_i met
        s.lowercase();
    }
    lUInt16 id = doc->getAttrNameIndex( lString16(attrname).c_str() );
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
        else if ( *str == '#' ) // node Id follows
        {
            _id = 0; // (elementName internal id)
        }
        else if ( css_is_alpha( *str ) ) // element name follows
        {
            // ident
            char ident[64];
            if (!parse_ident( str, ident ))
                return false;
            // All element names have been lowercased by HTMLParser (except
            // a few ones that are added explicitely by crengine): we need
            // to lowercase them here too to expect a match.
            lString16 element(ident);
            if ( element.length() < 8 ) {
                // Avoid following string comparisons if element
                // is shorter than the shortest of them (floatBox)
                element = element.lowercase();
            }
            else if (element != "DocFragment" && element != "autoBoxing" & element != "floatBox" && element != "FictionBook" ) {
                element = element.lowercase();
            }
            _id = doc->getElementNameIndex( element.c_str() );
                // Note: non standard element names (not listed in fb2def.h) in
                // selectors (eg: blah {font-style: italic}) may have different values
                // returned by getElementNameIndex() across book loadings, and cause:
                // "cached rendering is invalid (style hash mismatch): doing full rendering"
            _specificity += 1; // we have an element: this adds 1 to specificity
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
                if (!rule)
                    return false;
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

LVCssSelectorRule::LVCssSelectorRule( LVCssSelectorRule & v )
: _type(v._type), _id(v._id), _attrid(v._attrid)
, _next(NULL)
, _value( v._value )
{
    if ( v._next )
        _next = new LVCssSelectorRule( *v._next );
}

LVCssSelector::LVCssSelector( LVCssSelector & v )
: _id(v._id), _decl(v._decl), _specificity(v._specificity), _next(NULL), _rules(NULL)
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
{
    set( sheet._selectors );
}

void LVStyleSheet::apply( const ldomNode * node, css_style_rec_t * style )
{
    if (!_selectors.length())
        return; // no rules!
        
    lUInt16 id = node->getNodeId();
    
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

lUInt32 LVCssSelector::getHash()
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

bool LVStyleSheet::parse( const char * str, bool higher_importance, lString16 codeBase )
{
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
            selector = new LVCssSelector;
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
                for (LVCssSelector * p = selector; p; p=p->getNext())
                    p->setDeclaration( decl );
                rule_count++;
            }
            break;
        }
        if (err)
        {
            // error:
            // delete chain of selectors
            delete selector;
            // ignore current rule
            skip_until_end_of_rule( str );
        }
        else
        {
            // Ok:
            // place rules to sheet
            for (LVCssSelector * p = selector; p;  )
            {
                LVCssSelector * item = p;
                p=p->getNext();
                lUInt16 id = item->getElementNameId();
                if (_selectors.length()<=id)
                    _selectors.set(id, NULL);
                // insert with specificity sorting
                if ( _selectors[id] == NULL 
                    || _selectors[id]->getSpecificity() > item->getSpecificity() )
                {
                    // insert as first item
                    item->setNext( _selectors[id] );
                    _selectors[id] = item;
                }
                else
                {
                    // insert as internal item
                    for (LVCssSelector * p = _selectors[id]; p; p = p->getNext() )
                    {
                        if ( p->getNext() == NULL
                            || p->getNext()->getSpecificity() > item->getSpecificity() )
                        {
                            item->setNext( p->getNext() );
                            p->setNext( item );
                            break;
                        }
                    }
                }
            }
        }
    }
    return _selectors.length() > 0;
}

/// extract @import filename from beginning of CSS
bool LVProcessStyleSheetImport( const char * &str, lString8 & import_file )
{
    const char * p = str;
    import_file.clear();
    skip_spaces( p );
    if ( *p !='@' )
        return false;
    p++;
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
    // Remove trailing ';' at end of "@import url(..);"
    skip_spaces( p );
    if ( *p==';' )
        p++;
    if ( import_file.empty() )
        return false;
    str = p;
    return true;
}

/// load stylesheet from file, with processing of import
bool LVLoadStylesheetFile( lString16 pathName, lString8 & css )
{
    LVStreamRef file = LVOpenFileStream( pathName.c_str(), LVOM_READ );
    if ( file.isNull() )
        return false;
    lString8 txt = UnicodeToUtf8( LVReadTextFile( file ) );
    lString8 txt2;
    const char * s = txt.c_str();
    lString8 import_file;
    if ( LVProcessStyleSheetImport( s, import_file ) ) {
        lString16 importFilename = LVMakeRelativeFilename( pathName, Utf8ToUnicode(import_file) );
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
