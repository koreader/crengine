/** \file lvfntman.h
    \brief font manager interface

    CoolReader Engine

    (c) Vadim Lopatin, 2000-2006

    This source code is distributed under the terms of
    GNU General Public License.

    See LICENSE file for details.

*/

#ifndef __LV_FNT_MAN_H_INCLUDED__
#define __LV_FNT_MAN_H_INCLUDED__

#include <stdlib.h>
#include "crsetup.h"
#include "lvtypes.h"
#include "lvplatform.h"
#include "lvfnt.h"
#include "cssdef.h"
#include "lvstring.h"
#include "lvref.h"
#include "lvptrvec.h"
#include "hyphman.h"
#include "lvdrawbuf.h"
#include "textlang.h"

#if !defined(__SYMBIAN32__) && defined(_WIN32)
extern "C" {
#include <windows.h>
}
#endif

#if USE_GLYPHCACHE_HASHTABLE==1
#include "lvhashtable.h"
#define GLYPHCACHE_TABLE_SZ         256
#endif

struct LVFontGlyphCacheItem;

union GlyphCacheItemData {
	lChar32 ch;
#if USE_HARFBUZZ==1
	lUInt32 gindex;
#endif
};

#if USE_GLYPHCACHE_HASHTABLE == 1
inline lUInt32 getHash(GlyphCacheItemData data)
{
    return getHash(*((lUInt32*)&data));
}

// Note: this may cause some issue when building on Win32 (or anywhere lChar32
// is not 32bits), see https://github.com/buggins/coolreader/pull/117
inline bool operator==(GlyphCacheItemData data1, GlyphCacheItemData data2)
{
    return (*((lUInt32*)&data1)) == (*((lUInt32*)&data2));
}
#endif

class LVFontGlobalGlyphCache
{
private:
    LVFontGlyphCacheItem * head;
    LVFontGlyphCacheItem * tail;
    int size;
    int max_size;
    void removeNoLock( LVFontGlyphCacheItem * item );
    void putNoLock( LVFontGlyphCacheItem * item );
public:
    LVFontGlobalGlyphCache( int maxSize )
        : head(NULL), tail(NULL), size(0), max_size(maxSize )
    {
    }
    ~LVFontGlobalGlyphCache()
    {
        clear();
    }
    void put( LVFontGlyphCacheItem * item );
    void remove( LVFontGlyphCacheItem * item );
#if USE_GLYPHCACHE_HASHTABLE != 1
    void refresh( LVFontGlyphCacheItem * item );
#endif
    void clear();
};

class LVFontLocalGlyphCache
{
private:
    LVFontGlobalGlyphCache *global_cache;
#if USE_GLYPHCACHE_HASHTABLE == 1
    LVHashTable<GlyphCacheItemData, struct LVFontGlyphCacheItem*> hashTable;
#else
    LVFontGlyphCacheItem * head;
    LVFontGlyphCacheItem * tail;
#endif
    //int size;
public:
    LVFontLocalGlyphCache( LVFontGlobalGlyphCache * globalCache )
    : global_cache(globalCache),
#if USE_GLYPHCACHE_HASHTABLE == 1
    hashTable(GLYPHCACHE_TABLE_SZ)
#else
    head(NULL), tail(NULL)
#endif
    {}
    ~LVFontLocalGlyphCache()
    {
        clear();
    }
    void clear();
    LVFontGlyphCacheItem * getByChar(lChar32 ch);
    #if USE_HARFBUZZ==1
    LVFontGlyphCacheItem * getByIndex(lUInt32 index);
    #endif
    void put( LVFontGlyphCacheItem * item );
    void remove( LVFontGlyphCacheItem * item );
};

struct LVFontGlyphCacheItem
{
    LVFontGlyphCacheItem * prev_global;
    LVFontGlyphCacheItem * next_global;
    LVFontGlyphCacheItem * prev_local;
    LVFontGlyphCacheItem * next_local;
    LVFontLocalGlyphCache * local_cache;
    GlyphCacheItemData data;
    lUInt16 bmp_width;
    lUInt16 bmp_height;
    lInt16  origin_x;
    lInt16  origin_y;
    lUInt16 advance;
    // NOTE: We stash the actual *storage* in there, so, make sure this will be on an address aligned like a 64-bit malloc would.
    //       This is usually 16 on 64-bit and 8 otherwise (c.f., https://www.gnu.org/software/libc/manual/html_node/Aligned-Memory-Blocks.html).
    //       It's also 16 on x86 on recent glibcs (c.f., https://sourceware.org/bugzilla/show_bug.cgi?id=21120).
    //       We just use 16 everywhere, as this turned out to be mildly helpful on armv7 (c.f., https://github.com/koreader/crengine/pull/441).
    alignas(16) lUInt8 bmp[1];
    //=======================================================================
    int getSize() const
    {
        // NOTE: Again, we stash the data *in place of* the bmp array, so, the effective size of our object is:
        //       LVFontGlyphCacheItem-up-to-bmp + the glyph storage size.
        //       Given the alignment constraints on bmp, the only sane way to compute that is via offsetof.
        return offsetof(LVFontGlyphCacheItem, bmp) + ((bmp_width * bmp_height) * sizeof(*bmp));
    }
    static LVFontGlyphCacheItem * newItem( LVFontLocalGlyphCache * local_cache, lChar32 ch, int w, int h )
    {
        LVFontGlyphCacheItem * item = (LVFontGlyphCacheItem *)malloc( offsetof(LVFontGlyphCacheItem, bmp)
                                                                        + ((w*h) * sizeof(*bmp)) );
	if (item) {
	    item->data.ch = ch;
	    item->bmp_width = (lUInt16)w;
	    item->bmp_height = (lUInt16)h;
	    item->origin_x =   0;
	    item->origin_y =   0;
	    item->advance =    0;
	    item->prev_global = NULL;
	    item->next_global = NULL;
	    item->prev_local = NULL;
	    item->next_local = NULL;
	    item->local_cache = local_cache;
        }
        return item;
    }
    #if USE_HARFBUZZ==1
    static LVFontGlyphCacheItem *newItem(LVFontLocalGlyphCache* local_cache, lUInt32 glyph_index, int w, int h)
    {
        LVFontGlyphCacheItem * item = (LVFontGlyphCacheItem *)malloc( offsetof(LVFontGlyphCacheItem, bmp)
                                                                        + ((w*h) * sizeof(*bmp)) );
        if (item) {
            item->data.gindex = glyph_index;
            item->bmp_width = (lUInt16) w;
            item->bmp_height = (lUInt16) h;
            item->origin_x = 0;
            item->origin_y = 0;
            item->advance = 0;
            item->prev_global = NULL;
            item->next_global = NULL;
            item->prev_local = NULL;
            item->next_local = NULL;
            item->local_cache = local_cache;
        }
        return item;
    }
    #endif
    static void freeItem( LVFontGlyphCacheItem * item )
    {
        if (item)
            free( item );
    }
};

