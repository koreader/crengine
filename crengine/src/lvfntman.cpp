/** \file lvfntman.cpp
    \brief font manager implementation

    CoolReader Engine


    (c) Vadim Lopatin, 2000-2006
    This source code is distributed under the terms of
    GNU General Public License.

    See LICENSE file for details.

*/

#include <stdlib.h>
#include <stdio.h>



#include "../include/crsetup.h"
#include "../include/lvfntman.h"
#include "../include/lvstream.h"
#include "../include/lvdrawbuf.h"
#include "../include/lvstyles.h"
#include "../include/lvthread.h"

// Uncomment for debugging text measurement or drawing
// #define DEBUG_MEASURE_TEXT
// #define DEBUG_DRAW_TEXT

// define to filter out all fonts except .ttf
//#define LOAD_TTF_FONTS_ONLY
// DEBUG ONLY
#if 0
#define USE_FREETYPE 1
#define USE_FONTCONFIG 1
//#define DEBUG_FONT_SYNTHESIS 1
//#define DEBUG_FONT_MAN 1
//#define DEBUG_FONT_MAN_LOG_FILE "/tmp/font_man.log"
#endif

#define GAMMA_TABLES_IMPL
#include "../include/gammatbl.h"

#if (USE_FREETYPE==1)

#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftoutln.h>    // for FT_Outline_Embolden()
#include <freetype/ftsynth.h>    // for FT_GlyphSlot_Oblique()
#include <freetype/ftglyph.h>    // for FT_Matrix_Multiply()
#include <freetype/tttables.h>   // for FT_Get_Sfnt_Table()

// Use Freetype embolden API instead of LVFontBoldTransform to
// make fake bold (for fonts that do not provide a bold face).
// This gives a chance to get them working with Harfbuzz, even if
// they won't look as nice as if they came with a real bold font.
#define USE_FT_EMBOLDEN

#if USE_HARFBUZZ==1
#include <hb.h>
#include <hb-ft.h>
#include <hb-ot.h>
#include "lvhashtable.h"
#endif

#if (USE_FONTCONFIG==1)
    #include <fontconfig/fontconfig.h>
#endif

#endif

#if USE_FREETYPE==1
// These svg path helpers are for use by LVFreeTypeFace->collectGlyphSVGPath()

#if USE_HARFBUZZ!=1
// So we can use the next functions in collectGlyphSVGPath() when not using Harfbuzz but FreeType only
typedef void hb_draw_funcs_t;
typedef void hb_draw_state_t;
#endif

static void ft_error_trace(const char *where, const char *call, FT_Error error) {
    const char *errstr = FT_Error_String(error);
    if (!errstr) {
        static char hex[17];
        snprintf(hex, sizeof (hex), "%#x", error);
        errstr = hex;
    }
    fprintf(stderr, "CRE: %s: %s failed: %s\n", where, call, errstr);
}

// Note: y are inverted as the glyphs shapes are in a mirrored coordinates system
static void SVGGlyphsCollector_svg_move_to(hb_draw_funcs_t *, SVGGlyphsCollector *collector, hb_draw_state_t *,
                                        float to_x, float to_y, void *) {
    double scale = collector->_scale;
    double offset_x = collector->_offset_x;
    double offset_y = collector->_offset_y;
    char svg_path_step[64];
    snprintf(svg_path_step, 64, "M%g,%g",
                     offset_x + scale * to_x,
                     offset_y - scale * to_y);
    collector->addPathDfragment(svg_path_step);
}

static void SVGGlyphsCollector_svg_line_to(hb_draw_funcs_t *, SVGGlyphsCollector *collector, hb_draw_state_t *,
                                        float to_x, float to_y, void *) {
    double scale = collector->_scale;
    double offset_x = collector->_offset_x;
    double offset_y = collector->_offset_y;
    char svg_path_step[64];
    snprintf(svg_path_step, 64, "L%g,%g",
                     offset_x + scale * to_x,
                     offset_y - scale * to_y);
    collector->addPathDfragment(svg_path_step);
}

static void SVGGlyphsCollector_svg_quadratic_to(hb_draw_funcs_t *, SVGGlyphsCollector *collector, hb_draw_state_t *,
                                        float control_x, float control_y, float to_x, float to_y, void *) {
    double scale = collector->_scale;
    double offset_x = collector->_offset_x;
    double offset_y = collector->_offset_y;
    char svg_path_step[128];
    snprintf(svg_path_step, 128, "Q%g,%g,%g,%g",
                     offset_x + scale * control_x,
                     offset_y - scale * control_y,
                     offset_x + scale * to_x,
                     offset_y - scale * to_y);
    collector->addPathDfragment(svg_path_step);
}

static void SVGGlyphsCollector_svg_cubic_to(hb_draw_funcs_t *, SVGGlyphsCollector *collector, hb_draw_state_t *,
                                        float control1_x, float control1_y, float control2_x, float control2_y,
                                        float to_x, float to_y, void *) {
    double scale = collector->_scale;
    double offset_x = collector->_offset_x;
    double offset_y = collector->_offset_y;
    char svg_path_step[192];
    snprintf(svg_path_step, 192, "C%g,%g,%g,%g,%g,%g",
                     offset_x + scale * control1_x,
                     offset_y - scale * control1_y,
                     offset_x + scale * control2_x,
                     offset_y - scale * control2_y,
                     offset_x + scale * to_x,
                     offset_y - scale * to_y);
    collector->addPathDfragment(svg_path_step);
}

static void SVGGlyphsCollector_svg_close_path(hb_draw_funcs_t *, SVGGlyphsCollector *collector, hb_draw_state_t *, void *) {
    collector->addPathDfragment("Z");
}

// These two are not for use with hb_draw_funcs_t, but used by collectGlyphSVGPath() with FreeType only,
// so we get the same kind of signatures as the ones above (used with Harfbuzz and FreeType).
static void SVGGlyphsCollector_svg_continue_to(hb_draw_funcs_t *, SVGGlyphsCollector *collector, hb_draw_state_t *,
                                        float to_x, float to_y, void *) {
    double scale = collector->_scale;
    double offset_x = collector->_offset_x;
    double offset_y = collector->_offset_y;
    char svg_path_step[64];
    snprintf(svg_path_step, 64, " %g,%g",
                     offset_x + scale * to_x,
                     offset_y - scale * to_y);
    collector->addPathDfragment(svg_path_step);
}
static void SVGGlyphsCollector_svg_continue_to(hb_draw_funcs_t *, SVGGlyphsCollector *collector, hb_draw_state_t *,
                                        float control_x, float control_y, float to_x, float to_y, void *) {
    double scale = collector->_scale;
    double offset_x = collector->_offset_x;
    double offset_y = collector->_offset_y;
    char svg_path_step[128];
    snprintf(svg_path_step, 128, " %g,%g,%g,%g",
                     offset_x + scale * control_x,
                     offset_y - scale * control_y,
                     offset_x + scale * to_x,
                     offset_y - scale * to_y);
    collector->addPathDfragment(svg_path_step);
}

#if USE_HARFBUZZ==1

// For use with hb_font_draw_glyph()
static hb_draw_funcs_t *SVGGlyphsCollector_svg_funcs = NULL;

static void setup_SVGGlyphsCollector_svg_funcs() {
    if ( SVGGlyphsCollector_svg_funcs != NULL )
        return;
    SVGGlyphsCollector_svg_funcs = hb_draw_funcs_create();
    hb_draw_funcs_set_move_to_func(      SVGGlyphsCollector_svg_funcs, (hb_draw_move_to_func_t)      SVGGlyphsCollector_svg_move_to, nullptr, nullptr);
    hb_draw_funcs_set_line_to_func(      SVGGlyphsCollector_svg_funcs, (hb_draw_line_to_func_t)      SVGGlyphsCollector_svg_line_to, nullptr, nullptr);
    hb_draw_funcs_set_quadratic_to_func( SVGGlyphsCollector_svg_funcs, (hb_draw_quadratic_to_func_t) SVGGlyphsCollector_svg_quadratic_to, nullptr, nullptr);
    hb_draw_funcs_set_cubic_to_func(     SVGGlyphsCollector_svg_funcs, (hb_draw_cubic_to_func_t)     SVGGlyphsCollector_svg_cubic_to, nullptr, nullptr);
    hb_draw_funcs_set_close_path_func(   SVGGlyphsCollector_svg_funcs, (hb_draw_close_path_func_t)   SVGGlyphsCollector_svg_close_path, nullptr, nullptr);
    hb_draw_funcs_make_immutable(SVGGlyphsCollector_svg_funcs);
}

static void destroy_SVGGlyphsCollector_svg_funcs() {
    if ( SVGGlyphsCollector_svg_funcs == NULL )
        return;
    hb_draw_funcs_destroy(SVGGlyphsCollector_svg_funcs);
    SVGGlyphsCollector_svg_funcs = NULL;
}

#endif

#endif // USE_FREETYPE==1

// Helpers with font metrics (units are 1/64 px)
// #define FONT_METRIC_FLOOR(x)    ((x) & -64)
// #define FONT_METRIC_CEIL(x)     (((x)+63) & -64)
// #define FONT_METRIC_ROUND(x)    (((x)+32) & -64)
// #define FONT_METRIC_TRUNC(x)    ((x) >> 6)
#define FONT_METRIC_TO_PX(x)    (((x)+32) >> 6) // ROUND + TRUNC
// Uncomment to use the former >>6 (trunc) with no rounding (instead of previous one)
// #define FONT_METRIC_TO_PX(x)    ((x) >> 6)

#if COLOR_BACKBUFFER==0
//#define USE_BITMAP_FONT
#endif

#define MAX_LINE_CHARS 2048
#define MAX_LINE_WIDTH 2048
#define MAX_LETTER_SPACING MAX_LINE_WIDTH/2

// For use by getBulletListItemFont(): list of possibly shipped fonts with consistent glyphs
// (enough body but not too heavy, normal vertical position, small left and right spacing)
// for disc (U+2022), circle (U+25E6) and square (U+25AA), to be used instead of the normal
// list item node font for drawing its marker.
// FreeSans is preferred as it has these glyphs larger than FreeSerif; both have all of the needed glyphs.
// (Times New Roman has all three, but the glyphs are a bit too up to be interesting.)
static const char * PREFERRED_BULLET_FONTS = "FreeSans, FreeSerif";

//DEFINE_NULL_REF( LVFont )


inline int myabs(int n) { return n < 0 ? -n : n; }

LVFontManager * fontMan = NULL;

static double gammaLevel = 1.0;
static int gammaIndex = GAMMA_NO_CORRECTION_INDEX;

/// returns first found face from passed list, or return face for font found by family only
lString8 LVFontManager::findFontFace(lString8 commaSeparatedFaceList, css_font_family_t fallbackByFamily) {
    // faces we want
    lString8Collection list;
    splitPropertyValueList(commaSeparatedFaceList.c_str(), list);
    // faces we have
    lString32Collection faces;
    getFaceList(faces);
    // find first matched
    for (int i = 0; i < list.length(); i++) {
        lString8 wantFace = list[i];
        for (int j = 0; j < faces.length(); j++) {
            lString32 haveFace = faces[j];
            if (wantFace == haveFace)
                return wantFace;
        }
    }
    // not matched - get by family name
    LVFontRef fnt = GetFont(10, 400, false, fallbackByFamily, lString8("Arial"));
    if (fnt.isNull())
        return lString8::empty_str; // not found
    // get face from found font
    return fnt->getTypeFace();
}

/// fills array with list of available gamma levels
void LVFontManager::GetGammaLevels(LVArray<double> dst) {
    dst.clear();
    for ( int i=0; i<GAMMA_LEVELS; i++ )
        dst.add(cr_gamma_levels[i]);
}

/// returns current gamma level index
int  LVFontManager::GetGammaIndex() {
    return gammaIndex;
}

/// sets current gamma level index
void LVFontManager::SetGammaIndex( int index ) {
    if ( index<0 )
        index = 0;
    if ( index>=GAMMA_LEVELS )
        index = GAMMA_LEVELS-1;
    if ( gammaIndex!=index ) {
        CRLog::trace("FontManager gamma index changed from %d to %d", gammaIndex, index);
        gammaIndex = index;
        gammaLevel = cr_gamma_levels[index];
        gc();
        clearGlyphCache();
    }
}

/// returns current gamma level
double LVFontManager::GetGamma() {
    return gammaLevel;
}

/// sets current gamma level
void LVFontManager::SetGamma( double gamma ) {
    // gammaLevel = cr_ft_gamma_levels[GAMMA_LEVELS/2];
    // gammaIndex = GAMMA_LEVELS/2;
    int oldGammaIndex = gammaIndex;
    for ( int i=0; i<GAMMA_LEVELS; i++ ) {
        double diff1 = cr_gamma_levels[i] - gamma;
        if ( diff1<0 ) diff1 = -diff1;
        double diff2 = gammaLevel - gamma;
        if ( diff2<0 ) diff2 = -diff2;
        if ( diff1 < diff2 ) {
            gammaLevel = cr_gamma_levels[i];
            gammaIndex = i;
        }
    }
    if ( gammaIndex!=oldGammaIndex ) {
        CRLog::trace("FontManager gamma index changed from %d to %d", oldGammaIndex, gammaIndex);
        gc();
        clearGlyphCache();
    }
}


////////////////////////////////////////////////////////////////////

static const char * EMBEDDED_FONT_LIST_MAGIC = "FNTL";
static const char * EMBEDDED_FONT_DEF_MAGIC = "FNTD";

////////////////////////////////////////////////////////////////////
// LVEmbeddedFontDef
////////////////////////////////////////////////////////////////////
bool LVEmbeddedFontDef::serialize(SerialBuf & buf) {
    buf.putMagic(EMBEDDED_FONT_DEF_MAGIC);
    buf << _url << _face << _bold << _italic;
    return !buf.error();
}

bool LVEmbeddedFontDef::deserialize(SerialBuf & buf) {
    if (!buf.checkMagic(EMBEDDED_FONT_DEF_MAGIC))
        return false;
    buf >> _url >> _face >> _bold >> _italic;
    return !buf.error();
}

////////////////////////////////////////////////////////////////////
// LVEmbeddedFontList
////////////////////////////////////////////////////////////////////
LVEmbeddedFontDef * LVEmbeddedFontList::findByUrl(lString32 url) {
    for (int i=0; i<length(); i++) {
        if (get(i)->getUrl() == url)
            return get(i);
    }
    return NULL;
}

bool LVEmbeddedFontList::addAll(LVEmbeddedFontList & list) {
    bool changed = false;
    for (int i=0; i<list.length(); i++) {
        LVEmbeddedFontDef * def = list.get(i);
        changed = add(def->getUrl(), def->getFace(), def->getBold(), def->getItalic()) || changed;
    }
    return changed;
}

bool LVEmbeddedFontList::add(lString32 url, lString8 face, bool bold, bool italic) {
    LVEmbeddedFontDef * def = findByUrl(url);
    if (def) {
        bool changed = false;
        if (def->getFace() != face) {
            def->setFace(face);
            changed = true;
        }
        if (def->getBold() != bold) {
            def->setBold(bold);
            changed = true;
        }
        if (def->getItalic() != italic) {
            def->setItalic(italic);
            changed = true;
        }
        return changed;
    }
    def = new LVEmbeddedFontDef(url, face, bold, italic);
    add(def);
    return false;
}

bool LVEmbeddedFontList::serialize(SerialBuf & buf) {
    buf.putMagic(EMBEDDED_FONT_LIST_MAGIC);
    lUInt32 count = length();
    buf << count;
    for (lUInt32 i = 0; i < count; i++) {
        get(i)->serialize(buf);
        if (buf.error())
            return false;
    }
    return !buf.error();
}

bool LVEmbeddedFontList::deserialize(SerialBuf & buf) {
    if (!buf.checkMagic(EMBEDDED_FONT_LIST_MAGIC))
        return false;
    lUInt32 count = 0;
    buf >> count;
    if (buf.error())
        return false;
    for (lUInt32 i = 0; i < count; i++) {
        LVEmbeddedFontDef * item = new LVEmbeddedFontDef();
        if (!item->deserialize(buf)) {
            delete item;
            return false;
        }
        add(item);
    }
    return !buf.error();
}

////////////////////////////////////////////////////////////////////


/**
 * Max width of -/./,/!/? to use for visial alignment by width
 */
int LVFont::getVisualAligmentWidth() {
    FONT_GUARD
    if ( _visual_alignment_width==-1 ) {
        lChar32 chars[] = { getHyphChar(), ',', '.', '!', '?', ':', ';',
                    (lChar32)U'，', (lChar32)U'。', (lChar32)U'！', 0 };
        int maxw = 0;
        for ( int i=0; chars[i]; i++ ) {
            int w = getCharWidth( chars[i] );
            if ( w > maxw )
                maxw = w;
        }
        _visual_alignment_width = maxw;
    }
    return _visual_alignment_width;
}

static lChar32 getReplacementChar(lUInt32 code, bool * can_be_ignored = NULL) {
    switch (code) {
    case UNICODE_SOFT_HYPHEN_CODE:
        return '-';
    case 0x0401: // CYRILLIC CAPITAL LETTER IO
        return 0x0415; //CYRILLIC CAPITAL LETTER IE
    case 0x0451: // CYRILLIC SMALL LETTER IO
        return 0x0435; // CYRILLIC SMALL LETTER IE
    case UNICODE_NO_BREAK_SPACE:
        return ' ';
    case UNICODE_WORD_JOINER:
        if (can_be_ignored)
            *can_be_ignored = true;
        return UNICODE_ZERO_WIDTH_SPACE;
    case UNICODE_ZERO_WIDTH_SPACE:
        if (can_be_ignored)
            *can_be_ignored = true;
        // If the font lacks a zero-width breaking space glyph (like
        // some Kindle built-ins) substitute a different zero-width
        // character instead of one with width.
        return UNICODE_ZERO_WIDTH_NO_BREAK_SPACE;
    case 0x2000: // Other Unicode non-zero-fixed-width spaces
    case 0x2001:
    case 0x2002:
    case 0x2003:
    case 0x2004:
    case 0x2005:
    case 0x2006:
    case 0x2007:
    case 0x2008:
    case 0x2009: // thin space: let's keep using a space rather than zero width
    case 0x200a: // hair space                        even for these small ones
    case 0x202f:
    case 0x205f:
    case 0x3000:
        return ' ';
    case 0x202a: // Unicode BiDi directionnal formatting characters
    case 0x202b:
    case 0x202c:
    case 0x202d:
    case 0x202e:
    case 0x2066:
    case 0x2067:
    case 0x2068:
    case 0x2069:
        if (can_be_ignored)
            *can_be_ignored = true;
        return UNICODE_ZERO_WIDTH_SPACE;
    case 0x2010:
    case 0x2011:
    case 0x2012:
    case 0x2013:
    case 0x2014:
    case 0x2015:
        return '-';
    case 0x2018:
    case 0x2019:
    case 0x201a:
    case 0x201b:
        return '\'';
    case 0x201c:
    case 0x201d:
    case 0x201e:
    case 0x201f:
    case 0x00ab:
    case 0x00bb:
        return '\"';
    case 0x2039:
        return '<';
    case 0x203A:
        return '>';
    case 0x2044:
        return '/';
    case 0x2022: // css_lst_disc:
    case 0x26AB: // css_lst_disc:
    case 0x2981: // css_lst_disc:
    case 0x25CF: // css_lst_disc:
        return '*';
    case 0x26AA: // css_lst_circle:
    case 0x25E6: // css_lst_circle:
    case 0x26AC: // css_lst_circle:
    case 0x25CB: // css_lst_circle:
        return 'o';
    case 0x25A0: // css_lst_square:
    case 0x25AA: // css_lst_square:
    case 0x25FE: // css_lst_square:
        return '-';
    case 0x21AF: // DOWNWARDS ZIGZAG ARROW
    case 0x26A1: // HIGH VOLTAGE SIGN
    case 0x2B4D: // DOWNWARDS TRIANGLE-HEADED ZIGZAG ARROW
    case 0x1F5F2:// LIGHTNING MOOD
        return '+';
    default:
        break;
    }
    return 0;
}


#if USE_HARFBUZZ==1
bool isHBScriptCursive( hb_script_t script ) {
    // https://github.com/harfbuzz/harfbuzz/issues/64
    // From https://android.googlesource.com/platform/frameworks/minikin/
    //               +/refs/heads/experimental/libs/minikin/Layout.cpp
    return  script == HB_SCRIPT_ARABIC ||
            script == HB_SCRIPT_NKO ||
            script == HB_SCRIPT_PSALTER_PAHLAVI ||
            script == HB_SCRIPT_MANDAIC ||
            script == HB_SCRIPT_MONGOLIAN ||
            script == HB_SCRIPT_PHAGS_PA ||
            script == HB_SCRIPT_DEVANAGARI ||
            script == HB_SCRIPT_BENGALI ||
            script == HB_SCRIPT_GURMUKHI ||
            script == HB_SCRIPT_MODI ||
            script == HB_SCRIPT_SHARADA ||
            script == HB_SCRIPT_SYLOTI_NAGRI ||
            script == HB_SCRIPT_TIRHUTA ||
            script == HB_SCRIPT_OGHAM;
}
#endif

// LVFontDef carries a font definition, and can be used to identify:
// - registered fonts, from available font files (size=-1 if scalable)
// - instantiated fonts from one of the registered fonts, with some
//   updated properties:
//     - the specific size, > -1
//     - _italic=2 (if font has no real italic, and it is synthesized
//       thanks to Freetype from the regular font glyphs)
//     - _weight=600 (updated weight if synthesized weight made from
//       the regular font glyphs)
// It can be used as a key by caches to retrieve a registered font
// or an instantiated one, and as a query to find in the cache an
// exact or an approximate font.

/**
    \brief Font properties definition
*/
class LVFontDef
{
private:
    int               _size;
    int               _weight;
    int               _italic;
    int               _features; // OpenType features requested
    css_font_family_t _family;
    lString8          _typeface;
    lString8          _name;
    int               _index;
    // for document font: _documentId, _buf, _name
    int               _documentId;
    LVByteArrayRef    _buf;
    int               _bias;
    bool _real_weight;
    // Store some info we can know about a font at registration time
    bool _has_ot_math;
    bool _has_emojis;
public:
    LVFontDef(const lString8 & name, int size, int weight, int italic, int features, css_font_family_t family,
                const lString8 & typeface, int index=-1, int documentId=-1, LVByteArrayRef buf = LVByteArrayRef())
        : _size(size)
        , _weight(weight)
        , _italic(italic)
        , _features(features)
        , _family(family)
        , _typeface(typeface)
        , _name(name)
        , _index(index)
        , _documentId(documentId)
        , _buf(buf)
        , _bias(0)
        , _real_weight(true)
        , _has_ot_math(false)
        , _has_emojis(false)
        {
        }
    LVFontDef(const LVFontDef & def)
        : _size(def._size)
        , _weight(def._weight)
        , _italic(def._italic)
        , _features(def._features)
        , _family(def._family)
        , _typeface(def._typeface)
        , _name(def._name)
        , _index(def._index)
        , _documentId(def._documentId)
        , _buf(def._buf)
        , _bias(def._bias)
        , _real_weight(def._real_weight)
        , _has_ot_math(def._has_ot_math)
        , _has_emojis(def._has_emojis)
        {
        }

    /// returns true if definitions are equal
    bool operator == ( const LVFontDef & def ) const
    {
        return ( _size == def._size || _size == -1 || def._size == -1 )
            && ( _weight == def._weight || _weight==-1 || def._weight==-1 )
            && ( _italic == def._italic || _italic==-1 || def._italic==-1 )
            && _real_weight == def._real_weight
            && _features == def._features
            && _family == def._family
            && _typeface == def._typeface
            && _name == def._name
            && ( _index == def._index || def._index == -1 )
            && (_documentId == def._documentId || _documentId == -1)
            ;
    }

    lUInt32 getHash() const {
        lUInt32 h =  (((((_size * 31) + _weight)*31  + _italic)*31 + _features)*31+ _family)*31 + _name.getHash();
        // _bias is not a static property, and can be set/unset on these definitions.
        // If a same _bias value is moved from one font to another, *adding* _bias
        // to this hash may result in a final identical GetFontListHash() value
        // (as GetFontListHash() does add all these hashes).
        // We need to use it a multiplicator to be sure it changes GetFontListHash().
        if ( _bias > 0 )
            h *= _bias + 1;
        return h;
    }

    /// returns font file name
    lString8 getName() const { return _name; }
    void setName( lString8 name) {  _name = name; }
    int getIndex() const { return _index; }
    void setIndex( int index ) { _index = index; }
    int getSize() const { return _size; }
    void setSize( int size ) { _size = size; }
    int getWeight() const { return _weight; }
    void setWeight(int weight, bool real = true) { _weight = weight; _real_weight = real; }
    bool isRealWeight() const { return _real_weight; }
    bool getItalic() const { return _italic!=0; }
    bool isRealItalic() const { return _italic==1; }
    void setItalic( int italic ) { _italic=italic; }
    css_font_family_t getFamily() const { return _family; }
    void getFamily( css_font_family_t family ) { _family = family; }
    lString8 getTypeFace() const { return _typeface; }
    void setTypeFace(lString8 tf) { _typeface = tf; }
    int getFeatures() const { return _features; }
    void setFeatures( int features ) { _features = features; }
    bool hasOTMath() const { return _has_ot_math; }
    void setHasOTMath(bool has_ot_math) { _has_ot_math = has_ot_math; }
    bool hasEmojis() const { return _has_emojis; }
    void setHasEmojis(bool has_emojis) { _has_emojis = has_emojis; }
    int getDocumentId() const { return _documentId; }
    void setDocumentId(int id) { _documentId = id; }
    LVByteArrayRef getBuf() const { return _buf; }
    void setBuf(LVByteArrayRef buf) { _buf = buf; }
    ~LVFontDef() {}
    /// calculates difference between two fonts
    int CalcMatch( const LVFontDef & def, bool useBias ) const;
    /// difference between fonts for duplicates search
    int CalcDuplicateMatch( const LVFontDef & def ) const;
    /// calc match for fallback font search
    int CalcFallbackMatch( lString8 face, int size ) const;

    bool setBiasIfNameMatch( lString8 facename, int bias, bool clearIfNot=true ) {
        if (_typeface.compare(facename) == 0) {
            _bias = bias;
            return true;
        }
        if (clearIfNot) {
            _bias = 0; // reset bias for other fonts
        }
        return false;
    }
};

/// font cache item
class LVFontCacheItem
{
    friend class LVFontCache;
    LVFontDef _def;
    LVFontRef _fnt;
public:
    LVFontDef * getDef() { return &_def; }
    LVFontRef & getFont() { return _fnt; }
    void setFont(LVFontRef & fnt) { _fnt = fnt; }
    LVFontCacheItem( const LVFontDef & def )
        : _def( def )
        { }
};

/// font cache
class LVFontCache
{
    LVPtrVector< LVFontCacheItem > _registered_list;
    LVPtrVector< LVFontCacheItem > _instance_list;
public:
    void clear() {
        // First: execute a garbage collection pass.
        gc();
        // Finally: clear remaining references.
        int i;
        for (i = _registered_list.length(); i--; ) {
#ifndef NDEBUG
            LVFontRef &fnt = _registered_list[i]->_fnt;
            assert(fnt.isNull() || fnt.getRefCount() == 1);
#endif
            delete _registered_list.remove(i);
        }
        for (i = _instance_list.length(); i--; ) {
#ifndef NDEBUG
            LVFontRef &fnt = _instance_list[i]->_fnt;
            assert(fnt.isNull() || fnt.getRefCount() == 1);
#endif
            delete _instance_list.remove(i);
        }
    }
    void gc(); // garbage collector
    void update( const LVFontDef * def, LVFontRef ref );
    void _removeDocumentFonts(int documentId, LVPtrVector< LVFontCacheItem > &list);
    void removeDocumentFonts(int documentId);
    void getAvailableFontWeights(LVArray<int>& weights, lString8 faceName);
    int  length() const { return _registered_list.length(); }
    void addInstance( const LVFontDef * def, LVFontRef ref );
    bool setAsPreferredFontWithBias( lString8 face, int bias, bool clearOthersBias );
    LVPtrVector< LVFontCacheItem > * getInstances() { return &_instance_list; }
    LVFontCacheItem * find( const LVFontDef * def, bool useBias=false );
    LVFontCacheItem * findFallback( lString8 face, int size );
    LVFontCacheItem * findDuplicate( const LVFontDef * def );
    LVFontCacheItem * findDocumentFontDuplicate(int documentId, lString8 name);

    /// get hash of installed fonts and fallback font
    virtual lUInt32 GetFontListHash(int documentId)
    {
        lUInt32 hash = 0;
        for ( int i=0; i<_registered_list.length(); i++ ) {
            int doc = _registered_list[i]->getDef()->getDocumentId();
            if (doc == -1 || doc == documentId) // skip document fonts
                hash = hash + _registered_list[i]->getDef()->getHash();
        }
        return hash;
    }
    virtual void getFaceList( lString32Collection & list )
    {
        list.clear();
        for ( int i=0; i<_registered_list.length(); i++ ) {
            if (_registered_list[i]->getDef()->getDocumentId() != -1)
                continue;
            lString32 name = Utf8ToUnicode( _registered_list[i]->getDef()->getTypeFace() );
            if ( !list.contains(name) )
                list.add( name );
        }
        list.sort();
    }
    virtual void getFontFileNameList(lString32Collection &list)
    {
        list.clear();
        for ( int i=0; i<_registered_list.length(); i++ ) {
            if (_registered_list[i]->getDef()->getDocumentId() == -1) {
                lString32 name = Utf8ToUnicode(_registered_list[i]->getDef()->getName());
                if (!list.contains(name))
                    list.add(name);
            }
        }
        list.sort();
    }
    virtual bool getFontFileNameAndFaceIndex( lString32 name, bool bold, bool italic, lString8 & filename, int & index, int & family_type, bool & has_ot_math, bool & has_emojis )
    {
        int base_weight = bold ? 700 : 400;
        LVFontDef * best_def = NULL;
        int best_delta = INT_MAX;
        for ( int i=0; i<_registered_list.length(); i++ ) {
            if (_registered_list[i]->getDef()->getDocumentId() == -1) {
                LVFontDef * fdef = _registered_list[i]->getDef();
                lString32 facename = Utf8ToUnicode( fdef->getTypeFace() );
                if (facename != name )
                    continue;
                if ( (italic && !fdef->isRealItalic()) || (!italic && fdef->isRealItalic()) )
                    continue;
                // We may meet multiple fonts with various weight: get the nearest.
                // We add +1 to get consistent results: when looking for 400, if both 300
                // and 500 are found, 300 will get -99 and 500 will get 101: we'll choose 300
                int delta = fdef->getWeight() - base_weight + 1;
                if ( !best_def || myabs(delta) < best_delta ) {
                    best_def = fdef;
                    best_delta = myabs(delta);
                }
            }
        }
        if ( best_def ) {
            filename = best_def->getName();
            index = best_def->getIndex();
            family_type = best_def->getFamily();
            has_ot_math = best_def->hasOTMath();
            has_emojis = best_def->hasEmojis();
            return true;
        }
        return false;
    }
    virtual void getRegisteredDocumentFontList( int document_id, lString32Collection & list )
    {
        list.clear();
        for ( int i=0; i<_registered_list.length(); i++ ) {
            if (_registered_list[i]->getDef()->getDocumentId() != document_id)
                continue;
            lString32 name = Utf8ToUnicode( _registered_list[i]->getDef()->getTypeFace() );
            if ( !list.contains(name) )
                list.add( name );
        }
        list.sort();
    }
    virtual void getInstantiatedDocumentFontList( int document_id, lString32Collection & list )
    {
        list.clear();
        for ( int i=0; i<_instance_list.length(); i++ ) {
            if (_instance_list[i]->getDef()->getDocumentId() != document_id)
                continue;
            lString32 name = Utf8ToUnicode( _instance_list[i]->getDef()->getTypeFace() );
            if ( !list.contains(name) )
                list.add( name );
        }
        list.sort();
    }
    virtual void clearFallbackFonts()
    {
        LVPtrVector< LVFontCacheItem > * fonts = getInstances();
        for ( int i=0; i<fonts->length(); i++ ) {
            LVFontRef fontRef = fonts->get(i)->getFont();
            if ( !fontRef.isNull() ) {
                fontRef->setFallbackFont(LVFontRef());
                fontRef->setNextFallbackFont(LVFontRef());
            }
        }
        for ( int i=0; i<_registered_list.length(); i++ ) {
            LVFontRef fontRef = _registered_list[i]->getFont();
            if ( !fontRef.isNull() ) {
                fontRef->setFallbackFont(LVFontRef());
                fontRef->setNextFallbackFont(LVFontRef());
            }
        }
    }
    virtual void regularizeRegisteredFontsWeights(bool print_updates=false)
    {
        // Make sure registered fonts have a proper entry at weight 400 and 700 when
        // possible, to avoid having synthesized fonts for these normal and bold weights.
        // This allows restoring a bit of the previous behaviour of crengine when it
        // wasn't handling font styles, and associated for each typeface one single
        // font to regular (400) and one to bold (700).
        // It should ensure we use real fonts (and not synthesized ones) for normal text
        // and bold text with the font_base_weight setting set to its default value of 400.
        class BestCandidates {
            public:
            LVFontCacheItem * regular;
            LVFontCacheItem * regular_fake_italic;
            LVFontCacheItem * regular_italic;
            LVFontCacheItem * bold;
            LVFontCacheItem * bold_fake_italic;
            LVFontCacheItem * bold_italic;
            BestCandidates()
                : regular(NULL), regular_fake_italic(NULL), regular_italic(NULL)
                , bold(NULL), bold_fake_italic(NULL), bold_italic(NULL)
                { }
            ~BestCandidates() { }
        };
        // Find the best registered font for each typeface (weight 400|700 , italic 0|1|2)
        // (each non-italic font got a clone with italic=2, in case fake italic is needed)
        LVHashTable<lString8, BestCandidates*> typefaces(20);
        for ( int i=0; i<_registered_list.length(); i++ ) {
            LVFontCacheItem * font = _registered_list[i];
            LVFontDef * def = font->getDef();
            if (def->getDocumentId() != -1)
                continue;
            lString8 name = def->getTypeFace();
            int weight = def->getWeight();
            int italic = def->getItalic() ? (def->isRealItalic() ? 1 : 2) : 0;
            BestCandidates * best;
            if ( !typefaces.get(name, best) ) {
                best = new BestCandidates();
                typefaces.set(name, best);
            }
            LVFontCacheItem ** slot;
            if ( weight < 700 ) {
                if ( italic == 0 )
                    slot = &best->regular;
                else if ( italic == 1 )
                    slot = &best->regular_italic;
                else
                    slot = &best->regular_fake_italic;
                // We want the nearest to 400, but gives preference to 500 over 300 by comparing against 401
                if ( *slot == NULL || (myabs(weight - 401) < myabs((*slot)->getDef()->getWeight() - 401)) ) {
                    *slot = font;
                }
            }
            else {
                if ( italic == 0 )
                    slot = &best->bold;
                else if ( italic == 1 )
                    slot = &best->bold_italic;
                else
                    slot = &best->bold_fake_italic;
                if ( *slot == NULL || (weight - 700 < (*slot)->getDef()->getWeight() - 700) ) {
                    *slot = font;
                }
            }
        }
        // Check each best candidates, and update their weight is it's not 400 or 700
        LVHashTable<lString8, BestCandidates*>::iterator it = typefaces.forwardIterator();
        LVHashTable<lString8, BestCandidates*>::pair* pair;
        while ( (pair = it.next()) ) {
            lString8 name = pair->key;
            BestCandidates * best = pair->value;
            if ( best->regular && best->regular->getDef()->getWeight() != 400 ) {
                printf("CRE: font %s regular: updated weight from %d to 400\n", name.c_str(), best->regular->getDef()->getWeight());
                best->regular->getDef()->setWeight(400);
            }
            if ( best->regular_italic && best->regular_italic->getDef()->getWeight() != 400 ) {
                printf("CRE: font %s regular italic: updated weight from %d to 400\n", name.c_str(), best->regular_italic->getDef()->getWeight());
                best->regular_italic->getDef()->setWeight(400);
            }
            if ( best->regular_fake_italic && best->regular_fake_italic->getDef()->getWeight() != 400 ) {
                // No printf needed, as each non-italic get one of these
                best->regular_fake_italic->getDef()->setWeight(400);
            }
            if ( best->bold && best->bold->getDef()->getWeight() != 700 ) {
                printf("CRE: font %s bold: updated weight from %d to 700\n", name.c_str(), best->bold->getDef()->getWeight());
                best->bold->getDef()->setWeight(700);
            }
            if ( best->bold_italic && best->bold_italic->getDef()->getWeight() != 700 ) {
                printf("CRE: font %s bold italic: updated weight from %d to 700\n", name.c_str(), best->bold_italic->getDef()->getWeight());
                best->bold_italic->getDef()->setWeight(700);
            }
            if ( best->bold_fake_italic && best->bold_fake_italic->getDef()->getWeight() != 700 ) {
                // No printf needed, as each non-italic get one of these
                best->bold_fake_italic->getDef()->setWeight(700);
            }

            delete pair->value;
        }

    }

    LVFontCache( )
    { }
    virtual ~LVFontCache() { }
};


#if (USE_FREETYPE==1)


#define CACHED_UNSIGNED_METRIC_NOT_SET 0xFFFF
class LVFontGlyphUnsignedMetricCache
{
private:
    static const int COUNT = 360;
    lUInt16 * ptrs[COUNT]; //support up to 0X2CFFF=360*512-1
public:
    lUInt16 get( lChar32 ch )
    {
        FONT_GLYPH_CACHE_GUARD
        int inx = (ch>>9) & 0x1ff;
        if (inx >= COUNT) return CACHED_UNSIGNED_METRIC_NOT_SET;
        lUInt16 * ptr = ptrs[inx];
        if ( !ptr )
            return CACHED_UNSIGNED_METRIC_NOT_SET;
        return ptr[ch & 0x1FF ];
    }
    void put( lChar32 ch, lUInt16 m )
    {
        FONT_GLYPH_CACHE_GUARD
        int inx = (ch>>9) & 0x1ff;
        if (inx >= COUNT) return;
        lUInt16 * ptr = ptrs[inx];
        if ( !ptr ) {
            ptr = new lUInt16[512];
            ptrs[inx] = ptr;
            memset( ptr, CACHED_UNSIGNED_METRIC_NOT_SET, sizeof(lUInt16) * 512 );
        }
        ptr[ ch & 0x1FF ] = m;
    }
    void clear()
    {
        FONT_GLYPH_CACHE_GUARD
        for ( int i=0; i<360; i++ ) {
            if ( ptrs[i] )
                delete [] ptrs[i];
            ptrs[i] = NULL;
        }
    }
    LVFontGlyphUnsignedMetricCache()
    {
        memset( ptrs, 0, 360*sizeof(lUInt16*) );
    }
    ~LVFontGlyphUnsignedMetricCache()
    {
        clear();
    }
};

#define CACHED_SIGNED_METRIC_NOT_SET 0x7FFF
#define CACHED_SIGNED_METRIC_SHIFT 0x8000
class LVFontGlyphSignedMetricCache : public LVFontGlyphUnsignedMetricCache
{
public:
    lInt16 get( lChar32 ch )
    {
        return (lInt16) ( LVFontGlyphUnsignedMetricCache::get(ch) - CACHED_SIGNED_METRIC_SHIFT );
    }
    void put( lChar32 ch, lInt16 m )
    {
        LVFontGlyphUnsignedMetricCache::put(ch, (lUInt16)( m + CACHED_SIGNED_METRIC_SHIFT) );
    }
};

class LVFreeTypeFace;

static LVFontGlyphCacheItem * newItem( LVFontLocalGlyphCache * local_cache, lChar32 ch, FT_GlyphSlot slot ) // , bool drawMonochrome
{
    FONT_LOCAL_GLYPH_CACHE_GUARD
    FT_Bitmap*  bitmap = &slot->bitmap;
    int w = bitmap->width;
    int h = bitmap->rows;
    LVFontGlyphCacheItem * item = LVFontGlyphCacheItem::newItem(local_cache, ch, w, h);
    if (!item)
        return 0;
    if ( bitmap->pixel_mode==FT_PIXEL_MODE_MONO ) { //drawMonochrome
        lUInt8 mask = 0x80;
        const lUInt8 * ptr = (const lUInt8 *)bitmap->buffer;
        lUInt8 * dst = item->bmp;
        //int rowsize = ((w + 15) / 16) * 2;
        for ( int y=0; y<h; y++ ) {
            const lUInt8 * row = ptr;
            mask = 0x80;
            for ( int x=0; x<w; x++ ) {
                *dst++ = (*row & mask) ? 0xFF : 00;
                mask >>= 1;
                if ( !mask && x!=w-1) {
                    mask = 0x80;
                    row++;
                }
            }
            ptr += bitmap->pitch;//rowsize;
        }
    }
    else {
        #if 0
        if ( bitmap->pixel_mode==FT_PIXEL_MODE_MONO ) {
            memset( item->bmp, 0, w*h );
            lUInt8 * srcrow = bitmap->buffer;
            lUInt8 * dstrow = item->bmp;
            for ( int y=0; y<h; y++ ) {
                lUInt8 * src = srcrow;
                for ( int x=0; x<w; x++ ) {
                    dstrow[x] =  ( (*src)&(0x80>>(x&7)) ) ? 255 : 0;
                    if ((x&7)==7)
                        src++;
                }
                srcrow += bitmap->pitch;
                dstrow += w;
            }
        } // else:
        #endif
        if (bitmap->buffer && w > 0 && h > 0) {
            memcpy( item->bmp, bitmap->buffer, w*h );
            // correct gamma
            if ( gammaIndex!=GAMMA_NO_CORRECTION_INDEX )
                cr_correct_gamma_buf(item->bmp, w*h, gammaIndex);
        }
    }
    item->origin_x =   (lInt16)slot->bitmap_left;
    item->origin_y =   (lInt16)slot->bitmap_top;
    item->advance =    (lUInt16)(FONT_METRIC_TO_PX( myabs(slot->metrics.horiAdvance) ));
    return item;
}

#if USE_HARFBUZZ==1
static LVFontGlyphCacheItem * newItem(LVFontLocalGlyphCache *local_cache, lUInt32 index, FT_GlyphSlot slot )
{
    FONT_LOCAL_GLYPH_CACHE_GUARD
    FT_Bitmap*  bitmap = &slot->bitmap;
    int w = bitmap->width;
    int h = bitmap->rows;
    LVFontGlyphCacheItem *item = LVFontGlyphCacheItem::newItem(local_cache, index, w, h);
    if (!item)
        return 0;
    if ( bitmap->pixel_mode==FT_PIXEL_MODE_MONO ) { //drawMonochrome
        lUInt8 mask = 0x80;
        const lUInt8 * ptr = (const lUInt8 *)bitmap->buffer;
        lUInt8 * dst = item->bmp;
        //int rowsize = ((w + 15) / 16) * 2;
        for ( int y=0; y<h; y++ ) {
            const lUInt8 * row = ptr;
            mask = 0x80;
            for ( int x=0; x<w; x++ ) {
                *dst++ = (*row & mask) ? 0xFF : 00;
                mask >>= 1;
                if ( !mask && x!=w-1) {
                    mask = 0x80;
                    row++;
                }
            }
            ptr += bitmap->pitch;//rowsize;
        }
    }
    else {
        if (bitmap->buffer && w > 0 && h > 0) {
            memcpy( item->bmp, bitmap->buffer, w*h );
            // correct gamma
            if ( gammaIndex!=GAMMA_NO_CORRECTION_INDEX )
                cr_correct_gamma_buf(item->bmp, w*h, gammaIndex);
        }
    }
    item->origin_x =   (lInt16)slot->bitmap_left;
    item->origin_y =   (lInt16)slot->bitmap_top;
    item->advance =    (lUInt16)(FONT_METRIC_TO_PX( myabs(slot->metrics.horiAdvance) ));
    return item;
}
#endif

// Each LVFontGlyphCacheItem is put in 2 caches:
// - the LVFontLocalGlyphCache LVFreeTypeFace->_glyph_cache of the
//   font it comes from
// - the LVFontGlobalGlyphCache LVFreeTypeFontManager->_globalCache of
//   the global and unique FontManager.
// The first one is used for quick iteration to find the glyph in a
// known font/size instance.
// The global one is used to limit the number of cached glyphs, globally
// across all fonts.
// When adding the glyph to the local cache, the local cache adds it
// to the global cache. When that happens, the global cache checks
// its max_size, and remove any LRU item, by deleting it from itself,
// and asking the relevant local cache to remove it too.

void LVFontLocalGlyphCache::clear()
{
    FONT_LOCAL_GLYPH_CACHE_GUARD
#if USE_GLYPHCACHE_HASHTABLE == 1
    LVHashTable<GlyphCacheItemData, struct LVFontGlyphCacheItem*>::iterator it = hashTable.forwardIterator();
    LVHashTable<GlyphCacheItemData, struct LVFontGlyphCacheItem*>::pair* pair;
    while ((pair = it.next()))
    {
        global_cache->remove(pair->value);
        LVFontGlyphCacheItem::freeItem(pair->value);
    }
    hashTable.clear();
#else
    while ( head ) {
        LVFontGlyphCacheItem * ptr = head;
        remove( ptr );
        global_cache->remove( ptr );
        LVFontGlyphCacheItem::freeItem( ptr );
    }
#endif
}

LVFontGlyphCacheItem * LVFontLocalGlyphCache::getByChar(lChar32 ch)
{
    FONT_LOCAL_GLYPH_CACHE_GUARD
#if USE_GLYPHCACHE_HASHTABLE == 1
    LVFontGlyphCacheItem *ptr = 0;
    GlyphCacheItemData data;
    data.ch = ch;
    if (hashTable.get(data, ptr))
        return ptr;
#else
    LVFontGlyphCacheItem * ptr = head;
    for ( ; ptr; ptr = ptr->next_local ) {
        if ( ptr->data.ch == ch ) {
            global_cache->refresh( ptr );
            return ptr;
        }
    }
#endif
    return NULL;
}

#if USE_HARFBUZZ==1
LVFontGlyphCacheItem * LVFontLocalGlyphCache::getByIndex(lUInt32 index)
{
    FONT_LOCAL_GLYPH_CACHE_GUARD
#if USE_GLYPHCACHE_HASHTABLE == 1
    LVFontGlyphCacheItem *ptr = 0;
    GlyphCacheItemData data;
    data.gindex = index;
    if (hashTable.get(data, ptr))
        return ptr;
#else
    LVFontGlyphCacheItem *ptr = head;
    for (; ptr; ptr = ptr->next_local) {
        if (ptr->data.gindex == index) {
            global_cache->refresh( ptr );
            return ptr;
        }
    }
#endif
    return NULL;
}
#endif

void LVFontLocalGlyphCache::put( LVFontGlyphCacheItem * item )
{
    FONT_LOCAL_GLYPH_CACHE_GUARD
    global_cache->put( item );
#if USE_GLYPHCACHE_HASHTABLE == 1
    hashTable.set(item->data, item);
#else
    item->next_local = head;
    if ( head )
        head->prev_local = item;
    if ( !tail )
        tail = item;
    head = item;
#endif
}

/// remove from list, but don't delete
void LVFontLocalGlyphCache::remove( LVFontGlyphCacheItem * item )
{
    FONT_LOCAL_GLYPH_CACHE_GUARD
#if USE_GLYPHCACHE_HASHTABLE == 1
    hashTable.remove(item->data);
#else
    if ( item==head )
        head = item->next_local;
    if ( item==tail )
        tail = item->prev_local;
    if ( !head || !tail )
        return;
    if ( item->prev_local )
        item->prev_local->next_local = item->next_local;
    if ( item->next_local )
        item->next_local->prev_local = item->prev_local;
    item->next_local = NULL;
    item->prev_local = NULL;
#endif
}

#if USE_GLYPHCACHE_HASHTABLE != 1
void LVFontGlobalGlyphCache::refresh( LVFontGlyphCacheItem * item )
{
    FONT_GLYPH_CACHE_GUARD
    if ( tail!=item ) {
        //move to head
        removeNoLock( item );
        putNoLock( item );
    }
}
#endif

void LVFontGlobalGlyphCache::put( LVFontGlyphCacheItem * item )
{
    FONT_GLYPH_CACHE_GUARD
    putNoLock(item);
}

void LVFontGlobalGlyphCache::putNoLock( LVFontGlyphCacheItem * item )
{
    int sz = item->getSize();
    // remove extra items from tail
    while ( sz + size > max_size ) {
        LVFontGlyphCacheItem * removed_item = tail;
        if ( !removed_item )
            break;
        removeNoLock( removed_item );
        removed_item->local_cache->remove( removed_item );
        LVFontGlyphCacheItem::freeItem( removed_item );
    }
    // add new item to head
    item->next_global = head;
    if ( head )
        head->prev_global = item;
    head = item;
    if ( !tail )
        tail = item;
    size += sz;
}

void LVFontGlobalGlyphCache::remove( LVFontGlyphCacheItem * item )
{
    FONT_GLYPH_CACHE_GUARD
    removeNoLock(item);
}

void LVFontGlobalGlyphCache::removeNoLock( LVFontGlyphCacheItem * item )
{
    size -= item->getSize();
    if ( item==head )
        head = item->next_global;
    if ( item==tail )
        tail = item->prev_global;
    if ( !head || !tail )
        return;
    if ( item->prev_global )
        item->prev_global->next_global = item->next_global;
    if ( item->next_global )
        item->next_global->prev_global = item->prev_global;
    item->next_global = NULL;
    item->prev_global = NULL;
}

void LVFontGlobalGlyphCache::clear()
{
    FONT_GLYPH_CACHE_GUARD
    while ( head ) {
        LVFontGlyphCacheItem * ptr = head;
        remove( ptr );
        ptr->local_cache->remove( ptr );
        LVFontGlyphCacheItem::freeItem( ptr );
    }
}

lString8 familyName( FT_Face face )
{
    lString8 faceName( face->family_name );
    if ( face->style_name ) {
        if (faceName == "Arial" && !strcmp(face->style_name, "Narrow"))
            faceName << " " << face->style_name;
        else if (strstr(face->style_name, "ExtraCondensed"))
            faceName << " " << "ExtraCondensed";
        else if (strstr(face->style_name, "SemiCondensed"))
            faceName << " " << "SemiCondensed";
        else if (strstr(face->style_name, "Condensed"))
            faceName << " " << "Condensed";
    }
    return faceName;
}

int getFontWeight(FT_Face face) {
    if (!face)
        return -1;
    int weight = -1;
    bool bold_flag = (face->style_flags & FT_STYLE_FLAG_BOLD) != 0;
    lString32 style32(face->style_name);
    style32 = style32.lowercase();
    if (style32.pos("extrablack") >= 0 || style32.pos("ultrablack") >= 0
          || style32.pos("extra black") >= 0 || style32.pos("ultra black") >= 0)
        weight = 950;
    else if (style32.pos("extrabold") >= 0 || style32.pos("ultrabold") >= 0
          || style32.pos("extra bold") >= 0 || style32.pos("ultra bold") >= 0)
        weight = 800;
    else if (style32.pos("demibold") >= 0 || style32.pos("semibold") >= 0
          || style32.pos("demi bold") >= 0 || style32.pos("semi bold") >= 0)
        weight = 600;
    else if (style32.pos("extralight") >= 0 || style32.pos("ultralight") >= 0
          || style32.pos("extra light") >= 0 || style32.pos("ultra light") >= 0)
        weight = 200;
    else if (style32.pos("demilight") >= 0 || style32.pos("light") >= 0
          || style32.pos("demi light") >= 0)
        weight = 300;
    else if (style32.pos("regular") >= 0 || style32.pos("normal") >= 0 || style32.pos("book") >= 0 || style32.pos("text") >= 0)
        weight = 400;
    else if (style32.pos("thin") >= 0)
        weight = 100;
    else if (style32.pos("medium") >= 0)
        weight = 500;
    else if (style32.pos("bold") >= 0)
        weight = 700;
    else if (style32.pos("black") >= 0 || style32.pos("heavy") >= 0)
        weight = 900;

    if (-1 == weight)
        weight = bold_flag ? 700 : 400;
    else if (weight <= 400 && bold_flag)
        weight = 700;
    // printf("%s %d: %s => %d\n", face->family_name, face->style_flags & FT_STYLE_FLAG_BOLD, face->style_name, weight);
    return weight;
}

// The 2 slots with "LCHAR_IS_SPACE | LCHAR_ALLOW_WRAP_AFTER" on the 2nd line previously
// were: "LCHAR_IS_SPACE | LCHAR_IS_EOL | LCHAR_ALLOW_WRAP_AFTER".
// LCHAR_IS_EOL was not used by any code, and has been replaced by LCHAR_IS_CLUSTER_TAIL
// (as flags were lUInt8, and the 8 bits were used, one needed to be dropped - they
// have since been upgraded to be lUInt16)
// (LCHAR_DEPRECATED_WRAP_AFTER for '-' and '/', as they may be used to
// separate words.)
static const lUInt16 char_flags[] = {
    0, 0, 0, 0, 0, 0, 0, 0, // 0    00
    0, 0, LCHAR_IS_SPACE | LCHAR_ALLOW_WRAP_AFTER, 0, // 8    08
    0, LCHAR_IS_SPACE | LCHAR_ALLOW_WRAP_AFTER, 0, 0, // 12   0C
    0, 0, 0, 0, 0, 0, 0, 0, // 16   10
    0, 0, 0, 0, 0, 0, 0, 0, // 24   18
    LCHAR_IS_SPACE | LCHAR_ALLOW_WRAP_AFTER, 0, 0, 0, 0, 0, 0, 0, // 32   20
    0, 0, 0, 0, 0, LCHAR_DEPRECATED_WRAP_AFTER, 0, LCHAR_DEPRECATED_WRAP_AFTER, // 40   28
    0, 0, 0, 0, 0, 0, 0, 0, // 48   30
};

// removed, as soft hyphens are now exclusively handled by hyphman:
//      (ch==UNICODE_SOFT_HYPHEN_CODE?LCHAR_ALLOW_WRAP_AFTER:
#define GET_CHAR_FLAGS(ch) \
     (ch<48?char_flags[ch]: \
        (ch==UNICODE_NO_BREAK_SPACE ? LCHAR_IS_SPACE: \
        (ch==UNICODE_NO_BREAK_HYPHEN ? 0: \
        (ch>=UNICODE_HYPHEN && ch<=UNICODE_EM_DASH ? LCHAR_DEPRECATED_WRAP_AFTER: \
        (ch==UNICODE_ARMENIAN_HYPHEN ? LCHAR_DEPRECATED_WRAP_AFTER: \
        (ch==UNICODE_FIGURE_SPACE ? 0: \
        (ch>=UNICODE_EN_QUAD && ch<=UNICODE_ZERO_WIDTH_SPACE ? LCHAR_ALLOW_WRAP_AFTER: \
         0)))))))

#if USE_HARFBUZZ==1
// For use with Harfbuzz light
struct LVCharTriplet
{
    lChar32 prevChar;
    lChar32 Char;
    lChar32 nextChar;
    bool operator == (const struct LVCharTriplet& other) const {
        return prevChar == other.prevChar && Char == other.Char && nextChar == other.nextChar;
    }
};

struct LVCharPosInfo
{
    lInt16 offset;
    lInt16 width;
};

inline lUInt32 getHash( const struct LVCharTriplet& triplet )
{
    // lUInt32 hash = (((
    //       getHash((lUInt32)triplet.Char) )*31
    //     + getHash((lUInt32)triplet.prevChar) )*31
    //     + getHash((lUInt32)triplet.nextChar) );
    // Probably less expensive:
    lUInt32 hash = getHash( (lUInt64)triplet.Char
                        + (((lUInt64)triplet.prevChar)<<16)
                        + (((lUInt64)triplet.nextChar)<<32) );
    return hash;
}
#endif

class LVFreeTypeFace : public LVFont
{
protected:
    LVMutex &     _mutex;
    lString8      _fileName;
    lString8      _faceName;
    css_font_family_t _fontFamily;
    FT_Library    _library;
    FT_Face       _face;
    FT_GlyphSlot  _slot;
    FT_Matrix     _matrix; // helper matrix for fake italic metrics
    int           _face_size; // font size in pixels (requested from face, internal)
    int           _size; // font size (~ visual char height) in pixels (public)
    int           _height; // full line height in pixels
    int           _hyphen_width;
    int           _baseline;
    int           _weight; // original font weight 400: normal, 700: bold, 100..900 thin..black
    int           _italic; // 0: regular, 1: italic, 2: fake/synthesized italic
    int           _underline_offset;
    int           _underline_thickness;
    int *         _extra_metric;
    LVFontGlyphUnsignedMetricCache   _wcache;   // glyph width cache
    LVFontGlyphSignedMetricCache     _lsbcache; // glyph left side bearing cache
    LVFontGlyphSignedMetricCache     _rsbcache; // glyph right side bearing cache
    LVFontLocalGlyphCache            _glyph_cache;
    bool           _drawMonochrome;
    hinting_mode_t _hintingMode;
    kerning_mode_t _kerningMode;
    bool           _fallbackFontIsSet;
    LVFontRef      _fallbackFont;
    bool           _nextFallbackFontIsSet;
    LVFontRef      _nextFallbackFont;
    LVFontRef      _DecimalListItemFont;
    LVFontRef      _BulletListItemFont;
    int            _synth_weight; // fake/synthesized weight
    FT_Pos         _synth_weight_strength; // for emboldening with FT_Outline_Embolden()
    FT_Pos         _synth_weight_half_strength;
    int            _features; // requested OpenType features bitmap
#if USE_HARFBUZZ==1
    hb_font_t* _hb_font;
    hb_buffer_t* _hb_buffer;
    LVArray<hb_feature_t> _hb_features;
    // For use with KERNING_MODE_HARFBUZZ:
    LVFontLocalGlyphCache _glyph_cache2;
    // For use with KERNING_MODE_HARFBUZZ_LIGHT:
    LVHashTable<struct LVCharTriplet, struct LVCharPosInfo> _width_cache2;
#endif
public:

    virtual void clearFontRefs( const LVFont *ptr ) {
        if ( _fallbackFont.get() == ptr ) {
            _fallbackFontIsSet = false;
            _fallbackFont.Clear();
        }
        if ( _nextFallbackFont.get() == ptr ) {
            _nextFallbackFontIsSet = false;
            _nextFallbackFont.Clear();
        }
        if ( _DecimalListItemFont.get() == ptr )
            _DecimalListItemFont.Clear();
        if ( _BulletListItemFont.get() == ptr )
            _BulletListItemFont.Clear();
    }

    virtual void clearFontRefs() {
        _fallbackFontIsSet = false;
        _fallbackFont.Clear();
        _nextFallbackFontIsSet = false;
        _nextFallbackFont.Clear();
        _DecimalListItemFont.Clear();
        _BulletListItemFont.Clear();
    }

    // fallback font support
    /// set fallback font for this font
    virtual void setFallbackFont( LVFontRef font ) {
        _fallbackFont = font;
        _fallbackFontIsSet = !font.isNull();
        clearCache();
    }

    /// get fallback font for this font (when it is used as the main font)
    LVFontRef getFallbackFont() {
        if ( _fallbackFontIsSet )
            return _fallbackFont;
        _fallbackFont = fontMan->GetFallbackFont(_size, getWeight(), _italic!=0);
        if ( fontMan->GetFallbackFontSizesAdjusted() ) {
            _fallbackFont = getVisuallyAdjustedOtherFont( _fallbackFont );
        }
        _fallbackFontIsSet = true;
        return _fallbackFont;
    }

    /// set next fallback font for this font (for when used as a fallback font)
    virtual void setNextFallbackFont( LVFontRef font ) {
        _nextFallbackFont = font;
        _nextFallbackFontIsSet = !font.isNull();
        clearCache();
    }

    /// get next fallback font for this font (when it is already used as a fallback font)
    LVFontRef getNextFallbackFont() {
        if ( _nextFallbackFontIsSet )
            return _nextFallbackFont;
        _nextFallbackFont = fontMan->GetFallbackFont(_size, getWeight(), _italic!=0, _faceName);
        if ( fontMan->GetFallbackFontSizesAdjusted() ) {
            _nextFallbackFont = getVisuallyAdjustedOtherFont( _nextFallbackFont );
        }
        _nextFallbackFontIsSet = true;
        return _nextFallbackFont;
    }

    LVFontRef getVisuallyAdjustedOtherFont( LVFontRef other_font ) {
        if ( other_font.isNull() )
            return other_font;
        // We use the x-height (the height of the lowercase 'x')
        // as the visual cue we want to make similar.
        int h = getXHeight();
        int other_h = ((LVFreeTypeFace*)(other_font.get()))->getXHeight();
        int other_adjusted_size = (_size * h + _size/2) / other_h; // round it
        if ( other_font->getSize() == other_adjusted_size )
            return other_font;
        return fontMan->GetFont(other_adjusted_size, other_font->getWeight(), other_font->getItalic()!=0,
                            other_font->getFontFamily(), other_font->getTypeFace(), other_font->getFeatures(), -1);
    }

    int getXHeight() {
        int x_height = 0;
        int glyph_index = getCharIndex( 'x', 0 );
        if ( glyph_index ) {
            int error = FT_Load_Glyph( _face, glyph_index, FT_LOAD_DEFAULT );
            if ( !error ) {
                x_height = _slot->metrics.horiBearingY;
            }
        }
        if ( x_height <= 0 ) {
            TT_OS2 * os2 = (TT_OS2 *)FT_Get_Sfnt_Table(_face, FT_SFNT_OS2);
            if ( os2 && os2->sxHeight > 0 ) {
                // The os2 table values are each a constant, that we need to scale
                x_height = FT_MulFix(os2->sxHeight, _face->size->metrics.y_scale);
            }
        }
        if ( x_height <= 0 ) {
            x_height = _size*64 / 2; // 0.5em
        }
        return x_height;
    }

    virtual LVFontRef getDecimalListItemFont() {
        if ( !_DecimalListItemFont.isNull() )
            return _DecimalListItemFont;
        if ( _kerningMode == KERNING_MODE_HARFBUZZ && !(getFeatures() & LFNT_OT_FEATURES_P_TNUM) ) {
            // We can request the same font with OpenType feature "tabular nums", for fixed width
            // digits and better alignment of decimal list items (no need to do it if this feature
            // has already been enabled for this font). If the font does not support the feature,
            // we'll just have another useless instance of the font.
            _DecimalListItemFont = fontMan->GetFont(
                getSize(),
                getWeight(),
                getItalic(),
                getFontFamily(),
                getTypeFace(),
                getFeatures() | LFNT_OT_FEATURES_P_TNUM,
                -1, false);
            if ( _DecimalListItemFont.isNull() ) // shouldn't happen
                _DecimalListItemFont = LVFontRef(this);
        }
        else {
            // LFNT_OT_FEATURES_P_TNUM won't be used with other kerning modes, so use this same font
            _DecimalListItemFont = LVFontRef(this);
        }
        return _DecimalListItemFont;
    }

    virtual LVFontRef getBulletListItemFont() {
        if ( !_BulletListItemFont.isNull() )
            return _BulletListItemFont;
        lString8 preferred_bullet_fonts(PREFERRED_BULLET_FONTS);
        _BulletListItemFont = fontMan->GetFont(
            getSize(),
            400,  // regular weight   // bullets stays the regular weight and non-italic, even
            0,    // no italic        // when their UL and LI are bold and/or italic
            getFontFamily(),
            preferred_bullet_fonts,   // We can provide a list, fontMan->GetFont() will split t and look for each
            0,    // no feature needed
            -1, false);
        if ( _BulletListItemFont.isNull() ) { // shouldn't happen
            _BulletListItemFont = LVFontRef(this);
        }
        else {
            // Be sure the font we get is from our requested list
            lString8Collection list;
            splitPropertyValueList(preferred_bullet_fonts.c_str(), list);
            bool found = false;
            for (int i = 0; i < list.length(); i++) {
                if ( list[i] == _BulletListItemFont->getTypeFace() ) {
                    found = true;
                    break;
                }
            }
            if ( !found ) // None found: use this same font
                _BulletListItemFont = LVFontRef(this);
        }
        return _BulletListItemFont;
    }

    /// returns font weight
    virtual int getWeight() const { return _synth_weight > 0 ? _synth_weight : _weight; }
    /// returns italic flag
    virtual int getItalic() const { return _italic; }
    /// sets face name
    virtual void setFaceName( lString8 face ) { _faceName = face; }
    virtual lString8 getFaceName() { return _faceName; }

    LVMutex & getMutex() { return _mutex; }
    FT_Library getLibrary() { return _library; }

    LVFreeTypeFace( LVMutex &mutex, FT_Library  library, LVFontGlobalGlyphCache * globalCache )
        : _mutex(mutex), _fontFamily(css_ff_sans_serif), _library(library), _face(NULL), _face_size(0)
        , _size(0), _hyphen_width(0), _baseline(0), _weight(400), _italic(0)
        , _underline_offset(0), _underline_thickness(0), _extra_metric(NULL)
        , _glyph_cache(globalCache), _drawMonochrome(false)
        , _hintingMode(HINTING_MODE_AUTOHINT), _kerningMode(KERNING_MODE_DISABLED)
        , _fallbackFontIsSet(false), _nextFallbackFontIsSet(false)
        , _synth_weight(0), _synth_weight_strength(0), _synth_weight_half_strength(0)
        , _features(0)
        #if USE_HARFBUZZ==1
        , _glyph_cache2(globalCache)
        , _width_cache2(1024)
        #endif
    {
        _hintingMode = fontMan->GetHintingMode();

        #if USE_HARFBUZZ==1
        _hb_font = 0;
        _hb_buffer = hb_buffer_create();
        setupHBFeatures();
        #endif
    }

    virtual ~LVFreeTypeFace()
    {
        #if USE_HARFBUZZ==1
        if (_hb_buffer)
            hb_buffer_destroy(_hb_buffer);
        #endif
        Clear();
    }

    void clearCache() {
        _glyph_cache.clear();
        _wcache.clear();
        _lsbcache.clear();
        _rsbcache.clear();
        #if USE_HARFBUZZ==1
        _glyph_cache2.clear();
        _width_cache2.clear();
        #endif
    }

    virtual lChar32 getHyphChar() {
        // UNICODE_SOFT_HYPHEN U+00AD was a bad choice: it's supposed to be a control code and may not
        // have a glyph (in practice, it is there and correct in many recent fonts, but in others, it
        // may be absent, or have a slot but a blank glyph, or it might be a too large glyph).
        //   return UNICODE_SOFT_HYPHEN_CODE;
        //
        // UNICODE_HYPHEN U+2010 is what the font designer designs as a hyphen (which might be intentionally
        // different from UNICODE_HYPHEN_MINUS U+002D). So, in a CJK font, it may be a fullwidth square glyph
        // with a small hyphen in the middle, and in an Arabic font, it may look like an underscore to account
        // for the Uyghur use (the only language using Arabic script that allows for hyphenation) where the
        // hyphen sits on the baseline but disconnected from the letter before it.
        // As we don't hyphenate fullwidth ASCII in CJK, and neither RTL words, we don't want to have this
        // one picked from the main font if CJK/Arabic, as if we are going to draw and hyphenate English text
        // with a fallback, we would be using this large or lower hyphenation glyph.
        // So, don't consider it.
        //   if ( FT_Get_Char_Index(_face, UNICODE_HYPHEN) > 0 )
        //      return UNICODE_HYPHEN;
        //
        // UNICODE_HYPHEN_MINUS U+002D seems to be good and to work well for hyphenation in all fonts
        // (except one: SimSun, a Chinese font).
        //
        // In most recent/classic reading fonts, these 3 glyphs looks similar.
        // So, use U+002D:
        return UNICODE_HYPHEN_MINUS;
        // Note: this has not been updated in LVWin32Font.
    }

    virtual int getHyphenWidth() {
        FONT_GUARD
        if ( !_hyphen_width ) {
            _hyphen_width = getCharWidth( getHyphChar() );
        }
        return _hyphen_width;
    }

    void updateUnderlineMetrics() {
        // Defaults if no valid metrics:
        _underline_thickness = _size > 30 ? 2 : 1;
        _underline_offset = _underline_thickness;
        if ( FT_IS_SCALABLE(_face) ) {
            // Looking at many fonts, regular vs bold, it seems the bold font does not
            // get these metrics different from the regular one.
            // Thought this was a bug and tried to scale the thickness with the weight
            // (real or synthesized) delta from the regular 400, but it feels it looks
            // better if we just don't.
            // If needed, alternative ad-hoc algorithm for when no metric is available:
            //   https://code.qt.io/cgit/qt/qtbase.git/tree/src/gui/text/freetype/qfontengine_ft.cpp#n761
            int thickness = FT_MulFix(_face->underline_thickness, _face->size->metrics.y_scale);
            int position = FT_MulFix(_face->underline_position, _face->size->metrics.y_scale);
            if (thickness > 0) {
                _underline_thickness = FONT_METRIC_TO_PX(thickness);
                if ( _underline_thickness < 1 )
                    _underline_thickness = 1;
            }
            if (position < 0) {
                // Feels like the offset below baseline too does not need to be changed
                // with weight.
                _underline_offset = FONT_METRIC_TO_PX(-position);
                // But be sure we don't overflow the font natural line height bottom, and
                // that we keep 1px free (so to keep a gap between a overline on next line)
                int overflow = _baseline + _underline_offset + _underline_thickness + 1 - _height;
                if ( overflow > 0 )
                    _underline_offset -= overflow;
                if ( _underline_offset < 1 )
                    _underline_offset = 1;
            }
        }
    }

    #if USE_HARFBUZZ==1
    bool addHBFeature(const char * tag) {
        hb_feature_t hb_feature;
        if ( hb_feature_from_string(tag, -1, &hb_feature) ) {
            _hb_features.add(hb_feature);
            return true;
        }
        return false;
    }
    void setupHBFeatures() {
        _hb_features.clear();
        if ( _kerningMode == KERNING_MODE_HARFBUZZ ) {
            // We reserve 2 for those we're adding now, +2 for possibly added CSS font features
            // (otherwise, LVArray would expand from 2 to 11 on the next addition - it will
            // then expand from 4 to 14 on the 3rd added CSS font feature).
            _hb_features.reserve(4);
            // HarfBuzz features for full text shaping
            addHBFeature("+kern");  // font kerning
            addHBFeature("+liga");  // ligatures
        }
        else if (_kerningMode == KERNING_MODE_HARFBUZZ_LIGHT) {
            // We reserve 22 for those we're adding now. We won't add any anymore
            // as we don't support CSS font features with HARFBUZZ_LIGHT.
            _hb_features.reserve(22);
            // HarfBuzz features for lighweight characters width calculating with caching.
            // We need to disable all the features, enabled by default in Harfbuzz, that
            // may split a char into more glyphs, or merge chars into one glyph.
            // (see harfbuzz/src/hb-ot-shape.cc hb_ot_shape_collect_features() )
            //
            // We can enable these ones:
            addHBFeature("+kern");  // Kerning: Fine horizontal positioning of one glyph to the next, based on the shapes of the glyphs
            addHBFeature("+mark");  // Mark Positioning: Fine positioning of a mark glyph to a base character
            addHBFeature("+mkmk");  // Mark-to-mark Positioning: Fine positioning of a mark glyph to another mark character
            addHBFeature("+curs");  // Cursive Positioning: Precise positioning of a letter's connection to an adjacent one
            addHBFeature("+locl");  // Substitutes character with the preferred form based on script language
            //
            // We should disable these ones:
            addHBFeature("-liga");  // Standard Ligatures: replaces (by default) sequence of characters with a single ligature glyph
            addHBFeature("-rlig");  // Ligatures required for correct text display (any script, but in cursive) - Arabic, semitic
            addHBFeature("-clig");  // Applies a second ligature feature based on a match of a character pattern within a context of surrounding patterns
            addHBFeature("-ccmp");  // Glyph composition/decomposition: either calls a ligature replacement on a sequence of characters or replaces a character with a sequence of glyphs
                                    // (provides logic that can for example effectively alter the order of input characters).
            addHBFeature("-calt");  // Contextual Alternates: Applies a second substitution feature based on a match of a character pattern within a context of surrounding patterns
            addHBFeature("-rclt");  // Required Contextual Alternates: Contextual alternates required for correct text display which differs from the default join for other letters, required especially important by Arabic
            addHBFeature("-rvrn");  // Required Variation Alternates: Special variants of a single character, which need apply to specific font variation, required by variable fonts
            addHBFeature("-ltra");  // Left-to-right glyph alternates: Replaces characters with forms befitting left-to-right presentation
            addHBFeature("-ltrm");  // Left-to-right mirrored forms: Replaces characters with possibly mirrored forms befitting left-to-right presentation
            addHBFeature("-rtla");  // Right-to-left glyph alternates: Replaces characters with forms befitting right-to-left presentation
            addHBFeature("-rtlm");  // Right-to-left mirrored forms: Replaces characters with possibly mirrored forms befitting right-to-left presentation
            addHBFeature("-frac");  // Fractions: Converts figures separated by slash with diagonal fraction
            addHBFeature("-numr");  // Numerator: Converts to appropriate fraction numerator form, invoked by frac
            addHBFeature("-dnom");  // Denominator: Converts to appropriate fraction denominator form, invoked by frac
            addHBFeature("-rand");  // Replaces character with random forms (meant to simulate handwriting)
            addHBFeature("-trak");  // Tracking (?)
            addHBFeature("-vert");  // Vertical (?)
            // Especially needed with FreeSerif and french texts: -ccmp
            // Especially needed with Fedra Serif and "The", "Thuringe": -calt
            // These tweaks seem fragile (adding here +smcp to experiment with small caps would break FreeSerif again).
            // So, when tuning these, please check it still behave well with FreeSerif.
            //
            // The way KERNING_MODE_HARFBUZZ_LIGHT is implemented, we'll be drawing the
            // original codepoints, so there's no much use allowing additional HB features,
            // even the one-to-one substitutions like small-cap or oldstyle-nums...
            return;
        }
        else { // Not using Harfbuzz
            return;
        }
        if ( _features != LFNT_OT_FEATURES_NORMAL ) {
            // Add requested features
            if ( _features & LFNT_OT_FEATURES_M_LIGA ) { addHBFeature("-liga"); addHBFeature("-clig"); }
            if ( _features & LFNT_OT_FEATURES_M_CALT ) { addHBFeature("-calt"); }
            if ( _features & LFNT_OT_FEATURES_P_DLIG ) { addHBFeature("+dlig"); }
            if ( _features & LFNT_OT_FEATURES_M_DLIG ) { addHBFeature("-dlig"); }
            if ( _features & LFNT_OT_FEATURES_P_HLIG ) { addHBFeature("+hlig"); }
            if ( _features & LFNT_OT_FEATURES_M_HLIG ) { addHBFeature("-hlig"); }
            if ( _features & LFNT_OT_FEATURES_P_HIST ) { addHBFeature("+hist"); }
            if ( _features & LFNT_OT_FEATURES_P_RUBY ) { addHBFeature("+ruby"); }
            if ( _features & LFNT_OT_FEATURES_P_SMCP ) { addHBFeature("+smcp"); }
            if ( _features & LFNT_OT_FEATURES_P_C2SC ) { addHBFeature("+c2sc"); addHBFeature("+smcp"); }
            if ( _features & LFNT_OT_FEATURES_P_PCAP ) { addHBFeature("+pcap"); }
            if ( _features & LFNT_OT_FEATURES_P_C2PC ) { addHBFeature("+c2pc"); addHBFeature("+pcap"); }
            if ( _features & LFNT_OT_FEATURES_P_UNIC ) { addHBFeature("+unic"); }
            if ( _features & LFNT_OT_FEATURES_P_TITL ) { addHBFeature("+titl"); }
            if ( _features & LFNT_OT_FEATURES_P_SUPS ) { addHBFeature("+sups"); }
            if ( _features & LFNT_OT_FEATURES_P_SUBS ) { addHBFeature("+subs"); }
            if ( _features & LFNT_OT_FEATURES_P_LNUM ) { addHBFeature("+lnum"); }
            if ( _features & LFNT_OT_FEATURES_P_ONUM ) { addHBFeature("+onum"); }
            if ( _features & LFNT_OT_FEATURES_P_PNUM ) { addHBFeature("+pnum"); }
            if ( _features & LFNT_OT_FEATURES_P_TNUM ) { addHBFeature("+tnum"); }
            if ( _features & LFNT_OT_FEATURES_P_ZERO ) { addHBFeature("+zero"); }
            if ( _features & LFNT_OT_FEATURES_P_ORDN ) { addHBFeature("+ordn"); }
            if ( _features & LFNT_OT_FEATURES_P_FRAC ) { addHBFeature("+frac"); }
            if ( _features & LFNT_OT_FEATURES_P_AFRC ) { addHBFeature("+afrc"); }
            if ( _features & LFNT_OT_FEATURES_P_SMPL ) { addHBFeature("+smpl"); }
            if ( _features & LFNT_OT_FEATURES_P_TRAD ) { addHBFeature("+trad"); }
            if ( _features & LFNT_OT_FEATURES_P_FWID ) { addHBFeature("+fwid"); }
            if ( _features & LFNT_OT_FEATURES_P_PWID ) { addHBFeature("+pwid"); }
            if ( _features & LFNT_OT_FEATURES_P_JP78 ) { addHBFeature("+jp78"); }
            if ( _features & LFNT_OT_FEATURES_P_JP83 ) { addHBFeature("+jp83"); }
            if ( _features & LFNT_OT_FEATURES_P_JP04 ) { addHBFeature("+jp04"); }
        }
    }
    #endif

    virtual void setFeatures( int features ) {
        _features = features;
        _hash = 0; // Force lvstyles.cpp calcHash(font_ref_t) to recompute the hash
    }
    virtual int getFeatures() const {
        return _features;
    }

    virtual void setKerningMode( kerning_mode_t kerningMode ) {
        _kerningMode = kerningMode;
        _DecimalListItemFont.Clear(); // depends on kerning mode
        _hash = 0; // Force lvstyles.cpp calcHash(font_ref_t) to recompute the hash
        #if USE_HARFBUZZ==1
        setupHBFeatures();
        // Reset buffer (to have it shrunk if HB full > light that will need only a 3 slots buffer)
        hb_buffer_reset(_hb_buffer);
        // in cache may be found some ligatures, so clear it
        clearCache();
        #endif
    }
    virtual kerning_mode_t getKerningMode() const { return _kerningMode; }

    virtual void setHintingMode(hinting_mode_t mode) {
        if (_hintingMode == mode)
            return;
        _hintingMode = mode;
        _hash = 0; // Force lvstyles.cpp calcHash(font_ref_t) to recompute the hash
        clearCache();
        #if USE_HARFBUZZ==1
        // Also update HB load flags with the updated hinting mode.
        // We need this destroy/create, as only these will clear some internal HB caches
        // (ft_font->advance_cache, ft_font->cached_x_scale); hb_ft_font_set_load_flags will not.
        if (_hb_font)
            hb_font_destroy(_hb_font);
        _hb_font = hb_ft_font_create(_face, NULL);
        if (_hb_font) {
            // Use the same load flags as we do when using FT directly, to avoid mismatching advances & raster
            int flags = FT_LOAD_DEFAULT;
            flags |= (!_drawMonochrome ? FT_LOAD_TARGET_LIGHT : FT_LOAD_TARGET_MONO);
            if (_hintingMode == HINTING_MODE_BYTECODE_INTERPRETOR) {
                flags |= FT_LOAD_NO_AUTOHINT;
            }
            else if (_hintingMode == HINTING_MODE_AUTOHINT) {
                flags |= FT_LOAD_FORCE_AUTOHINT;
            }
            else if (_hintingMode == HINTING_MODE_DISABLED) {
                flags |= FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING;
            }
            hb_ft_font_set_load_flags(_hb_font, flags);
        }
        #endif
    }
    virtual hinting_mode_t  getHintingMode() const { return _hintingMode; }

    /// get/set bitmap mode (true=bitmap, false=antialiased)
    virtual void setBitmapMode( bool drawBitmap )
    {
        if ( _drawMonochrome == drawBitmap )
            return;
        _drawMonochrome = drawBitmap;
        clearCache();
    }
    virtual bool getBitmapMode() { return _drawMonochrome; }

    // Synthetic thin/bold on a font that does not come with a corresponding variant.
    void setSynthWeight(int synth_weight) {
        if (_weight == synth_weight) {
            _synth_weight = 0;
            _synth_weight_half_strength = 0;
            _synth_weight_strength = 0;
            clearCache();
            return;
        }
        _synth_weight = synth_weight;

        // Previously, when not KERNING_MODE_HARFBUZZ, we were using FT_GlyphSlot_Embolden()
        // to get the glyphinfo and glyph with synthesized bold (with one FreeType hardcoded
        // weight strength) and increased metrics, and everything was working naturally:
        //   "Embolden a glyph by a 'reasonable' value (which is highly a matter
        //   of taste) [...] For emboldened outlines the height, width, and
        //   advance metrics are increased by the strength of the emboldening".
        //
        // Previously, when KERNING_MODE_HARFBUZZ, we were using FT_Outline_Embolden(),
        // but as HarfBuzz uses itself the original font metrics (so, we got all
        // positionnings based on not-bolded font), we were not increasing advances:
        // we did as MuPDF does (source/fitz/font.c), we kept the HB positions, offset
        // and advances, embolden the glyph by some value of 'strength', and shift
        // left/bottom by 1/2 'strength', so the boldened glyph is centered on its
        // original: the glyph being a bit larger, but not the advance, it blended
        // over its neighbour glyphs, but it looked quite allright (even if words in
        // fake bold were bolder, but not larger than the same word in the regular
        // font, unlike with a real bold font).
        //   We used to compute the strength as done in FT_GlyphSlot_Embolden():
        //     xstr = FT_MulFix( face->units_per_EM, face->size->metrics.y_scale ) / 24;
        //     ystr = xstr;
        //     FT_Outline_EmboldenXY( &slot->outline, xstr, ystr );
        //   and did as MuPDF does (with some private value of 'strength'):
        //     FT_Outline_Embolden(&face->glyph->outline, strength);
        //     FT_Outline_Translate(&face->glyph->outline, -strength/2, -strength/2);
        //   (with strength: 0=no change; 64=1px embolden; 128=2px embolden and 1px x/y translation)
        //   int strength = (_face->units_per_EM * _face->size->metrics.y_scale) / 24;
        //   FT_Pos embolden_strength = FT_MulFix(_face->units_per_EM, _face->size->metrics.y_scale) / 24;
        //   Make it slightly less bold than Freetype's bold, as we get less spacing
        //   around glyphs with HarfBuzz, by getting the unbolded advances.
        //   embolden_strength = embolden_strength * 3/4; // (*1/2 is fine but a tad too light)

        // Now, with the wish for more granular weight strengths, in all kerning modes,
        // we need to use FT_Outline_Embolden(), and we do adjust the advances to make
        // synthetic bold look less condensed and a bit more like real bold.

        // Simplifing...
        // embolden_strength = (_face->units_per_EM*_face->size->metrics.y_scale)/(65536*32)
        // So for any other scaling:
        // Introducing scaling coefficient:
        // diff_weight == 0   -> k = 0
        // diff_weight == 200 -> k = 1/32
        // k = ((_synth_weight - _weight) / 200) / 32;
        // embolden_strength = k * ((_face->units_per_EM * _face->size->metrics.y_scale) / 65536);
        _synth_weight_strength = FT_MulFix(_face->units_per_EM, _face->size->metrics.y_scale);
        _synth_weight_strength = FT_MulDiv(_synth_weight_strength, _synth_weight - _weight, 6400);
        _synth_weight_half_strength = _synth_weight_strength / 2;
        updateUnderlineMetrics();
        clearCache();
    }

    // Used when an embedded font (registered by RegisterDocumentFont()) is intantiated
    bool loadFromBuffer(LVByteArrayRef buf, int index, int size, css_font_family_t fontFamily,
                                            bool monochrome, bool italicize, int weight=-1, int face_size=-1 ) {
        FONT_GUARD;
        Clear();
        FT_Error error = FT_New_Memory_Face( _library, buf->get(), buf->length(), index, &_face ); /* create face object */
        if (error != FT_Err_Ok) {
            ft_error_trace(__func__, "FT_New_Memory_Face", error);
            return false;
        }
        return setupFace(index, size, fontFamily, monochrome, italicize, weight, face_size);
    }

    // Load font from file path
    bool loadFromFile( const char * fname, int index, int size, css_font_family_t fontFamily,
                                           bool monochrome, bool italicize, int weight=-1, int face_size=-1 ) {
        FONT_GUARD;
        Clear();
        _fileName = fname;
        FT_Error error = FT_New_Face( _library, _fileName.c_str(), index, &_face ); /* create face object */
        if (error != FT_Err_Ok) {
            ft_error_trace(__func__, "FT_New_Face", error);
            return false;
        }
        if ( _fileName.endsWith(".pfb") || _fileName.endsWith(".pfa") ) {
            lString8 kernFile = _fileName.substr(0, _fileName.length()-4);
            if ( LVFileExists(Utf8ToUnicode(kernFile) + ".afm") ) {
                kernFile += ".afm";
            }
            else if ( LVFileExists(Utf8ToUnicode(kernFile) + ".pfm" ) ) {
                kernFile += ".pfm";
            }
            else {
                kernFile.clear();
            }
            if ( !kernFile.empty() ) {
                error = FT_Attach_File( _face, kernFile.c_str() );
                if (error != FT_Err_Ok)
                    ft_error_trace(__func__, "FT_Attach_File", error);
            }
        }
        return setupFace(index, size, fontFamily, monochrome, italicize, weight, face_size);
    }

    bool setupFace(int index, int size, css_font_family_t fontFamily,
                   bool monochrome, bool italicize, int weight, int face_size) {
        //FT_Face_SetUnpatentedHinting( _face, 1 );
        _slot = _face->glyph;
        _faceName = familyName(_face);
        _face_size = face_size > 0 ? face_size : size;
        _hintingMode = fontMan->GetHintingMode();
        _drawMonochrome = monochrome;
        _fontFamily = fontFamily;
        //if ( !FT_IS_SCALABLE( _face ) ) {
        //    Clear();
        //    return false;
        //}
        FT_Error error = FT_Set_Pixel_Sizes(
            _face,    /* handle to face object */
            0,        /* pixel_width           */
            _face_size );  /* pixel_height          */

        #if USE_HARFBUZZ==1
        if (FT_Err_Ok == error) {
            if (_hb_font)
                hb_font_destroy(_hb_font);
            _hb_font = hb_ft_font_create(_face, NULL);
            if (!_hb_font) {
                error = FT_Err_Invalid_Argument;
            }
            else {
                // Use the same load flags as we do when using FT directly, to avoid mismatching advances & raster
                int flags = FT_LOAD_DEFAULT;
                flags |= (!_drawMonochrome ? FT_LOAD_TARGET_LIGHT : FT_LOAD_TARGET_MONO);
                if (_hintingMode == HINTING_MODE_BYTECODE_INTERPRETOR) {
                    flags |= FT_LOAD_NO_AUTOHINT;
                }
                else if (_hintingMode == HINTING_MODE_AUTOHINT) {
                    flags |= FT_LOAD_FORCE_AUTOHINT;
                }
                else if (_hintingMode == HINTING_MODE_DISABLED) {
                    flags |= FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING;
                }
                hb_ft_font_set_load_flags(_hb_font, flags);
            }
        }
        #endif

        if (error) {
            Clear();
            return false;
        }

        #if 0
        int nheight = _face->size->metrics.height;
        int targetheight = size << 6;
        error = FT_Set_Pixel_Sizes(
            _face,    /* handle to face object */
            0,        /* pixel_width           */
            (size * targetheight + nheight/2)/ nheight );  /* pixel_height          */
        #endif

        _height = FONT_METRIC_TO_PX( _face->size->metrics.height );
        _size = size; //(_face->size->metrics.height >> 6);
        _baseline = _height + FONT_METRIC_TO_PX( _face->size->metrics.descender );
        // When enumerating fonts using FontConfig, we already got a font weight
        // it may not match the font weight obtained by parsing the FreeType font style.
        // Well, let's trust FontConfig for this.
        _weight = weight > 0 ? weight : getFontWeight(_face);
        _italic = _face->style_flags & FT_STYLE_FLAG_ITALIC ? 1 : 0;
        updateUnderlineMetrics();

        if ( !error && italicize && !_italic ) {
            _italic = 2;
            // We'll use FT_GlyphSlot_Oblique(), with this additional
            // matrix to fix up fake italic glyph metrics.
            // We must use the same matrix values as FT_GlyphSlot_Oblique()
            // (see freetype2/src/base/ftsynth.c)
            _matrix.xx = 0x10000L;
            _matrix.yx = 0x00000L;
            _matrix.xy = 0x0366AL;
            _matrix.yy = 0x10000L;
        }

        if ( error ) {
            Clear();
            return false;
        }

        // If no unicode charmap, select any symbol charmap.
        // This is needed with Harfbuzz shaping (with Freetype, we switch charmap
        // when needed). It might not be needed with a Harfbuzz newer than 2.6.1
        // that will include https://github.com/harfbuzz/harfbuzz/pull/1948.
        if (FT_Select_Charmap(_face, FT_ENCODING_UNICODE)) // non-zero means failure
            // If no unicode charmap found, try symbol charmap
            FT_Select_Charmap(_face, FT_ENCODING_MS_SYMBOL);

        CRLog::debug("Loaded font %s [%d]: faceName=%s, ", _fileName.c_str(), index, _faceName.c_str() );

        return true;
    }

#if USE_HARFBUZZ==1
    // Used by Harfbuzz full
    lChar32 filterChar(lChar32 code, lChar32 def_char=0) {
        if (code == '\t')    // (FreeSerif doesn't have \t, get a space
            code = ' ';      // rather than a '?')

        FT_UInt ch_glyph_index = FT_Get_Char_Index(_face, code);
        if (ch_glyph_index != 0) { // found
            return code;
        }

        if ( code >= 0xF000 && code <= 0xF0FF) {
            // If no glyph found and code is among the private unicode
            // area classically used by symbol fonts (range U+F020-U+F0FF),
            // try to switch to FT_ENCODING_MS_SYMBOL
            if (!FT_Select_Charmap(_face, FT_ENCODING_MS_SYMBOL)) {
                ch_glyph_index = FT_Get_Char_Index( _face, code );
                // restore unicode charmap if there is one
                FT_Select_Charmap(_face, FT_ENCODING_UNICODE);
                if (ch_glyph_index != 0) { // glyph found: code is valid
                    return code;
                }
            }
        }
        lChar32 res = getReplacementChar(code);
        if (res != 0)
            return res;
        if (def_char != 0)
            return def_char;
        // If nothing found, let code be
        return code;
    }

    bool hbCalcCharWidth(struct LVCharPosInfo* posInfo, const struct LVCharTriplet& triplet, lChar32 def_char) {
        if (!posInfo)
            return false;
        int segLen = 0;
        int cluster;
        hb_buffer_clear_contents(_hb_buffer);
        if ( triplet.prevChar != 0 ) {
            hb_buffer_add(_hb_buffer, (hb_codepoint_t)triplet.prevChar, segLen);
            segLen++;
        }
        hb_buffer_add(_hb_buffer, (hb_codepoint_t)triplet.Char, segLen);
        cluster = segLen;
        segLen++;
        if ( triplet.nextChar != 0 ) {
            hb_buffer_add(_hb_buffer, (hb_codepoint_t)triplet.nextChar, segLen);
            segLen++;
        }
        hb_buffer_set_content_type(_hb_buffer, HB_BUFFER_CONTENT_TYPE_UNICODE);
        hb_buffer_guess_segment_properties(_hb_buffer);
        hb_shape(_hb_font, _hb_buffer, _hb_features.ptr(), (unsigned int)_hb_features.length());
        int glyph_count = hb_buffer_get_length(_hb_buffer);
        if (segLen == glyph_count) {
            hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(_hb_buffer, NULL);
            hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(_hb_buffer, NULL);
            // Ignore HB measurements when there is a single glyph not found,
            // as it may be found in a fallback font
            int codepoint_notfound_nb = 0;
            for (int i=0; i<glyph_count; i++) {
                if ( glyph_info[i].codepoint == 0 )
                    codepoint_notfound_nb++;
                // This does not look like it's really needed to ignore
                // more measurements (I felt it was needed for hebrew with
                // many diacritics).
                // if ( glyph_pos[i].x_advance == 0 )
                //    zero_advance_nb++;
            }
            if ( codepoint_notfound_nb == 0 ) {
                // Be sure HB chosen glyph is the same as freetype chosen glyph,
                // which will be the one that will be rendered
                FT_UInt ch_glyph_index = FT_Get_Char_Index( _face, triplet.Char );
                if ( glyph_info[cluster].codepoint == ch_glyph_index ) {
                    posInfo->offset = (lInt16)FONT_METRIC_TO_PX(glyph_pos[cluster].x_offset);
                    posInfo->width = (lInt16)FONT_METRIC_TO_PX(glyph_pos[cluster].x_advance);
                    if (_synth_weight > 0) {
                        // Tweak some metrics if synthesized weight
                        if ( glyph_pos[cluster].x_advance > 0 ) {
                            posInfo->width = FONT_METRIC_TO_PX(glyph_pos[cluster].x_advance + _synth_weight_strength );
                        }
                        else { // probable diacritic
                            // Compensate the width added to the previous advancing char
                            // by making the negative originX more negative
                            posInfo->offset = FONT_METRIC_TO_PX(glyph_pos[cluster].x_offset - _synth_weight_strength);
                        }
                    }
                    return true;
                }
            }
        }
        // Otherwise, use plain Freetype getGlyphInfo() which will check
        // again with this font, or the fallback one
        glyph_info_t glyph;
        if ( getGlyphInfo(triplet.Char, &glyph, def_char) ) {
            posInfo->offset = 0;
            posInfo->width = glyph.width;
            return true;
        }
        return false;
    }
#endif // USE_HARFBUZZ==1

    FT_UInt getCharIndex( lChar32 code, lChar32 def_char ) {
        if ( code=='\t' )
            code = ' ';
        FT_UInt ch_glyph_index = FT_Get_Char_Index( _face, code );
        if ( ch_glyph_index==0 && code >= 0xF000 && code <= 0xF0FF) {
            // If no glyph found and code is among the private unicode
            // area classically used by symbol fonts (range U+F020-U+F0FF),
            // try to switch to FT_ENCODING_MS_SYMBOL
            if (!FT_Select_Charmap(_face, FT_ENCODING_MS_SYMBOL)) {
                ch_glyph_index = FT_Get_Char_Index( _face, code );
                // restore unicode charmap if there is one
                FT_Select_Charmap(_face, FT_ENCODING_UNICODE);
            }
        }
        if ( ch_glyph_index==0 && def_char != 0 ) {
            bool can_be_ignored = false;
            lUInt32 replacement = getReplacementChar( code, &can_be_ignored );
            if ( replacement )
                ch_glyph_index = FT_Get_Char_Index( _face, replacement );
            if ( ch_glyph_index==0 && !can_be_ignored ) {
                // if neither the index of this character nor the index of the replacement character is found,
                // and if this character can be safely ignored,
                // we simply skip it so as not to draw the unnecessary replacement character.
                ch_glyph_index = FT_Get_Char_Index( _face, def_char );
            }
        }
        return ch_glyph_index;
    }

    /** \brief get glyph info
        \param glyph is pointer to glyph_info_t struct to place retrieved info
        \return true if glyh was found
    */
    virtual bool getGlyphInfo( lUInt32 code, glyph_info_t * glyph, lChar32 def_char=0, bool code_is_glyph_index=false, bool is_fallback=false ) {
        //FONT_GUARD
        FT_UInt glyph_index;
        if ( code_is_glyph_index ) {
            // Accept 0 and give info about the notdef/tofu char
            glyph_index = code;
        }
        else {
            glyph_index = getCharIndex( code, 0 );
            if ( glyph_index==0 ) {
                LVFontRef fallback = is_fallback ? getNextFallbackFont() : getFallbackFont();
                if ( !fallback ) {
                    // No fallback
                    glyph_index = getCharIndex( code, def_char );
                    if ( glyph_index==0 )
                        return false;
                }
                else {
                    // Fallback
                    return fallback->getGlyphInfo(code, glyph, def_char, false, true);
                }
            }
        }

        int flags = FT_LOAD_DEFAULT;
        flags |= (!_drawMonochrome ? FT_LOAD_TARGET_LIGHT : FT_LOAD_TARGET_MONO);
        if (_hintingMode == HINTING_MODE_BYTECODE_INTERPRETOR) {
            flags |= FT_LOAD_NO_AUTOHINT;
        }
        else if (_hintingMode == HINTING_MODE_AUTOHINT) {
            flags |= FT_LOAD_FORCE_AUTOHINT;
        }
        else if (_hintingMode == HINTING_MODE_DISABLED) {
            flags |= FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING;
        }
        if (_synth_weight > 0 || _italic == 2) { // Don't render yet
            flags &= ~FT_LOAD_RENDER;
            // Also disable any hinting, as it would be wrong after embolden.
            // But it feels this is now fine after switching to FT_LOAD_TARGET_LIGHT.
            // flags |= FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING;
        }
        updateTransform(); // no-op
        int error = FT_Load_Glyph(
            _face,          /* handle to face object */
            glyph_index,   /* glyph index           */
            flags );  /* load flags, see below */
        if ( error == FT_Err_Execution_Too_Long && _hintingMode == HINTING_MODE_BYTECODE_INTERPRETOR ) {
            // Native hinting bytecode may fail with some bad fonts: try again with no hinting
            flags |= FT_LOAD_NO_HINTING;
            error = FT_Load_Glyph( _face, glyph_index, flags );
        }
        if ( error )
            return false;

        if (_synth_weight > 0) { // Synthetized weight so we get the real metrics
            if ( _slot->format == FT_GLYPH_FORMAT_OUTLINE ) {
                // See setSynthWeight() for details
                FT_Outline_Embolden(&_slot->outline, _synth_weight_strength);
                FT_Outline_Translate(&_slot->outline, 0, -_synth_weight_half_strength);
            }
        }
        if (_italic == 2) {
            // When the font does not provide italic glyphs (_italic = 2), some fake
            // italic/oblique is obtained with FreeType transformation (formerly with
            // _matrix.xy and FT_Set_Transform(), now with FT_GlyphSlot_Oblique()).
            // freetype.h states about FT_Set_Transform():
            //     Note that this also transforms the `face.glyph.advance' field,
            //     but *not* the values in `face.glyph.metrics'.
            // So, with such fake italic, the values we'll use below are wrong,
            // and may cause some wrong glyphs positioning or advance.
            FT_GlyphSlot_Oblique(_slot); // This uses FT_Outline_Transform(), see freetype2/src/base/ftsynth.c

            // QT has some code that seem to fix these metrics in transformBoundingBox() at
            // https://code.qt.io/cgit/qt/qtbase.git/tree/src/gui/text/freetype/qfontengine_ft.cpp#n909
            // So let's use it:
            if ( _slot->format == FT_GLYPH_FORMAT_OUTLINE ) {
                int left   = _slot->metrics.horiBearingX;
                int right  = _slot->metrics.horiBearingX + _slot->metrics.width;
                int top    = _slot->metrics.horiBearingY;
                int bottom = _slot->metrics.horiBearingY - _slot->metrics.height;
                int l, r, t, b;
                FT_Vector vector;
                vector.x = left;
                vector.y = top;
                FT_Vector_Transform(&vector, &_matrix);
                l = r = vector.x;
                t = b = vector.y;
                vector.x = right;
                vector.y = top;
                FT_Vector_Transform(&vector, &_matrix);
                if (l > vector.x) l = vector.x;
                if (r < vector.x) r = vector.x;
                if (t < vector.y) t = vector.y;
                if (b > vector.y) b = vector.y;
                vector.x = right;
                vector.y = bottom;
                FT_Vector_Transform(&vector, &_matrix);
                if (l > vector.x) l = vector.x;
                if (r < vector.x) r = vector.x;
                if (t < vector.y) t = vector.y;
                if (b > vector.y) b = vector.y;
                vector.x = left;
                vector.y = bottom;
                FT_Vector_Transform(&vector, &_matrix);
                if (l > vector.x) l = vector.x;
                if (r < vector.x) r = vector.x;
                if (t < vector.y) t = vector.y;
                if (b > vector.y) b = vector.y;
                _slot->metrics.horiBearingX = l;
                _slot->metrics.horiBearingY = t;
                _slot->metrics.width        = r - l;
                _slot->metrics.height       = t - b;
            }
        }

        glyph->blackBoxX = (lUInt16)( FONT_METRIC_TO_PX( _slot->metrics.width ) );
        glyph->blackBoxY = (lUInt16)( FONT_METRIC_TO_PX( _slot->metrics.height ) );
        glyph->originX =   (lInt16)( FONT_METRIC_TO_PX( _slot->metrics.horiBearingX ) );
        glyph->originY =   (lInt16)( FONT_METRIC_TO_PX( _slot->metrics.horiBearingY ) );
        glyph->width =     (lUInt16)( FONT_METRIC_TO_PX( myabs(_slot->metrics.horiAdvance) ) );
        if (glyph->blackBoxX == 0) // If a glyph has no blackbox (a spacing
            glyph->rsb =   0;      // character), there is no bearing
        else
            glyph->rsb =   (lInt16)(FONT_METRIC_TO_PX( (myabs(_slot->metrics.horiAdvance)
                                        - _slot->metrics.horiBearingX - _slot->metrics.width) ) );
        // printf("%c: %d + %d + %d = %d (y: %d + %d)\n", code, glyph->originX, glyph->blackBoxX,
        //                            glyph->rsb, glyph->width, glyph->originY, glyph->blackBoxY);
        // (Old) Note: these >>6 on a negative number will floor() it, so we'll get
        // a ceil()'ed value when considering negative numbers as some overflow,
        // which is good when we're using it for adding some padding.

        if (_synth_weight > 0) {
            // Tweak some metrics if synthesized weight
            // We can't use _slot->metrics.horiAdvance, which may differ whether hinting or not (if hinting,
            // it is rounded to a multiple of 64, otherwise not), and could give different values below when
            // rounded to px after we add _synth_weight_strength, resulting in different advances and widths.
            // Above, FT_LOAD_TARGET_LIGHT ensures that hinting does not change the rounded advance, which
            // allows us to not need a re-rendering when switching hinting modes. We want to keep this
            // feature also when synthetic weights are in use.
            // So, use linearHoriAdvance, which is "The advance width of the unhinted glyph".
            lInt32 advance = _slot->linearHoriAdvance >> 10; // expressed in 16.16 fractional pixels (/64/16)
            if ( advance > 0 ) {
                glyph->width = (lUInt16)( FONT_METRIC_TO_PX( myabs(advance) + _synth_weight_strength ) );
            }
            else { // probable diacritic
                // Compensate the width added to the previous advancing char
                // by making the negative originX more negative
                glyph->originX =   (lInt16)( FONT_METRIC_TO_PX( _slot->metrics.horiBearingX - _synth_weight_strength ) );
            }
            if (glyph->blackBoxX != 0)
                glyph->rsb = (lInt16)(FONT_METRIC_TO_PX( (myabs(advance) + _synth_weight_strength
                                            - _slot->metrics.horiBearingX - _slot->metrics.width) ) );
        }
        return true;
    }
#if 0
    // USE GET_CHAR_FLAGS instead
    inline int calcCharFlags( lChar32 ch )
    {
        switch ( ch ) {
        case 0x0020:
            return LCHAR_IS_SPACE | LCHAR_ALLOW_WRAP_AFTER;
        case UNICODE_SOFT_HYPHEN_CODE:
            return LCHAR_ALLOW_WRAP_AFTER;
        case '-':
            return LCHAR_DEPRECATED_WRAP_AFTER;
        case '\r':
        case '\n':
            return LCHAR_IS_SPACE | LCHAR_IS_EOL | LCHAR_ALLOW_WRAP_AFTER;
        default:
            return 0;
        }
    }
#endif

    virtual bool collectGlyphSVGPath(SVGGlyphsCollector * svg_collector, lUInt32 code, bool code_is_glyph_index=false, bool is_fallback=false) {

        // All the points/distances/metrics we get with Harfbuzz and Freetype here are in *64 units,
        // so update our scale and current values fed into svg_collector
        double scale = svg_collector->_orig_scale / 64.0;
        svg_collector->_scale = scale;
        svg_collector->_offset_x = svg_collector->_offset_x * scale;
        svg_collector->_offset_y = svg_collector->_offset_y * scale;
        svg_collector->_advance_x = svg_collector->_advance_x * scale;
        svg_collector->_advance_y = svg_collector->_advance_y * scale;

        #if USE_HARFBUZZ==1
            if (_kerningMode == KERNING_MODE_HARFBUZZ && code_is_glyph_index ) {
                // With kerning mode harfbuzz, we get called with the fallback font and the glyph
                // index in that font. No need to check if it is present or iterate fallback fonts.
                // We can use hb_font_draw_glyph() which will make a smoother shape than
                // what we can get with FreeType outline contours below.
                // But Harfbuzz doesn't know about any fake italic or synth weight, and has
                // no way to do itself these transforms.
                // So fallback to the Freetype method in that case.
                if ( _synth_weight == 0 && _italic != 2 ) {
                    // No synthetic weight or italic: the font is used as-is.
                    setup_SVGGlyphsCollector_svg_funcs();
                    hb_font_draw_glyph(_hb_font, code, SVGGlyphsCollector_svg_funcs, svg_collector);
                    return true;
                }
            }
        #endif

        FT_UInt glyph_index;
        if ( code_is_glyph_index ) {
            // Accept 0 and give info about the notdef/tofu char
            glyph_index = code;
        }
        else {
            glyph_index = getCharIndex( code, 0 );
            if ( glyph_index==0 ) {
                LVFontRef fallback = is_fallback ? getNextFallbackFont() : getFallbackFont();
                if ( !fallback ) {
                    // No fallback
                    glyph_index = getCharIndex( code, '?' );
                    if ( glyph_index==0 )
                        return false;
                }
                else {
                    // Fallback
                    return fallback->collectGlyphSVGPath(svg_collector, code, false, true);
                }
            }
        }

        int flags = FT_LOAD_DEFAULT;
        flags |= (!_drawMonochrome ? FT_LOAD_TARGET_LIGHT : FT_LOAD_TARGET_MONO);
        flags |= FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING;
        flags &= ~FT_LOAD_RENDER;
        int error = FT_Load_Glyph(
            _face,          /* handle to face object */
            glyph_index,   /* glyph index           */
            flags );  /* load flags, see below */
        if ( error == FT_Err_Execution_Too_Long && _hintingMode == HINTING_MODE_BYTECODE_INTERPRETOR ) {
            // Native hinting bytecode may fail with some bad fonts: try again with no hinting
            flags |= FT_LOAD_NO_HINTING;
            error = FT_Load_Glyph( _face, glyph_index, flags );
        }
        if ( error )
            return false;

        // We do the same transformations as in getGlyphInfo() (see it for more comments)
        // Fortunately, these transformations will affect the outline and the paths we will get
        if (_synth_weight > 0) {
            if ( _slot->format == FT_GLYPH_FORMAT_OUTLINE ) {
                FT_Outline_Embolden(&_slot->outline, _synth_weight_strength);
                FT_Outline_Translate(&_slot->outline, 0, -_synth_weight_half_strength);
            }
        }
        if (_italic == 2) {
            FT_GlyphSlot_Oblique(_slot);
        }

        // Not super sure about this, but doing it as in getGlyphInfo() above seems to work in all cases
        svg_collector->_advance_x = _slot->metrics.horiAdvance * scale;
        if (_synth_weight > 0) {
            lInt32 advance = _slot->linearHoriAdvance >> 10; // expressed in 16.16 fractional pixels (/64/16)
            if ( advance > 0 ) {
                advance += _synth_weight_strength;
            }
            svg_collector->_advance_x = advance * scale;
        }

        // From https://github.com/donbright/font_to_svg/blob/master/font_to_svg2.hpp , adapted
        // to use our SVGGlyphsCollector_svg_move_to... funcs so that scaling and offsets
        // are applied to the paths.
        // (We tried the more complex https://github.com/donbright/font_to_svg/blob/master/font_to_svg.hpp,
        // but got visually the same results.)
        if (_slot->outline.n_points!=0 && _slot->outline.n_contours!=0) {
            /* tag bit 1 indicates whether its a control point on a bez curve
             or not. two consecutive control points imply another point halfway
             between them */
            // Step 1. move to starting point (M x-coord y-coord )
            // Step 2. decide whether to draw a line or a bezier curve or to move
            // Step 3. for bezier: Q control-point-x control-point-y, destination-x, destination-y
            //         for line:   L x-coord, y-coord
            //         for move:   M x-coord, y-coord
            const FT_Outline * outline = &_slot->outline;
            const FT_Vector * points = outline->points;
            int contour_starti = 0;
            for (int i = 0 ; i < outline->n_contours ; i++ ) {
                int contour_endi = outline->contours[i];
                int offset = contour_starti;
                int npts = contour_endi - contour_starti + 1;
                SVGGlyphsCollector_svg_move_to(NULL, svg_collector, NULL, points[contour_starti].x, points[contour_starti].y, NULL);
                char mode='M';
                for ( int j = 0; j < npts; j++ ) {
                    int a = j%npts + offset;
                    int nexti = (j+1)%npts + offset;
                    int nextnexti = (j+2)%npts + offset;
                    long x =   points[a].x;
                    long y =   points[a].y;
                    long nx =  points[nexti].x;
                    long ny =  points[nexti].y;
                    bool this_tagbit1 = (outline->tags[a] & 1);
                    bool next_tagbit1 = (outline->tags[nexti] & 1);
                    bool nextnext_tagbit1 = (outline->tags[ nextnexti ] & 1);
                    bool this_isctl = !this_tagbit1;
                    bool next_isctl = !next_tagbit1;
                    bool nextnext_isctl = !nextnext_tagbit1;
                    if (this_isctl && next_isctl) {
                        // two adjacent ctl pts. adding point halfway between;
                        // reseting x and y ";
                        x = (x + nx) / 2;
                        y = (y + ny) / 2;
                        this_isctl = false;
                        //  Check if first pt in contour was a ctrl pt. moving to non-ctrl pt
                        if (j==0) {
                            if (mode!='M')
                                SVGGlyphsCollector_svg_move_to(NULL, svg_collector, NULL, x, y, NULL);
                            else
                                SVGGlyphsCollector_svg_continue_to(NULL, svg_collector, NULL, x, y, NULL);
                            mode='M';
                        }
                    }
                    if (!this_isctl && next_isctl) {
                        long nnx = points[nextnexti].x;
                        long nny = points[nextnexti].y;
                        if (nextnext_isctl) {
                            // two ctl pts coming. adding point halfway between
                            // reseting nnx and nny to halfway pt
                            nnx = (nx + nnx) / 2;
                            nny = (ny + nny) / 2;
                        }
                        // produce bezier path
                        if (mode!='Q')
                            SVGGlyphsCollector_svg_quadratic_to(NULL, svg_collector, NULL, nx, ny, nnx, nny, NULL);
                        else
                            SVGGlyphsCollector_svg_continue_to(NULL, svg_collector, NULL, nx, ny, nnx, nny, NULL);
                        mode='Q';
                    }
                    else if (!this_isctl && !next_isctl) {
                        // line to new point
                        if (mode!='L')
                            SVGGlyphsCollector_svg_line_to(NULL, svg_collector, NULL, nx, ny, NULL);
                        else
                            SVGGlyphsCollector_svg_continue_to(NULL, svg_collector, NULL, nx, ny, NULL);
                        mode='L';
                    }
                    else if (this_isctl && !next_isctl) {
                        // this is ctrl pt. skipping to
                    }
                }
                contour_starti = contour_endi+1;
                SVGGlyphsCollector_svg_close_path(NULL, svg_collector, NULL, NULL);
                mode='Z';
            }
        }
        return true;
    }

    virtual bool getGlyphExtraMetric( glyph_extra_metric_t metric, lUInt32 code, int & value, bool scaled_to_px, lChar32 def_char=0, bool is_fallback=false ) {
        int glyph_index = getCharIndex( code, 0 );
        if ( glyph_index==0 ) {
            LVFontRef fallback = is_fallback ? getNextFallbackFont() : getFallbackFont();
            if ( !fallback ) {
                // No fallback
                glyph_index = getCharIndex( code, def_char );
                if ( glyph_index==0 )
                    return false;
            }
            else {
                // Fallback
                return fallback->getGlyphExtraMetric(metric, code, value, scaled_to_px, def_char, true);
            }
        }
        switch (metric) {
        case glyph_metric_dummy:
            value = 0;
            return true;
            break;
#if MATHML_SUPPORT==1
        case glyph_metric_math_italics_correction:
            {
                #if USE_HARFBUZZ==1
                if ( hb_ot_math_has_data(hb_font_get_face(_hb_font)) ) {
                    value = hb_ot_math_get_glyph_italics_correction(_hb_font, glyph_index);
                    if ( scaled_to_px )
                        value = FONT_METRIC_TO_PX(value);
                    return true;
                }
                #endif
                return false;
            }
            break;
        case glyph_metric_math_top_accent_attachment:
            {
                #if USE_HARFBUZZ==1
                if ( hb_ot_math_has_data(hb_font_get_face(_hb_font)) ) {
                    value = hb_ot_math_get_glyph_top_accent_attachment(_hb_font, glyph_index);
                    if ( scaled_to_px )
                        value = FONT_METRIC_TO_PX(value);
                    return true;
                }
                #endif
                return false;
            }
            break;
#endif // MATHML_SUPPORT==1
        }
        return false;
    }

    /** \brief measure text
        \param text is text string pointer
        \param len is number of characters to measure
        \param max_width is maximum width to measure line
        \param def_char is character to replace absent glyphs in font
        \param letter_spacing is number of pixels to add between letters
        \param allow_hyphenation whether to check for hyphenation if max_width reached
        \param hints: hint flags (direction, begin/end of paragraph, for Harfbuzz - unrelated to font hinting)
        \return number of characters before max_width reached
    */
    virtual lUInt16 measureText(
                        const lChar32 * text,
                        int len,
                        lUInt16 * widths,
                        lUInt8 * flags,
                        int max_width,
                        lChar32 def_char,
                        TextLangCfg * lang_cfg = NULL,
                        int letter_spacing = 0,
                        bool allow_hyphenation = true,
                        lUInt32 hints=0
                     )
    {
        FONT_GUARD
        if ( len <= 0 || _face==NULL )
            return 0;
        if ( letter_spacing < 0 ) {
            letter_spacing = 0;
        }
        else if ( letter_spacing > MAX_LETTER_SPACING ) {
            letter_spacing = MAX_LETTER_SPACING;
        }

        int i;

        lUInt16 prev_width = 0;
        lUInt16 cur_width = 0;
        lUInt16 lastFitChar = 0; // actually the index of the char after the one that fitted
        updateTransform(); // no-op
        // measure character widths

    #if USE_HARFBUZZ==1
        if (_kerningMode == KERNING_MODE_HARFBUZZ) {

            /** from harfbuzz/src/hb-buffer.h
             * hb_glyph_info_t:
             * @codepoint: either a Unicode code point (before shaping) or a glyph index
             *             (after shaping).
             * @cluster: the index of the character in the original text that corresponds
             *           to this #hb_glyph_info_t, or whatever the client passes to
             *           hb_buffer_add(). More than one #hb_glyph_info_t can have the same
             *           @cluster value, if they resulted from the same character (e.g. one
             *           to many glyph substitution), and when more than one character gets
             *           merged in the same glyph (e.g. many to one glyph substitution) the
             *           #hb_glyph_info_t will have the smallest cluster value of them.
             *           By default some characters are merged into the same cluster
             *           (e.g. combining marks have the same cluster as their bases)
             *           even if they are separate glyphs, hb_buffer_set_cluster_level()
             *           allow selecting more fine-grained cluster handling.
             */
            int glyph_count;
            hb_glyph_info_t* glyph_info = 0;
            hb_glyph_position_t* glyph_pos = 0;
            hb_buffer_clear_contents(_hb_buffer);

            // hb_buffer_set_replacement_codepoint(_hb_buffer, def_char);
            // /\ This would just set the codepoint to use when parsing
            // invalid utf8/16/32. As we provide codepoints, Harfbuzz
            // won't use it. This does NOT set the codepoint/glyph that
            // would be used when a glyph does not exist in that for that
            // codepoint. There is currently no way to specify that, and
            // it's always the .notdef/tofu glyph that is measured/drawn.

            // Fill HarfBuzz buffer
            // No need to call filterChar() on the input: HarfBuzz seems to do
            // the right thing with symbol fonts, and we'd better not replace
            // bullets & al unicode chars with generic equivalents, as they
            // may be found in the fallback font.
            // So, we don't, unless the current font has no fallback font,
            // in which case we need to get a replacement, in the worst case
            // def_char (?), because the glyph for 0/.notdef (tofu) has so
            // many different looks among fonts that it would mess the text.
            // We'll then get the '?' glyph of the fallback font only.
            // Note: not sure if Harfbuzz is able to be fine by using other
            // glyphs when the main codepoint does not exist by itself in
            // the font... in which case we'll mess things up.
            // todo: (if needed) might need a pre-pass in the fallback case:
            // full shaping without filterChar(), and if any .notdef
            // codepoint, re-shape with filterChar()...
            bool is_fallback_font = hints & LFNT_HINT_IS_FALLBACK_FONT;
            LVFontRef fallback = is_fallback_font ? getNextFallbackFont() : getFallbackFont();
            bool has_fallback_font = !fallback.isNull();
            if ( has_fallback_font ) { // It has a fallback font, add chars as-is
                for (i = 0; i < len; i++) {
                    hb_buffer_add(_hb_buffer, (hb_codepoint_t)(text[i]), i);
                }
            }
            else { // No fallback font, check codepoint presence or get replacement char
                for (i = 0; i < len; i++) {
                    hb_buffer_add(_hb_buffer, (hb_codepoint_t)filterChar(text[i], def_char), i);
                }
            }
            // Note: hb_buffer_add_codepoints(_hb_buffer, (hb_codepoint_t*)text, len, 0, len)
            // would do the same kind of loop we did above, so no speedup gain using it; and we
            // get to be sure of the cluster initial value we set to each of our added chars.
            hb_buffer_set_content_type(_hb_buffer, HB_BUFFER_CONTENT_TYPE_UNICODE);

            // If we are provided with direction and hints, let harfbuzz know
            if ( hints ) {
                if ( hints & LFNT_HINT_DIRECTION_KNOWN ) {
                    if ( hints & LFNT_HINT_DIRECTION_IS_RTL )
                        hb_buffer_set_direction(_hb_buffer, HB_DIRECTION_RTL);
                    else
                        hb_buffer_set_direction(_hb_buffer, HB_DIRECTION_LTR);
                }
                int hb_flags = HB_BUFFER_FLAG_DEFAULT; // (hb_buffer_flags_t won't let us do |= )
                if ( hints & LFNT_HINT_BEGINS_PARAGRAPH )
                    hb_flags |= HB_BUFFER_FLAG_BOT;
                if ( hints & LFNT_HINT_ENDS_PARAGRAPH )
                    hb_flags |= HB_BUFFER_FLAG_EOT;
                hb_buffer_set_flags(_hb_buffer, (hb_buffer_flags_t)hb_flags); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
            }
            if ( lang_cfg ) {
                hb_buffer_set_language(_hb_buffer, lang_cfg->getHBLanguage());
            }
            // Let HB guess what's not been set (script, direction, language)
            hb_buffer_guess_segment_properties(_hb_buffer);

            // Some additional care might need to be taken, see:
            //   https://www.w3.org/TR/css-text-3/#letter-spacing-property
            if ( letter_spacing > 0 ) {
                // Don't apply letter-spacing if the script is cursive
                hb_script_t script = hb_buffer_get_script(_hb_buffer);
                if ( isHBScriptCursive(script) )
                    letter_spacing = 0;
            }
            // todo: if letter_spacing, ligatures should be disabled (-liga, -clig)
            // todo: letter-spacing must not be applied at the beginning or at the end of a line
            // todo: it should be applied half-before/half-after each grapheme
            // cf in *some* minikin repositories: libs/minikin/Layout.cpp

            // Shape
            hb_shape(_hb_font, _hb_buffer, _hb_features.ptr(), (unsigned int)_hb_features.length());

            // Harfbuzz has guessed and set a direction even if we did not provide one.
            #ifdef DEBUG_MEASURE_TEXT
            bool is_rtl = false;
            #endif
            if ( hb_buffer_get_direction(_hb_buffer) == HB_DIRECTION_RTL ) {
                #ifdef DEBUG_MEASURE_TEXT
                is_rtl = true;
                #endif
                // "For buffers in the right-to-left (RTL) or bottom-to-top (BTT) text
                // flow direction, the directionality of the buffer itself is reversed
                // for final output as a matter of design. Therefore, HarfBuzz inverts
                // the monotonic property: client programs are guaranteed that
                // monotonically increasing initial cluster values will be returned as
                // monotonically decreasing final cluster values."
                // hb_buffer_reverse_clusters() puts the advance on the last char of a
                // cluster, unlike hb_buffer_reverse() which puts it on the first, which
                // looks more natural (like it happens when LTR).
                // But hb_buffer_reverse_clusters() is required to have the clusters
                // ordered as our text indices, so we can map them back to our text.
                hb_buffer_reverse_clusters(_hb_buffer);
            }

            glyph_count = hb_buffer_get_length(_hb_buffer);
            glyph_info = hb_buffer_get_glyph_infos(_hb_buffer, 0);
            glyph_pos = hb_buffer_get_glyph_positions(_hb_buffer, 0);

            #ifdef DEBUG_MEASURE_TEXT
                printf("MTHB >>> measureText %x len %d is_rtl=%d [%s]\n", text, len, is_rtl, _faceName.c_str());
                for (i = 0; i < glyph_count; i++) {
                    char glyphname[32];
                    hb_font_get_glyph_name(_hb_font, glyph_info[i].codepoint, glyphname, sizeof(glyphname));
                    printf("MTHB g%d c%d(=t:%x) [%x %s]\tadvance=(%d,%d)", i, glyph_info[i].cluster,
                                text[glyph_info[i].cluster], glyph_info[i].codepoint, glyphname,
                                FONT_METRIC_TO_PX(glyph_pos[i].x_advance), FONT_METRIC_TO_PX(glyph_pos[i].y_advance)
                                );
                    if (glyph_pos[i].x_offset || glyph_pos[i].y_offset)
                        printf("\toffset=(%d,%d)", FONT_METRIC_TO_PX(glyph_pos[i].x_offset),
                                                   FONT_METRIC_TO_PX(glyph_pos[i].y_offset));
                    printf("\n");
                }
                printf("MTHB ---\n");
            #endif

            // We need to set widths and flags on our original text.
            // hb_shape has modified buffer to contain glyphs, and text
            // and buffer may desync (because of clusters, ligatures...)
            // in both directions in a same run.
            // Also, a cluster must not be cut, so we want to set the same
            // width to all our original text chars that are part of the
            // same cluster (so 2nd+ chars in a cluster, will get a 0-width,
            // and, when splitting lines, will fit in a word with the first
            // char).
            // So run along our original text (chars, t), and try to follow
            // harfbuzz buffer (glyphs, hg), putting the advance of all
            // the glyphs that belong to the same cluster (hcl) on the
            // first char that started that cluster (and 0-width on the
            // followup chars).
            // It looks like Harfbuzz makes a cluster of combined glyphs
            // even when the font does not have any or all of the required
            // glyphs:
            // When meeting a not-found glyph (codepoint=0, name=".notdef"),
            // we record the original starting t of that cluster, and
            // keep processing (possibly other chars with .notdef glyphs,
            // giving them the width of the 'tofu' char), until we meet a char
            // with a found glyph. We then hold on on this one, while we go
            // measureText() the previous segment of text (that got .notdef
            // glyphs) with the fallback font, and update the wrongs width
            // and flags.

            int cur_cluster = 0;
            int hg = 0;  // index in glyph_info/glyph_pos
            int hcl = 0; // cluster glyph at hg
            int t_notdef_start = -1;
            int t_notdef_end = -1;
            bool max_width_reached = false; // set when reached while measuring with the fallback font
            for (int t = 0; t < len; t++) {
                #ifdef DEBUG_MEASURE_TEXT
                    printf("MTHB t%d (=%x) ", t, text[t]);
                #endif
                // Grab all glyphs that do not belong to a cluster greater that our char position
                while ( hg < glyph_count ) {
                    hcl = glyph_info[hg].cluster;
                    if (hcl <= t) {
                        int advance = 0;
                        if ( glyph_info[hg].codepoint != 0 ) { // Codepoint found in this font
                            #ifdef DEBUG_MEASURE_TEXT
                                printf("(found cp=%x) ", glyph_info[hg].codepoint);
                            #endif
                            // Only process past notdef when the first glyph of a cluster is found.
                            // (It could happen that a cluster of 2 glyphs has its 1st one notdef
                            // while the 2nd one has a valid codepoint: we'll have to reprocess the
                            // whole cluster with the fallback font. If the 1st glyph is found but
                            // the 2nd is notdef, we'll process past notdef with the fallback font
                            // now, but we'll be processing this whole cluster with the fallback
                            // font when a later valid codepoint is found).
                            if ( t_notdef_start >= 0 && hcl > cur_cluster ) {
                                // We have a segment of previous ".notdef", and this glyph starts a new cluster
                                t_notdef_end = t;
                                // The code ensures the main fallback font has no fallback font
                                if ( has_fallback_font ) {
                                    // Let the fallback font replace the wrong values in widths and flags
                                    #ifdef DEBUG_MEASURE_TEXT
                                        printf("[...]\nMTHB ### measuring past failures with fallback font %d>%d\n",
                                                                t_notdef_start, t_notdef_end);
                                    #endif
                                    // Drop BOT/EOT flags if this segment is not at start/end
                                    lUInt32 fb_hints = hints | LFNT_HINT_IS_FALLBACK_FONT;
                                    if ( t_notdef_start > 0 )
                                        fb_hints &= ~LFNT_HINT_BEGINS_PARAGRAPH;
                                    if ( t_notdef_end < len )
                                        fb_hints &= ~LFNT_HINT_ENDS_PARAGRAPH;
                                    lUInt16 last_good_width = t_notdef_start > 0 ? widths[t_notdef_start-1] : 0;
                                    lUInt16 chars_notdef_len = t_notdef_end - t_notdef_start;
                                    lUInt16 chars_measured = fallback->measureText( text + t_notdef_start, chars_notdef_len,
                                                    widths + t_notdef_start, flags + t_notdef_start,
                                                    max_width - last_good_width, def_char, lang_cfg, letter_spacing, allow_hyphenation,
                                                    fb_hints );
                                    if ( chars_measured < chars_notdef_len ) {
                                        max_width_reached = true;
                                    }
                                    lastFitChar = t_notdef_start + chars_measured;
                                    // Fix previous bad measurements
                                    for (int tn = t_notdef_start; tn < lastFitChar; tn++) {
                                        widths[tn] += last_good_width;
                                    }
                                    // And fix our current width
                                    cur_width = widths[lastFitChar-1];
                                    prev_width = cur_width;
                                    #ifdef DEBUG_MEASURE_TEXT
                                        printf("MTHB ### measured past failures > W= %d\n[...]", cur_width);
                                    #endif
                                    if ( max_width_reached ) {
                                        t_notdef_start = -1; // be sure we won't "Process .notdef glyphs at end of text"
                                        break; // break hg loop
                                    }
                                }
                                else {
                                    // No fallback font: stay with what's been measured: the notdef/tofu char
                                    #ifdef DEBUG_MEASURE_TEXT
                                        printf("[...]\nMTHB no fallback font to measure past failures, keeping def_char\nMTHB [...]");
                                    #endif
                                }
                                t_notdef_start = -1;
                                // And go on with the found glyph now that we fixed what was before
                            }
                            // Glyph found in this font
                            if ( glyph_pos[hg].x_advance )
                                advance = FONT_METRIC_TO_PX(glyph_pos[hg].x_advance + _synth_weight_strength);
                        }
                        else {
                            #ifdef DEBUG_MEASURE_TEXT
                                printf("(glyph not found) ");
                            #endif
                            // Keep the advance of .notdef/tofu in case there is no fallback font to correct them
                            if ( glyph_pos[hg].x_advance )
                                advance = FONT_METRIC_TO_PX(glyph_pos[hg].x_advance + _synth_weight_strength);
                            if ( t_notdef_start < 0 ) {
                                t_notdef_start = t;
                            }
                        }
                        #ifdef DEBUG_MEASURE_TEXT
                            printf("c%d+%d ", hcl, advance);
                        #endif
                        cur_width += advance;
                        cur_cluster = hcl;
                        hg++;
                        continue; // keep grabbing glyphs
                    }
                    break; // break hg loop
                }
                if ( max_width_reached ) {
                    break; // break t loop
                }
                // Done grabbing clustered glyphs: they contributed to cur_width.
                // All 't' lower than the next cluster will have that same cur_width.
                if (cur_cluster < t) {
                    // Our char is part of a cluster that started on a previous char
                    flags[t] = LCHAR_IS_CLUSTER_TAIL;
                    // todo: see at using HB_GLYPH_FLAG_UNSAFE_TO_BREAK to
                    // set this flag instead/additionally
                }
                else {
                    // We're either a single char cluster, or the start
                    // of a multi chars cluster.
                    flags[t] = GET_CHAR_FLAGS(text[t]);
                    if (cur_width == prev_width) {
                        // But if there is no advance (this happens with soft-hyphens),
                        // flag it and don't add any letter spacing.
                        flags[t] |= LCHAR_IS_CLUSTER_TAIL;
                    }
                    else {
                        cur_width += letter_spacing; // only between clusters/graphemes
                    }
                    // It seems each soft-hyphen is in its own cluster, of length 1 and width 0,
                    // so HarfBuzz must already deal correctly with soft-hyphens.
                }
                widths[t] = cur_width;
                #ifdef DEBUG_MEASURE_TEXT
                    printf("=> %d (flags=%d) => W=%d\n", cur_width - prev_width, flags[t], cur_width);
                #endif
                prev_width = cur_width;

                // (Not sure about how that max_width limit could play and if it could mess things)
                if (cur_width > max_width) {
                    if (lastFitChar < hcl + 7)
                        break;
                }
                else {
                    lastFitChar = t+1;
                }
            } // process next char t

            // Process .notdef glyphs at end of text (same logic as above)
            if ( t_notdef_start >= 0 ) {
                t_notdef_end = len;
                if ( has_fallback_font ) {
                    #ifdef DEBUG_MEASURE_TEXT
                        printf("[...]\nMTHB ### measuring past failures at EOT with fallback font %d>%d\n",
                                                t_notdef_start, t_notdef_end);
                    #endif
                    // Drop BOT flag if this segment is not at start (it is at end)
                    lUInt32 fb_hints = hints | LFNT_HINT_IS_FALLBACK_FONT;
                    if ( t_notdef_start > 0 )
                        fb_hints &= ~LFNT_HINT_BEGINS_PARAGRAPH;
                    lUInt16 last_good_width = t_notdef_start > 0 ? widths[t_notdef_start-1] : 0;
                    lUInt16 chars_notdef_len = t_notdef_end - t_notdef_start;
                    lUInt16 chars_measured = fallback->measureText( text + t_notdef_start, chars_notdef_len,
                                    widths + t_notdef_start, flags + t_notdef_start,
                                    max_width - last_good_width, def_char, lang_cfg, letter_spacing, allow_hyphenation,
                                    fb_hints );
                    lastFitChar = t_notdef_start + chars_measured;
                    for (int tn = t_notdef_start; tn < lastFitChar; tn++) {
                        widths[tn] += last_good_width;
                    }
                    // And add all that to our current width
                    cur_width = widths[lastFitChar-1];
                    #ifdef DEBUG_MEASURE_TEXT
                        printf("MTHB ### measured past failures at EOT > W= %d\n[...]", cur_width);
                    #endif
                }
                else {
                    #ifdef DEBUG_MEASURE_TEXT
                        printf("[...]\nMTHB no fallback font to measure past failures at EOT, keeping def_char\nMTHB [...]");
                    #endif
                }
            }

            // i is used below to "fill props for rest of chars", so make it accurate
            i = lastFitChar;

            #ifdef DEBUG_MEASURE_TEXT
                printf("MTHB <<< W=%d [%s]\n", cur_width, _faceName.c_str());
                printf("MTHB dwidths[]: ");
                // (This will show invalid values after where we stopped when reaching max_width)
                for (int t = 0; t < len; t++)
                    printf("%d:%d ", t, widths[t] - (t>0?widths[t-1]:0));
                printf("\n");
            #endif
        } // _kerningMode == KERNING_MODE_HARFBUZZ

        else if (_kerningMode == KERNING_MODE_HARFBUZZ_LIGHT) {
            struct LVCharTriplet triplet;
            struct LVCharPosInfo posInfo;
            triplet.Char = 0;
            for ( i=0; i<len; i++) {
                lChar32 ch = text[i];
                bool isHyphen = (ch==UNICODE_SOFT_HYPHEN_CODE);
                if (isHyphen) {
                    // do just what would be done below if zero width (no change
                    // in prev_width), and don't get involved in kerning
                    flags[i] = 0; // no LCHAR_ALLOW_WRAP_AFTER, will be dealt with by hyphenate()
                    widths[i] = prev_width;
                    lastFitChar = i + 1;
                    continue;
                }
                flags[i] = GET_CHAR_FLAGS(ch); //calcCharFlags( ch );
                triplet.prevChar = triplet.Char;
                triplet.Char = ch;
                if (i < len - 1)
                    triplet.nextChar = text[i + 1];
                else
                    triplet.nextChar = 0;
                if (!_width_cache2.get(triplet, posInfo)) {
                    if (hbCalcCharWidth(&posInfo, triplet, def_char))
                        _width_cache2.set(triplet, posInfo);
                    else { // (seems this never happens, unlike with KERNING_MODE_DISABLED)
                        widths[i] = prev_width;
                        lastFitChar = i + 1;
                        continue;  /* ignore errors */
                    }
                }
                widths[i] = prev_width + posInfo.width;
                if ( posInfo.width == 0 ) {
                    // Assume zero advance means it's a diacritic, and we should not apply
                    // any letter spacing on this char (now, and when justifying)
                    flags[i] |= LCHAR_IS_CLUSTER_TAIL;
                }
                else {
                    widths[i] += letter_spacing;
                }
                prev_width = widths[i];
                if ( prev_width > max_width ) {
                    if ( lastFitChar < i + 7)
                        break;
                }
                else {
                    lastFitChar = i + 1;
                }
            }
        } // _kerningMode == KERNING_MODE_HARFBUZZ_LIGHT

        else { // _kerningMode == KERNING_MODE_DISABLED or KERNING_MODE_FREETYPE:
               // fallback to the non harfbuzz code
    #endif // USE_HARFBUZZ

        FT_UInt previous = 0;
        int error;
        #if (ALLOW_KERNING==1)
        int use_kerning = _kerningMode != KERNING_MODE_DISABLED && FT_HAS_KERNING( _face );
        #endif
        for ( i=0; i<len; i++) {
            lChar32 ch = text[i];
            bool isHyphen = (ch==UNICODE_SOFT_HYPHEN_CODE);
            if (isHyphen) {
                // do just what would be done below if zero width (no change
                // in prev_width), and don't get involved in kerning
                flags[i] = 0; // no LCHAR_ALLOW_WRAP_AFTER, will be dealt with by hyphenate()
                widths[i] = prev_width;
                lastFitChar = i + 1;
                continue;
            }
            FT_UInt ch_glyph_index = (FT_UInt)-1;
            int kerning = 0;
            #if (ALLOW_KERNING==1)
            if ( use_kerning && previous>0  ) {
                if ( ch_glyph_index==(FT_UInt)-1 )
                    ch_glyph_index = getCharIndex( ch, def_char );
                if ( ch_glyph_index != 0 ) {
                    FT_Vector delta;
                    error = FT_Get_Kerning( _face,          /* handle to face object */
                                  previous,          /* left glyph index      */
                                  ch_glyph_index,         /* right glyph index     */
                                  FT_KERNING_DEFAULT,  /* kerning mode          */
                                  &delta );    /* target vector         */
                    if ( !error )
                        kerning = delta.x;
                }
            }
            #endif

            flags[i] = GET_CHAR_FLAGS(ch); //calcCharFlags( ch );

            /* load glyph image into the slot (erase previous one) */
            lUInt16 w = _wcache.get(ch);
            if ( w == CACHED_UNSIGNED_METRIC_NOT_SET ) {
                glyph_info_t glyph;
                if ( getGlyphInfo( ch, &glyph, def_char ) ) {
                    w = glyph.width;
                    _wcache.put(ch, w);
                }
                else {
                    widths[i] = prev_width;
                    lastFitChar = i + 1;
                    continue;  /* ignore errors */
                }
                // if ( ch_glyph_index==(FT_UInt)-1 )
                //     ch_glyph_index = getCharIndex( ch, 0 );
                // error = FT_Load_Glyph( _face,          /* handle to face object */
                //         ch_glyph_index,                /* glyph index           */
                //         FT_LOAD_DEFAULT );             /* load flags, see below */
                // if ( error ) {
                //     widths[i] = prev_width;
                //     continue;  /* ignore errors */
                // }
            }
            if ( use_kerning ) {
                if ( ch_glyph_index==(FT_UInt)-1 )
                    ch_glyph_index = getCharIndex( ch, 0 );
                previous = ch_glyph_index;
            }
            widths[i] = prev_width + w + FONT_METRIC_TO_PX(kerning);
            if ( w == 0 ) {
                // Assume zero advance means it's a diacritic, and we should not apply
                // any letter spacing on this char (now, and when justifying)
                flags[i] |= LCHAR_IS_CLUSTER_TAIL;
            }
            else {
                widths[i] += letter_spacing;
            }
            prev_width = widths[i];
            if ( prev_width > max_width ) {
                if ( lastFitChar < i + 7)
                    break;
            }
            else {
                lastFitChar = i + 1;
            }
        }

    #if USE_HARFBUZZ==1
        } // else fallback to the non harfbuzz code
    #endif

        // fill props for rest of chars
        for ( int ii=i; ii<len; ii++ ) {
            flags[ii] = GET_CHAR_FLAGS( text[ii] );
        }

        // find last word
        if ( allow_hyphenation ) {
            if ( !_hyphen_width )
                _hyphen_width = getCharWidth( getHyphChar() );
            if ( lastFitChar > 3 ) {
                int hwStart, hwEnd;
                bool hasRtl;
                lStr_findWordBounds( text, len, lastFitChar-1, hwStart, hwEnd, hasRtl );
                if ( !hasRtl && hwStart < (int)(lastFitChar-1) && hwEnd > hwStart+3 ) {
                    //int maxw = max_width - (hwStart>0 ? widths[hwStart-1] : 0);
                    if ( lang_cfg )
                        lang_cfg->getHyphMethod()->hyphenate(text+hwStart, hwEnd-hwStart, widths+hwStart, flags+hwStart, _hyphen_width, max_width);
                    else // Use global lang hyph method
                        HyphMan::hyphenate(text+hwStart, hwEnd-hwStart, widths+hwStart, flags+hwStart, _hyphen_width, max_width);
                }
            }
        }
        return lastFitChar; //i;
    }

    /** \brief measure text
        \param text is text string pointer
        \param len is number of characters to measure
        \return width of specified string
    */
    virtual lUInt32 getTextWidth( const lChar32 * text, int len, TextLangCfg * lang_cfg=NULL) {
        static lUInt16 widths[MAX_LINE_CHARS+1];
        static lUInt8 flags[MAX_LINE_CHARS+1];
        if ( len>MAX_LINE_CHARS )
            len = MAX_LINE_CHARS;
        if ( len<=0 )
            return 0;
        lUInt16 res = measureText(
                        text, len,
                        widths,
                        flags,
                        MAX_LINE_WIDTH,
                        U' ',  // def_char
                        lang_cfg
                     );
        if ( res>0 && res<MAX_LINE_CHARS )
            return widths[res-1];
        return 0;
    }

    void updateTransform() { // called, but no-op
        // static void * transformOwner = NULL;
        // if ( transformOwner!=this ) {
        //     FT_Set_Transform(_face, &_matrix, NULL);
        //     transformOwner = this;
        // }
    }

    /** \brief get glyph item
        \param code is unicode character
        \return glyph pointer if glyph was found, NULL otherwise
    */
    virtual LVFontGlyphCacheItem * getGlyph(lUInt32 ch, lChar32 def_char=0, bool is_fallback=false) {
        //FONT_GUARD
        FT_UInt ch_glyph_index = getCharIndex( ch, 0 );
        if ( ch_glyph_index==0 ) {
            LVFontRef fallback = is_fallback ? getNextFallbackFont() : getFallbackFont();
            if ( !fallback ) {
                // No fallback
                ch_glyph_index = getCharIndex( ch, def_char );
                if ( ch_glyph_index==0 )
                    return NULL;
            }
            else {
                // Fallback
                // todo: find a way to adjust origin_y by this font and
                // fallback font baseline difference, without modifying
                // the item in the cache of the fallback font
                return fallback->getGlyph(ch, def_char, true);
            }
        }
        LVFontGlyphCacheItem * item = _glyph_cache.getByChar( ch );
        if ( !item ) {
            int rend_flags = FT_LOAD_RENDER | ( !_drawMonochrome ? FT_LOAD_TARGET_LIGHT : FT_LOAD_TARGET_MONO );
                                                    //|FT_LOAD_MONOCHROME|FT_LOAD_FORCE_AUTOHINT
            if (_hintingMode == HINTING_MODE_BYTECODE_INTERPRETOR) {
                rend_flags |= FT_LOAD_NO_AUTOHINT;
            }
            else if (_hintingMode == HINTING_MODE_AUTOHINT) {
                rend_flags |= FT_LOAD_FORCE_AUTOHINT;
            }
            else if (_hintingMode == HINTING_MODE_DISABLED) {
                rend_flags |= FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING;
            }
            if (_synth_weight > 0 || _italic == 2) { // Don't render yet
                rend_flags &= ~FT_LOAD_RENDER;
                // Also disable any hinting, as it would be wrong after embolden.
                // But it feels this is now fine after switching to FT_LOAD_TARGET_LIGHT.
                // rend_flags |= FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING;
            }

            /* load glyph image into the slot (erase previous one) */
            updateTransform(); // no-op
            int error = FT_Load_Glyph( _face, /* handle to face object */
                    ch_glyph_index,           /* glyph index           */
                    rend_flags );             /* load flags, see below */
            if ( error == FT_Err_Execution_Too_Long && _hintingMode == HINTING_MODE_BYTECODE_INTERPRETOR ) {
                // Native hinting bytecode may fail with some bad fonts: try again with no hinting
                rend_flags |= FT_LOAD_NO_HINTING;
                error = FT_Load_Glyph( _face, ch_glyph_index, rend_flags );
            }
            if ( error ) {
                return NULL;  /* ignore errors */
            }

            bool is_embolden = false;
            if (_synth_weight > 0) {
                if ( _slot->format == FT_GLYPH_FORMAT_OUTLINE ) {
                    // See setSynthWeight() for details
                    FT_Outline_Embolden(&_slot->outline, _synth_weight_strength);
                    FT_Outline_Translate(&_slot->outline, 0, -_synth_weight_half_strength);
                    is_embolden = true; // _slot->format changes to BITMAP after render
                }
            }
            if (_italic == 2) {
                FT_GlyphSlot_Oblique(_slot);
            }
            if (_synth_weight > 0 || _italic == 2) {
                // Render now that transformations are applied
                FT_Render_Glyph(_slot, _drawMonochrome?FT_RENDER_MODE_MONO:FT_RENDER_MODE_LIGHT);
            }

            if (_synth_weight > 0 && is_embolden) {
                // Tweak some metrics if synthesized weight
                if ( _slot->metrics.horiAdvance > 0 ) {
                    // _slot->metrics.horiAdvance += _synth_weight_strength;
                    // As in getGlyphInfo(), we need to use _slot->linearHoriAdvance to get
                    // the same advance/width whether hinting or not
                    _slot->metrics.horiAdvance = (_slot->linearHoriAdvance >> 10) + _synth_weight_strength;
                }
                else { // probable diacritic
                    // Compensate the width added to the previous advancing char
                    // by making the negative originX more negative
                    _slot->metrics.horiBearingX -= _synth_weight_strength;
                }
            }

            item = newItem( &_glyph_cache, (lChar32)ch, _slot ); //, _drawMonochrome
            if (item)
                _glyph_cache.put( item );
        }
        return item;
    }

#if USE_HARFBUZZ==1
    LVFontGlyphCacheItem * getGlyphByIndex(lUInt32 index) {
        //FONT_GUARD
        LVFontGlyphCacheItem *item = _glyph_cache2.getByIndex(index);
        if (!item) {
            // glyph not found in cache, rendering...
            int rend_flags = FT_LOAD_RENDER | ( !_drawMonochrome ? FT_LOAD_TARGET_LIGHT : FT_LOAD_TARGET_MONO );
                                                    //|FT_LOAD_MONOCHROME|FT_LOAD_FORCE_AUTOHINT
            if (_hintingMode == HINTING_MODE_BYTECODE_INTERPRETOR) {
                rend_flags |= FT_LOAD_NO_AUTOHINT;
            }
            else if (_hintingMode == HINTING_MODE_AUTOHINT) {
                rend_flags |= FT_LOAD_FORCE_AUTOHINT;
            }
            else if (_hintingMode == HINTING_MODE_DISABLED) {
                rend_flags |= FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING;
            }

            if (_synth_weight > 0 || _italic == 2) { // Don't render yet
                rend_flags &= ~FT_LOAD_RENDER;
                // Also disable any hinting, as it would be wrong after embolden.
                // But it feels this is now fine after switching to FT_LOAD_TARGET_LIGHT.
                // rend_flags |= FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING;
            }

            /* load glyph image into the slot (erase previous one) */
            updateTransform(); // no-op
            int error = FT_Load_Glyph( _face, /* handle to face object */
                    index,                    /* glyph index           */
                    rend_flags );             /* load flags, see below */
            if ( error == FT_Err_Execution_Too_Long && _hintingMode == HINTING_MODE_BYTECODE_INTERPRETOR ) {
                // Native hinting bytecode may fail with some bad fonts: try again with no hinting
                rend_flags |= FT_LOAD_NO_HINTING;
                error = FT_Load_Glyph( _face, index, rend_flags );
            }
            if ( error ) {
                return NULL;  /* ignore errors */
            }

            bool is_embolden = false;
            if (_synth_weight > 0) {
                if ( _slot->format == FT_GLYPH_FORMAT_OUTLINE ) {
                    // See setSynthWeight() for details
                    FT_Outline_Embolden(&_slot->outline, _synth_weight_strength);
                    FT_Outline_Translate(&_slot->outline, 0, -_synth_weight_half_strength);
                    is_embolden = true; // _slot->format changes to BITMAP after render
                }
            }
            if (_italic==2) {
                FT_GlyphSlot_Oblique(_slot);
            }
            if (_synth_weight > 0 || _italic == 2) {
                // Render now that transformations are applied
                FT_Render_Glyph(_slot, _drawMonochrome?FT_RENDER_MODE_MONO:FT_RENDER_MODE_LIGHT);
            }
            if (_synth_weight > 0 && is_embolden) {
                if ( _slot->format == FT_GLYPH_FORMAT_OUTLINE ) {
                    // Tweak some metrics if synthesized weight
                    if ( _slot->metrics.horiAdvance > 0 ) {
                        // _slot->metrics.horiAdvance += _synth_weight_strength;
                        // As in getGlyphInfo(), we need to use _slot->linearHoriAdvance to get
                        // the same advance/width whether hinting or not
                        _slot->metrics.horiAdvance = (_slot->linearHoriAdvance >> 10) + _synth_weight_strength;
                        // (this won't be used with Harfbuzz, but we'll tweak glyph_pos[hg].x_advance the same way)
                    }
                    else { // probable diacritic
                        // Compensate the width added to the previous advancing char
                        // by making the negative originX more negative
                        _slot->metrics.horiBearingX -= _synth_weight_strength;
                    }
                }
            }

            item = newItem(&_glyph_cache2, index, _slot);
            if (item)
                _glyph_cache2.put(item);
        }
        return item;
    }
#endif

//    /** \brief get glyph image in 1 byte per pixel format
//        \param code is unicode character
//        \param buf is buffer [width*height] to place glyph data
//        \return true if glyph was found
//    */
//    virtual bool getGlyphImage(lUInt16 ch, lUInt8 * bmp, lChar32 def_char=0)
//    {
//        LVFontGlyphCacheItem * item = getGlyph(ch);
//        if ( item )
//            memcpy( bmp, item->bmp, item->bmp_width * item->bmp_height );
//        return item;
//    }

    /// returns font baseline offset
    virtual int getBaseline()
    {
        return _baseline;
    }

    /// returns font height
    virtual int getHeight() const
    {
        return _height;
    }

    /// returns font character size
    virtual int getSize() const
    {
        return _size;
    }

    /// returns char glyph advance width
    virtual int getCharWidth( lChar32 ch, lChar32 def_char='?' )
    {
        int w = _wcache.get(ch);
        if ( w == CACHED_UNSIGNED_METRIC_NOT_SET ) {
            glyph_info_t glyph;
            if ( getGlyphInfo( ch, &glyph, def_char ) ) {
                w = glyph.width;
            }
            else {
                w = 0;
            }
            _wcache.put(ch, w);
        }
        return w;
    }

    /// returns char glyph left side bearing
    virtual int getLeftSideBearing( lChar32 ch, bool negative_only=false, bool italic_only=false )
    {
        if ( italic_only && !getItalic() )
            return 0;
        int b = _lsbcache.get(ch);
        if ( b == CACHED_SIGNED_METRIC_NOT_SET ) {
            glyph_info_t glyph;
            if ( getGlyphInfo( ch, &glyph, '?' ) ) {
                b = glyph.originX;
            }
            else {
                b = 0;
            }
            _lsbcache.put(ch, b);
        }
        if (negative_only && b >= 0)
            return 0;
        return b;
    }

    /// returns char glyph right side bearing
    virtual int getRightSideBearing( lChar32 ch, bool negative_only=false, bool italic_only=false )
    {
        if ( italic_only && !getItalic() )
            return 0;
        int b = _rsbcache.get(ch);
        if ( b == CACHED_SIGNED_METRIC_NOT_SET ) {
            glyph_info_t glyph;
            if ( getGlyphInfo( ch, &glyph, '?' ) ) {
                b = glyph.rsb;
            }
            else {
                b = 0;
            }
            _rsbcache.put(ch, b);
        }
        if (negative_only && b >= 0)
            return 0;
        return b;
    }

    virtual bool hasOTMathSupport() const
    {
        #if USE_HARFBUZZ==1
            return hb_ot_math_has_data(hb_font_get_face(_hb_font));
        #endif
        return false;
    }

    virtual int getExtraMetric(font_extra_metric_t metric, bool scaled_to_px)
    {
        if ( _extra_metric == NULL ) {
            _extra_metric = (int *)malloc(sizeof(int)*FONT_METRIC_MAX);
            for (int i=0; i<FONT_METRIC_MAX; i++) {
                _extra_metric[i] = CACHED_SIGNED_METRIC_NOT_SET;
            }
        }
        if ( _extra_metric[metric] == CACHED_SIGNED_METRIC_NOT_SET ) {
            // We can get ideas from:
            // https://github.com/mozilla/gecko-dev/blob/master/gfx/thebes/gfxFT2Utils.cpp
            bool value_set = false;
            int value = 0;
            // Tables we might look at
            TT_OS2 * os2 = (TT_OS2 *)FT_Get_Sfnt_Table(_face, FT_SFNT_OS2);
#if MATHML_SUPPORT==1
            #if USE_HARFBUZZ==1
                bool has_ot_math_data = hb_ot_math_has_data(hb_font_get_face(_hb_font));
                #define VALUE_FROM_OT_MATH_CONSTANT(x) if ( has_ot_math_data ) { \
                        value = hb_ot_math_get_constant(_hb_font, HB_OT_MATH_CONSTANT_##x); \
                        value_set = true; }
            #else
                #define VALUE_FROM_OT_MATH_CONSTANT(x)
            #endif
#endif

            switch (metric) {
            case font_metric_x_height: {
                int glyph_index = getCharIndex( 'x', 0 );
                if ( glyph_index ) {
                    int error = FT_Load_Glyph( _face, glyph_index, FT_LOAD_DEFAULT );
                    if ( !error ) {
                        value = _slot->metrics.horiBearingY;
                        value_set = true;
                    }
                }
                if ( !value_set && os2 && os2->sxHeight > 0 ) {
                    // The os2 table values are each a constant, that we need to scale
                    value = FT_MulFix(os2->sxHeight, _face->size->metrics.y_scale);
                    value_set = true;
                }
                if ( !value_set ) {
                    value = _size*64 / 2; // 0.5em
                }
                break;
            }
            case font_metric_ch_width: {
                // Could be used for CSS unit 'ch': equal to the used advance measure
                // of the "0" (ZERO, U+0030) glyph. But let's keep approximating it
                // with 0.5em, which may be good enough.
                int glyph_index = getCharIndex( '0', 0 );
                if ( glyph_index ) {
                    int error = FT_Load_Glyph( _face, glyph_index, FT_LOAD_DEFAULT );
                    if ( !error ) {
                        value = myabs(_slot->metrics.horiAdvance);
                        // printf("glyph: %d", value);
                        value_set = true;
                    }
                }
                if ( !value_set ) {
                    value = _size*64 / 2; // assumed to be 0.5em wide
                }
                break;
            }
            case font_metric_y_superscript_y_offset: {
                if ( os2 && os2->ySuperscriptYOffset != 0 ) {
                    value = FT_MulFix(os2->ySuperscriptYOffset, _face->size->metrics.y_scale);
                    if (value < 0) // Probable font bug (the os2 value should be positive)
                        value = -value;
                    value_set = true;
                }
                if ( !value_set ) {
                    // As Firefox, which itself uses this instead of the OS/2 metric
                    // https://bugzilla.mozilla.org/show_bug.cgi?id=1029307
                    // https://github.com/mozilla/gecko-dev/commit/1e79307a4f068f29247d1db3ca897b2785e22b6f
                    value = _face->size->metrics.height * 1/3;
                }
                break;
            }
            case font_metric_y_subscript_y_offset: {
                if ( os2 && os2->ySubscriptYOffset != 0 ) {
                    value = FT_MulFix(os2->ySubscriptYOffset, _face->size->metrics.y_scale);
                    if (value < 0) // Probable font bug (the os2 value should be positive)
                        value = -value;
                    value_set = true;
                }
                if ( !value_set ) {
                    // As Firefox, which itself uses this instead of the OS/2 metric
                    value = _face->size->metrics.height * 1/5;
                }
                break;
            }
#if MATHML_SUPPORT==1
            // Reference (and fallback values) from
            // https://mathml-refresh.github.io/mathml-core/#layout-constants-mathconstants
            case font_metric_underline_thickness: {
                #if USE_HARFBUZZ==1
                // This will make HB fetch the value from post->table.underlineThickness
                if ( hb_ot_metrics_get_position(_hb_font, HB_OT_METRICS_TAG_UNDERLINE_SIZE, &value) )
                    value_set = true;
                #endif
                if ( !value_set )
                    value = 0;
                break;
            }
            case font_metric_math_axis_height: {
                VALUE_FROM_OT_MATH_CONSTANT(AXIS_HEIGHT)
                // FreeSerif seems to have this a bit too large, but tweaking it causes other
                // issues as some other math metric constants interplay with this one - also,
                // the factor correction needed (to align the fraction bar with +/-/=) would
                // vary with the font size...
                //   if ( value_set && _faceName == "FreeSerif" ) value = value * 90/100;
                //   if ( value_set && _faceName == "FreeSerif" ) value_set = false;
                if ( !value_set ) {
                    // Firefox nsMathMLFrame.cpp does this (which feels better than x_height/2)
                    int glyph_index = getCharIndex( 0x2212, 0 ); // minus sign
                    if ( !glyph_index )
                        glyph_index = getCharIndex( '-', 0 ); // ASCII minus
                    if ( glyph_index ) {
                        int error = FT_Load_Glyph( _face, glyph_index, FT_LOAD_DEFAULT );
                        if ( !error ) {
                            value = _slot->metrics.horiBearingY;
                            value_set = true;
                        }
                    }
                }
                if ( !value_set )
                    value = getExtraMetric(font_metric_x_height, false) * 1/2;
                break;
            }
            case font_metric_math_fraction_rule_thickness:
                VALUE_FROM_OT_MATH_CONSTANT(FRACTION_RULE_THICKNESS)
                if ( !value_set )
                    value = getExtraMetric(font_metric_underline_thickness, false); // default rule thickness
                break;
            case font_metric_math_fraction_numerator_shift_up:
                VALUE_FROM_OT_MATH_CONSTANT(FRACTION_NUMERATOR_SHIFT_UP)
                // Default to 0
                break;
            case font_metric_math_fraction_numerator_display_style_shift_up:
                VALUE_FROM_OT_MATH_CONSTANT(FRACTION_NUMERATOR_DISPLAY_STYLE_SHIFT_UP)
                // Default to 0
                break;
            case font_metric_math_fraction_numerator_gap_min:
                VALUE_FROM_OT_MATH_CONSTANT(FRACTION_NUMERATOR_GAP_MIN)
                if ( !value_set )
                    value = getExtraMetric(font_metric_underline_thickness, false); // default rule thickness
                break;
            case font_metric_math_fraction_num_display_style_gap_min:
                VALUE_FROM_OT_MATH_CONSTANT(FRACTION_NUM_DISPLAY_STYLE_GAP_MIN)
                if ( !value_set )
                    value = getExtraMetric(font_metric_underline_thickness, false) * 3; // 3 x default rule thickness
                break;
            case font_metric_math_fraction_denominator_shift_down:
                VALUE_FROM_OT_MATH_CONSTANT(FRACTION_DENOMINATOR_SHIFT_DOWN)
                // Default to 0
                break;
            case font_metric_math_fraction_denominator_display_style_shift_down:
                VALUE_FROM_OT_MATH_CONSTANT(FRACTION_DENOMINATOR_DISPLAY_STYLE_SHIFT_DOWN)
                // Default to 0
                break;
            case font_metric_math_fraction_denominator_gap_min:
                VALUE_FROM_OT_MATH_CONSTANT(FRACTION_DENOMINATOR_GAP_MIN)
                if ( !value_set )
                    value = getExtraMetric(font_metric_underline_thickness, false); // default rule thickness
                break;
            case font_metric_math_fraction_denom_display_style_gap_min:
                VALUE_FROM_OT_MATH_CONSTANT(FRACTION_DENOM_DISPLAY_STYLE_GAP_MIN)
                if ( !value_set )
                    value = getExtraMetric(font_metric_underline_thickness, false) * 3; // 3 x default rule thickness
                break;
            case font_metric_math_stack_top_shift_up:
                VALUE_FROM_OT_MATH_CONSTANT(STACK_TOP_SHIFT_UP)
                // Default to 0
                break;
            case font_metric_math_stack_top_display_style_shift_up:
                VALUE_FROM_OT_MATH_CONSTANT(STACK_TOP_DISPLAY_STYLE_SHIFT_UP)
                // Default to 0
                break;
            case font_metric_math_stack_bottom_shift_down:
                VALUE_FROM_OT_MATH_CONSTANT(STACK_BOTTOM_SHIFT_DOWN)
                // Default to 0
                break;
            case font_metric_math_stack_bottom_display_style_shift_down:
                VALUE_FROM_OT_MATH_CONSTANT(STACK_BOTTOM_DISPLAY_STYLE_SHIFT_DOWN)
                // Default to 0
                break;
            case font_metric_math_stack_gap_min:
                VALUE_FROM_OT_MATH_CONSTANT(STACK_GAP_MIN)
                if ( !value_set )
                    value = getExtraMetric(font_metric_underline_thickness, false) * 3; // 3 x default rule thickness
                break;
            case font_metric_math_stack_display_style_gap_min:
                VALUE_FROM_OT_MATH_CONSTANT(STACK_DISPLAY_STYLE_GAP_MIN)
                if ( !value_set )
                    value = getExtraMetric(font_metric_underline_thickness, false) * 7; // 7 x default rule thickness
                break;
            case font_metric_math_script_percent_scale_down:
                VALUE_FROM_OT_MATH_CONSTANT(SCRIPT_PERCENT_SCALE_DOWN)
                if ( !value_set || value <= 0)
                    value = 71;
                break;
            case font_metric_math_script_script_percent_scale_down:
                VALUE_FROM_OT_MATH_CONSTANT(SCRIPT_SCRIPT_PERCENT_SCALE_DOWN)
                if ( !value_set || value <= 0)
                    value = 50;
                break;
            case font_metric_math_display_operator_min_height:
                VALUE_FROM_OT_MATH_CONSTANT(DISPLAY_OPERATOR_MIN_HEIGHT)
                if ( !value_set || value <= 0)
                    // Specs say 0, but we want something > 1 and sqrt(2) is mentionned in a few places
                    value = _size*64 * 1.41; // sqrt(2)
                break;
            case font_metric_math_accent_base_height:
                VALUE_FROM_OT_MATH_CONSTANT(ACCENT_BASE_HEIGHT)
                if ( !value_set )
                    value = getExtraMetric(font_metric_x_height, false);
                break;
            case font_metric_math_overbar_vertical_gap:
                VALUE_FROM_OT_MATH_CONSTANT(OVERBAR_VERTICAL_GAP)
                if ( !value_set )
                    value = getExtraMetric(font_metric_underline_thickness, false) * 3; // 3 x default rule thickness
                break;
            case font_metric_math_underbar_vertical_gap:
                VALUE_FROM_OT_MATH_CONSTANT(UNDERBAR_VERTICAL_GAP)
                if ( !value_set )
                    value = getExtraMetric(font_metric_underline_thickness, false) * 3; // 3 x default rule thickness
                break;
            case font_metric_math_overbar_extra_ascender:
                VALUE_FROM_OT_MATH_CONSTANT(OVERBAR_EXTRA_ASCENDER)
                if ( !value_set )
                    value = getExtraMetric(font_metric_underline_thickness, false); // default rule thickness
                break;
            case font_metric_math_underbar_extra_descender:
                VALUE_FROM_OT_MATH_CONSTANT(UNDERBAR_EXTRA_DESCENDER)
                if ( !value_set )
                    value = getExtraMetric(font_metric_underline_thickness, false); // default rule thickness
                break;
            case font_metric_math_upper_limit_baseline_rise_min:
                VALUE_FROM_OT_MATH_CONSTANT(UPPER_LIMIT_BASELINE_RISE_MIN)
                // Defaults to 0
                break;
            case font_metric_math_upper_limit_gap_min:
                VALUE_FROM_OT_MATH_CONSTANT(UPPER_LIMIT_GAP_MIN)
                // Defaults to 0
                break;
            case font_metric_math_stretch_stack_top_shift_up:
                VALUE_FROM_OT_MATH_CONSTANT(STRETCH_STACK_TOP_SHIFT_UP)
                // Defaults to 0
                break;
            case font_metric_math_stretch_stack_gap_below_min:
                VALUE_FROM_OT_MATH_CONSTANT(STRETCH_STACK_GAP_BELOW_MIN)
                // Defaults to 0
                break;
            case font_metric_math_lower_limit_baseline_drop_min:
                VALUE_FROM_OT_MATH_CONSTANT(LOWER_LIMIT_BASELINE_DROP_MIN)
                // Defaults to 0
                break;
            case font_metric_math_lower_limit_gap_min:
                VALUE_FROM_OT_MATH_CONSTANT(LOWER_LIMIT_GAP_MIN)
                // Defaults to 0
                break;
            case font_metric_math_stretch_stack_bottom_shift_down:
                VALUE_FROM_OT_MATH_CONSTANT(STRETCH_STACK_BOTTOM_SHIFT_DOWN)
                // Defaults to 0
                break;
            case font_metric_math_stretch_stack_gap_above_min:
                VALUE_FROM_OT_MATH_CONSTANT(STRETCH_STACK_GAP_ABOVE_MIN)
                // Defaults to 0
                break;
            case font_metric_math_superscript_shift_up:
                VALUE_FROM_OT_MATH_CONSTANT(SUPERSCRIPT_SHIFT_UP)
                if ( !value_set )
                    value = getExtraMetric(font_metric_y_superscript_y_offset, false);
                break;
            case font_metric_math_superscript_shift_up_cramped:
                VALUE_FROM_OT_MATH_CONSTANT(SUPERSCRIPT_SHIFT_UP_CRAMPED)
                // Defaults to 0
                break;
            case font_metric_math_superscript_bottom_min:
                VALUE_FROM_OT_MATH_CONSTANT(SUPERSCRIPT_BOTTOM_MIN)
                if ( !value_set )
                    value = getExtraMetric(font_metric_x_height, false) * 1/4;
                break;
            case font_metric_math_superscript_baseline_drop_max:
                VALUE_FROM_OT_MATH_CONSTANT(SUPERSCRIPT_BASELINE_DROP_MAX)
                // Defaults to 0
                break;
            case font_metric_math_subscript_shift_down:
                VALUE_FROM_OT_MATH_CONSTANT(SUBSCRIPT_SHIFT_DOWN)
                if ( !value_set )
                    value = getExtraMetric(font_metric_y_subscript_y_offset, false);
                break;
            case font_metric_math_subscript_top_max:
                VALUE_FROM_OT_MATH_CONSTANT(SUBSCRIPT_TOP_MAX)
                if ( !value_set )
                    value = getExtraMetric(font_metric_x_height, false) * 4/5;
                break;
            case font_metric_math_subscript_baseline_drop_min:
                VALUE_FROM_OT_MATH_CONSTANT(SUBSCRIPT_BASELINE_DROP_MIN)
                // Defaults to 0
                break;
            case font_metric_math_sub_superscript_gap_min:
                VALUE_FROM_OT_MATH_CONSTANT(SUB_SUPERSCRIPT_GAP_MIN)
                if ( !value_set )
                    value = getExtraMetric(font_metric_underline_thickness, false) * 4; // 4 x default rule thickness
                break;
            case font_metric_math_superscript_bottom_max_with_subscript:
                VALUE_FROM_OT_MATH_CONSTANT(SUPERSCRIPT_BOTTOM_MAX_WITH_SUBSCRIPT)
                if ( !value_set )
                    value = getExtraMetric(font_metric_x_height, false) * 4/5;
                break;
            case font_metric_math_radical_vertical_gap:
                VALUE_FROM_OT_MATH_CONSTANT(RADICAL_VERTICAL_GAP)
                if ( !value_set )
                    value = getExtraMetric(font_metric_underline_thickness, false) * 5/4; // 1.25 x default rule thickness
                break;
            case font_metric_math_radical_display_style_vertical_gap:
                VALUE_FROM_OT_MATH_CONSTANT(RADICAL_DISPLAY_STYLE_VERTICAL_GAP)
                if ( !value_set )
                    value = getExtraMetric(font_metric_underline_thickness, false) // default rule thickness + 1/4 OS/2.sxHeight
                                + getExtraMetric(font_metric_x_height, false) * 1/4;
                break;
            case font_metric_math_radical_rule_thickness:
                VALUE_FROM_OT_MATH_CONSTANT(RADICAL_RULE_THICKNESS)
                if ( !value_set )
                    value = getExtraMetric(font_metric_underline_thickness, false); // default rule thickness
                break;
            case font_metric_math_radical_extra_ascender:
                VALUE_FROM_OT_MATH_CONSTANT(RADICAL_EXTRA_ASCENDER)
                if ( !value_set )
                    value = getExtraMetric(font_metric_underline_thickness, false); // default rule thickness
                break;
            case font_metric_math_radical_kern_before_degree:
                VALUE_FROM_OT_MATH_CONSTANT(RADICAL_KERN_BEFORE_DEGREE)
                if ( !value_set )
                    value = _size*64 * 5/18; // 5/18em
                break;
            case font_metric_math_radical_kern_after_degree:
                VALUE_FROM_OT_MATH_CONSTANT(RADICAL_KERN_AFTER_DEGREE)
                if ( !value_set )
                    value = - _size*64 * 10/18; // -10/18em
                break;
            case font_metric_math_radical_degree_bottom_raise_percent:
                VALUE_FROM_OT_MATH_CONSTANT(RADICAL_DEGREE_BOTTOM_RAISE_PERCENT)
                if ( !value_set || value <= 0)
                    value = 60;
                break;
#endif // MATHML_SUPPORT==1
            default:
                value = 0;
                break;
            }
            _extra_metric[metric] = value;
        }
        return scaled_to_px ? FONT_METRIC_TO_PX(_extra_metric[metric]) : _extra_metric[metric];
    }

    /// retrieves font handle
    virtual void * GetHandle()
    {
        return NULL;
    }

    /// returns font typeface name
    virtual lString8 getTypeFace() const
    {
        return _faceName;
    }

    /// returns font family id
    virtual css_font_family_t getFontFamily() const
    {
        return _fontFamily;
    }

    virtual bool kerningEnabled() {
        #if (ALLOW_KERNING==1)
            #if USE_HARFBUZZ==1
                return _kerningMode == KERNING_MODE_HARFBUZZ
                    || (_kerningMode == KERNING_MODE_FREETYPE && FT_HAS_KERNING( _face ));
            #else
                return _kerningMode != KERNING_MODE_DISABLED && FT_HAS_KERNING( _face );
            #endif
        #else
            return false;
        #endif
    }

    /// draws text string (returns x advance)
    virtual int DrawTextString( LVDrawBuf * buf, int x, int y,
                       const lChar32 * text, int len,
                       lChar32 def_char, lUInt32 * palette, bool addHyphen,
                       TextLangCfg * lang_cfg,
                       lUInt32 flags, int letter_spacing, int width,
                       int text_decoration_back_gap,
                       int target_w=-1, int target_h=-1,
                       SVGGlyphsCollector * svg_collector=NULL)
    {
        FONT_GUARD
        if ( len <= 0 || _face==NULL )
            return 0;
        if ( letter_spacing < 0 ) {
            letter_spacing = 0;
        }
        else if ( letter_spacing > MAX_LETTER_SPACING ) {
            letter_spacing = MAX_LETTER_SPACING;
        }
        lvRect clip;
        buf->GetClipRect( &clip );
        updateTransform(); // no-op

        bool transform_stretch = flags & LFNT_HINT_TRANSFORM_STRETCH;
        int text_height = transform_stretch ? target_h : _height;
        if ( y + text_height < clip.top || y >= clip.bottom )
            return 0;

        int i;
        //lUInt16 prev_width = 0;
        lChar32 ch;
        // measure character widths
        bool isHyphen = false;
        int x0 = x;

    #if USE_HARFBUZZ==1
        if (_kerningMode == KERNING_MODE_HARFBUZZ) {
            // See measureText() for more comments on how to work with Harfbuzz,
            // as we do and must work the same way here.
            int glyph_count;
            hb_glyph_info_t *glyph_info = 0;
            hb_glyph_position_t *glyph_pos = 0;
            hb_buffer_clear_contents(_hb_buffer);
            // Fill HarfBuzz buffer
            bool is_fallback_font = flags & LFNT_HINT_IS_FALLBACK_FONT;
            LVFontRef fallback = is_fallback_font ? getNextFallbackFont() : getFallbackFont();
            bool has_fallback_font = !fallback.isNull();
            if ( has_fallback_font ) { // It has a fallback font, add chars as-is
                for (i = 0; i < len; i++) {
                    hb_buffer_add(_hb_buffer, (hb_codepoint_t)(text[i]), i);
                }
            }
            else { // No fallback font, check codepoint presence or get replacement char
                for (i = 0; i < len; i++) {
                    hb_buffer_add(_hb_buffer, (hb_codepoint_t)filterChar(text[i], def_char), i);
                }
            }
            hb_buffer_set_content_type(_hb_buffer, HB_BUFFER_CONTENT_TYPE_UNICODE);

            // If we are provided with direction and hints, let harfbuzz know
            if ( flags ) {
                if ( flags & LFNT_HINT_DIRECTION_KNOWN ) {
                    // Trust direction decided by fribidi: if we made a word containing just '(',
                    // harfbuzz wouldn't be able to determine its direction and would render
                    // it LTR - while it could be in some RTL text and needs to be mirrored.
                    if ( flags & LFNT_HINT_DIRECTION_IS_RTL )
                        hb_buffer_set_direction(_hb_buffer, HB_DIRECTION_RTL);
                    else
                        hb_buffer_set_direction(_hb_buffer, HB_DIRECTION_LTR);
                }
                int hb_flags = HB_BUFFER_FLAG_DEFAULT; // (hb_buffer_flags_t won't let us do |= )
                if ( flags & LFNT_HINT_BEGINS_PARAGRAPH )
                    hb_flags |= HB_BUFFER_FLAG_BOT;
                if ( flags & LFNT_HINT_ENDS_PARAGRAPH )
                    hb_flags |= HB_BUFFER_FLAG_EOT;
                hb_buffer_set_flags(_hb_buffer, (hb_buffer_flags_t)hb_flags); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
            }
            if ( lang_cfg ) {
                hb_buffer_set_language(_hb_buffer, lang_cfg->getHBLanguage());
            }
            // Let HB guess what's not been set (script, direction, language)
            hb_buffer_guess_segment_properties(_hb_buffer);
            // In case HB couldn't guess a script from the unicode chars we added to its buffer,
            // (which can happen when we give it a single CJK punctuation which would be considered
            // as script COMMON, or a sequence of digits and punctuations), make sure we have HB aware
            // of some valid script, so that at least the 'locl' feature works and is able to provide
            // glyphs for the requested hb_language.
            if ( hb_buffer_get_script(_hb_buffer) == HB_SCRIPT_INVALID ) {
                // Provide "latn", which has the best chance to be known by the font and have
                // its OpenType features working.
                hb_buffer_set_script(_hb_buffer, HB_SCRIPT_LATIN);
            }

            // See measureText() for details
            if ( letter_spacing > 0 ) {
                // Don't apply letter-spacing if the script is cursive
                hb_script_t script = hb_buffer_get_script(_hb_buffer);
                if ( isHBScriptCursive(script) )
                    letter_spacing = 0;
            }

            // Shape
            hb_shape(_hb_font, _hb_buffer, _hb_features.ptr(), (unsigned int)_hb_features.length());

            // If direction is RTL, hb_shape() has reversed the order of the glyphs, so
            // they are in visual order and ready to be iterated and drawn. So,
            // we do not revert them, unlike in measureText().
            bool is_rtl = hb_buffer_get_direction(_hb_buffer) == HB_DIRECTION_RTL;

            glyph_count = hb_buffer_get_length(_hb_buffer);
            glyph_info = hb_buffer_get_glyph_infos(_hb_buffer, 0);
            glyph_pos = hb_buffer_get_glyph_positions(_hb_buffer, 0);

            #ifdef DEBUG_DRAW_TEXT
                printf("DTHB >>> drawTextString %x len %d is_rtl=%d [%s]\n", text, len, is_rtl, _faceName.c_str());
                for (i = 0; i < glyph_count; i++) {
                    char glyphname[32];
                    hb_font_get_glyph_name(_hb_font, glyph_info[i].codepoint, glyphname, sizeof(glyphname));
                    printf("DTHB g%d c%d(=t:%x) [%x %s]\tadvance=(%d,%d)", i, glyph_info[i].cluster,
                                text[glyph_info[i].cluster], glyph_info[i].codepoint, glyphname,
                                FONT_METRIC_TO_PX(glyph_pos[i].x_advance), FONT_METRIC_TO_PX(glyph_pos[i].y_advance));
                    if (glyph_pos[i].x_offset || glyph_pos[i].y_offset)
                        printf("\toffset=(%d,%d)", FONT_METRIC_TO_PX(glyph_pos[i].x_offset),
                                                   FONT_METRIC_TO_PX(glyph_pos[i].y_offset));
                    printf("\n");
                }
                printf("DTHB ---\n");
            #endif

            // We want to do just like in measureText(): drawing found glyphs with
            // this font, and .notdef glyphs with the fallback font, as a single segment,
            // once a defined glyph is found, before drawing that defined glyph.
            // The code is different from in measureText(), as the glyphs might be
            // inverted for RTL drawing, and we can't uninvert them. We also loop
            // thru glyphs here rather than chars.
            int w;

            // Cluster numbers may increase or decrease (if RTL) while we walk the glyphs.
            // We'll update fallback drawing text indices as we walk glyphs and cluster
            // (cluster numbers are boundaries in text indices, but it's quite tricky
            // to get right).
            int fb_t_start = 0;
            int fb_t_end = len;
            int hg = 0;  // index in glyph_info/glyph_pos
            while (hg < glyph_count) { // hg is the start of a new cluster at this point
                bool draw_with_fallback = false;
                int hcl = glyph_info[hg].cluster;
                fb_t_start = hcl; // if fb drawing needed from this glyph: t[hcl:..]
                    // /\ Logical if !is_rtl, but also needed if is_rtl and immediately
                    // followed by a found glyph (so, a single glyph to draw with the
                    // fallback font): = hclbad
                #ifdef DEBUG_DRAW_TEXT
                    printf("DTHB g%d c%d: ", hg, hcl);
                #endif
                int hg2 = hg;
                while ( hg2 < glyph_count ) {
                    int hcl2 = glyph_info[hg2].cluster;
                    if ( hcl2 != hcl ) { // New cluster starts at hg2: we can draw hg > hg2-1
                        #ifdef DEBUG_DRAW_TEXT
                            printf("all found, ");
                        #endif
                        if (is_rtl)
                            fb_t_end = hcl; // if fb drawing needed from next glyph: t[..:hcl]
                        break;
                    }
                    if ( glyph_info[hg2].codepoint != 0 || !has_fallback_font ) {
                        // Glyph found in this font, or not but we have no
                        // fallback font: we will draw the .notdef/tofu chars.
                        hg2++;
                        continue;
                    }
                    #ifdef DEBUG_DRAW_TEXT
                        printf("g%d c%d notdef, ", hg2, hcl2);
                    #endif
                    // Glyph notdef but we have a fallback font
                    // Go look ahead for a complete cluster, or segment of notdef,
                    // so we can draw it all with the fallback using harfbuzz
                    draw_with_fallback = true;
                    // We will update hg2 and hcl2 to be the last glyph of
                    // a cluster/segment with notdef
                    int hclbad = hcl2;
                    int hclgood = -1;
                    int hg3 = hg2+1;
                    while ( hg3 < glyph_count ) {
                        int hcl3 = glyph_info[hg3].cluster;
                        if ( hclgood >=0 && hcl3 != hclgood ) {
                            // Found a complete cluster
                            // We can draw hg > hg2-1 with fallback font
                            #ifdef DEBUG_DRAW_TEXT
                                printf("c%d complete, need redraw up to g%d", hclgood, hg2);
                            #endif
                            if (!is_rtl)
                                fb_t_end = hclgood; // fb drawing t[..:hclgood]
                            hg2 += 1; // set hg2 to the first ok glyph
                            break;
                        }
                        if ( glyph_info[hg3].codepoint == 0 || hcl3 == hclbad) {
                            #ifdef DEBUG_DRAW_TEXT
                                printf("g%d c%d -, ", hg3, hcl3);
                            #endif
                            // notdef, or def but part of uncomplete previous cluster
                            hcl2 = hcl3;
                            hg2 = hg3; // move hg2 to this bad glyph
                            hclgood = -1; // un'good found good cluster
                            hclbad = hcl3;
                            if (is_rtl)
                                fb_t_start = hclbad; // fb drawing t[hclbad::..]
                            hg3++;
                            continue;
                        }
                        // Codepoint found, and we're not part of an uncomplete cluster
                        #ifdef DEBUG_DRAW_TEXT
                            printf("g%d c%d +, ", hg3, hcl3);
                        #endif
                        hclgood = hcl3;
                        hg3++;
                    }
                    if ( hg3 == glyph_count && hclgood >=0 ) { // last glyph was a good cluster
                        if (!is_rtl)
                            fb_t_end = hclgood; // fb drawing t[..:hclgood]
                        hg2 += 1; // set hg2 to the first ok glyph (so, the single last one)
                        break;
                    }
                    if ( hg3 == glyph_count ) { // no good cluster met till end of text
                        hg2 = glyph_count; // get out of hg2 loop
                        if (is_rtl)
                            fb_t_start = 0;
                        else
                            fb_t_end = len;
                    }
                    break;
                }
                // Draw glyphs from hg to hg2 excluded
                if (draw_with_fallback) {
                    #ifdef DEBUG_DRAW_TEXT
                        printf("[...]\nDTHB ### drawing past notdef with fallback font %d>%d ", hg, hg2);
                        printf(" => %d > %d\n", fb_t_start, fb_t_end);
                    #endif
                    // Adjust DrawTextString() params for fallback drawing
                    lUInt32 fb_flags = flags | LFNT_HINT_IS_FALLBACK_FONT;
                    fb_flags &= ~LFNT_DRAW_DECORATION_MASK; // main font will do text decoration
                    // We must keep direction, but we should drop BOT/EOT flags
                    // if this segment is not at start/end (this might be bogus
                    // if the char at start or end is a space that could be drawn
                    // with the main font).
                    if (fb_t_start > 0)
                        fb_flags &= ~LFNT_HINT_BEGINS_PARAGRAPH;
                    if (fb_t_end < len)
                        fb_flags &= ~LFNT_HINT_ENDS_PARAGRAPH;
                    // Adjust fallback y so baselines of both fonts match
                    int fb_y = y + _baseline - fallback->getBaseline();
                    bool fb_addHyphen = false; // will be added by main font
                    const lChar32 * fb_text = text + fb_t_start;
                    int fb_len = fb_t_end - fb_t_start;
                    // (width and text_decoration_back_gap are only used for
                    // text decoration, that we dropped: no update needed)
                    int fb_advance = fallback->DrawTextString( buf, x, fb_y,
                       fb_text, fb_len,
                       def_char, palette, fb_addHyphen, lang_cfg, fb_flags, letter_spacing,
                       width, text_decoration_back_gap, target_w, target_h, svg_collector );
                    x += fb_advance;
                    #ifdef DEBUG_DRAW_TEXT
                        printf("DTHB ### drawn past notdef > X+= %d\n[...]", fb_advance);
                    #endif
                }
                else {
                    #ifdef DEBUG_DRAW_TEXT
                        printf("regular g%d>%d: ", hg, hg2);
                    #endif
                    // Draw glyphs of this same cluster
                    int prev_x = x;
                    for (i = hg; i < hg2; i++) {
                        if ( svg_collector ) {
                            bool can_adjust_from_previous = (i == hg) && !isHBScriptCursive(hb_buffer_get_script(_hb_buffer));
                            svg_collector->collectGlyph(this, glyph_info[i].codepoint, true, text[glyph_info[i].cluster],
                                                        glyph_pos[i].x_offset,  -glyph_pos[i].y_offset,
                                                        glyph_pos[i].x_advance, -glyph_pos[i].y_advance,
                                                        can_adjust_from_previous);
                            svg_collector->addGlyph();
                            continue;
                        }
                        LVFontGlyphCacheItem *item = getGlyphByIndex(glyph_info[i].codepoint);
                        if (item) {
                            if ( transform_stretch ) {
                                // Stretched drawing of glyph to the x/y/w/h provided (used with MathML)
                                // Split the targeted width to each glyph in case we have more than one
                                int w = target_w / glyph_count;
                                DrawStretchedGlyph(buf, glyph_info[i].codepoint, x, y, w, target_h, palette);
                                x += w;
                            }
                            else {
                                // Regular drawing of glyph at the baseline
                                int w;
                                if ( glyph_pos[i].x_advance )
                                    w = FONT_METRIC_TO_PX(glyph_pos[i].x_advance + _synth_weight_strength);
                                else
                                    w = 0;
                                #ifdef DEBUG_DRAW_TEXT
                                    printf("%x(x=%d+%d,w=%d) ", glyph_info[i].codepoint, x,
                                            item->origin_x + FONT_METRIC_TO_PX(glyph_pos[i].x_offset), w);
                                #endif
                                if ( flags & LFNT_HINT_CJK_ALTERED_WIDTH ) {
                                    int orig_width = width;
                                    if ( flags & LFNT_HINT_CJK_SCALED_WIDTH ) {
                                        // We want the below positionning to work inside the unscaled original glyph width
                                        // (this feels like the most natural way to handle this, but may not look optimal
                                        // when there are multiple consecutive opening/closing such flexible cjk chars)
                                        int cjk_width_scale_percent = target_w; // We've passed it via this unused param
                                        x += (w * cjk_width_scale_percent / 100 - w) / 2;
                                        width = width * 100 / cjk_width_scale_percent;
                                    }
                                    // We got x and have w of a fullwidth CJK char, normally some punctuation
                                    // char whose blackbox is narrow and smaller or equal to half its width.
                                    // But the position of this blackbox may depends on the opening/closing
                                    // punctuation status, and on the language requested (Simplified Chinese
                                    // get punctuations left- or right-anchored in the glyph, while Traditional
                                    // Chinese may get them centered in the glyph). We only know about the glyph
                                    // returned by the font here, so we should try to guess how to shift the
                                    // drawing to get this glyph to look alright in half of w at the original x.
                                    if ( item->origin_x + item->bmp_width <= w/2 ) {
                                        // Glyph fully in the left half part (ie. Simplified Chinese closing punctuation)
                                        // Nothing to tweak.
                                    }
                                    else if ( item->origin_x >= w/2 ) {
                                        // Glyph fully in the right half part (ie. Simplified Chinese opening punctuation)
                                        x += width - w;
                                    }
                                    else if ( item->origin_x <= w*1/5 && w - item->origin_x - item->bmp_width >= w*2/5) {
                                        // With some fonts (ie. SimSun), some left/right glyphs may leak slightly over
                                        // the middle: do a few more checks to catch these and handle them as above.
                                        // Glyph mostly in the left half part: nothing to tweak
                                    }
                                    else if ( item->origin_x >= w*2/5 && w - item->origin_x - item->bmp_width <= w*1/5) {
                                        // Glyph mostly in the rightly half part
                                        x += width - w;
                                    }
                                    else {
                                        // Glyph overlapping the middle of the glyph (ie. Traditional Chinese opening
                                        // or closing punctuation), so probably centered in its glyph.
                                        // We want to keep it centered in the provided width.
                                        x += (width - w) / 2;
                                    }
                                    width = orig_width; // restore it in case we tweaked it
                                    // We draw such CJK glyph one by one, so make sure the 'x += w' just below
                                    // gives x=x0+width, which is necessary to correctly draw any underline
                                    w = x0 + width - x;
                                    // Note: no thought given about what we should do if non-zero letter_spacing
                                }
                                else if ( flags & LFNT_HINT_CJK_SCALED_WIDTH ) {
                                    // We need to shift x by half of what was added for scaling.
                                    // (We could use 'width', which should usually be the glyph 'w' scaled, but we
                                    // don't, as it may have been increased by overlap correction.)
                                    int cjk_width_scale_percent = target_w; // We've passed it via this unused param
                                    x += (w * cjk_width_scale_percent / 100 - w) / 2;
                                    // We draw such CJK glyph one by one, so also make sure the 'x += w' just below
                                    // gives x=x0+width, which is necessary to correctly draw any underline
                                    w = x0 + width - x;
                                }
                                buf->Draw(x + item->origin_x + FONT_METRIC_TO_PX(glyph_pos[i].x_offset),
                                          y + _baseline - item->origin_y - FONT_METRIC_TO_PX(glyph_pos[i].y_offset),
                                          item->bmp,
                                          item->bmp_width,
                                          item->bmp_height,
                                          palette);
                                x += w;
                            }
                        }
                        #ifdef DEBUG_DRAW_TEXT
                        else
                            printf("SKIPPED %x", glyph_info[i].codepoint);
                        #endif
                    }
                    // Whole cluster drawn: add letter spacing
                    if ( x > prev_x ) {
                        // But only if this cluster has some advance
                        // (e.g. a soft-hyphen makes its own cluster, that
                        // draws a space glyph, but with no advance)
                        x += letter_spacing;
                    }
                }
                hg = hg2;
                #ifdef DEBUG_DRAW_TEXT
                    printf("\n");
                #endif
            }

            // Wondered if item->origin_x and glyph_pos[hg].x_offset must really
            // be added (harfbuzz' x_offset correcting Freetype's origin_x),
            // or are the same thing (harfbuzz' x_offset=0 replacing and
            // cancelling FreeType's origin_x) ?
            // Comparing screenshots seems to indicate they must be added.

            if (addHyphen) {
                ch = getHyphChar();
                LVFontGlyphCacheItem *item = getGlyph(ch, def_char);
                if (item) {
                    w = item->advance;
                    buf->Draw( x + item->origin_x,
                               y + _baseline - item->origin_y,
                               item->bmp,
                               item->bmp_width,
                               item->bmp_height,
                               palette);
                    x  += w; // + letter_spacing; (let's not add any letter-spacing after hyphen)
                }
            }

        } // _kerningMode == KERNING_MODE_HARFBUZZ
        else if (_kerningMode == KERNING_MODE_HARFBUZZ_LIGHT) {
            struct LVCharTriplet triplet;
            struct LVCharPosInfo posInfo;
            triplet.Char = 0;
            bool is_rtl = (flags & LFNT_HINT_DIRECTION_KNOWN) && (flags & LFNT_HINT_DIRECTION_IS_RTL);
            for ( i=0; i<=len; i++) {
                if ( i==len && !addHyphen )
                    break;
                if ( i<len ) {
                    // If RTL, draw glyphs starting from the of the node text
                    ch = is_rtl ? text[len-1-i] : text[i];
                    if ( ch=='\t' )
                        ch = ' ';
                    // don't draw any soft hyphens inside text string
                    isHyphen = (ch==UNICODE_SOFT_HYPHEN_CODE);
                }
                else {
                    ch = getHyphChar();
                    isHyphen = false; // an hyphen, but not one to not draw
                }
                if ( svg_collector ) {
                    triplet.prevChar = triplet.Char;
                    triplet.Char = ch;
                    if (i < len - 1)
                        triplet.nextChar = is_rtl ? text[len-1-i-1] : text[i + 1];
                    else
                        triplet.nextChar = 0;
                    if (!hbCalcCharWidth(&posInfo, triplet, def_char)) {
                        posInfo.offset = 0;
                        posInfo.width = 0;
                    }
                    svg_collector->collectGlyph(this, ch, false, ch, posInfo.offset*64, 0, posInfo.width*64, 0, posInfo.width!=0);
                    // Reset the advance with the one gotten from hbCalcCharWidth()
                    svg_collector->_advance_x = posInfo.width*64 * svg_collector->_scale;
                    svg_collector->addGlyph();
                    continue;
                }
                LVFontGlyphCacheItem * item = getGlyph(ch, def_char);
                if ( !item )
                    continue;
                if ( (item && !isHyphen) || i==len ) { // only draw soft hyphens at end of string
                    triplet.prevChar = triplet.Char;
                    triplet.Char = ch;
                    if (i < len - 1)
                        triplet.nextChar = is_rtl ? text[len-1-i-1] : text[i + 1];
                    else
                        triplet.nextChar = 0;
                    if (!_width_cache2.get(triplet, posInfo)) {
                        if (!hbCalcCharWidth(&posInfo, triplet, def_char)) {
                            posInfo.offset = 0;
                            posInfo.width = item->advance;
                        }
                        _width_cache2.set(triplet, posInfo);
                    }
                    if ( transform_stretch ) {
                        // Stretched drawing of glyph to the x/y/w/h provided (used with MathML)
                        // Split the targeted width to each glyph in case we have more than one
                        int w = target_w / len;
                        DrawStretchedGlyph(buf, getCharIndex(ch, def_char), x, y, w, target_h, palette);
                        x += w;
                    }
                    else {
                        // Regular drawing of glyph at the baseline
                        int cjk_dx = 0;
                        if ( flags & LFNT_HINT_CJK_ALTERED_WIDTH ) {
                            // See KERNING_MODE_HARFBUZZ section above for details and comments.
                            int w = posInfo.width;
                            int orig_width = width;
                            if ( flags & LFNT_HINT_CJK_SCALED_WIDTH ) {
                                int cjk_width_scale_percent = target_w; // We've passed it via this unused param
                                x += (w * cjk_width_scale_percent / 100 - w) / 2;
                                width = width * 100 / cjk_width_scale_percent;
                            }
                            if ( item->origin_x + item->bmp_width <= w/2 ) {
                                // Glyph fully in the left half part
                            }
                            else if ( item->origin_x >= w/2 ) {
                                // Glyph fully in the right half part
                                x += width - w;
                            }
                            else if ( item->origin_x <= w*1/5 && w - item->origin_x - item->bmp_width >= w*2/5) {
                                // Glyph mostly in the left half part
                            }
                            else if ( item->origin_x >= w*2/5 && w - item->origin_x - item->bmp_width <= w*1/5) {
                                // Glyph mostly in the rightly half part
                                x += width - w;
                            }
                            else {
                                // Glyph overlapping the middle of the glyph
                                x += (width - w) / 2;
                            }
                            width = orig_width; // restore it in case we tweaked it
                            // We draw such CJK glyph one by one, so make sure the 'x += posInfo.width' just below
                            // gives x=x0+width, which is necessary to correctly draw any underline
                            cjk_dx = x0 + width - x - posInfo.width;
                        }
                        else if ( flags & LFNT_HINT_CJK_SCALED_WIDTH ) {
                            // See KERNING_MODE_HARFBUZZ section above for details and comments.
                            int cjk_width_scale_percent = target_w; // We've passed it via this unused param
                            x += (posInfo.width * cjk_width_scale_percent / 100 - posInfo.width) / 2;
                            cjk_dx = x0 + width - x - posInfo.width;
                        }
                        buf->Draw(x + item->origin_x + posInfo.offset,
                            y + _baseline - item->origin_y,
                            item->bmp,
                            item->bmp_width,
                            item->bmp_height,
                            palette);
                        // Assume zero advance means it's a diacritic, and we should not apply
                        // any letter spacing on this char (now, and when justifying)
                        if ( posInfo.width != 0 )
                            x += posInfo.width + letter_spacing + cjk_dx;
                    }
                }
            }
        } // _kerningMode == KERNING_MODE_HARFBUZZ_LIGHT

        else { // _kerningMode == KERNING_MODE_DISABLED or KERNING_MODE_FREETYPE:
               // fallback to the non harfbuzz code
    #endif // USE_HARFBUZZ

        FT_UInt previous = 0;
        int error;
        #if (ALLOW_KERNING==1)
        int use_kerning = _kerningMode != KERNING_MODE_DISABLED && FT_HAS_KERNING( _face );
        #endif
        bool is_rtl = (flags & LFNT_HINT_DIRECTION_KNOWN) && (flags & LFNT_HINT_DIRECTION_IS_RTL);
        for ( i=0; i<=len; i++) {
            if ( i==len && !addHyphen )
                break;
            if ( i<len ) {
                // If RTL, draw glyphs starting from the end of the node text segment
                // It seems all is fine, with RTL, getting diacritics glyphs before
                // the main glyph, while in LTR, they must come after. So no need for
                // reordering these.
                // Drawing a + diaresis:
                //   drawing 61  adv=8 kerning=0  => w=8 o_x=0
                //   drawing 308 adv=0 kerning=0  => w=0 o_x=-7 (origin_x going back)
                // Drawing some hebrew char with 2 diacritics before:
                //   drawing 5bc adv=0 kerning=0  => w=0 o_x=4 (diacritics don't advance)
                //   drawing 5b6 adv=0 kerning=0  => w=0 o_x=3
                //   drawing 5e1 adv=11 kerning=0 => w=11 o_x=0 (main char will advance)
                ch = is_rtl ? text[len-1-i] : text[i];
                if ( ch=='\t' )
                    ch = ' ';
                // don't draw any soft hyphens inside text string
                isHyphen = (ch==UNICODE_SOFT_HYPHEN_CODE);
            }
            else {
                ch = getHyphChar();
                isHyphen = false; // an hyphen, but not one to not draw
            }
            FT_UInt ch_glyph_index = getCharIndex( ch, def_char );
            int kerning = 0;
            #if (ALLOW_KERNING==1)
            if ( use_kerning && previous>0 && ch_glyph_index>0 ) {
                FT_Vector delta;
                error = FT_Get_Kerning( _face,          /* handle to face object */
                              previous,          /* left glyph index      */
                              ch_glyph_index,         /* right glyph index     */
                              FT_KERNING_DEFAULT,  /* kerning mode          */
                              &delta );    /* target vector         */
                if ( !error )
                    kerning = delta.x;
            }
            #endif
            if ( svg_collector ) {
                svg_collector->collectGlyph(this, ch, false, ch, kerning, 0, 0, 0);
                svg_collector->_advance_x += kerning * svg_collector->_scale;
                if ( svg_collector->_advance_x == 0 ) // Assume it's a diacritic
                    svg_collector->_can_adjust_from_previous = false;
                svg_collector->addGlyph();
                previous = ch_glyph_index;
                continue;
            }
            LVFontGlyphCacheItem * item = getGlyph(ch, def_char);
            if ( !item )
                continue;
            if ( (item && !isHyphen) || i==len ) { // only draw soft hyphens at end of string
                int w = item->advance + FONT_METRIC_TO_PX(kerning);
                #ifdef DEBUG_DRAW_TEXT
                    printf("DTFT drawing %x adv=%d kerning=%d => w=%d o_x=%d\n",
                                ch, item->advance, FONT_METRIC_TO_PX(kerning), w, item->origin_x);
                #endif
                if ( transform_stretch ) {
                    // Stretched drawing of glyph to the x/y/w/h provided (used with MathML)
                    // Split the targeted width to each glyph in case we have more than one
                    int w = target_w / len;
                    DrawStretchedGlyph(buf, getCharIndex(ch, def_char), x, y, w, target_h, palette);
                    x += w;
                }
                else {
                    // Regular drawing of glyph at the baseline
                    if ( flags & LFNT_HINT_CJK_ALTERED_WIDTH ) {
                        // See KERNING_MODE_HARFBUZZ section above for details and comments.
                        int orig_width = width;
                        if ( flags & LFNT_HINT_CJK_SCALED_WIDTH ) {
                            int cjk_width_scale_percent = target_w; // We've passed it via this unused param
                            x += (w * cjk_width_scale_percent / 100 - w) / 2;
                            width = width * 100 / cjk_width_scale_percent;
                        }
                        if ( item->origin_x + item->bmp_width <= w/2 ) {
                            // Glyph fully in the left half part
                        }
                        else if ( item->origin_x >= w/2 ) {
                            // Glyph fully in the right half part
                            x += width - w;
                        }
                        else if ( item->origin_x <= w*1/5 && w - item->origin_x - item->bmp_width >= w*2/5) {
                            // Glyph mostly in the left half part
                        }
                        else if ( item->origin_x >= w*2/5 && w - item->origin_x - item->bmp_width <= w*1/5) {
                            // Glyph mostly in the rightly half part
                            x += width - w;
                        }
                        else {
                            // Glyph overlapping the middle of the glyph
                            x += (width - w) / 2;
                        }
                        width = orig_width; // restore it in case we tweaked it
                        // We draw such CJK glyph one by one, so make sure the 'x += w' just below
                        // gives x=x0+width, which is necessary to correctly draw any underline
                        w = x0 + width - x;
                    }
                    else if ( flags & LFNT_HINT_CJK_SCALED_WIDTH ) {
                        // See KERNING_MODE_HARFBUZZ section above for details and comments.
                        int cjk_width_scale_percent = target_w; // We've passed it via this unused param
                        x += (w * cjk_width_scale_percent / 100 - w) / 2;
                        w = x0 + width - x;
                    }
                    buf->Draw( x + FONT_METRIC_TO_PX(kerning) + item->origin_x,
                        y + _baseline - item->origin_y,
                        item->bmp,
                        item->bmp_width,
                        item->bmp_height,
                        palette);

                    // Assume zero advance means it's a diacritic, and we should not apply
                    // any letter spacing on this char (now, and when justifying)
                    if ( w != 0 )
                        x += w + letter_spacing;
                }
                previous = ch_glyph_index;
            }
        }

    #if USE_HARFBUZZ==1
        } // else fallback to the non harfbuzz code
    #endif

        int advance = x - x0;
        if ( flags & LFNT_DRAW_DECORATION_MASK ) {
            // text decoration: underline, etc.
            // Don't overflow the provided width (which may be lower than our
            // pen x if last glyph was a space not accounted in word width)
            if ( width >= 0 && x > x0 + width)
                x = x0 + width;
            // And start the decoration before x0 if it is continued
            // from previous word
            x0 -= text_decoration_back_gap;
            lUInt32 cl = buf->GetTextColor();
            if ( flags & LFNT_DRAW_UNDERLINE ) {
                int liney = y + _baseline + _underline_offset;
                buf->FillRect( x0, liney, x, liney+_underline_thickness, cl );
            }
            if ( flags & LFNT_DRAW_OVERLINE ) {
                // For now, we use the underline thickness as the offset from top, so
                // to not be too high (should probably be computed from other metrics)
                int liney = y + _underline_thickness;
                buf->FillRect( x0, liney, x, liney+_underline_thickness, cl );
                // If we want to use this flag while developping for visually marking the start of words, use this instead:
                // buf->FillRect( x0-_baseline/4, y, x0+_baseline/4, y+1, cl );
            }
            if ( flags & LFNT_DRAW_LINE_THROUGH ) {
                // int liney = y + _baseline - _size/4 - h/2;
                int liney = y + _baseline - _size*2/7;
                buf->FillRect( x0, liney, x, liney+_underline_thickness, cl );
            }
        }
        return advance;
    }

    void DrawStretchedGlyph(LVDrawBuf * buf, int glyph_index, int x, int y, int w, int h, lUInt32 * palette=NULL)
    {
        // This is used for drawing stretched MathML operators,
        // and we do it the "cheap" way by just scaling the glyph
        // (which will have the edges of glyphs like '[' or '{'
        // look blury when scaled a lot on the y-asis only)
        //
        // More proper drawing could be done with the help of
        // the math font OpenType features. See:
        // Harfbuzz OT math support:
        //  https://github.com/harfbuzz/harfbuzz/issues/235
        //  https://github.com/harfbuzz/harfbuzz/issues/2585
        // Harfbuzz code (not merged) to properly shape/draw stretchy operators:
        //  https://github.com/fred-wang/harfbuzz/tree/MATH-3
        //  https://frederic-wang.fr/opentype-math-in-harfbuzz.html

        // We don't want to cache anything about these stretched glyphs
        glyph_info_t glyph;
        if ( !getGlyphInfo( glyph_index, &glyph, 0, true ) ) {
            return; // no glyph
        }

        if ( glyph.width == 0 || glyph.blackBoxY == 0 ) {
            return; // blank glyph
        }

        // Initial idea:
        // (Best to use the advance instead of the ink width, so that we keep a bit of the original lsb/rsb)
        // int scale_x = w * 256 / glyph.width; // instead of glyph.blackBoxX
        // int scale_y = h * 256 / glyph.blackBoxY;

        // But it feels better to try to get this a bit more adjusted:
        // We want to keep the original glyph lsb/rsb, so they feel naturally adjusted
        // to neighbours, as when unscaled:
        int pad_left = glyph.originX > 0 ? glyph.originX : 0;
        int pad_right = glyph.rsb > 0 ? glyph.rsb : 0;
        int target_w = w - pad_left - pad_right;
        // For the height, we can't really trust the font glyphs vertical position
        // and top/bottom side bearings. So, keep 1px on each side if we are stretching
        // vertically, but none if we are horizontally stretching (as these are usually
        // quite thin, and some vertical spacing is already accounted in munder/mover).
        int pad_top_bottom = h > w ? 1 : 0;
        int target_h = h - 2*pad_top_bottom;

        // Be sure we don't go negative in our computations
        if ( target_w <= 0 )
            target_w = 1;
        if ( target_h <= 0 )
            target_h = 1;

        int scale_x = target_w * 256 / glyph.blackBoxX;
        int scale_y = target_h * 256 / glyph.blackBoxY;

        // Hijack pixel size to let FreeType scale the glyph to our target size
        int size_x = _size * scale_x / 256;
        int size_y = _size * scale_y / 256;
        if ( size_x <= 0 )
            size_x = 1;
        if ( size_y <= 0 )
            size_y = 1;
        int error = FT_Set_Pixel_Sizes(
            _face,       /* handle to face object */
            size_x,      /* pixel_width  */
            size_y);     /* pixel_height */

        int rend_flags = FT_LOAD_RENDER | ( !_drawMonochrome ? FT_LOAD_TARGET_LIGHT : FT_LOAD_TARGET_MONO );
        if (_hintingMode == HINTING_MODE_BYTECODE_INTERPRETOR) {
            rend_flags |= FT_LOAD_NO_AUTOHINT;
        }
        else if (_hintingMode == HINTING_MODE_AUTOHINT) {
            rend_flags |= FT_LOAD_FORCE_AUTOHINT;
        }
        else if (_hintingMode == HINTING_MODE_DISABLED) {
            rend_flags |= FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING;
        }
        if (_synth_weight > 0 || _italic == 2) { // Don't render yet
            rend_flags &= ~FT_LOAD_RENDER;
            // Also disable any hinting, as it would be wrong after embolden.
            // But it feels this is now fine after switching to FT_LOAD_TARGET_LIGHT.
            // rend_flags |= FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING;
        }
        /* load glyph image into the slot (erase previous one) */
        updateTransform(); // no-op
        error = FT_Load_Glyph( _face, /* handle to face object */
                glyph_index,           /* glyph index           */
                rend_flags );             /* load flags, see below */
        if ( error == FT_Err_Execution_Too_Long && _hintingMode == HINTING_MODE_BYTECODE_INTERPRETOR ) {
            // Native hinting bytecode may fail with some bad fonts: try again with no hinting
            rend_flags |= FT_LOAD_NO_HINTING;
            error = FT_Load_Glyph( _face, glyph_index, rend_flags );
        }
        if ( error ) {
            return;
        }
        if (_synth_weight > 0) {
            // See setSynthWeight() for details
            if ( _slot->format == FT_GLYPH_FORMAT_OUTLINE ) {
                FT_Outline_Embolden(&_slot->outline, _synth_weight_strength);
                FT_Outline_Translate(&_slot->outline, 0, -_synth_weight_half_strength);
            }
        }
        if (_italic==2) {
            FT_GlyphSlot_Oblique(_slot);
        }
        if (_synth_weight > 0 || _italic == 2) {
            // Render now that transformations are applied
            FT_Render_Glyph(_slot, _drawMonochrome?FT_RENDER_MODE_MONO:FT_RENDER_MODE_LIGHT);
        }

        FT_Bitmap * bitmap = &_slot->bitmap;

        // Position the resulting bitmap in the targeted area (and center the rounding errors)
        int pad_x = pad_left + (target_w > (int)bitmap->width ? (target_w - bitmap->width)/2 : 0);
        int pad_y = pad_top_bottom + (target_h > (int)bitmap->rows ? (target_h - bitmap->rows)/2 : 0);

        // This felt needed at some point to draw tall stretchy glyphs, but seems no longer needed
        // buf->setHidePartialGlyphs(false);

        buf->Draw( x + pad_x,
            y + pad_y,
            bitmap->buffer,
            bitmap->width,
            bitmap->rows,
            palette);

        // Restore original pixel size
        error = FT_Set_Pixel_Sizes(
            _face,    /* handle to face object */
            0,        /* pixel_width           */
            _face_size );  /* pixel_height          */
        return;
    }

    /// returns true if font is empty
    virtual bool IsNull() const
    {
        return _face == NULL;
    }

    virtual bool operator ! () const
    {
        return _face == NULL;
    }

    virtual void Clear()
    {
        LVLock lock(_mutex);
        clearCache();
        #if USE_HARFBUZZ==1
        if (_hb_font) {
            hb_font_destroy(_hb_font);
            _hb_font = 0;
        }
        #endif
        if ( _face ) {
            FT_Done_Face(_face);
            _face = NULL;
        }
        if ( _extra_metric ) {
            free(_extra_metric);
            _extra_metric = NULL;
        }
    }

};

class LVFontBoldTransform : public LVFont
{
    LVFontRef _baseFontRef;
    LVFont * _baseFont;
    int _hyphWidth;
    int _hShift;
    int _vShift;
    int           _size;   // glyph height in pixels
    int           _height; // line height in pixels
    //int           _hyphen_width;
    int           _baseline;
    LVFontLocalGlyphCache _glyph_cache;
public:
    /// returns font weight
    virtual int getWeight() const
    {
        int w = _baseFont->getWeight() + 200;
        if ( w>900 )
            w = 900;
        return w;
    }
    /// returns italic flag
    virtual int getItalic() const
    {
        return _baseFont->getItalic();
    }
    LVFontBoldTransform( LVFontRef baseFont, LVFontGlobalGlyphCache * globalCache )
        : _baseFontRef( baseFont ), _baseFont( baseFont.get() ), _hyphWidth(-1), _glyph_cache(globalCache)
    {
        _size = _baseFont->getSize();
        _height = _baseFont->getHeight();
        _hShift = _size <= 36 ? 1 : 2;
        _vShift = _size <= 36 ? 0 : 1;
        _baseline = _baseFont->getBaseline();
    }

    /// hyphenation character
    virtual lChar32 getHyphChar() {
        // return UNICODE_SOFT_HYPHEN_CODE;
        return UNICODE_HYPHEN_MINUS;
    }

    /// hyphen width
    virtual int getHyphenWidth() {
        FONT_GUARD
        if ( _hyphWidth<0 )
            _hyphWidth = getCharWidth( getHyphChar() );
        return _hyphWidth;
    }

    /** \brief get glyph info
        \param glyph is pointer to glyph_info_t struct to place retrieved info
        \return true if glyh was found
    */
    virtual bool getGlyphInfo( lUInt32 code, glyph_info_t * glyph, lChar32 def_char=0, bool code_is_glyph_index=false, bool is_fallback=false  )
    {
        bool res = _baseFont->getGlyphInfo( code, glyph, def_char, is_fallback );
        if ( !res )
            return res;
        glyph->blackBoxX += glyph->blackBoxX>0 ? _hShift : 0;
        glyph->blackBoxY += _vShift;
        glyph->width += _hShift;

        return true;
    }

    /** \brief measure text
        \param text is text string pointer
        \param len is number of characters to measure
        \param max_width is maximum width to measure line
        \param def_char is character to replace absent glyphs in font
        \param letter_spacing is number of pixels to add between letters
        \return number of characters before max_width reached
    */
    virtual lUInt16 measureText(
                        const lChar32 * text, int len,
                        lUInt16 * widths,
                        lUInt8 * flags,
                        int max_width,
                        lChar32 def_char,
                        TextLangCfg * lang_cfg = NULL,
                        int letter_spacing=0,
                        bool allow_hyphenation=true,
                        lUInt32 hints=0
                     )
    {
        CR_UNUSED(allow_hyphenation);
        lUInt16 res = _baseFont->measureText(
                        text, len,
                        widths,
                        flags,
                        max_width,
                        def_char,
                        lang_cfg,
                        letter_spacing,
                        allow_hyphenation,
                        hints
                     );
        int w = 0;
        for ( int i=0; i<res; i++ ) {
            w += _hShift;
            widths[i] += w;
        }
        return res;
    }

    /** \brief measure text
        \param text is text string pointer
        \param len is number of characters to measure
        \return width of specified string
    */
    virtual lUInt32 getTextWidth( const lChar32 * text, int len, TextLangCfg * lang_cfg=NULL) {
        static lUInt16 widths[MAX_LINE_CHARS+1];
        static lUInt8 flags[MAX_LINE_CHARS+1];
        if ( len>MAX_LINE_CHARS )
            len = MAX_LINE_CHARS;
        if ( len<=0 )
            return 0;
        lUInt16 res = measureText(
                        text, len,
                        widths,
                        flags,
                        MAX_LINE_WIDTH,
                        U' ',  // def_char
                        lang_cfg
                     );
        if ( res>0 && res<MAX_LINE_CHARS )
            return widths[res-1];
        return 0;
    }

    /** \brief get glyph item
        \param code is unicode character
        \return glyph pointer if glyph was found, NULL otherwise
    */
    virtual LVFontGlyphCacheItem * getGlyph(lUInt32 ch, lChar32 def_char=0, bool is_fallback=false) {

        LVFontGlyphCacheItem * item = _glyph_cache.getByChar( ch );
        if ( item )
            return item;

        LVFontGlyphCacheItem * olditem = _baseFont->getGlyph( ch, def_char, is_fallback );
        if ( !olditem )
            return NULL;

        int oldx = olditem->bmp_width;
        int oldy = olditem->bmp_height;
        int dx = oldx ? oldx + _hShift : 0;
        int dy = oldy ? oldy + _vShift : 0;

        item = LVFontGlyphCacheItem::newItem( &_glyph_cache, (lChar32)ch, dx, dy ); //, _drawMonochrome
        if (item) {
            item->advance = olditem->advance + _hShift;
            item->origin_x = olditem->origin_x;
            item->origin_y = olditem->origin_y;

            if ( dx && dy ) {
                for ( int y=0; y<dy; y++ ) {
                    lUInt8 * dst = item->bmp + y*dx;
                    for ( int x=0; x<dx; x++ ) {
                        int s = 0;
                        for ( int yy=-_vShift; yy<=0; yy++ ) {
                            int srcy = y+yy;
                            if ( srcy<0 || srcy>=oldy )
                                continue;
                            lUInt8 * src = olditem->bmp + srcy*oldx;
                            for ( int xx=-_hShift; xx<=0; xx++ ) {
                                int srcx = x+xx;
                                if ( srcx>=0 && srcx<oldx && src[srcx] > s )
                                    s = src[srcx];
                            }
                        }
                        dst[x] = s;
                    }
                }
            }
            _glyph_cache.put( item );
        }
        return item;
    }

    /** \brief get glyph image in 1 byte per pixel format
        \param code is unicode character
        \param buf is buffer [width*height] to place glyph data
        \return true if glyph was found
    */
//    virtual bool getGlyphImage(lUInt16 code, lUInt8 * buf, lChar32 def_char=0 )
//    {
//        LVFontGlyphCacheItem * item = getGlyph( code, def_char );
//        if ( !item )
//            return false;
//        glyph_info_t glyph;
//        if ( !_baseFont->getGlyphInfo( code, &glyph, def_char ) )
//            return 0;
//        int oldx = glyph.blackBoxX;
//        int oldy = glyph.blackBoxY;
//        int dx = oldx + _hShift;
//        int dy = oldy + _vShift;
//        if ( !oldx || !oldy )
//            return true;
//        LVAutoPtr<lUInt8> tmp( new lUInt8[oldx*oldy+2000] );
//        memset(buf, 0, dx*dy);
//        tmp[oldx*oldy]=123;
//        bool res = _baseFont->getGlyphImage( code, tmp.get(), def_char );
//        if ( tmp[oldx*oldy]!=123 ) {
//            //CRLog::error("Glyph buffer corrupted!");
//            // clear cache
//            for ( int i=32; i<4000; i++ ) {
//                _baseFont->getGlyphInfo( i, &glyph, def_char );
//                _baseFont->getGlyphImage( i, tmp.get(), def_char );
//            }
//            _baseFont->getGlyphInfo( code, &glyph, def_char );
//            _baseFont->getGlyphImage( code, tmp.get(), def_char );
//        }
//        for ( int y=0; y<dy; y++ ) {
//            lUInt8 * dst = buf + y*dx;
//            for ( int x=0; x<dx; x++ ) {
//                int s = 0;
//                for ( int yy=-_vShift; yy<=0; yy++ ) {
//                    int srcy = y+yy;
//                    if ( srcy<0 || srcy>=oldy )
//                        continue;
//                    lUInt8 * src = tmp.get() + srcy*oldx;
//                    for ( int xx=-_hShift; xx<=0; xx++ ) {
//                        int srcx = x+xx;
//                        if ( srcx>=0 && srcx<oldx && src[srcx] > s )
//                            s = src[srcx];
//                    }
//                }
//                dst[x] = s;
//            }
//        }
//        return res;
//        return false;
//    }

    /// returns font baseline offset
    virtual int getBaseline()
    {
        return _baseline;
    }

    /// returns font height
    virtual int getHeight() const
    {
        return _height;
    }

    /// returns font character size
    virtual int getSize() const
    {
        return _size;
    }

    /// returns char glyph advance width
    virtual int getCharWidth( lChar32 ch, lChar32 def_char=0 )
    {
        int w = _baseFont->getCharWidth( ch, def_char ) + _hShift;
        return w;
    }

    /// returns char glyph left side bearing
    virtual int getLeftSideBearing( lChar32 ch, bool negative_only=false, bool italic_only=false )
    {
        return _baseFont->getLeftSideBearing( ch, negative_only, italic_only );
    }

    /// returns char glyph right side bearing
    virtual int getRightSideBearing( lChar32 ch, bool negative_only=false, bool italic_only=false )
    {
        return _baseFont->getRightSideBearing( ch, negative_only, italic_only );
    }

    /// retrieves font handle
    virtual void * GetHandle()
    {
        return NULL;
    }

    /// returns font typeface name
    virtual lString8 getTypeFace() const
    {
        return _baseFont->getTypeFace();
    }

    /// returns font family id
    virtual css_font_family_t getFontFamily() const
    {
        return _baseFont->getFontFamily();
    }

    /// draws text string (returns x advance)
    virtual int DrawTextString( LVDrawBuf * buf, int x, int y,
                       const lChar32 * text, int len,
                       lChar32 def_char, lUInt32 * palette, bool addHyphen,
                       TextLangCfg * lang_cfg,
                       lUInt32 flags, int letter_spacing, int width,
                       int text_decoration_back_gap,
                       int target_w, int target_h,
                       SVGGlyphsCollector * svg_collector)
    {
        if ( len <= 0 )
            return 0;
        if ( letter_spacing < 0 ) {
            letter_spacing = 0;
        }
        else if ( letter_spacing > MAX_LETTER_SPACING ) {
            letter_spacing = MAX_LETTER_SPACING;
        }
        lvRect clip;
        buf->GetClipRect( &clip );
        if ( y + _height < clip.top || y >= clip.bottom )
            return 0;

        //int error;

        int i;

        //lUInt16 prev_width = 0;
        lChar32 ch;
        // measure character widths
        bool isHyphen = false;
        int x0 = x;
        bool is_rtl = (flags & LFNT_HINT_DIRECTION_KNOWN) && (flags & LFNT_HINT_DIRECTION_IS_RTL);
        for ( i=0; i<=len; i++) {
            if ( i==len && !addHyphen )
                break;
            if ( i<len ) {
                ch = is_rtl ? text[len-1-i] : text[i];
                isHyphen = (ch==UNICODE_SOFT_HYPHEN_CODE);
            }
            else {
                ch = getHyphChar();
                isHyphen = 0;
            }

            LVFontGlyphCacheItem * item = getGlyph(ch, def_char);
            int w  = 0;
            if ( item ) {
                // avoid soft hyphens inside text string
                w = item->advance;
                if ( item->bmp_width && item->bmp_height && (!isHyphen || i==len) ) {
                    buf->Draw( x + item->origin_x,
                        y + _baseline - item->origin_y,
                        item->bmp,
                        item->bmp_width,
                        item->bmp_height,
                        palette);
                }
            }
            x  += w + letter_spacing;
        }
        int advance = x - x0;
        if ( flags & LFNT_DRAW_DECORATION_MASK ) {
            // text decoration: underline, etc.
            // Don't overflow the provided width (which may be lower than our
            // pen x if last glyph was a space not accounted in word width)
            if ( width >= 0 && x > x0 + width)
                x = x0 + width;
            // And start the decoration before x0 if it is continued
            // from previous word
            x0 -= text_decoration_back_gap;
            int h = _size > 30 ? 2 : 1;
            lUInt32 cl = buf->GetTextColor();
            if ( flags & LFNT_DRAW_UNDERLINE ) {
                int liney = y + _baseline + h;
                buf->FillRect( x0, liney, x, liney+h, cl );
            }
            if ( flags & LFNT_DRAW_OVERLINE ) {
                int liney = y + h;
                buf->FillRect( x0, liney, x, liney+h, cl );
            }
            if ( flags & LFNT_DRAW_LINE_THROUGH ) {
                int liney = y + _height/2 - h/2;
                buf->FillRect( x0, liney, x, liney+h, cl );
            }
        }
        return advance;
    }

    /// get bitmap mode (true=monochrome bitmap, false=antialiased)
    virtual bool getBitmapMode()
    {
        return _baseFont->getBitmapMode();
    }

    /// set bitmap mode (true=monochrome bitmap, false=antialiased)
    virtual void setBitmapMode( bool m )
    {
        _baseFont->setBitmapMode( m );
    }

    /// sets current hinting mode
    virtual void setHintingMode(hinting_mode_t mode) { _baseFont->setHintingMode(mode); }
    /// returns current hinting mode
    virtual hinting_mode_t  getHintingMode() const { return _baseFont->getHintingMode(); }

    /// get kerning mode
    virtual kerning_mode_t getKerningMode() const { return _baseFont->getKerningMode(); }

    /// get kerning mode
    virtual void setKerningMode( kerning_mode_t mode ) { _baseFont->setKerningMode( mode ); }

    /// clear cache
    virtual void clearCache() { _baseFont->clearCache(); }

    /// returns true if font is empty
    virtual bool IsNull() const
    {
        return _baseFont->IsNull();
    }

    virtual bool operator ! () const
    {
        return !(*_baseFont);
    }
    virtual void Clear()
    {
        _baseFont->Clear();
    }
    virtual ~LVFontBoldTransform()
    {
    }
};

/// create transform for font
//LVFontRef LVCreateFontTransform( LVFontRef baseFont, int transformFlags )
//{
//    if ( transformFlags & LVFONT_TRANSFORM_EMBOLDEN ) {
//        // BOLD transform
//        return LVFontRef( new LVFontBoldTransform( baseFont ) );
//    } else {
//        return baseFont; // no transform
//    }
//}

#if (DEBUG_FONT_SYNTHESIS==1)
static LVFontRef dumpFontRef( LVFontRef fnt ) {
    CRLog::trace("%s %d (%d) w=%d %s", fnt->getTypeFace().c_str(), fnt->getSize(), fnt->getHeight(), fnt->getWeight(), fnt->getItalic()?"italic":"" );
    return fnt;
}
#endif

class LVFreeTypeFontManager : public LVFontManager
{
private:
    lString8    _path;
    lString8    _fallbackFontFacesString; // comma separated list of fallback fonts
    lString8Collection _fallbackFontFaces;  // splitted from previous
    LVFontCache _cache;
    FT_Library  _library;
    LVFontGlobalGlyphCache _globalCache;
    lString32 _requiredChars;
    #if (DEBUG_FONT_MAN==1)
    FILE * _log;
    #endif
    LVMutex   _lock;
public:

    /// get hash of installed fonts and fallback font
    virtual lUInt32 GetFontListHash(int documentId) {
        FONT_MAN_GUARD
        return _cache.GetFontListHash(documentId) * 75 + _fallbackFontFacesString.getHash();
    }

    /// set fallback fonts
    virtual bool SetFallbackFontFaces( lString8 facesString ) {
        FONT_MAN_GUARD
        if ( facesString != _fallbackFontFacesString ) {
            // Multiple fallback font names can be provided, separated by '|'
            lString8Collection faces = lString8Collection(facesString, lString8("|"));
            bool has_valid_face = false;
            for (int i = 0; i < faces.length(); i++) {
                lString8 face = faces[i];
                CRLog::trace("Looking for fallback font %s", face.c_str());
                LVFontCacheItem * item = _cache.findFallback( face, -1 );
                if ( !item ) { // not found
                    continue;
                }
                if ( !has_valid_face ) {
                    has_valid_face = true;
                    // One valid font: clear previous set of fallback fonts
                    _fallbackFontFaces.clear();
                }
                // Check if duplicate (to avoid fallback font loops)
                bool is_duplicate = false;
                for ( int i=0; i < _fallbackFontFaces.length(); i++ ) {
                    if ( face == _fallbackFontFaces[i] ) {
                        is_duplicate = true;
                        break;
                    }
                }
                if ( is_duplicate ) {
                    continue;
                }
                _fallbackFontFaces.add(face);
            }
            if ( !has_valid_face ) {
                // Don't clear previous fallbacks and cache if this one has
                // not a single found and valid font
                return false;
            }
            _fallbackFontFacesString = facesString;
            _cache.clearFallbackFonts();
            // Somehow, with Fedra Serif (only!), changing the fallback font does
            // not prevent glyphs from previous fallback font to be re-used...
            // So let's clear glyphs caches too.
            gc();
            clearGlyphCache();
        }
        return !_fallbackFontFacesString.empty();
    }

    /// set as preferred font with the given bias to add in CalcMatch algorithm
    virtual bool SetAsPreferredFontWithBias( lString8 face, int bias, bool clearOthersBias ) {
        FONT_MAN_GUARD
        return _cache.setAsPreferredFontWithBias(face, bias, clearOthersBias);
    }


    /// get fallback font face (returns empty string if no fallback font is set)
    virtual lString8 GetFallbackFontFaces() { return _fallbackFontFacesString; }

    /// returns fallback font for specified size
    virtual LVFontRef GetFallbackFont(int size) {
        FONT_MAN_GUARD
        if ( _fallbackFontFaces.length() == 0 )
            return LVFontRef();
        // reduce number of possible distinct sizes for fallback font
        if ( size>40 )
            size &= 0xFFF8;
        else if ( size>28 )
            size &= 0xFFFC;
        else if ( size>16 )
            size &= 0xFFFE;
        LVFontCacheItem * item = _cache.findFallback( _fallbackFontFaces[0], size );
        if ( !item->getFont().isNull() )
            return item->getFont();
        return GetFont(size, 400, false, css_ff_sans_serif, _fallbackFontFaces[0], 0, -1);
    }

    /// returns fallback font for specified size, weight and italic
    virtual LVFontRef GetFallbackFont(int size, int weight=400, bool italic=false, lString8 forFaceName=lString8::empty_str) {
        FONT_MAN_GUARD
        if ( _fallbackFontFaces.length() == 0 )
            return LVFontRef();
        /* No real need to limit the number of instances (we prefer accuracy)
        // reduce number of possible distinct sizes for fallback font
        if ( size>40 )
            size &= 0xFFF8;
        else if ( size>28 )
            size &= 0xFFFC;
        else if ( size>16 )
            size &= 0xFFFE;
        */
        // If forFaceName not provided, returns first font among _fallbackFontFaces.
        // If forFaceName provided, returns the one just after it, if forFaceName is
        // among _fallbackFontFaces. If it is not, return the first one.
        int idx = 0;
        if ( !forFaceName.empty() ) {
            for ( int i=0; i < _fallbackFontFaces.length(); i++ ) {
                if ( forFaceName == _fallbackFontFaces[i] ) {
                    idx = i + 1;
                    if ( idx >= _fallbackFontFaces.length() ) // forFaceName was last fallback font
                        return LVFontRef();
                    break;
                }
            }
        }
        // We don't use/extend findFallback(), which was made to work
        // assuming the fallback font is a standalone regular font
        // without any bold/italic sibling.
        // GetFont() works just as fine when we need specified weigh and italic.
        return GetFont(size, weight, italic, css_ff_sans_serif, _fallbackFontFaces[idx], 0, -1);
    }

    bool isBitmapModeForSize( int size )
    {
        bool bitmap = false;
        switch ( _antialiasMode ) {
        case font_aa_none:
            bitmap = true;
            break;
        case font_aa_big:
            bitmap = size<20 ? true : false;
            break;
        case font_aa_all:
        default:
            bitmap = false;
            break;
        }
        return bitmap;
    }

    /// set antialiasing mode
    virtual void SetAntialiasMode( int mode )
    {
        _antialiasMode = mode;
        gc();
        clearGlyphCache();
        FONT_MAN_GUARD
        LVPtrVector< LVFontCacheItem > * fonts = _cache.getInstances();
        for ( int i=0; i<fonts->length(); i++ ) {
            fonts->get(i)->getFont()->setBitmapMode( isBitmapModeForSize( fonts->get(i)->getFont()->getHeight() ) );
        }
    }

    /// sets current gamma level
    virtual void SetHintingMode(hinting_mode_t mode) {
        if (_hintingMode == mode)
            return;
        FONT_MAN_GUARD
        CRLog::debug("Hinting mode is changed: %d", (int)mode);
        _hintingMode = mode;
        gc();
        clearGlyphCache();
        LVPtrVector< LVFontCacheItem > * fonts = _cache.getInstances();
        for ( int i=0; i<fonts->length(); i++ ) {
            fonts->get(i)->getFont()->setHintingMode(mode);
        }
    }

    /// sets current gamma level
    virtual hinting_mode_t  GetHintingMode() {
        return _hintingMode;
    }

    /// set antialiasing mode
    virtual void SetKerningMode( kerning_mode_t mode )
    {
        FONT_MAN_GUARD
        _kerningMode = mode;
        gc();
        clearGlyphCache();
        LVPtrVector< LVFontCacheItem > * fonts = _cache.getInstances();
        for ( int i=0; i<fonts->length(); i++ ) {
            fonts->get(i)->getFont()->setKerningMode( mode );
        }
    }

    /// set monospace size scale percent
    virtual void SetMonospaceSizeScale( int scale )
    {
        FONT_MAN_GUARD
        _monospaceSizeScale = scale;
        gc();
        clearGlyphCache();
        // We would need to loop thru each instance and somehow invalidate
        // the monospace instances, as we need to load another face size
        // from the font file. This seems quite tedious to implement, as
        // nodes at this moment keep each instance's refCount alive.
        // But, when having only a single document loaded, _monospaceSizeScale
        // being part of the document hash, changing it will cause a rerendering,
        // which will cause all font instances to have no references and
        // being destroyed before the re-rendering, which will instantiate
        // them again with the adjusted size.
    }

    virtual void SetFallbackFontSizesAdjusted( bool adjusted )
    {
        FONT_MAN_GUARD
        _fallbackFontSizesAdjusted = adjusted;
        _cache.clearFallbackFonts();
        gc();
    }

    /// clear glyph cache
    virtual void clearGlyphCache()
    {
        FONT_MAN_GUARD
        _globalCache.clear();
        #if USE_HARFBUZZ==1
        // needs to clear each font _glyph_cache2 (for Gamma change, which
        // does not call any individual font method)
        LVPtrVector< LVFontCacheItem > * fonts = _cache.getInstances();
        for ( int i=0; i<fonts->length(); i++ ) {
            fonts->get(i)->getFont()->clearCache();
        }
        #endif
    }

    virtual int GetFontCount()
    {
        return _cache.length();
    }

    bool initSystemFonts()
    {
        #if (DEBUG_FONT_SYNTHESIS==1)
            fontMan->RegisterFont(lString8("/usr/share/fonts/liberation/LiberationSans-Regular.ttf"));
            CRLog::debug("fonts:");
            LVFontRef fnt4 = dumpFontRef( fontMan->GetFont(24, 200, true, css_ff_sans_serif, cs8("Arial, Helvetica") ) );
            LVFontRef fnt1 = dumpFontRef( fontMan->GetFont(18, 200, false, css_ff_sans_serif, cs8("Arial, Helvetica") ) );
            LVFontRef fnt2 = dumpFontRef( fontMan->GetFont(20, 400, false, css_ff_sans_serif, cs8("Arial, Helvetica") ) );
            LVFontRef fnt3 = dumpFontRef( fontMan->GetFont(22, 600, false, css_ff_sans_serif, cs8("Arial, Helvetica") ) );
            LVFontRef fnt5 = dumpFontRef( fontMan->GetFont(26, 400, true, css_ff_sans_serif, cs8("Arial, Helvetica") ) );
            LVFontRef fnt6 = dumpFontRef( fontMan->GetFont(28, 600, true, css_ff_sans_serif, cs8("Arial, Helvetica") ) );
            CRLog::debug("end of font testing");
        #elif (USE_FONTCONFIG==1)
        {
            CRLog::info("Reading list of system fonts using FONTCONFIG");
            lString32Collection fonts;

            int facesFound = 0;

            FcFontSet *fontset;

            FcObjectSet *os = FcObjectSetBuild(FC_FILE, FC_WEIGHT, FC_FAMILY,
                                               FC_SLANT, FC_SPACING, FC_INDEX,
                                               FC_STYLE, FC_SCALABLE, NULL);
            FcPattern *pat = FcPatternCreate();
            //FcBool b = 1;
            FcPatternAddBool(pat, FC_SCALABLE, 1);

            fontset = FcFontList(NULL, pat, os);

            FcPatternDestroy(pat);
            FcObjectSetDestroy(os);

            // load fonts from file
            CRLog::debug("FONTCONFIG: %d font files found", fontset->nfont);
            for(int i = 0; i < fontset->nfont; i++) {
                FcChar8 *s=(FcChar8*)"";
                FcChar8 *family=(FcChar8*)"";
                FcChar8 *style=(FcChar8*)"";
                //FcBool b;
                FcResult res;
                //FC_SCALABLE
                //res = FcPatternGetBool( fontset->fonts[i], FC_OUTLINE, 0, (FcBool*)&b);
                //if(res != FcResultMatch)
                //    continue;
                //if ( !b )
                //    continue; // skip non-scalable fonts
                res = FcPatternGetString(fontset->fonts[i], FC_FILE, 0, (FcChar8 **)&s);
                if(res != FcResultMatch) {
                    continue;
                }
                lString8 fn( (const char *)s );
                lString32 fn32( fn.c_str() );
                fn32.lowercase();
                if (!fn32.endsWith(".ttf") && !fn32.endsWith(".odf") && !fn32.endsWith(".otf") &&
                                              !fn32.endsWith(".pfb") && !fn32.endsWith(".pfa") ) {
                    continue;
                }
                int weight = FC_WEIGHT_REGULAR;
                res = FcPatternGetInteger(fontset->fonts[i], FC_WEIGHT, 0, &weight);
                if(res != FcResultMatch) {
                    CRLog::debug("no FC_WEIGHT for %s", s);
                    //continue;
                }
                switch ( weight ) {
                case FC_WEIGHT_THIN:          //    0
                    weight = 100;
                    break;
                case FC_WEIGHT_EXTRALIGHT:    //    40
                case 45:                      // TODO: find what is it...
                //case FC_WEIGHT_ULTRALIGHT        FC_WEIGHT_EXTRALIGHT
                    weight = 200;
                    break;
                case FC_WEIGHT_LIGHT:         //    50
                case FC_WEIGHT_DEMILIGHT:     //    55
                    weight = 300;
                    break;
                case FC_WEIGHT_BOOK:          //    75
                case FC_WEIGHT_REGULAR:       //    80
                //case FC_WEIGHT_NORMAL:            FC_WEIGHT_REGULAR
                    weight = 400;
                    break;
                case FC_WEIGHT_MEDIUM:        //    100
                    weight = 500;
                    break;
                case FC_WEIGHT_DEMIBOLD:      //    180
                //case FC_WEIGHT_SEMIBOLD:          FC_WEIGHT_DEMIBOLD
                    weight = 600;
                    break;
                case FC_WEIGHT_BOLD:          //    200
                    weight = 700;
                    break;
                case FC_WEIGHT_EXTRABOLD:     //    205
                //case FC_WEIGHT_ULTRABOLD:         FC_WEIGHT_EXTRABOLD
                    weight = 800;
                    break;
                case FC_WEIGHT_BLACK:         //    210
                //case FC_WEIGHT_HEAVY:             FC_WEIGHT_BLACK
                    weight = 900;
                    break;
            #ifdef FC_WEIGHT_EXTRABLACK
                case FC_WEIGHT_EXTRABLACK:    //    215
                //case FC_WEIGHT_ULTRABLACK:        FC_WEIGHT_EXTRABLACK
                    weight = 950;
                    break;
            #endif
                default:
                    weight = 400;
                    break;
                }
                FcBool scalable = FcFalse;
                res = FcPatternGetBool(fontset->fonts[i], FC_SCALABLE, 0, &scalable);
                if(res != FcResultMatch) {
                    CRLog::debug("no FC_SCALABLE for %s", s);
                }
                int index = 0;
                res = FcPatternGetInteger(fontset->fonts[i], FC_INDEX, 0, &index);
                if(res != FcResultMatch) {
                    CRLog::debug("no FC_INDEX for %s", s);
                    //continue;
                }
                res = FcPatternGetString(fontset->fonts[i], FC_FAMILY, 0, (FcChar8 **)&family);
                if(res != FcResultMatch) {
                    CRLog::debug("no FC_FAMILY for %s", s);
                    continue;
                }
                res = FcPatternGetString(fontset->fonts[i], FC_STYLE, 0, (FcChar8 **)&style);
                if(res != FcResultMatch) {
                    CRLog::debug("no FC_STYLE for %s", s);
                    style = (FcChar8*)"";
                    //continue;
                }
                int slant = FC_SLANT_ROMAN;
                res = FcPatternGetInteger(fontset->fonts[i], FC_SLANT, 0, &slant);
                if(res != FcResultMatch) {
                    CRLog::debug("no FC_SLANT for %s", s);
                    //continue;
                }
                int spacing = 0;
                res = FcPatternGetInteger(fontset->fonts[i], FC_SPACING, 0, &spacing);
                if(res != FcResultMatch) {
                    //CRLog::debug("no FC_SPACING for %s", s);
                    //continue;
                }
                // int cr_weight;
                // switch(weight) {
                //     case FC_WEIGHT_LIGHT: cr_weight = 200; break;
                //     case FC_WEIGHT_MEDIUM: cr_weight = 300; break;
                //     case FC_WEIGHT_DEMIBOLD: cr_weight = 500; break;
                //     case FC_WEIGHT_BOLD: cr_weight = 700; break;
                //     case FC_WEIGHT_BLACK: cr_weight = 800; break;
                //     default: cr_weight=300; break;
                // }
                css_font_family_t fontFamily = css_ff_sans_serif;
                lString32 face32((const char *)family);
                face32.lowercase();
                if ( spacing==FC_MONO )
                    fontFamily = css_ff_monospace;
                else if (face32.pos("sans") >= 0)
                    fontFamily = css_ff_sans_serif;
                else if (face32.pos("serif") >= 0)
                    fontFamily = css_ff_serif;

                //css_ff_inherit,
                //css_ff_serif,
                //css_ff_sans_serif,
                //css_ff_cursive,
                //css_ff_fantasy,
                //css_ff_monospace,
                bool italic = (slant!=FC_SLANT_ROMAN);

                lString8 face((const char*)family);
                lString8 style8((const char*)style);
                style8.lowercase();
                if (style8.pos("extracondensed") >= 0)
                    face << " ExtraCondensed";
                else if (style8.pos("semicondensed") >= 0)
                    face << " SemiCondensed";
                else if (style8.pos("condensed") >= 0)
                    face << " Condensed";

                LVFontDef def(
                    lString8((const char*)s),
                    -1, // height==-1 for scalable fonts
                    weight,
                    italic,
                    -1, // OpenType features = -1 for not yet instantiated fonts
                    fontFamily,
                    face,
                    index
                );
                CRLog::debug("FONTCONFIG: Font family:%s style:%s weight:%d slant:%d spacing:%d file:%s",
                                                family, style, weight, slant, spacing, s);
                if ( _cache.findDuplicate( &def ) ) {
                    CRLog::debug("is duplicate, skipping");
                    continue;
                }
                _cache.update( &def, LVFontRef(NULL) );

                if ( scalable != FcFalse && !def.getItalic() ) {
                    LVFontDef newDef( def );
                    newDef.setItalic(2); // can italicize
                    if ( !_cache.findDuplicate( &newDef ) )
                        _cache.update( &newDef, LVFontRef(NULL) );
                }

                facesFound++;
            }

            FcFontSetDestroy(fontset);
            CRLog::info("FONTCONFIG: %d fonts registered", facesFound);

            const char * fallback_faces = "Arial Unicode MS|AR PL ShanHeiSun Uni|Liberation Sans|Roboto|DejaVu Sans|Noto Sans|Droid Sans";
            SetFallbackFontFaces(lString8(fallback_faces));

            return facesFound > 0;
        }
        #else // !USE_FONTCONFIG
        return false;
        #endif
    }

    virtual ~LVFreeTypeFontManager()
    {
        FONT_MAN_GUARD
        _globalCache.clear();
        _cache.clear();
        if ( _library )
            FT_Done_FreeType( _library );
        #if USE_HARFBUZZ==1
        destroy_SVGGlyphsCollector_svg_funcs();
        #endif
        #if (DEBUG_FONT_MAN==1)
            if ( _log ) {
                fclose(_log);
            }
        #endif
    }

    LVFreeTypeFontManager()
    : _library(NULL), _globalCache(GLYPH_CACHE_SIZE)
    {
        FONT_MAN_GUARD
        int error = FT_Init_FreeType( &_library );
        if ( error ) {
            // error
            CRLog::error("Error while initializing freetype library");
        }
        #if (DEBUG_FONT_MAN==1)
            _log = fopen(DEBUG_FONT_MAN_LOG_FILE, "at" STDIO_CLOEXEC);
            if ( _log ) {
                fprintf(_log, "=========================== LOGGING STARTED ===================\n");
            }
        #endif
        // _requiredChars = U"azAZ09";//\x0410\x042F\x0430\x044F";
        // Some fonts come without any of these (ie. NotoSansMyanmar.ttf), there's
        // no reason to prevent them from being used.
        // So, check only for the presence of the space char, hoping it's there in any font.
        _requiredChars = U" ";
    }

    virtual void gc() // garbage collector
    {
        FONT_MAN_GUARD
        _cache.gc();
    }

    lString8 makeFontFileName( lString8 name )
    {
        lString8 filename = _path;
        if (!filename.empty() && _path[_path.length()-1]!=PATH_SEPARATOR_CHAR)
            filename << PATH_SEPARATOR_CHAR;
        filename << name;
        return filename;
    }

    /// returns available typefaces
    virtual void getFaceList( lString32Collection & list )
    {
        FONT_MAN_GUARD
        _cache.getFaceList( list );
    }

    /// returns registered font files
    virtual void getFontFileNameList( lString32Collection & list )
    {
        FONT_MAN_GUARD
        _cache.getFontFileNameList(list);
    }

    /// makes sure registered fonts have a proper entry at weight 400 and 700 when possible,
    /// to avoid having synthesized fonts for these normal and bold weights
    virtual void RegularizeRegisteredFontsWeights(bool print_updates)
    {
        FONT_MAN_GUARD
        _cache.regularizeRegisteredFontsWeights(print_updates);
    }

    virtual bool getFontFileNameAndFaceIndex( lString32 name, bool bold, bool italic, lString8 & filename, int & index, int & family_type, bool & has_ot_math, bool & has_emojis )
    {
        FONT_MAN_GUARD
        return _cache.getFontFileNameAndFaceIndex(name, bold, italic, filename, index, family_type, has_ot_math, has_emojis);
    }

    virtual void getRegisteredDocumentFontList( int document_id, lString32Collection & list )
    {
        FONT_MAN_GUARD
        _cache.getRegisteredDocumentFontList(document_id, list);
    }
    virtual void getInstantiatedDocumentFontList( int document_id, lString32Collection & list )
    {
        FONT_MAN_GUARD
        _cache.getInstantiatedDocumentFontList(document_id, list);
    }

    bool SetAlias(lString8 alias,lString8 facename,int id,bool bold,bool italic)
    {
        FONT_MAN_GUARD
        lString8 fontname=lString8("\0");
        LVFontDef def(
            fontname,
            -1,
            bold?700:400,
            italic,
            -1, // OpenType features = -1 for not yet instantiated fonts
            css_ff_inherit,
            facename,
            -1,
            id
        );
        LVFontCacheItem * item = _cache.find( &def);
        LVFontDef def1(
            fontname,
            -1,
            bold?700:400,
            italic,
            -1, // OpenType features = -1 for not yet instantiated fonts
            css_ff_inherit,
            alias,
            -1,
            id
        );

        int index = 0;

        FT_Face face = NULL;

        // for all faces in file
        for ( ;; index++ ) {
            FT_Error error = FT_New_Face( _library, item->getDef()->getName().c_str(), index, &face ); /* create face object */
            if (error != FT_Err_Ok) {
                ft_error_trace(__func__, "FT_New_Face", error);
                break;
            }
            int num_faces = face->num_faces;

            css_font_family_t fontFamily = css_ff_sans_serif;
            if ( face->face_flags & FT_FACE_FLAG_FIXED_WIDTH )
                fontFamily = css_ff_monospace;
            lString8 familyName(!facename.empty() ? facename : ::familyName(face));
            // We don't need this here and in other places below: all fonts (except
            // monospaces) will be marked as sans-serif, and elements with
            // style {font-family:serif;} will use the default font too.
            // (we don't ship any Times, and someone having unluckily such
            // a font among his would see it used for {font-family:serif;}
            // elements instead of his default font)
            /*
            if ( familyName=="Times" || familyName=="Times New Roman" )
                fontFamily = css_ff_serif;
            */

            int weight = !facename.empty() ? (bold ? 700 : 400) : getFontWeight(face);
            bool italicFlag = !facename.empty() ? italic : (face->style_flags & FT_STYLE_FLAG_ITALIC) != 0;

            LVFontDef def2(
                    item->getDef()->getName(),
                    -1, // height==-1 for scalable fonts
                    weight,
                    italicFlag,
                    -1, // OpenType features = -1 for not yet instantiated fonts
                    fontFamily,
                    alias,
                    index,
                    id
            );

            def2.setHasEmojis( checkForEmojis(face) );
            #if USE_HARFBUZZ==1
                hb_face_t * hb_face = hb_ft_face_create(face, NULL);
                if ( hb_ot_math_has_data(hb_face) )
                    def2.setHasOTMath(true);
                hb_face_destroy(hb_face);
            #endif

            if ( face ) {
                FT_Done_Face( face );
                face = NULL;
            }

            if ( _cache.findDuplicate( &def2 ) ) {
                CRLog::trace("font definition is duplicate");
                return false;
            }
            _cache.update( &def2, LVFontRef(NULL) );
            if (!def.getItalic()) {
                LVFontDef newDef( def2 );
                newDef.setItalic(2); // can italicize
                if ( !_cache.findDuplicate( &newDef ) )
                    _cache.update( &newDef, LVFontRef(NULL) );
            }
            if ( index>=num_faces-1 )
                break;
        }
        item = _cache.find( &def1);
        if (item->getDef()->getTypeFace()==alias ) {
            return true;
        }
        else {
            return false;
        }
    }

    virtual LVFontRef GetFont(int size, int weight, bool italic, css_font_family_t family, lString8 typeface,
                                int features, int documentId, bool useBias=false)
    {
        FONT_MAN_GUARD
        #if (DEBUG_FONT_MAN==1)
            if ( _log ) {
                 fprintf(_log, "GetFont(size=%d, weight=%d, italic=%d, family=%d, typeface='%s')\n",
                    size, weight, italic?1:0, (int)family, typeface.c_str() );
            }
        #endif
        lString8 fontname;
        LVFontDef def(
            fontname,
            size,
            weight,
            italic,
            features,
            family,
            typeface,
            -1,
            documentId
        );
        #if (DEBUG_FONT_MAN==1)
            if ( _log )
                fprintf( _log, "GetFont: %s %d %s %s\n",
                    typeface.c_str(),
                    size,
                    weight>400?"bold":"",
                    italic?"italic":"" );
        #endif

        LVFontCacheItem * item = _cache.find( &def, useBias );
        #if (DEBUG_FONT_MAN==1)
            if ( item && _log ) { //_log &&
                fprintf(_log, "   found item: (file=%s[%d], size=%d, weight=%d, italic=%d, family=%d, typeface=%s, weightDelta=%d) FontRef=%d\n",
                    item->getDef()->getName().c_str(), item->getDef()->getIndex(), item->getDef()->getSize(),
                    item->getDef()->getWeight(), item->getDef()->getItalic()?1:0,
                    (int)item->getDef()->getFamily(), item->getDef()->getTypeFace().c_str(),
                    weight - item->getDef()->getWeight(), item->getFont().isNull()?0:item->getFont()->getHeight()
                );
            }
        #endif

        if (NULL == item) {
            CRLog::error("_cache.find() return NULL: size=%d, weight=%d, italic=%d, family=%d, typeface=%s", size, weight, italic, family, typeface.c_str());
            CRLog::error("possible font cache cleared!");
            return LVFontRef(NULL);
        }

        bool italicize = false;

        LVFontDef newDef(*item->getDef());
        // printf("  got %s\n", newDef.getTypeFace().c_str());

        if (!item->getFont().isNull()) {
            if ( item->getDef()->getFeatures() != features ) {
                // Be sure we ignore any instantiated font found in cache that
                // has features different than the ones requested.
            }
            else {
            #ifdef USE_FT_EMBOLDEN
                int deltaWeight = myabs(weight - item->getDef()->getWeight());
                if (deltaWeight >= 25) {
                    // This instantiated cached font has a too different weight
                    // when USE_FT_EMBOLDEN, ignore this other-weight cached font instance
                    // and go loading from the font file again to apply embolden.
                }
            #else
                int deltaWeight = weight - item->getDef()->getWeight();
                if ( deltaWeight >= 200 ) {
                    // This instantiated cached font has a too low weight
                        // embolden using LVFontBoldTransform
                        CRLog::debug("font: apply Embolding to increase weight from %d to %d",
                                            newDef.getWeight(), newDef.getWeight() + 200 );
                        newDef.setWeight( newDef.getWeight() + 200 );
                        LVFontRef ref = LVFontRef( new LVFontBoldTransform( item->getFont(), &_globalCache ) );
                        _cache.update( &newDef, ref );
                        return ref;
                }
            #endif
                else {
                    //fprintf(_log, "    : fount existing\n");
                    return item->getFont();
                }
            }
        }

        lString8 fname = item->getDef()->getName();
        #if (DEBUG_FONT_MAN==1)
            if ( _log ) {
                int index = item->getDef()->getIndex();
                fprintf(_log, "   no instance: adding new one for filename=%s, index = %d\n", fname.c_str(), index );
            }
        #endif
        LVFreeTypeFace * font = new LVFreeTypeFace(_lock, _library, &_globalCache);
        lString8 pathname = makeFontFileName( fname );

        //def.setName( fname );
        //def.setIndex( index );
        //if ( fname.empty() || pathname.empty() ) {
        //    pathname = lString8("arial.ttf");
        //}

        if ( !item->getDef()->isRealItalic() && italic ) {
            //CRLog::debug("font: fake italic");
            newDef.setItalic(2);
            italicize = true;
        }

        // Use the family of the font we found in the cache (it may be different
        // from the requested family).
        // Assigning the requested familly to this new font could be wrong, and
        // may cause a style or font mismatch when loading from cache, forcing a
        // full re-rendering).
        family = item->getDef()->getFamily();

        //printf("going to load font file %s\n", fname.c_str());
        bool loaded = false;
        int face_size = size;
        if ( family == css_ff_monospace && GetMonospaceSizeScale() != 100 ) {
            face_size = size * GetMonospaceSizeScale() / 100;
        }
        if (item->getDef()->getBuf().isNull())
            loaded = font->loadFromFile( pathname.c_str(), item->getDef()->getIndex(), size, family, isBitmapModeForSize(size), italicize, item->getDef()->getWeight(), face_size );
        else
            loaded = font->loadFromBuffer(item->getDef()->getBuf(), item->getDef()->getIndex(), size, family, isBitmapModeForSize(size), italicize, item->getDef()->getWeight(), face_size );
        if (loaded) {
            //fprintf(_log, "    : loading from file %s : %s %d\n", item->getDef()->getName().c_str(),
            //    item->getDef()->getTypeFace().c_str(), item->getDef()->getSize() );
            LVFontRef ref(font);
            // Instantiate this font with the requested OpenType features
            newDef.setFeatures( features );
            font->setFeatures( features ); // followup setKerningMode() will create/update hb_features if needed
            font->setKerningMode( GetKerningMode() );
            font->setFaceName( item->getDef()->getTypeFace() );
            newDef.setSize( size );
            //item->setFont( ref );
            //_cache.update( def, ref );
        #ifdef USE_FT_EMBOLDEN
            int deltaWeight = myabs(weight - newDef.getWeight());
            if (deltaWeight >= 25) {
                // embolden
                // Will make some of this font's methods do embolden the glyphs and widths
                font->setSynthWeight(weight);
                newDef.setWeight( weight, false );
                // Now newDef contains fake/synthetic weight
            }
        #else
            int deltaWeight = weight - newDef.getWeight();
            if ( deltaWeight >= 200 ) {
                CRLog::debug("font: apply Embolding to increase weight from %d to %d",
                                    newDef.getWeight(), newDef.getWeight() + 200 );
                // Create a wrapper with LVFontBoldTransform which will bolden the glyphs
                newDef.setWeight( newDef.getWeight() + 200 );
                ref = LVFontRef( new LVFontBoldTransform( ref, &_globalCache ) );
            }
        #endif
                /*
                printf("CRE: %s:%d [%s %d%s]: fake bold%s\n", fname.c_str(), item->getDef()->getIndex(),
                        font->getFaceName().c_str(), font->getSize(),
                        italic?" i":"", italicize?", fake italic":""); // font->getWeight());
                */
            _cache.update( &newDef, ref );
            // int rsz = ref->getSize();
            // if ( rsz!=size ) {
            //     size++;
            // }
            //delete def;
            return ref;
        }
        else {
            //printf("    not found!\n");
        }
        //delete def;
        delete font;
        return LVFontRef(NULL);
    }

    void GetAvailableFontWeights(LVArray<int>& weights, lString8 typeface) {
        _cache.getAvailableFontWeights(weights, typeface);
    }

    bool checkCharSet( FT_Face face )
    {
        // TODO: check existance of required characters (e.g. cyrillic)
        if (face==NULL)
            return false; // invalid face
        for ( int i=0; i<_requiredChars.length(); i++ ) {
            lChar32 ch = _requiredChars[i];
            FT_UInt ch_glyph_index = FT_Get_Char_Index( face, ch );
            if ( ch_glyph_index==0 ) {
                CRLog::debug("Required char not found in font: %04x", ch);
                return false; // no required char!!!
            }
        }
        return true;
    }

    bool checkForEmojis( FT_Face face )
    {
        if (face==NULL)
            return false; // invalid face
        // We check for "early" emojis (some fonts may not have all emojis added later)
        lString32 sample_emojis = U"\x1F600\x1F60A"; // Smiling faces
        for ( int i=0; i<sample_emojis.length(); i++ ) {
            lChar32 ch = sample_emojis[i];
            FT_UInt ch_glyph_index = FT_Get_Char_Index( face, ch );
            if ( ch_glyph_index==0 ) {
                return false;
            }
        }
        return true;
    }

    /*
    bool isMonoSpaced( FT_Face face )
    {
        // TODO: check existance of required characters (e.g. cyrillic)
        if (face==NULL)
            return false; // invalid face
        lChar32 ch1 = 'i';
        FT_UInt ch_glyph_index1 = FT_Get_Char_Index( face, ch1 );
        if ( ch_glyph_index1==0 )
            return false; // no required char!!!
        int w1, w2;
        int error1 = FT_Load_Glyph( face,  //    handle to face object
                ch_glyph_index1,           //    glyph index
                FT_LOAD_DEFAULT );         //   load flags, see below
        if ( error1 )
            w1 = 0;
        else
            w1 = (face->glyph->metrics.horiAdvance >> 6);
        int error2 = FT_Load_Glyph( face,  //     handle to face object
                ch_glyph_index2,           //     glyph index
                FT_LOAD_DEFAULT );         //     load flags, see below
        if ( error2 )
            w2 = 0;
        else
            w2 = (face->glyph->metrics.horiAdvance >> 6);

        lChar32 ch2 = 'W';
        FT_UInt ch_glyph_index2 = FT_Get_Char_Index( face, ch2 );
        if ( ch_glyph_index2==0 )
            return false; // no required char!!!
        return w1==w2;
    }
    */

    /// registers document font
    // Note: publishers can specify font-variant/font-feature-settings/font-variation-settings
    // in the @font-face declaration.
    // todo: parse it and pass it here, and set it on the non-instantiated font (instead of -1)
    virtual bool RegisterDocumentFont(int documentId, LVContainerRef container, lString32 name, lString8 faceName, bool bold, bool italic) {
        FONT_MAN_GUARD
        lString8 name8 = UnicodeToUtf8(name);
        CRLog::debug("RegisterDocumentFont(documentId=%d, path=%s)", documentId, name8.c_str());
        if (_cache.findDocumentFontDuplicate(documentId, name8)) {
            return false;
        }
        name.trim(); // Remove any " " appended to avoid url override with duplicates
        LVStreamRef stream = container->OpenStream(name.c_str(), LVOM_READ);
        if (stream.isNull())
            return false;
        lUInt32 size = (lUInt32)stream->GetSize();
        if (size < 100 || size > 5000000)
            return false;
        LVByteArrayRef buf = stream->GetData();
        if (!buf)
            return false;

        bool res = false;
        int index = 0;
        FT_Face face = NULL;

        // for all faces in file
        for ( ;; index++ ) {
            FT_Error error = FT_New_Memory_Face( _library, buf->get(), buf->length(), index, &face ); /* create face object */
            if (error != FT_Err_Ok) {
                ft_error_trace(__func__, "FT_New_Memory_Face", error);
                break;
            }
            // bool scal = FT_IS_SCALABLE( face );
            // bool charset = checkCharSet( face );
            // //bool monospaced = isMonoSpaced( face );
            // if ( !scal || !charset ) {
            //     //#if (DEBUG_FONT_MAN==1)
            //     //    if ( _log ) {
            //               CRLog::debug("    won't register font %s: %s",
            //               name.c_str(), !charset?"no mandatory characters in charset" : "font is not scalable");
            //     //    }
            //     //#endif
            //     if ( face ) {
            //         FT_Done_Face( face );
            //         face = NULL;
            //     }
            //     break;
            // }
            int num_faces = face->num_faces;

            css_font_family_t fontFamily = css_ff_sans_serif;
            if ( face->face_flags & FT_FACE_FLAG_FIXED_WIDTH )
                fontFamily = css_ff_monospace;
            lString8 familyName(!faceName.empty() ? faceName : ::familyName(face));
            /*
            if ( familyName=="Times" || familyName=="Times New Roman" )
                fontFamily = css_ff_serif;
            */

            int weight = !faceName.empty() ? (bold ? 700 : 400) : getFontWeight(face);
            bool italicFlag = !faceName.empty() ? italic : (face->style_flags & FT_STYLE_FLAG_ITALIC) != 0;

            LVFontDef def(
                name8,
                -1, // height==-1 for scalable fonts
                weight,
                italicFlag,
                -1, // OpenType features = -1 for not yet instantiated fonts
                fontFamily,
                familyName,
                index,
                documentId,
                buf
            );
            def.setHasEmojis( checkForEmojis(face) );
            #if USE_HARFBUZZ==1
                hb_face_t * hb_face = hb_ft_face_create(face, NULL);
                if ( hb_ot_math_has_data(hb_face) )
                    def.setHasOTMath(true);
                hb_face_destroy(hb_face);
            #endif
            #if (DEBUG_FONT_MAN==1)
                if ( _log ) {
                    fprintf(_log, "registering font: (file=%s[%d], size=%d, weight=%d, italic=%d, family=%d, typeface=%s)\n",
                        def.getName().c_str(), def.getIndex(), def.getSize(), def.getWeight(),
                        def.getItalic()?1:0, (int)def.getFamily(), def.getTypeFace().c_str() );
                }
            #endif
            if ( face ) {
                FT_Done_Face( face );
                face = NULL;
            }

            if ( _cache.findDuplicate( &def ) ) {
                CRLog::trace("font definition is duplicate");
                return false;
            }
            _cache.update( &def, LVFontRef(NULL) );
            if (!def.getItalic()) {
                LVFontDef newDef( def );
                newDef.setItalic(2); // can italicize
                if ( !_cache.findDuplicate( &newDef ) )
                    _cache.update( &newDef, LVFontRef(NULL) );
            }
            res = true;

            if ( index>=num_faces-1 )
                break;
        }
        return res;
    }

    /// unregisters all document fonts
    virtual void UnregisterDocumentFonts(int documentId) {
        _cache.removeDocumentFonts(documentId);
    }

    virtual bool RegisterExternalFont(int documentId, lString32 name, lString8 family_name, bool bold, bool italic) {
        if (name.startsWithNoCase(lString32("res://")))
            name = name.substr(6);
        else if (name.startsWithNoCase(lString32("file://")))
            name = name.substr(7);
        lString8 fname = UnicodeToUtf8(name);
        CRLog::debug("RegisterExternalFont(documentId=%d, path=%s)", documentId, fname.c_str());
        if (_cache.findDocumentFontDuplicate(documentId, fname)) {
            return false;
        }

        bool res = false;
        int index = 0;
        FT_Face face = NULL;

        // for all faces in file
        for ( ;; index++ ) {
            FT_Error error = FT_New_Face( _library, fname.c_str(), index, &face ); /* create face object */
            if (error != FT_Err_Ok) {
                ft_error_trace(__func__, "FT_New_Face", error);
                break;
            }
            bool scal = FT_IS_SCALABLE( face ) != 0;
            bool charset = checkCharSet( face );
            if (!charset) {
                if (FT_Select_Charmap(face, FT_ENCODING_UNICODE)) // returns 0 on success
                    // If no unicode charmap found, try symbol charmap
                    if (!FT_Select_Charmap(face, FT_ENCODING_MS_SYMBOL))
                        // It has a symbol charmap: consider it valid
                        charset = true;
            }
            //bool monospaced = isMonoSpaced( face );
            if ( !scal || !charset ) {
                CRLog::debug("    won't register font %s: %s",
                    name.c_str(), !charset?"no mandatory characters in charset" : "font is not scalable"
                    );
                if ( face ) {
                    FT_Done_Face( face );
                    face = NULL;
                }
                break;
            }
            int num_faces = face->num_faces;

            css_font_family_t fontFamily = css_ff_sans_serif;
            if ( face->face_flags & FT_FACE_FLAG_FIXED_WIDTH )
                fontFamily = css_ff_monospace;
            lString8 familyName( ::familyName(face) );
            /*
            if ( familyName=="Times" || familyName=="Times New Roman" )
                fontFamily = css_ff_serif;
            */

            LVFontDef def(
                fname,
                -1, // height==-1 for scalable fonts
                bold ? 700 : 400,
                italic,
                -1, // OpenType features = -1 for not yet instantiated fonts
                fontFamily,
                family_name,
                index,
                documentId
            );
            def.setHasEmojis( checkForEmojis(face) );
            #if USE_HARFBUZZ==1
                hb_face_t * hb_face = hb_ft_face_create(face, NULL);
                if ( hb_ot_math_has_data(hb_face) )
                    def.setHasOTMath(true);
                hb_face_destroy(hb_face);
            #endif
            #if (DEBUG_FONT_MAN==1)
                if ( _log ) {
                    fprintf(_log, "registering font: (file=%s[%d], size=%d, weight=%d, italic=%d, family=%d, typeface=%s)\n",
                        def.getName().c_str(), def.getIndex(), def.getSize(), def.getWeight(),
                        def.getItalic()?1:0, (int)def.getFamily(), def.getTypeFace().c_str()
                    );
                }
            #endif

            FT_Done_Face( face );
            face = NULL;

            if ( _cache.findDuplicate( &def ) ) {
                CRLog::trace("font definition is duplicate");
                return false;
            }
            _cache.update( &def, LVFontRef(NULL) );
            if ( scal && !def.getItalic() ) {
                LVFontDef newDef( def );
                newDef.setItalic(2); // can italicize
                if ( !_cache.findDuplicate( &newDef ) )
                    _cache.update( &newDef, LVFontRef(NULL) );
            }
            res = true;

            if ( index>=num_faces-1 )
                break;
        }
        return res;
    }

    // RegisterFont (and the 2 similar functions above) adds to the cache
    // definitions for all the fonts in their font file.
    // Font instances will be created as need from the LVFontDef name or buf.
    // (The similar functions do most of the same work, and some code
    // could be factorized between them.)
    virtual bool RegisterFont( lString8 name )
    {
        FONT_MAN_GUARD
        #ifdef LOAD_TTF_FONTS_ONLY
        if ( name.pos( cs8(".ttf") ) < 0 && name.pos( cs8(".TTF") ) < 0 )
            return false; // load ttf fonts only
        #endif
        //CRLog::trace("RegisterFont(%s)", name.c_str());
        lString8 fname = makeFontFileName( name );
        //CRLog::trace("font file name : %s", fname.c_str());
        #if (DEBUG_FONT_MAN==1)
            if ( _log ) {
                fprintf(_log, "RegisterFont( %s ) path=%s\n", name.c_str(), fname.c_str());
            }
        #endif

        bool res = false;
        int index = 0;
        FT_Face face = NULL;

        // for all faces in file
        for ( ;; index++ ) {
            FT_Error error = FT_New_Face( _library, fname.c_str(), index, &face ); /* create face object */
            if (error != FT_Err_Ok) {
                ft_error_trace(__func__, "FT_New_Face", error);
                break;
            }
            bool scal = FT_IS_SCALABLE( face );
            bool charset = checkCharSet( face );
            if (!charset) {
                if (FT_Select_Charmap(face, FT_ENCODING_UNICODE)) // returns 0 on success
                    // If no unicode charmap found, try symbol charmap
                    if (!FT_Select_Charmap(face, FT_ENCODING_MS_SYMBOL))
                        // It has a symbol charmap: consider it valid
                        charset = true;
            }

            //bool monospaced = isMonoSpaced( face );
            if ( !scal || !charset ) {
                CRLog::debug("    won't register font %s: %s",
                    name.c_str(), !charset?"no mandatory characters in charset" : "font is not scalable");
                if ( face ) {
                    FT_Done_Face( face );
                    face = NULL;
                }
                break;
            }
            int num_faces = face->num_faces;

            css_font_family_t fontFamily = css_ff_sans_serif;
            if ( face->face_flags & FT_FACE_FLAG_FIXED_WIDTH )
                fontFamily = css_ff_monospace;
            lString8 familyName( ::familyName(face) );

            /*
            if ( familyName=="Times" || familyName=="Times New Roman" )
                fontFamily = css_ff_serif;
            */

            LVFontDef def(
                name,
                -1, // height==-1 for scalable fonts
                getFontWeight(face),
                ( face->style_flags & FT_STYLE_FLAG_ITALIC ) ? true : false,
                -1, // OpenType features = -1 for not yet instantiated fonts
                fontFamily,
                familyName,
                index
            );
            def.setHasEmojis( checkForEmojis(face) );
            #if USE_HARFBUZZ==1
                hb_face_t * hb_face = hb_ft_face_create(face, NULL);
                if ( hb_ot_math_has_data(hb_face) )
                    def.setHasOTMath(true);
                hb_face_destroy(hb_face);
            #endif
            #if (DEBUG_FONT_MAN==1)
                if ( _log ) {
                    fprintf(_log, "registering font: (file=%s[%d], size=%d, weight=%d, italic=%d, family=%d, typeface=%s)\n",
                        def.getName().c_str(), def.getIndex(), def.getSize(), def.getWeight(),
                        def.getItalic()?1:0, (int)def.getFamily(), def.getTypeFace().c_str()
                    );
                }
            #endif

            if ( face ) {
                FT_Done_Face( face );
                face = NULL;
            }

            if ( _cache.findDuplicate( &def ) ) {
                CRLog::trace("font definition is duplicate");
                return false;
            }
            _cache.update( &def, LVFontRef(NULL) );
            if ( scal && !def.getItalic() ) {
                // If this font is not italic, create another definition
                // with italic=2 (=fake italic) as we can italicize it.
                // A real italic font (italic=1) will be found first
                // when italic is requested.
                // (Strange that italic and embolden are managed differently...
                // maybe it makes the 2x2 combinations easier to manage?)
                LVFontDef newDef( def );
                newDef.setItalic(2); // can italicize
                if ( !_cache.findDuplicate( &newDef ) )
                    _cache.update( &newDef, LVFontRef(NULL) );
            }
            res = true;

            if ( index>=num_faces-1 )
                break;
        }
        return res;
    }

    virtual bool Init( lString8 path )
    {
        _path = path;
        initSystemFonts();
        return (_library != NULL);
    }
};
#endif // (USE_FREETYPE==1)


#if (USE_BITMAP_FONTS==1)
class LVBitmapFontManager : public LVFontManager
{
private:
    lString8    _path;
    LVFontCache _cache;
    //FILE * _log;
public:
    virtual int GetFontCount()
    {
        return _cache.length();
    }
    virtual ~LVBitmapFontManager()
    {
        //if (_log)
        //    fclose(_log);
    }
    LVBitmapFontManager()
    {
        //_log = fopen( "fonts.log", "wt" STDIO_CLOEXEC );
    }
    virtual void gc() // garbage collector
    {
        _cache.gc();
    }
    lString8 makeFontFileName( lString8 name )
    {
        lString8 filename = _path;
        if (!filename.empty() && _path[filename.length()-1]!=PATH_SEPARATOR_CHAR)
            filename << PATH_SEPARATOR_CHAR;
        filename << name;
        return filename;
    }
    virtual LVFontRef GetFont(int size, int weight, bool italic, css_font_family_t family, lString8 typeface,
                                int features, int documentId, bool useBias=false)
    {
        LVFontDef * def = new LVFontDef(
            lString8::empty_str,
            size,
            weight,
            italic,
            0,
            family,
            typeface,
            documentId,
            useBias
        );
        //fprintf( _log, "GetFont: %s %d %s %s\n",
        //    typeface.c_str(),
        //    size,
        //    weight>400?"bold":"",
        //    italic?"italic":"" );
        LVFontCacheItem * item = _cache.find( def );
        delete def;
        if (!item->getFont().isNull())
        {
            //fprintf(_log, "    : fount existing\n");
            return item->getFont();
        }
        LBitmapFont * font = new LBitmapFont;
        lString8 fname = makeFontFileName( item->getDef()->getName() );
        //printf("going to load font file %s\n", fname.c_str());
        if (font->LoadFromFile( fname.c_str() ) )
        {
            //fprintf(_log, "    : loading from file %s : %s %d\n", item->getDef()->getName().c_str(),
            //    item->getDef()->getTypeFace().c_str(), item->getDef()->getSize() );
            LVFontRef ref(font);
            item->setFont( ref );
            return ref;
        }
        else
        {
            //printf("    not found!\n");
        }
        delete font;
        return LVFontRef(NULL);
    }

    virtual void GetAvailableFontWeights(LVArray<int>& weights, lString8 typeface) {}

    virtual bool RegisterFont( lString8 name )
    {
        lString8 fname = makeFontFileName( name );
        //printf("going to load font file %s\n", fname.c_str());
        LVStreamRef stream = LVOpenFileStream( fname.c_str(), LVOM_READ );
        if (!stream)
        {
            //printf("    not found!\n");
            return false;
        }
        tag_lvfont_header hdr;
        bool res = false;
        lvsize_t bytes_read = 0;
        if ( stream->Read( &hdr, sizeof(hdr), &bytes_read ) == LVERR_OK && bytes_read == sizeof(hdr) )
        {
            LVFontDef def(
                name,
                hdr.fontHeight,
                hdr.flgBold?700:400,
                hdr.flgItalic?true:false,
                -1,
                (css_font_family_t)hdr.fontFamily,
                lString8(hdr.fontName)
            );
            //fprintf( _log, "Register: %s %s %d %s %s\n",
            //    name.c_str(), hdr.fontName,
            //    hdr.fontHeight,
            //    hdr.flgBold?"bold":"",
            //    hdr.flgItalic?"italic":"" );
            _cache.update( &def, LVFontRef(NULL) );
            res = true;
        }
        return res;
    }
    /// returns registered font files
    virtual void getFontFileNameList( lString32Collection & list )
    {
        FONT_MAN_GUARD
        _cache.getFontFileNameList(list);
    }
    virtual bool Init( lString8 path )
    {
        _path = path;
        return true;
    }
};
#endif // (USE_BITMAP_FONTS==1)


#if !defined(__SYMBIAN32__) && defined(_WIN32) && USE_FREETYPE!=1

// prototype
int CALLBACK LVWin32FontEnumFontFamExProc(
  const LOGFONTA *lpelfe,    // logical-font data
  const TEXTMETRICA *lpntme,  // physical-font data
  //ENUMLOGFONTEX *lpelfe,    // logical-font data
  //NEWTEXTMETRICEX *lpntme,  // physical-font data
  DWORD FontType,           // type of font
  LPARAM lParam             // application-defined data
);

class LVWin32FontManager : public LVFontManager
{
private:
    lString8    _path;
    LVFontCache _cache;
    //FILE * _log;
public:
    virtual int GetFontCount()
    {
        return _cache.length();
    }
    virtual ~LVWin32FontManager()
    {
        //if (_log)
        //    fclose(_log);
    }
    LVWin32FontManager()
    {
        //_log = fopen( "fonts.log", "wt" STDIO_CLOEXEC );
    }
    virtual void gc() // garbage collector
    {
        _cache.gc();
    }
    virtual LVFontRef GetFont(int size, int weight, bool bitalic, css_font_family_t family, lString8 typeface )
    {
        int italic = bitalic?1:0;
        if (size<8)
            size = 8;
        if (size>255)
            size = 255;

        LVFontDef def(
            lString8::empty_str,
            size,
            weight,
            italic,
            0,
            family,
            typeface
        );

        //fprintf( _log, "GetFont: %s %d %s %s\n",
        //    typeface.c_str(),
        //    size,
        //    weight>400?"bold":"",
        //    italic?"italic":"" );
        LVFontCacheItem * item = _cache.find( &def );
        if (!item->getFont().isNull())
        {
            //fprintf(_log, "    : fount existing\n");
            return item->getFont();
        }

        #if COLOR_BACKBUFFER==0
        LVWin32Font * font = new LVWin32Font;
        #else
        LVWin32DrawFont * font = new LVWin32DrawFont;
        #endif

        LVFontDef * fdef = item->getDef();
        LVFontDef def2( fdef->getName(), size, weight, italic,
            fdef->getFamily(), fdef->getTypeFace() );

        if ( font->Create(size, weight, italic?true:false, fdef->getFamily(), fdef->getTypeFace()) )
        {
            //fprintf(_log, "    : loading from file %s : %s %d\n", item->getDef()->getName().c_str(),
            //    item->getDef()->getTypeFace().c_str(), item->getDef()->getSize() );
            LVFontRef ref(font);
            _cache.addInstance( &def2, ref );
            return ref;
        }
        delete font;
        return LVFontRef(NULL);
    }

    virtual void GetAvailableFontWeights(LVArray<int>& weights, lString8 typeface) {}

    virtual bool RegisterFont( const LOGFONTA * lf )
    {
        lString8 face(lf->lfFaceName);
        css_font_family_t ff;
        switch (lf->lfPitchAndFamily & 0x70)
        {
        case FF_ROMAN:
            ff = css_ff_serif;
            break;
        case FF_SWISS:
            ff = css_ff_sans_serif;
            break;
        case FF_SCRIPT:
            ff = css_ff_cursive;
            break;
        case FF_DECORATIVE:
            ff = css_ff_fantasy;
            break;
        case FF_MODERN:
            ff = css_ff_monospace;
            break;
        default:
            ff = css_ff_sans_serif;
            break;
        }
        LVFontDef def(
            face,
            -1, //lf->lfHeight>0 ? lf->lfHeight : -lf->lfHeight,
            -1, //lf->lfWeight,
            -1, //lf->lfItalic!=0,
            -1,
            ff,
            face
        );
        _cache.update( &def, LVFontRef(NULL) );
        return true;
    }
    virtual bool RegisterFont( lString8 name )
    {
        return false;
    }
    virtual bool Init( lString8 path )
    {
        LVColorDrawBuf drawbuf(1,1);
        LOGFONTA lf = { 0 };
        lf.lfCharSet = ANSI_CHARSET;
        int res =
        EnumFontFamiliesExA(
          drawbuf.GetDC(),                  // handle to DC
          &lf,                              // font information
          LVWin32FontEnumFontFamExProc, // callback function (FONTENUMPROC)
          (LPARAM)this,                    // additional data
          0                     // not used; must be 0
        );

        return res!=0;
    }

    virtual void getFaceList( lString32Collection & list )
    {
        _cache.getFaceList(list);
    }
    /// returns registered font files
    virtual void getFontFileNameList( lString32Collection & list )
    {
        FONT_MAN_GUARD
        _cache.getFontFileNameList(list);
    }
};

// definition
int CALLBACK LVWin32FontEnumFontFamExProc(
  const LOGFONTA *lf,    // logical-font data
  const TEXTMETRICA *lpntme,  // physical-font data
  //ENUMLOGFONTEX *lpelfe,    // logical-font data
  //NEWTEXTMETRICEX *lpntme,  // physical-font data
  DWORD FontType,           // type of font
  LPARAM lParam             // application-defined data
)
{
    //
    if (FontType == TRUETYPE_FONTTYPE)
    {
        LVWin32FontManager * fontman = (LVWin32FontManager *)lParam;
        LVWin32Font fnt;
        //if (strcmp(lf->lfFaceName, "Courier New"))
        //    return 1;
        if ( fnt.Create( *lf ) )
        {
            //
            static lChar32 chars[] = {0, 0xBF, 0xE9, 0x106, 0x410, 0x44F, 0 };
            for (int i=0; chars[i]; i++)
            {
                LVFont::glyph_info_t glyph;
                if (!fnt.getGlyphInfo( chars[i], &glyph, U' ' )) //def_char
                    return 1;
            }
            fontman->RegisterFont( lf ); //&lpelfe->elfLogFont
        }
    }
    return 1;
}
#endif // !defined(__SYMBIAN32__) && defined(_WIN32) && USE_FREETYPE!=1

#if (USE_BITMAP_FONTS==1)

LVFontRef LoadFontFromFile( const char * fname )
{
    LVFontRef ref;
    LBitmapFont * font = new LBitmapFont;
    if (font->LoadFromFile( fname ) )
    {
        ref = font;
    }
    else
    {
        delete font;
    }
    return ref;
}

#endif // (USE_BITMAP_FONTS==1)

// Init unique font manager: either win32, freetype, or bitmap
bool InitFontManager( lString8 path )
{
    if ( fontMan ) {
        return true;
        //delete fontMan;
    }
#if (USE_WIN32_FONTS==1)
    fontMan = new LVWin32FontManager;
#elif (USE_FREETYPE==1)
    fontMan = new LVFreeTypeFontManager;
#else
    fontMan = new LVBitmapFontManager;
#endif
    return fontMan->Init( path );
}

bool ShutdownFontManager()
{
    if ( fontMan )
    {
        delete fontMan;
        fontMan = NULL;
        return true;
    }
    return false;
}

// Font definitions matching/scoring
int LVFontDef::CalcDuplicateMatch( const LVFontDef & def ) const
{
    if (def._documentId != -1 && _documentId != def._documentId)
        return false;

    bool size_match = (_size==-1 || def._size==-1) ?
              true
            : (def._size == _size);

    bool weight_match = (_weight==-1 || def._weight==-1) ?
              true
            : (def._weight == _weight);

    bool italic_match = (_italic == def._italic || _italic==-1 || def._italic==-1);

    bool features_match = (_features == def._features || _features==-1 || def._features==-1);

    bool family_match = (_family==css_ff_inherit || def._family==css_ff_inherit || def._family == _family);

    bool typeface_match = (_typeface == def._typeface);

    return size_match && weight_match && italic_match && features_match && family_match && typeface_match;
}

int LVFontDef::CalcMatch( const LVFontDef & def, bool useBias ) const
{
    if (_documentId != -1 && _documentId != def._documentId)
        return 0;

    // size
    int size_match = (_size==-1 || def._size==-1) ?
              256
            : (def._size>_size ?
                      _size*256/def._size
                    : def._size*256/_size );

    // weight
    int weight_diff = def._weight - _weight;
    if ( weight_diff < 0 )
        weight_diff = -weight_diff;
    if ( weight_diff > 800 )
        weight_diff = 800;
    int weight_match = (_weight==-1 || def._weight==-1) ?
                256
            : ( 256 - weight_diff * 256 / 800 );
    // It might happen that 2 fonts with different weights can get the same
    // score, e.g. with def._weight=550, a font with _weight=400 and an other
    // with _weight=700. Any could then be picked depending on their random
    // ordering in the cache, which may mess a book on re-openings.
    // To avoid this inconsistency, we give arbitrarily a small increase to
    // the score of the smaller weight font (mostly so that with the above
    // case, we keep synthesizing the 550 from the 400)
    if ( _weight < def._weight )
        weight_match += 1;

    // italic
    int italic_match = (_italic == def._italic || _italic==-1 || def._italic==-1) ?
              256
            :   0;
    // lower the score if any is fake italic
    if ( (_italic==2 || def._italic==2) && _italic>0 && def._italic>0 )
        italic_match = 128;

    // OpenType features
    int features_match = (_features == def._features || _features==-1 || def._features==-1) ?
              256
            :   0;

    // family
    int family_match = (_family==css_ff_inherit || def._family==css_ff_inherit || def._family == _family) ?
              256
            : ( (_family==css_ff_monospace)==(def._family==css_ff_monospace) ? 64 : 0 );
              // lower score if one is monospace and the other is not

    // typeface
    int typeface_match = (_typeface == def._typeface) ? 256 : 0;

    // bias
    // If the typeface matches the requested one, we don't add the bias:
    // - not using the bias will avoid the bias'ed font to be wrongly chosen
    //   when handling 'font-family: ExistingFont1, BiasedFont2'
    // - the typeface_match score is so much larger than our usual bias,
    //   that a biased font will win anyway if requested by name
    // - but if no font matches the typeface, the bias will be used and
    //   the biased font will win (which is the main purpose of this bias)
    // - the bias is the same for all instances of this typeface, so
    //   not using it won't affect scoring among these instances
    int bias = (useBias && !typeface_match) ? _bias : 0;

    // Special handling for synthesized fonts:
    // The way this function is called:
    // 'this' (or '', properties not prefixed) is either an instance of a
    //     registered font, or a registered font definition,
    // 'def' is the requested definition.
    // 'def' can never be italic=2 (fake italic) or !_real_weight (fake weight), but
    //    either 0 or 1, or a 400, 500, 600, 700, ... for standard weight
    //    or any multiple of 25 for synthetic weights
    // 'this' registered can be only 400 when the font has no bold sibling,
    //           or any other real weight: 500, 600, 700 ...
    // 'this' instantiated can be 400 (for the regular original)
    //           or 700 when 'this' is the bold sibling instantiated
    //           or any other value with disabled flags _real_weight
    //           when it has been synthesized from the other font with real weight.
    // We want to avoid an instantiated fake bold (resp. fake bold italic) to
    // have a higher score than the registered original when a fake bold italic
    // (resp. fake bold) is requested, so the italic/non italic requested can
    // be re-synthesized. Otherwise, we'll get some italic when not wanting
    // italic (or vice versa), depending on which has been instantiated first...
    //
    if ( !_real_weight ) {        // 'this' is an instantiated fake weight font
        if ( def._italic > 0 ) {  // italic requested
            if ( _italic == 0 ) { // 'this' is fake bold but non-italic
                weight_match = 0; // => drop score
                italic_match = 0;
                // The regular (italic or fake italic) is available
                // and will get a better score than 'this'
            }
            // otherwise, 'this' is a fake bold italic, and it can match
        }
        else {                    // non-italic requested
            if ( _italic > 0 ) {  // 'this' is fake bold of (real or fake) italic
                weight_match = 0; // => drop score
                italic_match = 0;
                // The regular is available and will get a better score
                // than 'this'
            }
        }
        // Also, never use a synthetic weight font to synthesize another one
        if ( weight_diff >= 25 ) {
            weight_match = 0;
            italic_match = 0;
        }
    }

    // final score
    int score = bias
        + (size_match     * 100)
        + (weight_match   * 5)
        + (italic_match   * 5)
        + (features_match * 1000)
        + (family_match   * 100)
        + (typeface_match * 10000);

//    printf("### %s (%d) vs %s (%d): size=%d weight=%d italic=%d family=%d typeface=%d bias=%d => %d\n",
//        _typeface.c_str(), _family, def._typeface.c_str(), def._family,
//        size_match, weight_match, italic_match, family_match, typeface_match, _bias, score);
    return score;
}

int LVFontDef::CalcFallbackMatch( lString8 face, int size ) const
{
    if (_typeface != face) {
        //CRLog::trace("'%s'' != '%s'", face.c_str(), _typeface.c_str());
        return 0;
    }

    int size_match = (_size==-1 || size==-1 || _size==size) ?
              256
            :   0;

    int weight_match = (_weight==-1) ?
              256
            : ( 256 - _weight * 256 / 800 );

    int italic_match = _italic == 0 ?
              256
            :   0;

    // Don't let instantiated font with non-zero features be usable as a fallback font
    int features_match = (_features == -1 || _features == 0 ) ?
              256
            :   0;

    return
        + (size_match     * 100)
        + (weight_match   * 5)
        + (italic_match   * 5)
        + (features_match * 1000);
}


/// draws text string (returns x advance)
int LVBaseFont::DrawTextString( LVDrawBuf * buf, int x, int y,
                   const lChar32 * text, int len,
                   lChar32 def_char, lUInt32 * palette, bool addHyphen, TextLangCfg * lang_cfg, lUInt32 , int , int, int, int, int,
                   SVGGlyphsCollector *)
{
    //static lUInt8 glyph_buf[16384];
    //LVFont::glyph_info_t info;
    int baseline = getBaseline();
    int x0 = x;
    while (len>=(addHyphen?0:1)) {
        if (len<=1 || *text != UNICODE_SOFT_HYPHEN_CODE) {
            lChar32 ch = ((len==0)?UNICODE_SOFT_HYPHEN_CODE:*text);

            LVFontGlyphCacheItem * item = getGlyph(ch, def_char);
            int w  = 0;
            if ( item ) {
                // avoid soft hyphens inside text string
                w = item->advance;
                if ( item->bmp_width && item->bmp_height ) {
                    buf->Draw( x + item->origin_x,
                        y + baseline - item->origin_y,
                        item->bmp,
                        item->bmp_width,
                        item->bmp_height,
                        palette);
                }
            }
            x  += w; // + letter_spacing;

            // if ( !getGlyphInfo( ch, &info, def_char ) ) {
            //     ch = def_char;
            //     if ( !getGlyphInfo( ch, &info, def_char ) )
            //         ch = 0;
            // }
            // if (ch && getGlyphImage( ch, glyph_buf, def_char )) {
            //     if (info.blackBoxX && info.blackBoxY)
            //     {
            //         buf->Draw( x + info.originX,
            //             y + baseline - info.originY,
            //             glyph_buf,
            //             info.blackBoxX,
            //             info.blackBoxY,
            //             palette);
            //     }
            //     x += info.width;
            // }
        }
        else if (*text != UNICODE_SOFT_HYPHEN_CODE) {
            //len = len;
        }
        len--;
        text++;
    }
    return x - x0;
}

#if (USE_BITMAP_FONTS==1)
bool LBitmapFont::getGlyphInfo( lUInt32 code, LVFont::glyph_info_t * glyph, lChar32 def_char, bool code_is_glyph_index, bool is_fallback )
{
    const lvfont_glyph_t * ptr = lvfontGetGlyph( m_font, code );
    if (!ptr)
        return false;
    glyph->blackBoxX = ptr->blackBoxX;
    glyph->blackBoxY = ptr->blackBoxY;
    glyph->originX = ptr->originX;
    glyph->originY = ptr->originY;
    glyph->width = ptr->width;
    return true;
}

lUInt16 LBitmapFont::measureText(
                    const lChar32 * text, int len,
                    lUInt16 * widths,
                    lUInt8 * flags,
                    int max_width,
                    lChar32 def_char,
                    TextLangCfg * lang_cfg,
                    int letter_spacing,
                    bool allow_hyphenation,
                    lUInt32 hints
                 )
{
    return lvfontMeasureText( m_font, text, len, widths, flags, max_width, def_char );
}

lUInt32 LBitmapFont::getTextWidth( const lChar32 * text, int len, TextLangCfg * lang_cfg )
{
    static lUInt16 widths[MAX_LINE_CHARS+1];
    static lUInt8 flags[MAX_LINE_CHARS+1];
    if ( len>MAX_LINE_CHARS )
        len = MAX_LINE_CHARS;
    if ( len<=0 )
        return 0;
    lUInt16 res = measureText(
                    text, len,
                    widths,
                    flags,
                    MAX_LINE_WIDTH,
                    U' ',  // def_char
                    lang_cfg
                 );
    if ( res>0 && res<MAX_LINE_CHARS )
        return widths[res-1];
    return 0;
}

/// returns font baseline offset
int LBitmapFont::getBaseline()
{
    const lvfont_header_t * hdr = lvfontGetHeader( m_font );
    return hdr->fontBaseline;
}
/// returns font height
int LBitmapFont::getHeight() const
{
    const lvfont_header_t * hdr = lvfontGetHeader( m_font );
    return hdr->fontHeight;
}
bool LBitmapFont::getGlyphImage(lUInt32 code, lUInt8 * buf, lChar32 def_char)
{
    const lvfont_glyph_t * ptr = lvfontGetGlyph( m_font, code );
    if (!ptr)
        return false;
    const hrle_decode_info_t * pDecodeTable = lvfontGetDecodeTable( m_font );
    int sz = ptr->blackBoxX*ptr->blackBoxY;
    if (sz)
        lvfontUnpackGlyph(ptr->glyph, pDecodeTable, buf, sz);
    return true;
}
int LBitmapFont::LoadFromFile( const char * fname )
{
    Clear();
    int res = lvfontOpen( fname, &m_font );
    if (!res)
        return 0;
    lvfont_header_t * hdr = (lvfont_header_t*) m_font;
    _typeface = lString8( hdr->fontName );
    _family = (css_font_family_t) hdr->fontFamily;
    return 1;
}
#endif // (USE_BITMAP_FONTS==1)

// Font cache management
LVFontCacheItem * LVFontCache::findDuplicate( const LVFontDef * def )
{
    for (int i=0; i<_registered_list.length(); i++) {
        if ( _registered_list[i]->_def.CalcDuplicateMatch( *def ) )
            return _registered_list[i];
    }
    return NULL;
}

LVFontCacheItem * LVFontCache::findDocumentFontDuplicate(int documentId, lString8 name)
{
    for (int i=0; i<_registered_list.length(); i++) {
        if (_registered_list[i]->_def.getDocumentId() == documentId && _registered_list[i]->_def.getName() == name)
            return _registered_list[i];
    }
    return NULL;
}

LVFontCacheItem * LVFontCache::findFallback( lString8 face, int size )
{
    int best_index = -1;
    int best_match = -1;
    int best_instance_index = -1;
    int best_instance_match = -1;
    int i;
    for (i=0; i<_instance_list.length(); i++) {
        int match = _instance_list[i]->_def.CalcFallbackMatch( face, size );
        if (match > best_instance_match) {
            best_instance_match = match;
            best_instance_index = i;
        }
    }
    for (i=0; i<_registered_list.length(); i++) {
        int match = _registered_list[i]->_def.CalcFallbackMatch( face, size );
        if (match > best_match) {
            best_match = match;
            best_index = i;
        }
    }
    if (best_index<=0)
        return NULL;
    if (best_instance_match >= best_match)
        return _instance_list[best_instance_index];
    return _registered_list[best_index];
}

LVFontCacheItem * LVFontCache::find( const LVFontDef * fntdef, bool useBias )
{
    int best_index = -1;
    int best_match = -1;
    int best_instance_index = -1;
    int best_instance_match = -1;
    int i;
    LVFontDef def(*fntdef);
    lString8Collection list;
    splitPropertyValueList( fntdef->getTypeFace().c_str(), list );
    int nlen = list.length();
    for (int nindex=0; nindex==0 || nindex<nlen; nindex++) {
        // Give more weight to first fonts, so we don't risk (with the test at end)
        // picking an already instantiated second font over a not yet instantiated
        // first font with the same match.
        int ordering_weight = nlen - nindex;
        if ( nindex < nlen )
            def.setTypeFace( list[nindex] );
        else
            def.setTypeFace(lString8::empty_str);
        bool typeface_match = false;
        for (i=0; i<_instance_list.length(); i++) {
            int match = _instance_list[i]->_def.CalcMatch( def, useBias );
            if ( match >= 2560000 )
                typeface_match = true;
            match = match * 256 + ordering_weight;
            if (match > best_instance_match) {
                best_instance_match = match;
                best_instance_index = i;
            }
        }
        for (i=0; i<_registered_list.length(); i++) {
            int match = _registered_list[i]->_def.CalcMatch( def, useBias );
            if ( match >= 2560000 )
                typeface_match = true;
            match = match * 256 + ordering_weight;
            if (match > best_match) {
                best_match = match;
                best_index = i;
            }
        }
        if ( typeface_match ) {
            // No need to check next font names (which may get a better score
            // if the first fonts do not have the requested italic or weight
            // variants, so let's avoid that too).
            break;
        }
    }
    if (best_index<0)
        return NULL;
    // if (best_instance_match >= best_match) printf("Find '%s' best instance: %s\n", fntdef->getTypeFace().c_str(), _instance_list[best_instance_index]->_def.getTypeFace().c_str());
    // else printf("Find '%s' best registered: %s\n", fntdef->getTypeFace().c_str(), _registered_list[best_index]->_def.getTypeFace().c_str());
    if (best_instance_match >= best_match)
        return _instance_list[best_instance_index];
    return _registered_list[best_index];
}

bool LVFontCache::setAsPreferredFontWithBias( lString8 face, int bias, bool clearOthersBias )
{
    bool found = false;
    int i;
    for (i=0; i<_instance_list.length(); i++) {
        if (_instance_list[i]->_def.setBiasIfNameMatch( face, bias, clearOthersBias ))
            found = true;
    }
    for (i=0; i<_registered_list.length(); i++) {
        if (_registered_list[i]->_def.setBiasIfNameMatch( face, bias, clearOthersBias ))
            found = true;
    }
    return found;
}

void LVFontCache::addInstance( const LVFontDef * def, LVFontRef fnt )
{
    assert( !fnt.isNull() );
    LVFontCacheItem * item = new LVFontCacheItem(*def);
    item->_fnt = fnt;
    _instance_list.add( item );
}

void LVFontCache::update( const LVFontDef * def, LVFontRef fnt )
{
    int i;
    if ( !fnt.isNull() ) {
        for (i=0; i<_instance_list.length(); i++) {
            if ( _instance_list[i]->_def == *def ) {
                if (fnt.isNull()) {
                    _instance_list.erase(i, 1);
                }
                else {
                    _instance_list[i]->_fnt = fnt;
                }
                return;
            }
        }
        // add new
        //LVFontCacheItem * item;
        //item = new LVFontCacheItem(*def);
        addInstance( def, fnt );
    }
    else {
        for (i=0; i<_registered_list.length(); i++) {
            if ( _registered_list[i]->_def == *def ) {
                return;
            }
        }
        // add new
        LVFontCacheItem * item;
        item = new LVFontCacheItem(*def);
        _registered_list.add( item );
    }
}

void LVFontCache::_removeDocumentFonts(int documentId, LVPtrVector< LVFontCacheItem > &list)
{
    int *documentFonts = new int[list.length()];
    int numDocumentFonts = 0;
    for (int i = 0; i < list.length(); ++i) {
        if (list[i]->_def.getDocumentId() == documentId)
            documentFonts[numDocumentFonts++] = i;
    }
    for (int i = 0; i < numDocumentFonts; ++i) {
        LVFont *ptr = list[documentFonts[i]]->_fnt.get();
        if ( ptr == nullptr )
            continue;
        ptr->clearFontRefs();
        for (int j = 0; j < _registered_list.length(); ++j) {
            if ( !_registered_list[j]->_fnt.isNull() )
                _registered_list[j]->_fnt->clearFontRefs(ptr);
        }
        for (int j = 0; j < _instance_list.length(); ++j) {
            if ( !_instance_list[j]->_fnt.isNull() )
                _instance_list[j]->_fnt->clearFontRefs(ptr);
        }
    }
    while (numDocumentFonts--) {
#ifndef NDEBUG
        LVFontCacheItem *item = list[documentFonts[numDocumentFonts]];
        assert(!item->_fnt || item->_fnt.getRefCount() == 1);
#endif
        delete list.remove(documentFonts[numDocumentFonts]);
    }
    delete[] documentFonts;
}

void LVFontCache::removeDocumentFonts(int documentId)
{
    if (-1 == documentId)
        return;
    _removeDocumentFonts(documentId, _registered_list);
    _removeDocumentFonts(documentId, _instance_list);
}

static int s_int_comparator(const void * n1, const void * n2)
{
    int* i1 = (int*)n1;
    int* i2 = (int*)n2;
    return *i1 == *i2 ? 0 : (*i1 < *i2 ? -1 : 1);
}

void LVFontCache::getAvailableFontWeights(LVArray<int>& weights, lString8 faceName) {
    weights.clear();
    for (int i = 0; i < _registered_list.length(); i++) {
        const LVFontCacheItem* item = _registered_list[i];
        if (item->_def.getTypeFace() == faceName) {
            if (item->_def.isRealWeight()) {       // ignore fonts with fake weight
                int weight = item->_def.getWeight();
                if (weights.indexOf(weight) < 0) {
                    weights.add(weight);
                }
            }
        }
    }
    int* ptr = weights.get();
    qsort(ptr, (size_t)weights.length(), sizeof(int), s_int_comparator);
}

// garbage collector
void LVFontCache::gc()
{
    int droppedCount = 0;
    int usedCount = 0;
    for (int i=_instance_list.length()-1; i>=0; i--) {
        LVFontRef &fnt = _instance_list[i]->_fnt;
        fnt->clearFontRefs();
        assert( fnt.getRefCount() >=1 );
        if ( fnt.getRefCount() == 1 ) {
            if ( CRLog::isTraceEnabled() ) {
                CRLog::trace("dropping font instance %s[%d] by gc()",
                        _instance_list[i]->getDef()->getTypeFace().c_str(),
                        _instance_list[i]->getDef()->getSize() );
            }
            _instance_list.erase(i, 1);
            droppedCount++;
        }
        else {
            usedCount++;
        }
    }
    if ( CRLog::isDebugEnabled() )
        CRLog::debug("LVFontCache::gc() : %d fonts still used, %d fonts dropped", usedCount, droppedCount );
    if (droppedCount) {
        // We need more than 1 gc() for a complete cleanup: to drop font
        // instances that were only referenced by dropped fonts (fallbacks,
        // bullet, decimal, …).  The performance impact of reinstantiating
        // fonts is minimal.
        gc();
    }
}

#if !defined(__SYMBIAN32__) && defined(_WIN32) && USE_FREETYPE!=1
void LVBaseWin32Font::Clear()
{
    if (_hfont)
    {
        DeleteObject(_hfont);
        _hfont = NULL;
        _height = 0;
        _baseline = 0;
    }
}

bool LVBaseWin32Font::Create( const LOGFONTA & lf )
{
    if (!IsNull())
        Clear();
    memcpy( &_logfont, &lf, sizeof(LOGFONTA));
    _hfont = CreateFontIndirectA( &lf );
    if (!_hfont)
        return false;
    //memcpy( &_logfont, &lf, sizeof(LOGFONT) );
    // get text metrics
    SelectObject( _drawbuf.GetDC(), _hfont );
    TEXTMETRICW tm;
    GetTextMetricsW( _drawbuf.GetDC(), &tm );
    _logfont.lfHeight = tm.tmHeight;
    _logfont.lfWeight = tm.tmWeight;
    _logfont.lfItalic = tm.tmItalic;
    _logfont.lfCharSet = tm.tmCharSet;
    GetTextFaceA( _drawbuf.GetDC(), sizeof(_logfont.lfFaceName)-1, _logfont.lfFaceName );
    _height = tm.tmHeight;
    _baseline = _height - tm.tmDescent;
    return true;
}

bool LVBaseWin32Font::Create(int size, int weight, bool italic, css_font_family_t family, lString8 typeface )
{
    if (!IsNull())
        Clear();
    //
    LOGFONTA lf = { 0 };
    lf.lfHeight = size;
    lf.lfWeight = weight;
    lf.lfItalic = italic?1:0;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_ONLY_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    //lf.lfQuality = ANTIALIASED_QUALITY; //PROOF_QUALITY;
#ifdef USE_BITMAP_FONT
    lf.lfQuality = NONANTIALIASED_QUALITY; //CLEARTYPE_QUALITY; //PROOF_QUALITY;
#else
    lf.lfQuality = 5; //CLEARTYPE_QUALITY; //PROOF_QUALITY;
#endif
    strcpy(lf.lfFaceName, typeface.c_str());
    _typeface = typeface;
    _family = family;
    switch (family)
    {
    case css_ff_serif:
        lf.lfPitchAndFamily = VARIABLE_PITCH | FF_ROMAN;
        break;
    case css_ff_sans_serif:
        lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
        break;
    case css_ff_cursive:
        lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SCRIPT;
        break;
    case css_ff_fantasy:
        lf.lfPitchAndFamily = VARIABLE_PITCH | FF_DECORATIVE;
        break;
    case css_ff_monospace:
        lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
        break;
    default:
        lf.lfPitchAndFamily = VARIABLE_PITCH | FF_DONTCARE;
        break;
    }
    _hfont = CreateFontIndirectA( &lf );
    if (!_hfont)
        return false;
    //memcpy( &_logfont, &lf, sizeof(LOGFONT) );
    // get text metrics
    SelectObject( _drawbuf.GetDC(), _hfont );
    TEXTMETRICW tm;
    GetTextMetricsW( _drawbuf.GetDC(), &tm );
    memset(&_logfont, 0, sizeof(LOGFONT));
    _logfont.lfHeight = tm.tmHeight;
    _logfont.lfWeight = tm.tmWeight;
    _logfont.lfItalic = tm.tmItalic;
    _logfont.lfCharSet = tm.tmCharSet;
    GetTextFaceA( _drawbuf.GetDC(), sizeof(_logfont.lfFaceName)-1, _logfont.lfFaceName );
    _height = tm.tmHeight;
    _baseline = _height - tm.tmDescent;
    return true;
}


/** \brief get glyph info
    \param glyph is pointer to glyph_info_t struct to place retrieved info
    \return true if glyh was found
*/
bool LVWin32DrawFont::getGlyphInfo( lUInt16 code, glyph_info_t * glyph, lChar32 def_char, bool code_is_glyph_index=false, bool is_fallback=false )
{
    return false;
}

/// returns char width
int LVWin32DrawFont::getCharWidth( lChar32 ch, lChar32 def_char )
{
    if (_hfont==NULL)
        return 0;
    // measure character widths
    GCP_RESULTSW gcpres = { 0 };
    gcpres.lStructSize = sizeof(gcpres);
    lChar32 str[2];
    str[0] = ch;
    str[1] = 0;
    int dx[2];
    gcpres.lpDx = dx;
    gcpres.nMaxFit = 1;
    gcpres.nGlyphs = 1;

    lUInt32 res = GetCharacterPlacementW(
        _drawbuf.GetDC(),
        str,
        1,
        100,
        &gcpres,
        GCP_MAXEXTENT); //|GCP_USEKERNING

    if (!res)
    {
        // ERROR
        return 0;
    }

    return dx[0];
}

lUInt32 LVWin32DrawFont::getTextWidth( const lChar32 * text, int len, TextLangCfg * lang_cfg=NULL )
{
    //
    static lUInt16 widths[MAX_LINE_CHARS+1];
    static lUInt8 flags[MAX_LINE_CHARS+1];
    if ( len>MAX_LINE_CHARS )
        len = MAX_LINE_CHARS;
    if ( len<=0 )
        return 0;
    lUInt16 res = measureText(
                    text, len,
                    widths,
                    flags,
                    MAX_LINE_WIDTH,
                    U' ',  // def_char
                    lang_cfg
                 );
    if ( res>0 && res<MAX_LINE_CHARS )
        return widths[res-1];
    return 0;
}

/** \brief measure text
    \param glyph is pointer to glyph_info_t struct to place retrieved info
    \return true if glyph was found
*/
lUInt16 LVWin32DrawFont::measureText(
                    const lChar32 * text, int len,
                    lUInt16 * widths,
                    lUInt8 * flags,
                    int max_width,
                    lChar32 def_char,
                    TextLangCfg * lang_cfg = NULL,
                    int letter_spacing,
                    bool allow_hyphenation,
                    lUInt32 hints
                 )
{
    if (_hfont==NULL)
        return 0;

    lString32 str(text, len);
    assert(str[len]==0);
    //str += U"         ";
    lChar32 * pstr = str.modify();
    assert(pstr[len]==0);
    // substitute soft hyphens with zero width spaces
    for (int i=0; i<len; i++)
    {
        if (pstr[i]==UNICODE_SOFT_HYPHEN_CODE)
            pstr[i] = UNICODE_ZERO_WIDTH_SPACE;
    }
    assert(pstr[len]==0);
    // measure character widths
    GCP_RESULTSW gcpres = { 0 };
    gcpres.lStructSize = sizeof(gcpres);
    LVArray<int> dx( len+1, 0 );
    gcpres.lpDx = dx.ptr();
    gcpres.nMaxFit = len;
    gcpres.nGlyphs = len;

    lUInt32 res = GetCharacterPlacementW(
        _drawbuf.GetDC(),
        pstr,
        len,
        max_width,
        &gcpres,
        GCP_MAXEXTENT); //|GCP_USEKERNING
    if (!res)
    {
        // ERROR
        widths[0] = 0;
        flags[0] = 0;
        return 1;
    }

    if ( !_hyphen_width )
        _hyphen_width = getCharWidth( UNICODE_SOFT_HYPHEN_CODE );

    lUInt16 wsum = 0;
    lUInt16 nchars = 0;
    lUInt16 gwidth = 0;
    lUInt8 bflags;
    int isSpace;
    lChar32 ch;
    int hwStart, hwEnd;

    assert(pstr[len]==0);

    for ( ; wsum < max_width && nchars < len && nchars<gcpres.nMaxFit; nchars++ )
    {
        bflags = 0;
        ch = text[nchars];
        isSpace = lvfontIsUnicodeSpace(ch);
        if (isSpace ||  ch == UNICODE_SOFT_HYPHEN_CODE )
            bflags |= LCHAR_ALLOW_WRAP_AFTER;
        if (ch == '-')
            bflags |= LCHAR_DEPRECATED_WRAP_AFTER;
        if (isSpace)
            bflags |= LCHAR_IS_SPACE;
        gwidth = gcpres.lpDx[nchars];
        widths[nchars] = wsum + gwidth;
        if ( ch != UNICODE_SOFT_HYPHEN_CODE )
            wsum += gwidth; /* don't include hyphens to width */
        flags[nchars] = bflags;
    }
    //hyphwidth = glyph ? glyph->gi.width : 0;

    //try to add hyphen
    for (hwStart=nchars-1; hwStart>0; hwStart--)
    {
        if (lvfontIsUnicodeSpace(text[hwStart]))
        {
            hwStart++;
            break;
        }
    }
    for (hwEnd=nchars; hwEnd<len; hwEnd++)
    {
        lChar32 ch = text[hwEnd];
        if (lvfontIsUnicodeSpace(ch))
            break;
        if (flags[hwEnd-1]&LCHAR_ALLOW_WRAP_AFTER)
            break;
        if (ch=='.' || ch==',' || ch=='!' || ch=='?' || ch=='?' || ch==':' || ch==';')
            break;

    }
    if ( lang_cfg )
        lang_cfg->getHyphMethod()->hyphenate(text+hwStart, hwEnd-hwStart, widths+hwStart, flags+hwStart, _hyphen_width, max_width);
    else // Use global lang hyph method
        HyphMan::hyphenate(text+hwStart, hwEnd-hwStart, widths+hwStart, flags+hwStart, _hyphen_width, max_width);

    return nchars;
}

/// draws text string (returns x advance)
int LVWin32DrawFont::DrawTextString( LVDrawBuf * buf, int x, int y,
                   const lChar32 * text, int len,
                   lChar32 def_char, lUInt32 * palette, bool addHyphen,
                   TextLangCfg * lang_cfg,
                   lUInt32 flags, int letter_spacing, int width,
                   int text_decoration_back_gap, int, int, SVGGlyphsCollector * )
{
    if (_hfont==NULL)
        return 0;

    lString32 str(text, len);
    // substitute soft hyphens with zero width spaces
    if (addHyphen)
        str += UNICODE_SOFT_HYPHEN_CODE;
    //str += U"       ";
    lChar32 * pstr = str.modify();
    for (int i=0; i<len-1; i++)
    {
        if (pstr[i]==UNICODE_SOFT_HYPHEN_CODE)
            pstr[i] = UNICODE_ZERO_WIDTH_SPACE;
    }

    lvRect clip;
    buf->GetClipRect(&clip);
    if (y > clip.bottom || y+_height < clip.top)
        return 0;
    if (buf->GetBitsPerPixel()<16)
    {
        // draw using backbuffer
        SIZE sz;
        if ( !GetTextExtentPoint32W(_drawbuf.GetDC(),
                str.c_str(), str.length(), &sz) )
            return 0;
        LVColorDrawBuf colorbuf( sz.cx, sz.cy );
        colorbuf.Clear(0xFFFFFF);
        HDC dc = colorbuf.GetDC();
        SelectObject(dc, _hfont);
        SetTextColor(dc, 0x000000);
        SetBkMode(dc, TRANSPARENT);
        //ETO_OPAQUE
        if (ExtTextOutW( dc, 0, 0,
                0, //ETO_OPAQUE
                NULL,
                str.c_str(), str.length(), NULL ))
        {
            // COPY colorbuf to buf with converting
            colorbuf.DrawTo( buf, x, y, 0, NULL );
        }
    }
    else
    {
        // draw directly on DC
        //TODO
        HDC dc = ((LVColorDrawBuf*)buf)->GetDC();
        HFONT oldfont = (HFONT)SelectObject( dc, _hfont );
        SetTextColor( dc, RevRGB(buf->GetTextColor()) );
        SetBkMode(dc, TRANSPARENT);
        ExtTextOutW( dc, x, y,
            0, //ETO_OPAQUE
            NULL,
            str.c_str(), str.length(), NULL );
        SelectObject( dc, oldfont );
    }
    return 0; // advance not implemented
}

/** \brief get glyph image in 1 byte per pixel format
    \param code is unicode character
    \param buf is buffer [width*height] to place glyph data
    \return true if glyph was found
*/
bool LVWin32DrawFont::getGlyphImage(lUInt16 code, lUInt8 * buf, lChar32 def_char)
{
    return false;
}



int LVWin32Font::GetGlyphIndex( HDC hdc, wchar_t code )
{
    wchar_t s[2];
    wchar_t g[2];
    s[0] = code;
    s[1] = 0;
    g[0] = 0;
    GCP_RESULTSW gcp;
    gcp.lStructSize = sizeof(GCP_RESULTSW);
    gcp.lpOutString = NULL;
    gcp.lpOrder = NULL;
    gcp.lpDx = NULL;
    gcp.lpCaretPos = NULL;
    gcp.lpClass = NULL;
    gcp.lpGlyphs = g;
    gcp.nGlyphs = 2;
    gcp.nMaxFit = 2;

    DWORD res = GetCharacterPlacementW(
      hdc, s, 1,
      1000,
      &gcp,
      0
    );
    if (!res)
        return 0;
    return g[0];
}


glyph_t * LVWin32Font::GetGlyphRec( lChar32 ch )
{
    glyph_t * p = _cache.get( ch );
    if (p->flgNotExists)
        return NULL;
    if (p->flgValid)
        return p;
    p->flgNotExists = true;
    lUInt16 gi = GetGlyphIndex( _drawbuf.GetDC(), ch );
    if (gi==0 || gi==0xFFFF || (gi==_unknown_glyph_index && ch!=' '))
    {
        // glyph not found
        p->flgNotExists = true;
        return NULL;
    }
    GLYPHMETRICS metrics;
    p->glyph = NULL;

    MAT2 identity = { {0,1}, {0,0}, {0,0}, {0,1} };
    lUInt32 res;
    res = GetGlyphOutlineW( _drawbuf.GetDC(), ch,
        GGO_METRICS,
        &metrics,
        0,
        NULL,
        &identity );
    if (res==GDI_ERROR)
        return false;
    int gs = GetGlyphOutlineW( _drawbuf.GetDC(), ch,
#ifdef USE_BITMAP_FONT
        GGO_BITMAP, //GGO_METRICS
#else
        GGO_GRAY8_BITMAP, //GGO_METRICS
#endif
        &metrics,
        0,
        NULL,
        &identity );
    if (gs>0x10000 || gs<0)
        return false;

    p->gi.blackBoxX = metrics.gmBlackBoxX;
    p->gi.blackBoxY = metrics.gmBlackBoxY;
    p->gi.originX = (lInt8)metrics.gmptGlyphOrigin.x;
    p->gi.originY = (lInt8)metrics.gmptGlyphOrigin.y;
    p->gi.width = (lUInt8)metrics.gmCellIncX;

    if (p->gi.blackBoxX>0 && p->gi.blackBoxY>0)
    {
        p->glyph = new unsigned char[p->gi.blackBoxX * p->gi.blackBoxY];
        if (gs>0)
        {
            lUInt8 * glyph = new unsigned char[gs];
             res = GetGlyphOutlineW( _drawbuf.GetDC(), ch,
#ifdef USE_BITMAP_FONT
        GGO_BITMAP, //GGO_METRICS
#else
        GGO_GRAY8_BITMAP, //GGO_METRICS
#endif
               &metrics,
               gs,
               glyph,
               &identity );
            if (res==GDI_ERROR)
            {
                delete[] glyph;
                return NULL;
            }
#ifdef USE_BITMAP_FONT
            int glyph_row_size = (p->gi.blackBoxX + 31) / 8 / 4 * 4;
#else
            int glyph_row_size = (p->gi.blackBoxX + 3)/ 4 * 4;
#endif
            lUInt8 * src = glyph;
            lUInt8 * dst = p->glyph;
            for (int y=0; y<p->gi.blackBoxY; y++)
            {
                for (int x = 0; x<p->gi.blackBoxX; x++)
                {
#ifdef USE_BITMAP_FONT
                    lUInt8 b = (src[x>>3] & (0x80>>(x&7))) ? 0xFC : 0;
#else
                    lUInt8 b = src[x];
                    if (b>=64)
                       b = 63;
                    b = (b<<2) & 0xFC;
#endif
                    dst[x] = b;
                }
                src += glyph_row_size;
                dst += p->gi.blackBoxX;
            }
            delete[] glyph;
            //*(dst-1) = 0xFF;
        }
        else
        {
            // empty glyph
            for (int i=p->gi.blackBoxX * p->gi.blackBoxY-1; i>=0; i--)
                p->glyph[i] = 0;
        }
    }
    // found!
    p->flgValid = true;
    p->flgNotExists = false;
    return p;
}

/** \brief get glyph info
    \param glyph is pointer to glyph_info_t struct to place retrieved info
    \return true if glyh was found
*/
bool LVWin32Font::getGlyphInfo( lUInt16 code, glyph_info_t * glyph, lChar32 def_char, bool code_is_glyph_index=false, bool is_fallback=false )
{
    if (_hfont==NULL)
        return false;
    glyph_t * p = GetGlyphRec( code );
    if (!p)
        return false;
    *glyph = p->gi;
    return true;
}

lUInt32 LVWin32Font::getTextWidth( const lChar32 * text, int len, TextLangCfg * lang_cfg=NULL )
{
    //
    static lUInt16 widths[MAX_LINE_CHARS+1];
    static lUInt8 flags[MAX_LINE_CHARS+1];
    if ( len>MAX_LINE_CHARS )
        len = MAX_LINE_CHARS;
    if ( len<=0 )
        return 0;
    lUInt16 res = measureText(
                    text, len,
                    widths,
                    flags,
                    MAX_LINE_WIDTH,
                    U' ',  // def_char
                    lang_cfg
                 );
    if ( res>0 && res<MAX_LINE_CHARS )
        return widths[res-1];
    return 0;
}

/** \brief measure text
    \param glyph is pointer to glyph_info_t struct to place retrieved info
    \return true if glyph was found
*/
lUInt16 LVWin32Font::measureText(
                    const lChar32 * text, int len,
                    lUInt16 * widths,
                    lUInt8 * flags,
                    int max_width,
                    lChar32 def_char,
                    TextLangCfg * lang_cfg = NULL,
                    int letter_spacing,
                    bool allow_hyphenation,
                    lUInt32 hints
                 )
{
    if (_hfont==NULL)
        return 0;

    lUInt16 wsum = 0;
    lUInt16 nchars = 0;
    glyph_t * glyph; //GetGlyphRec( lChar32 ch )
    lUInt16 gwidth = 0;
    lUInt16 hyphwidth = 0;
    lUInt8 bflags;
    int isSpace;
    lChar32 ch;
    int hwStart, hwEnd;

    glyph = GetGlyphRec( UNICODE_SOFT_HYPHEN_CODE );
    hyphwidth = glyph ? glyph->gi.width : 0;

    for ( ; wsum < max_width && nchars < len; nchars++ )
    {
        bflags = 0;
        ch = text[nchars];
        isSpace = lvfontIsUnicodeSpace(ch);
        if (isSpace ||  ch == UNICODE_SOFT_HYPHEN_CODE )
            bflags |= LCHAR_ALLOW_WRAP_AFTER;
        if (ch == '-')
            bflags |= LCHAR_DEPRECATED_WRAP_AFTER;
        if (isSpace)
            bflags |= LCHAR_IS_SPACE;
        glyph = GetGlyphRec( ch );
        if (!glyph && def_char)
             glyph = GetGlyphRec( def_char );
        gwidth = glyph ? glyph->gi.width : 0;
        widths[nchars] = wsum + gwidth;
        if ( ch != UNICODE_SOFT_HYPHEN_CODE )
            wsum += gwidth; /* don't include hyphens to width */
        flags[nchars] = bflags;
    }

    //try to add hyphen
    for (hwStart=nchars-1; hwStart>0; hwStart--)
    {
        if (lvfontIsUnicodeSpace(text[hwStart]))
        {
            hwStart++;
            break;
        }
    }
    for (hwEnd=nchars; hwEnd<len; hwEnd++)
    {
        lChar32 ch = text[hwEnd];
        if (lvfontIsUnicodeSpace(ch))
            break;
        if (flags[hwEnd-1]&LCHAR_ALLOW_WRAP_AFTER)
            break;
        if (ch=='.' || ch==',' || ch=='!' || ch=='?' || ch=='?')
            break;

    }
    if ( lang_cfg )
        lang_cfg->getHyphMethod()->hyphenate(text+hwStart, hwEnd-hwStart, widths+hwStart, flags+hwStart, hyphwidth, max_width);
    else // Use global lang hyph method
        HyphMan::hyphenate(text+hwStart, hwEnd-hwStart, widths+hwStart, flags+hwStart, hyphwidth, max_width);

    return nchars;
}

/** \brief get glyph image in 1 byte per pixel format
    \param code is unicode character
    \param buf is buffer [width*height] to place glyph data
    \return true if glyph was found
*/
bool LVWin32Font::getGlyphImage(lUInt16 code, lUInt8 * buf, lChar32 def_char)
{
    if (_hfont==NULL)
        return false;
    glyph_t * p = GetGlyphRec( code );
    if (!p)
        return false;
    int gs = p->gi.blackBoxX*p->gi.blackBoxY;
    if (gs>0)
        memcpy( buf, p->glyph, gs );
    return true;
}

void LVWin32Font::Clear()
{
    LVBaseWin32Font::Clear();
    _cache.clear();
}

bool LVWin32Font::Create( const LOGFONTA & lf )
{
    if (!LVBaseWin32Font::Create(lf))
        return false;
    _unknown_glyph_index = GetGlyphIndex( _drawbuf.GetDC(), 1 );
    return true;
}

bool LVWin32Font::Create(int size, int weight, bool italic, css_font_family_t family, lString8 typeface )
{
    if (!LVBaseWin32Font::Create(size, weight, italic, family, typeface))
        return false;
    _unknown_glyph_index = GetGlyphIndex( _drawbuf.GetDC(), 1 );
    return true;
}

#endif // !defined(__SYMBIAN32__) && defined(_WIN32) && USE_FREETYPE!=1

/// to compare two fonts
bool operator == (const LVFont & r1, const LVFont & r2)
{
    if ( &r1 == &r2 )
        return true;
    return     r1.getSize() == r2.getSize()
            && r1.getWeight() == r2.getWeight()
            && r1.getItalic() == r2.getItalic()
            && r1.getFontFamily() == r2.getFontFamily()
            && r1.getTypeFace() == r2.getTypeFace()
            && r1.getKerningMode() == r2.getKerningMode()
            && r1.getHintingMode() == r2.getHintingMode()
            ;
}