#if USE_HARFBUZZ==1
bool isHBScriptCursive( hb_script_t script );
#endif

enum hinting_mode_t {
    HINTING_MODE_DISABLED,
    HINTING_MODE_BYTECODE_INTERPRETOR,
    HINTING_MODE_AUTOHINT
};

enum kerning_mode_t {
    KERNING_MODE_DISABLED,
    KERNING_MODE_FREETYPE,
    KERNING_MODE_HARFBUZZ_LIGHT,
    KERNING_MODE_HARFBUZZ
};


// Hint flags for measuring and drawing (some used only with full Harfbuzz)
// These 4 translate (after mask & shift) from LTEXT_WORD_* equivalents
// (see lvtextfm.h). Keep them in sync.
#define LFNT_HINT_DIRECTION_KNOWN        0x0001 /// segment direction is known
#define LFNT_HINT_DIRECTION_IS_RTL       0x0002 /// segment direction is RTL
#define LFNT_HINT_BEGINS_PARAGRAPH       0x0004 /// segment is at start of paragraph
#define LFNT_HINT_ENDS_PARAGRAPH         0x0008 /// segment is at end of paragraph

#define LFNT_HINT_IS_FALLBACK_FONT       0x0010 /// set on recursive Harfbuzz rendering/drawing with a fallback font

#define LFNT_HINT_TRANSFORM_STRETCH      0x0100 /// Glyph(s) are to be stretched so their bounding box fits the provided w/h
#define LFNT_HINT_CJK_ALTERED_WIDTH      0x0200 /// CJK full width glyph is to be shifted to look correct in a non-nominal width
#define LFNT_HINT_CJK_SCALED_WIDTH       0x0400 /// CJK full width glyph has been scaled by cjk_width_scale_percent

// These 4 translate from LTEXT_TD_* equivalents (see lvtextfm.h). Keep them in sync.
#define LFNT_DRAW_UNDERLINE              0x1000 /// underlined text
#define LFNT_DRAW_OVERLINE               0x2000 /// overlined text
#define LFNT_DRAW_LINE_THROUGH           0x4000 /// striked through text
#define LFNT_DRAW_DECORATION_MASK        0x7000


// CSS font-variant and font-feature-settings properties:
//   https://drafts.csswg.org/css-fonts-3/#propdef-font-variant
//   https://developer.mozilla.org/en-US/docs/Web/CSS/font-variant
// OpenType feature tags (to be provided to HarfBuzz)
//   https://en.wikipedia.org/wiki/List_of_typographic_features
//   https://docs.microsoft.com/en-us/typography/opentype/spec/featurelist
// See https://github.com/koreader/koreader/issues/5821#issuecomment-596243758
// Random notes:
// - common-ligatures : 'liga' + 'clig'  (the keyword 'normal' activates these ligatures)
//   and 'rlig' ("required ligatures, e.g. for arabic) are enabled by default by Harfbuzz
// - discretionary-ligatures : 'dlig'   (type designer choices) is not enabled by default
// - diagonal-fractions : 'frac' (enabling also 'numr' + 'dnom' seems not needed,
//   numr or dnom standalone just make the / more oblique)
// - stacked-fractions: "/" becomes horizontal
// - jis90 'jp90' is said to be the default in fonts. Other jp* replace this one.
// - Not supported (because no room and because they require some args, which
//   would complicate parsing and storing):
//   CSS font-variant-alternates:
//       stylistic()
//       styleset()
//       character-variant()
//       swash()
//       ornaments()
//       annotation()

// OpenType features to request
// Max 31 bits. We need "signed int features" to have -1 for non-instantiated fonts)
#define LFNT_OT_FEATURES_NORMAL 0x00000000

// #define LFNT_OT_FEATURES_P_LIGA 0x000000XX // +liga +clig enabled by default  (font-variant-ligatures: common-ligatures)
// #define LFNT_OT_FEATURES_P_CALT 0x000000XX // +calt       enabled by default  (font-variant-ligatures: contextual)
#define LFNT_OT_FEATURES_M_LIGA 0x00000001 // -liga -clig   (font-variant-ligatures: no-common-ligatures)
#define LFNT_OT_FEATURES_M_CALT 0x00000002 // -calt         (font-variant-ligatures: no-contextual)
#define LFNT_OT_FEATURES_P_DLIG 0x00000004 // +dlig         (font-variant-ligatures: discretionary-ligatures)
#define LFNT_OT_FEATURES_M_DLIG 0x00000008 // -dlig         (font-variant-ligatures: no-discretionary-ligatures)
#define LFNT_OT_FEATURES_P_HLIG 0x00000010 // +hlig         (font-variant-ligatures: historical-ligatures)
#define LFNT_OT_FEATURES_M_HLIG 0x00000020 // -hlig         (font-variant-ligatures: no-historical-ligatures)

#define LFNT_OT_FEATURES_P_HIST 0x00000040 // +hist         (font-variant-alternates: historical-forms)
#define LFNT_OT_FEATURES_P_RUBY 0x00000080 // +ruby         (font-variant-east-asian: ruby)

#define LFNT_OT_FEATURES_P_SMCP 0x00000100 // +smcp         (font-variant-caps: small-caps)
#define LFNT_OT_FEATURES_P_C2SC 0x00000200 // +c2sc +smcp   (font-variant-caps: all-small-caps)
#define LFNT_OT_FEATURES_P_PCAP 0x00000400 // +pcap         (font-variant-caps: petite-caps)
#define LFNT_OT_FEATURES_P_C2PC 0x00000800 // +c2pc +pcap   (font-variant-caps: all-petite-caps)
#define LFNT_OT_FEATURES_P_UNIC 0x00001000 // +unic         (font-variant-caps: unicase)
#define LFNT_OT_FEATURES_P_TITL 0x00002000 // +titl         (font-variant-caps: titling-caps)
#define LFNT_OT_FEATURES_P_SUPS 0x00004000 // +sups         (font-variant-position: super)
#define LFNT_OT_FEATURES_P_SUBS 0x00008000 // +subs         (font-variant-position: sub)

#define LFNT_OT_FEATURES_P_LNUM 0x00010000 // +lnum         (font-variant-numeric: lining-nums)
#define LFNT_OT_FEATURES_P_ONUM 0x00020000 // +onum         (font-variant-numeric: oldstyle-nums)
#define LFNT_OT_FEATURES_P_PNUM 0x00040000 // +pnum         (font-variant-numeric: proportional-nums)
#define LFNT_OT_FEATURES_P_TNUM 0x00080000 // +tnum         (font-variant-numeric: tabular-nums)
#define LFNT_OT_FEATURES_P_ZERO 0x00100000 // +zero         (font-variant-numeric: slashed-zero)
#define LFNT_OT_FEATURES_P_ORDN 0x00200000 // +ordn         (font-variant-numeric: ordinal)
#define LFNT_OT_FEATURES_P_FRAC 0x00400000 // +frac         (font-variant-numeric: diagonal-fractions)
#define LFNT_OT_FEATURES_P_AFRC 0x00800000 // +afrc         (font-variant-numeric: stacked-fractions)

#define LFNT_OT_FEATURES_P_SMPL 0x01000000 // +smpl         (font-variant-east-asian: simplified)
#define LFNT_OT_FEATURES_P_TRAD 0x02000000 // +trad         (font-variant-east-asian: traditional)
#define LFNT_OT_FEATURES_P_FWID 0x04000000 // +fwid         (font-variant-east-asian: full-width)
#define LFNT_OT_FEATURES_P_PWID 0x08000000 // +pwid         (font-variant-east-asian: proportional-width)
#define LFNT_OT_FEATURES_P_JP78 0x10000000 // +jp78         (font-variant-east-asian: jis78)
#define LFNT_OT_FEATURES_P_JP83 0x20000000 // +jp83         (font-variant-east-asian: jis83)
#define LFNT_OT_FEATURES_P_JP04 0x40000000 // +jp04         (font-variant-east-asian: jis04)
// No more room for: (let's hope it's really the default in fonts)
// #define LFNT_OT_FEATURES_P_JP90 0x80000000 // +jp90      (font-variant-east-asian: jis90)

// Extra font metrics (cached)
enum font_extra_metric_t
{
    font_metric_x_height = 0,
    font_metric_ch_width,
    font_metric_y_superscript_y_offset,
    font_metric_y_subscript_y_offset,
#if MATHML_SUPPORT==1
    font_metric_underline_thickness,
    font_metric_math_axis_height,
    font_metric_math_fraction_rule_thickness,
    font_metric_math_fraction_numerator_shift_up,
    font_metric_math_fraction_numerator_display_style_shift_up,
    font_metric_math_fraction_numerator_gap_min,
    font_metric_math_fraction_num_display_style_gap_min,
    font_metric_math_fraction_denominator_shift_down,
    font_metric_math_fraction_denominator_display_style_shift_down,
    font_metric_math_fraction_denominator_gap_min,
    font_metric_math_fraction_denom_display_style_gap_min,
    font_metric_math_stack_top_shift_up,
    font_metric_math_stack_top_display_style_shift_up,
    font_metric_math_stack_bottom_shift_down,
    font_metric_math_stack_bottom_display_style_shift_down,
    font_metric_math_stack_gap_min,
    font_metric_math_stack_display_style_gap_min,
    font_metric_math_script_percent_scale_down,
    font_metric_math_script_script_percent_scale_down,
    font_metric_math_display_operator_min_height,
    font_metric_math_accent_base_height,
    font_metric_math_overbar_vertical_gap,
    font_metric_math_underbar_vertical_gap,
    font_metric_math_overbar_extra_ascender,
    font_metric_math_underbar_extra_descender,
    font_metric_math_upper_limit_baseline_rise_min,
    font_metric_math_upper_limit_gap_min,
    font_metric_math_stretch_stack_top_shift_up,
    font_metric_math_stretch_stack_gap_below_min,
    font_metric_math_lower_limit_baseline_drop_min,
    font_metric_math_lower_limit_gap_min,
    font_metric_math_stretch_stack_bottom_shift_down,
    font_metric_math_stretch_stack_gap_above_min,
    font_metric_math_superscript_shift_up,
    font_metric_math_superscript_shift_up_cramped,
    font_metric_math_superscript_bottom_min,
    font_metric_math_superscript_baseline_drop_max,
    font_metric_math_subscript_shift_down,
    font_metric_math_subscript_top_max,
    font_metric_math_subscript_baseline_drop_min,
    font_metric_math_sub_superscript_gap_min,
    font_metric_math_superscript_bottom_max_with_subscript,
    font_metric_math_radical_vertical_gap,
    font_metric_math_radical_display_style_vertical_gap,
    font_metric_math_radical_rule_thickness,
    font_metric_math_radical_extra_ascender,
    font_metric_math_radical_kern_before_degree,
    font_metric_math_radical_kern_after_degree,
    font_metric_math_radical_degree_bottom_raise_percent,
#endif
    FONT_METRIC_MAX
};

// Extra glyph metrics (not cached)
enum glyph_extra_metric_t
{
    glyph_metric_dummy = 0,
#if MATHML_SUPPORT==1
    glyph_metric_math_italics_correction,
    glyph_metric_math_top_accent_attachment
#endif
};

class LVDrawBuf;
class SVGGlyphsCollector;
class LVFont;

typedef LVProtectedFastRef<LVFont> LVFontRef;

/** \brief base class for fonts

    implements single interface for font of any engine
*/
class LVFont : public LVRefCounter
{
protected:
    int _visual_alignment_width;
public:
    lUInt32 _hash;
    /// glyph properties structure
    struct glyph_info_t {
        lUInt16 blackBoxX;   ///< 0: width of glyph
        lUInt16 blackBoxY;   ///< 1: height of glyph black box
        lInt16  originX;     ///< 2: X origin for glyph (left side bearing)
        lInt16  originY;     ///< 3: Y origin for glyph
        lUInt16 width;       ///< 4: full advance width of glyph
        lInt16  rsb;         ///< 5: right side bearing
    };

    /// hyphenation character
    virtual lChar32 getHyphChar() { return UNICODE_SOFT_HYPHEN_CODE; }

    /// hyphen width
    virtual int getHyphenWidth() { return getCharWidth( getHyphChar() ); }

    /**
     * Max width of -/./,/!/? to use for visial alignment by width
     */
    virtual int getVisualAligmentWidth();

    /** \brief get glyph info
        \param glyph is pointer to glyph_info_t struct to place retrieved info
        \return true if glyh was found
    */
    virtual bool getGlyphInfo( lUInt32 code, glyph_info_t * glyph, lChar32 def_char=0, bool code_is_glyph_index=false, bool is_fallback=false ) = 0;

    /** \brief add glyph SVG path to SVGGlyphsCollector
    */
    virtual bool collectGlyphSVGPath(SVGGlyphsCollector * svg_collector, lUInt32 code, bool code_is_glyph_index=false, bool is_fallback=false) { return false; }

    /** \brief get extra glyph metric
    */
    virtual bool getGlyphExtraMetric( glyph_extra_metric_t metric, lUInt32 code, int & value, bool scaled_to_px=true, lChar32 def_char=0, bool is_fallback=false ) = 0;

    /** \brief measure text
        \param text is text string pointer
        \param len is number of characters to measure
        \param max_width is maximum width to measure line
        \param def_char is character to replace absent glyphs in font
        \param letter_spacing is number of pixels to add between letters
        \param hints: hint flags (direction, begin/end of paragraph, for Harfbuzz - unrelated to font hinting)
        \return number of characters before max_width reached
    */
    virtual lUInt16 measureText(
                        const lChar32 * text, int len,
                        lUInt16 * widths,
                        lUInt8 * flags,
                        int max_width,
                        lChar32 def_char,
                        TextLangCfg * lang_cfg=NULL,
                        int letter_spacing=0,
                        bool allow_hyphenation=true,
                        lUInt32 hints=0
                     ) = 0;

    /** \brief measure text
        \param text is text string pointer
        \param len is number of characters to measure
        \return width of specified string
    */
    virtual lUInt32 getTextWidth( const lChar32 * text, int len, TextLangCfg * lang_cfg=NULL ) = 0;

    // /** \brief get glyph image in 1 byte per pixel format
    //     \param code is unicode character
    //     \param buf is buffer [width*height] to place glyph data
    //     \return true if glyph was found
    // */
    // virtual bool getGlyphImage(lUInt16 code, lUInt8 * buf, lChar32 def_char=0) = 0;

    /** \brief get glyph item
        \param code is unicode character
        \return glyph pointer if glyph was found, NULL otherwise
    */
    virtual LVFontGlyphCacheItem * getGlyph(lUInt32 ch, lChar32 def_char=0, bool is_fallback=false) = 0;

    /// returns font baseline offset
    virtual int getBaseline() = 0;
    /// returns font height including normal interline space
    virtual int getHeight() const = 0;
    /// returns font character size
    virtual int getSize() const = 0;
    /// returns font weight
    virtual int getWeight() const = 0;
    /// returns italic flag
    virtual int getItalic() const = 0;
    /// returns char glyph advance width
    virtual int getCharWidth( lChar32 ch, lChar32 def_char=0 ) = 0;
    /// returns char glyph left side bearing
    virtual int getLeftSideBearing( lChar32 ch, bool negative_only=false, bool italic_only=false ) = 0;
    /// returns char glyph right side bearing
    virtual int getRightSideBearing( lChar32 ch, bool negative_only=false, bool italic_only=false ) = 0;
    /// returns extra metric
    virtual int getExtraMetric(font_extra_metric_t metric, bool scaled_to_px=true) = 0;
    /// returns if font has OpenType Math tables
    virtual bool hasOTMathSupport() const = 0;
    /// retrieves font handle
    virtual void * GetHandle() = 0;
    /// returns font typeface name
    virtual lString8 getTypeFace() const = 0;
    /// returns font family id
    virtual css_font_family_t getFontFamily() const = 0;
    /// draws text string (returns x advance)
    virtual int DrawTextString( LVDrawBuf * buf, int x, int y,
                       const lChar32 * text, int len,
                       lChar32 def_char, lUInt32 * palette = NULL, bool addHyphen = false,
                       TextLangCfg * lang_cfg=NULL,
                       lUInt32 flags=0, int letter_spacing=0, int width=-1,
                       int text_decoration_back_gap=0,
                       int target_w=-1, int target_h=-1, SVGGlyphsCollector * svg_collector=NULL ) = 0;
    /// constructor
    LVFont() : _visual_alignment_width(-1), _hash(0) { }

    /// get bitmap mode (true=monochrome bitmap, false=antialiased)
    virtual bool getBitmapMode() { return false; }
    /// set bitmap mode (true=monochrome bitmap, false=antialiased)
    virtual void setBitmapMode( bool ) { }

    /// get OpenType features (bitmap)
    virtual int getFeatures() const { return 0; }
    /// set OpenType features (bitmap)
    virtual void setFeatures( int features ) { }

    /// sets current kerning mode
    virtual void setKerningMode( kerning_mode_t /*mode*/ ) { }
    /// returns current kerning mode
    virtual kerning_mode_t getKerningMode() const { return KERNING_MODE_DISABLED; }

    /// sets current hinting mode
    virtual void setHintingMode(hinting_mode_t /*mode*/) { }
    /// returns current hinting mode
    virtual hinting_mode_t  getHintingMode() const { return HINTING_MODE_AUTOHINT; }

    /// clear cache
    virtual void clearCache() { }

    /// returns true if font is empty
    virtual bool IsNull() const = 0;
    virtual bool operator ! () const = 0;
    virtual void Clear() = 0;
    virtual ~LVFont() { }

    virtual bool kerningEnabled() { return false; }
    // Obsolete:
    // virtual int getKerningOffset(lChar32 ch1, lChar32 ch2, lChar32 def_char) { CR_UNUSED3(ch1,ch2,def_char); return 0; }

    /// clear internal references to this font pointer
    virtual void clearFontRefs( const LVFont *ptr ) { }
    /// clear all internal font references
    virtual void clearFontRefs() { }

    /// set fallback font for this font
    virtual void setFallbackFont( LVFontRef font ) { CR_UNUSED(font); }
    /// get fallback font for this font
    LVFontRef getFallbackFont() { return LVFontRef(); }

    /// set next fallback font for this font (for when used as a fallback font)
    virtual void setNextFallbackFont( LVFontRef font ) { CR_UNUSED(font); }
    /// get next fallback font for this font (when already used as a fallback font)
    LVFontRef getNextFallbackFont() { return LVFontRef(); }

    /// get alternative font for drawing decimal list item numbers (could return same font with OTF tabular-nums)
    virtual LVFontRef getDecimalListItemFont() { return LVFontRef(this); }
    /// get alternative font for drawing disc/circle/square list item markers (could return an other font with better bullet glyphs)
    virtual LVFontRef getBulletListItemFont() { return LVFontRef(this); }
};

enum font_antialiasing_t
{
    font_aa_none,
    font_aa_big,
    font_aa_all
};

class LVEmbeddedFontDef {
    lString32 _url;
    lString8 _face;
    bool _bold;
    bool _italic;
public:
    LVEmbeddedFontDef(lString32 url, lString8 face, bool bold, bool italic) :
        _url(url), _face(face), _bold(bold), _italic(italic)
    {
    }
    LVEmbeddedFontDef() : _bold(false), _italic(false) {
    }

    const lString32 & getUrl() const { return _url; }
    const lString8 & getFace() const { return _face; }
    bool getBold() const { return _bold; }
    bool getItalic() const { return _italic; }
    void setFace(const lString8 &  face) { _face = face; }
    void setBold(bool bold) { _bold = bold; }
    void setItalic(bool italic) { _italic = italic; }
    bool serialize(SerialBuf & buf);
    bool deserialize(SerialBuf & buf);
};

class LVEmbeddedFontList : public LVPtrVector<LVEmbeddedFontDef> {
public:
    LVEmbeddedFontDef * findByUrl(lString32 url);
    void add(LVEmbeddedFontDef * def) { LVPtrVector<LVEmbeddedFontDef>::add(def); }
    bool add(lString32 url, lString8 face, bool bold, bool italic);
    bool add(lString32 url) { return add(url, lString8::empty_str, false, false); }
    bool addAll(LVEmbeddedFontList & list);
    void set(LVEmbeddedFontList & list) { clear(); addAll(list); }
    bool serialize(SerialBuf & buf);
    bool deserialize(SerialBuf & buf);
};

/// font manager interface class
class LVFontManager
{
protected:
    int _antialiasMode;
    kerning_mode_t _kerningMode;
    hinting_mode_t _hintingMode;
    int _monospaceSizeScale;
    bool _fallbackFontSizesAdjusted;
public:
    /// garbage collector frees unused fonts
    virtual void gc() = 0;
    /// returns most similar font
    virtual LVFontRef GetFont(int size, int weight, bool italic, css_font_family_t family, lString8 typeface,
                                int features=0, int documentId = -1, bool useBias=false) = 0;

    /// return available font weight values
    virtual void GetAvailableFontWeights(LVArray<int>& weights, lString8 typeface) = 0;

    /// set fallback font faces (separated by '|')
    virtual bool SetFallbackFontFaces( lString8 facesString ) { CR_UNUSED(facesString); return false; }
    /// get fallback font faces string (returns empty string if no fallback font is set)
    virtual lString8 GetFallbackFontFaces() { return lString8::empty_str; }
    /// returns fallback font for specified size
    virtual LVFontRef GetFallbackFont(int /*size*/) { return LVFontRef(); }
    /// returns fallback font for specified size, weight and italic
    virtual LVFontRef GetFallbackFont(int size, int weight=400, bool italic=false, lString8 forFaceName=lString8::empty_str ) { return LVFontRef(); }
    /// registers font by name
    virtual bool RegisterFont( lString8 name ) = 0;
    /// registers font by name and face
    virtual bool RegisterExternalFont(int /*documentId*/, lString32 /*name*/, lString8 /*face*/, bool /*bold*/, bool /*italic*/) { return false; }
    /// registers document font
    virtual bool RegisterDocumentFont(int /*documentId*/, LVContainerRef /*container*/, lString32 /*name*/, lString8 /*face*/, bool /*bold*/, bool /*italic*/) { return false; }
    /// unregisters all document fonts
    virtual void UnregisterDocumentFonts(int /*documentId*/) { }
    /// makes sure registered fonts have a proper entry at weight 400 and 700 when possible,
    /// to avoid having synthesized fonts for these normal and bold weights
    virtual void RegularizeRegisteredFontsWeights(bool print_changes=false) { }
    /// initializes font manager
    virtual bool Init( lString8 path ) = 0;
    /// get count of registered fonts
    virtual int GetFontCount() = 0;
    /// get hash of installed fonts and fallback font
    virtual lUInt32 GetFontListHash(int /*documentId*/) { return 0; }
    /// clear glyph cache
    virtual void clearGlyphCache() { }

    /// get antialiasing mode
    virtual int GetAntialiasMode() { return _antialiasMode; }
    /// set antialiasing mode
    virtual void SetAntialiasMode( int mode ) { _antialiasMode = mode; gc(); clearGlyphCache(); }

    /// get kerning mode
    virtual kerning_mode_t GetKerningMode() { return _kerningMode; }
    /// get kerning mode: true==ON, false=OFF
    virtual void SetKerningMode( kerning_mode_t mode ) { _kerningMode = mode; gc(); clearGlyphCache(); }

    /// get monospace size scale percent
    virtual int GetMonospaceSizeScale() { return _monospaceSizeScale; }
    /// set monospace size scale percent
    virtual void SetMonospaceSizeScale( int scale ) { _monospaceSizeScale = scale; gc(); clearGlyphCache(); }

    /// get adjusted fallback font sizes state
    virtual int GetFallbackFontSizesAdjusted() { return _fallbackFontSizesAdjusted; }
    /// set adjusted fallback font sizes state
    virtual void SetFallbackFontSizesAdjusted( bool adjusted ) { _fallbackFontSizesAdjusted = adjusted; gc(); }

    /// constructor
    LVFontManager() : _antialiasMode(font_aa_all), _kerningMode(KERNING_MODE_DISABLED), _hintingMode(HINTING_MODE_AUTOHINT)
                     , _monospaceSizeScale(100), _fallbackFontSizesAdjusted(false) { }
    /// destructor
    virtual ~LVFontManager() { }
    /// returns available typefaces
    virtual void getFaceList( lString32Collection & ) { }
    /// returns available font files
    virtual void getFontFileNameList( lString32Collection & ) { }
    /// returns font filename and face index for font name
    virtual bool getFontFileNameAndFaceIndex( lString32 name, bool bold, bool italic, lString8 & filename, int & index, int & family_type, bool & has_ot_math, bool & has_emojis ) { return false; }
    /// returns registered or instantiated document embedded font list
    virtual void getRegisteredDocumentFontList( int document_id, lString32Collection & list ) { }
    virtual void getInstantiatedDocumentFontList( int document_id, lString32Collection & list ) { }

    /// returns first found face from passed list, or return face for font found by family only
    virtual lString8 findFontFace(lString8 commaSeparatedFaceList, css_font_family_t fallbackByFamily);

    /// fills array with list of available gamma levels
    virtual void GetGammaLevels(LVArray<double> dst);
    /// returns current gamma level index
    virtual int  GetGammaIndex();
    /// sets current gamma level index
    virtual void SetGammaIndex( int gammaIndex );
    /// returns current gamma level
    virtual double GetGamma();
    /// sets current gamma level
    virtual void SetGamma( double gamma );

    /// sets current hinting mode
    virtual void SetHintingMode(hinting_mode_t /*mode*/) { }
    /// returns current hinting mode
    virtual hinting_mode_t  GetHintingMode() { return HINTING_MODE_AUTOHINT; }

    virtual bool SetAlias(lString8 alias,lString8 facename,int id,bool bold,bool italic){ return false;}

    /// set as preferred font with the given bias to add in CalcMatch algorithm
    virtual bool SetAsPreferredFontWithBias( lString8 face, int bias, bool clearOthersBias=true ) { CR_UNUSED(face); return false; }
};

class LVBaseFont : public LVFont
{
protected:
    lString8 _typeface;
    css_font_family_t _family;
public:
    /// returns font typeface name
    virtual lString8 getTypeFace() const { return _typeface; }
    /// returns font family id
    virtual css_font_family_t getFontFamily() const { return _family; }
    /// draws text string (returns x advance)
    virtual int DrawTextString( LVDrawBuf * buf, int x, int y,
                       const lChar32 * text, int len,
                       lChar32 def_char, lUInt32 * palette, bool addHyphen,
                       TextLangCfg * lang_cfg=NULL,
                       lUInt32 flags=0, int letter_spacing=0, int width=-1,
                       int text_decoration_back_gap=0, int target_w=-1, int target_h=-1, SVGGlyphsCollector * svg_collector=NULL );
};

#if (USE_FREETYPE!=1) && (USE_BITMAP_FONTS==1)
/* C++ wrapper class */
class LBitmapFont : public LVBaseFont
{
private:
    lvfont_handle m_font;
public:
    LBitmapFont() : m_font(NULL) { }
    virtual bool getGlyphInfo( lUInt32 code, LVFont::glyph_info_t * glyph, lChar32 def_char=0, bool code_is_glyph_index=false, bool is_fallback=false );
    virtual lUInt16 measureText(
                        const lChar32 * text, int len,
                        lUInt16 * widths,
                        lUInt8 * flags,
                        int max_width,
                        lChar32 def_char,
                        TextLangCfg * lang_cfg=NULL,
                        int letter_spacing=0,
                        bool allow_hyphenation=true,
                        lUInt32 hints=0
                     );
    /** \brief measure text
        \param text is text string pointer
        \param len is number of characters to measure
        \return width of specified string
    */
    virtual lUInt32 getTextWidth(
                        const lChar32 * text, int len, TextLangCfg * lang_cfg=NULL
        );
    virtual LVFontGlyphCacheItem * getGlyph(lUInt32 ch, lChar32 def_char=0, bool is_fallback=false);
    /// returns font baseline offset
    virtual int getBaseline();
    /// returns font height
    virtual int getHeight() const;
    /// returns font character size
    virtual int getSize() const;
    /// returns font weight
    virtual int getWeight() const;
    /// returns italic flag
    virtual int getItalic() const;

    virtual bool getGlyphImage(lUInt32 code, lUInt8 * buf, lChar32 def_char=0 );

    /// returns char width
    virtual int getCharWidth( lChar32 ch, lChar32 def_char=0 )
    {
        glyph_info_t glyph;
        if ( getGlyphInfo(ch, &glyph, def_char ) )
            return glyph.width;
        return 0;
    }
    /// returns char glyph left side bearing (not implemented)
    virtual int getLeftSideBearing( lChar32 ch, bool negative_only=false, bool italic_only=false ) { return 0; }
    /// returns char glyph right side bearing (not implemented)
    virtual int getRightSideBearing( lChar32 ch, bool negative_only=false, bool italic_only=false ) { return 0; }
    /// returns extra metric
    virtual int getExtraMetric(font_extra_metric_t metric, bool scaled_to_px=true) { return 0; }
    /// returns if font has OpenType Math tables
    virtual bool hasOTMathSupport() const { return false; }
    /// returns extra metric
    virtual bool getGlyphExtraMetric( glyph_extra_metric_t metric, lUInt32 code, int & value, bool scaled_to_px=true, lChar32 def_char=0, bool is_fallback=false ) { return false; }

    virtual lvfont_handle GetHandle() { return m_font; }

    int LoadFromFile( const char * fname );

    // LVFont functions overrides
    virtual void Clear() { if (m_font) lvfontClose( m_font ); m_font = NULL; }
    virtual bool IsNull() const { return m_font==NULL; }
    virtual bool operator ! () const { return IsNull(); }
    virtual ~LBitmapFont() {
        Clear(); // NOLINT: Call to virtual function during destruction
    }
};
#endif

#if !defined(__SYMBIAN32__) && defined(_WIN32) && USE_FREETYPE!=1
class LVBaseWin32Font : public LVBaseFont
{
protected:
    HFONT   _hfont;
    LOGFONTA _logfont;
    int     _height;
    int     _baseline;
    LVColorDrawBuf _drawbuf;

public:

    LVBaseWin32Font() : _hfont(NULL), _height(0), _baseline(0), _drawbuf(1,1)
        { }

    virtual ~LVBaseWin32Font() { Clear(); }

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

    /// retrieves font handle
    virtual void * GetHandle()
    {
        return (void*)_hfont;
    }

    /// returns char width
    virtual int getCharWidth( lChar32 ch, lChar32 def_char=0 )
    {
        glyph_info_t glyph;
        if ( getGlyphInfo(ch, &glyph, def_char) )
            return glyph.width;
        return 0;
    }
    /// returns true if font is empty
    virtual bool IsNull() const
    {
        return (_hfont == NULL);
    }

    virtual bool operator ! () const
    {
        return (_hfont == NULL);
    }

    virtual void Clear();

    virtual bool Create( const LOGFONTA & lf );

    virtual bool Create(int size, int weight, bool italic, css_font_family_t family, lString8 typeface );

    virtual int getWeight() const {
        return _logfont.lfWeight;
    }

    virtual int getItalic() const {
        return _logfont.lfItalic;
    }

    virtual lString8 getTypeFace() const {
        return lString8::empty_str;
    }

    virtual css_font_family_t getFontFamily() const {
        return css_ff_inherit;
    }

    virtual LVFontGlyphCacheItem * getGlyph(lUInt32 ch, lChar32 def_char=0, bool is_fallback=false) {
        return NULL;
    }

    virtual int getSize() const {
        return 0;
    }

};


class LVWin32DrawFont : public LVBaseWin32Font
{
private:
    int _hyphen_width;
public:

    LVWin32DrawFont() : _hyphen_width(0) { }

    /** \brief get glyph info
        \param glyph is pointer to glyph_info_t struct to place retrieved info
        \return true if glyh was found
    */
    virtual bool getGlyphInfo( lUInt32 code, glyph_info_t * glyph, lChar32 def_char=0, bool code_is_glyph_index=false, bool is_fallback=false );

    /** \brief measure text
        \param glyph is pointer to glyph_info_t struct to place retrieved info
        \return true if glyph was found
    */
    virtual lUInt16 measureText(
                        const lChar32 * text, int len,
                        lUInt16 * widths,
                        lUInt8 * flags,
                        int max_width,
                        lChar32 def_char,
                        TextLangCfg * lang_cfg=NULL,
                        int letter_spacing=0,
                        bool allow_hyphenation=true,
                        lUInt32 hints=0
                     );
    /** \brief measure text
        \param text is text string pointer
        \param len is number of characters to measure
        \return width of specified string
    */
    virtual lUInt32 getTextWidth(
                        const lChar32 * text, int len, TextLangCfg * lang_cfg=NULL
        );

    /// returns char width
    virtual int getCharWidth( lChar32 ch, lChar32 def_char=0 );

    /// draws text string (returns x advance)
    virtual int DrawTextString( LVDrawBuf * buf, int x, int y,
                       const lChar32 * text, int len,
                       lChar32 def_char, lUInt32 * palette, bool addHyphen,
                       TextLangCfg * lang_cfg=NULL,
                       lUInt32 flags=0, int letter_spacing=0, int width=-1,
                       int text_decoration_back_gap=0,
                       int target_w=-1, int target_h=-1, SVGGlyphsCollector * svg_collector=NULL );

    /** \brief get glyph image in 1 byte per pixel format
        \param code is unicode character
        \param buf is buffer [width*height] to place glyph data
        \return true if glyph was found
    */
    virtual bool getGlyphImage(lUInt32 code, lUInt8 * buf, lChar32 def_char=0);

};

struct glyph_t {
    lUInt8 *     glyph;
    lChar32      ch;
    bool         flgNotExists;
    bool         flgValid;
    LVFont::glyph_info_t gi;
    glyph_t *    next;
    glyph_t(lChar32 c)
    : glyph(NULL), ch(c), flgNotExists(false), flgValid(false), next(NULL)
    {
        memset( &gi, 0, sizeof(gi) );
    }
    ~glyph_t()
    {
        if (glyph)
            delete glyph;
    }
};

class GlyphCache
{
private:
    lUInt32 _size;
    glyph_t * * _hashtable;
public:
    GlyphCache( lUInt32 size )
    : _size(size)
    {
        _hashtable = new glyph_t * [_size];
        for (lUInt32 i=0; i<_size; i++)
            _hashtable[i] = NULL;
    }
    void clear()
    {
        for (lUInt32 i=0; i<_size; i++)
        {
            glyph_t * p = _hashtable[i];
            while (p)
            {
                glyph_t * next = p->next;
                delete p;
                p = next;
            }
            _hashtable[i] = NULL;
        }
    }
    ~GlyphCache()
    {
        if (_hashtable)
        {
            clear();
            delete _hashtable;
        }
    }
    glyph_t * find( lChar32 ch )
    {
        lUInt32 index = (((lUInt32)ch)*113) % _size;
        glyph_t * p = _hashtable[index];
        // 3 levels
        if (!p)
            return NULL;
        if (p->ch == ch)
            return p;
        p = p->next;
        if (!p)
            return NULL;
        if (p->ch == ch)
            return p;
        p = p->next;
        if (!p)
            return NULL;
        if (p->ch == ch)
            return p;
        return NULL;
    }
    /// returns found or creates new
    glyph_t * get( lChar32 ch )
    {
        lUInt32 index = (((lUInt32)ch)*113) % _size;
        glyph_t * * p = &_hashtable[index];
        // 3 levels
        if (!*p)
        {
            return (*p = new glyph_t(ch));
        }
        if ((*p)->ch == ch)
        {
            return *p;
        }
        p = &(*p)->next;
        if (!*p)
        {
            return (*p = new glyph_t(ch));
        }
        if ((*p)->ch == ch)
        {
            return *p;
        }
        p = &(*p)->next;
        if (!*p)
        {
            return (*p = new glyph_t(ch));
        }
        if ((*p)->ch == ch)
        {
            return *p;
        }

        delete (*p);
        *p = NULL;

        glyph_t * pp = new glyph_t(ch);
        pp->next = _hashtable[index];
        _hashtable[index] = pp;
        return pp;
    }

};

class LVWin32Font : public LVBaseWin32Font
{
private:
    lChar32 _unknown_glyph_index;
    GlyphCache _cache;

    static int GetGlyphIndex( HDC hdc, wchar_t code );

    glyph_t * GetGlyphRec( lChar32 ch );

public:
    /** \brief get glyph info
        \param glyph is pointer to glyph_info_t struct to place retrieved info
        \return true if glyh was found
    */
    virtual bool getGlyphInfo( lUInt32 code, glyph_info_t * glyph, lChar32 def_char=0, bool code_is_glyph_index=false, bool is_fallback=false );

    /** \brief measure text
        \param glyph is pointer to glyph_info_t struct to place retrieved info
        \return true if glyph was found
    */
    virtual lUInt16 measureText(
                        const lChar32 * text, int len,
                        lUInt16 * widths,
                        lUInt8 * flags,
                        int max_width,
                        lChar32 def_char,
                        TextLangCfg * lang_cfg=NULL,
                        int letter_spacing=0,
                        bool allow_hyphenation=true,
                        lUInt32 hints=0
                     );
    /** \brief measure text
        \param text is text string pointer
        \param len is number of characters to measure
        \return width of specified string
    */
    virtual lUInt32 getTextWidth(
                        const lChar32 * text, int len, TextLangCfg * lang_cfg=NULL
        );

    /** \brief get glyph image in 1 byte per pixel format
        \param code is unicode character
        \param buf is buffer [width*height] to place glyph data
        \return true if glyph was found
    */
    virtual bool getGlyphImage(lUInt32 code, lUInt8 * buf, lChar32 def_char=0);

    virtual void Clear();

    virtual bool Create( const LOGFONTA & lf );

    virtual bool Create(int size, int weight, bool italic, css_font_family_t family, lString8 typeface );

    LVWin32Font() : _cache(256) {  }

    virtual ~LVWin32Font() { }
};

#endif


#define LVFONT_TRANSFORM_EMBOLDEN 1
/// create transform for font
LVFontRef LVCreateFontTransform( LVFontRef baseFont, int transformFlags );

/// current font manager pointer
extern LVFontManager * fontMan;

/// initializes font manager
bool InitFontManager( lString8 path );

/// deletes font manager
bool ShutdownFontManager();

LVFontRef LoadFontFromFile( const char * fname );

/// to compare two fonts
bool operator == (const LVFont & r1, const LVFont & r2);

class SVGGlyphsCollector
{
public:
    double _orig_scale;  // provided scale factor
    double _scale;       // scale factor to work with
    double _offset_x;
    double _offset_y;
    double _advance_x;
    double _advance_y;
        // With these fields, SVGGlyphsCollector can be used directly as the "draw_data" Harfbuzz funcs forward internally

    lUInt32  _base_char; // char this glyph is for (with Harfbuzz: char at start of cluster)
    bool _can_adjust_from_previous; // false if diacritic/cluster/cursive
    lString8 _path_d;
    void addPathDfragment(const char * fragment) { _path_d << fragment; }

    explicit SVGGlyphsCollector(double scale=1) : _orig_scale(scale), _scale(scale)
    {}
    ~SVGGlyphsCollector()
    {}
    void collectGlyph(LVFont * font, lUInt32 code, bool code_is_glyph_index, lUInt32 base_char,
                                        double offset_x=0, double offset_y=0,
                                        double advance_x=0, double advance_y=0,
                                        bool can_adjust_from_previous=true)
    {
        _offset_x = offset_x;
        _offset_y = offset_y;
        _advance_x = advance_x;
        _advance_y = advance_y;
        _path_d.clear();
        _base_char = base_char;
        _can_adjust_from_previous = can_adjust_from_previous;
        font->collectGlyphSVGPath(this, code, code_is_glyph_index);
    }

    // To be overridden by subclass to do something useful with path_d (the value for <path d=""> to be used in the final SVG)
    virtual void addGlyph() = 0;
};


#endif //__LV_FNT_MAN_H_INCLUDED__
