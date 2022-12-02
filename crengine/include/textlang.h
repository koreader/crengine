#ifndef __TEXTLANG_H_INCLUDED__
#define __TEXTLANG_H_INCLUDED__

#if USE_HARFBUZZ==1
#include <hb.h>
#include <hb-ft.h>
#endif

#if USE_LIBUNIBREAK==1
    #ifdef __cplusplus
    extern "C" {
    #endif
#include <linebreak.h>
#include <linebreakdef.h>
    #ifdef __cplusplus
    }
    #endif
#endif

// Be similar to HyphMan default state with "English_US.pattern"
#define TEXTLANG_DEFAULT_MAIN_LANG              "en"   // for LVDocView
#define TEXTLANG_DEFAULT_MAIN_LANG_32           U"en"  // for textlang.cpp
#define TEXTLANG_DEFAULT_EMBEDDED_LANGS_ENABLED false
#define TEXTLANG_DEFAULT_HYPHENATION_ENABLED    true
#define TEXTLANG_DEFAULT_HYPH_SOFT_HYPHENS_ONLY false
#define TEXTLANG_DEFAULT_HYPH_FORCE_ALGORITHMIC false
#define TEXTLANG_FALLBACK_HYPH_DICT_ID  U"English_US.pattern" // For languages without specific hyph dicts


// The following CJK categorisation is only used by lvtextfm.cpp - but put here to get
// the specific SC/TC/JA typography rules in textlang.cpp and get lvtextfm generic.

// Fullwidth CJK chars categories, based on jlreq https://www.w3.org/TR/jlreq/#character_classes
// (jlreq does not mention fullwidth ascii unicode codepoints, so we'll consider
// what it mentions about ascii chars for their fullwidth Unicode equivalents)
enum cjk_type_t {
    cjkt_other = 0,       // Anything not specifically handled (keeps its initial width, can get space after/before)
    cjkt_start_of_line,
    cjkt_end_of_line,
    cjkt_ambiguous_quote, // fullwidth quotation mark or apostrophe
    cjkt_opening_bracket, // jlreq cl-01 (opening parenthesis, left quotation mark...)
    cjkt_closing_bracket, // jlreq cl-02 (closing parenthesis, right quotation mark...)
    cjkt_dividing_punct,  // jlreq cl-04 (single and double exclamation and question mark)
    cjkt_middle_dot,      // jlreq cl-05 (colon, semicolon, middle-dot)
    cjkt_full_stop,       // jlreq cl-06 (ideographic full stop, ascii fullstop)
    cjkt_comma,           // jlreq cl-07 (ideographic comma, ascii comma)
    cjkt_fullwidth_space, // jlreq cl-14 (fullwidth ideographic space)
    CJKT_MAX
    // Other jlreq classes have usually larger glyphs that aren't squeezable, so we don't handle them specifically.
};

// Width adjustment tables are defined in textlang.cpp
typedef lInt8 cjk_width_adjustment_table_t[CJKT_MAX][CJKT_MAX];

inline cjk_type_t getCJKCharType( lChar32 ch ) {
    // Generic CJK fullwidth punctuation categorization, flagging chars
    // that have their glyph blackbox width way smaller than their advance,
    // and that could have their width reduced if needed by typography.
    // This shouldn't depend on lang_cfg, but how they behave depending
    // on their context and neighbours does: this is handled by the
    // language specific cjk_width_adjustment_table_t tables used
    // by lang_cfg->getCJKWidthAdjustment(current_cjk_type, next_cjk_type).
    cjk_type_t cjk_type = cjkt_other;
    if ( ch >= 0x3000 && ch <= 0x30FB ) {
        switch (ch) {
            case 0x3000: // IDEOGRAPHIC SPACE (Zs)
                cjk_type = cjkt_fullwidth_space;
                break;
            case 0x3001: // IDEOGRAPHIC COMMA (Po)
                cjk_type = cjkt_comma;
                break;
            case 0x3002: // IDEOGRAPHIC FULL STOP (Po)
                cjk_type = cjkt_full_stop;
                break;
            case 0x30FB: // KATAKANA MIDDLE DOT (Po)
                cjk_type = cjkt_middle_dot;
                break;
            case 0x3009: // RIGHT ANGLE BRACKET (Pe)
            case 0x300B: // RIGHT DOUBLE ANGLE BRACKET (Pe)
            case 0x300D: // RIGHT CORNER BRACKET (Pe)
            case 0x300F: // RIGHT WHITE CORNER BRACKET (Pe)
            case 0x3011: // RIGHT BLACK LENTICULAR BRACKET (Pe)
            case 0x3015: // RIGHT TORTOISE SHELL BRACKET (Pe)
            case 0x3017: // RIGHT WHITE LENTICULAR BRACKET (Pe)
            case 0x3019: // RIGHT WHITE TORTOISE SHELL BRACKET (Pe)
            case 0x301B: // RIGHT WHITE SQUARE BRACKET (Pe)
            case 0x301E: // DOUBLE PRIME QUOTATION MARK (Pe)
            case 0x301F: // LOW DOUBLE PRIME QUOTATION MARK (Pe)
                cjk_type = cjkt_closing_bracket;
                break;
            case 0x3008: // LEFT ANGLE BRACKET (Ps)
            case 0x300A: // LEFT DOUBLE ANGLE BRACKET (Ps)
            case 0x300C: // LEFT CORNER BRACKET (Ps)
            case 0x300E: // LEFT WHITE CORNER BRACKET (Ps)
            case 0x3010: // LEFT BLACK LENTICULAR BRACKET (Ps)
            case 0x3014: // LEFT TORTOISE SHELL BRACKET (Ps)
            case 0x3016: // LEFT WHITE LENTICULAR BRACKET (Ps)
            case 0x3018: // LEFT WHITE TORTOISE SHELL BRACKET (Ps)
            case 0x301A: // LEFT WHITE SQUARE BRACKET (Ps)
            case 0x301D: // REVERSED DOUBLE PRIME QUOTATION MARK (Ps)
                cjk_type = cjkt_opening_bracket;
                break;
            default:
                break;
        }
    }
    else if ( ch >= 0xFF01 && ch <= 0xFF60 ) {
        switch (ch) {
            case 0xFF01: // FULLWIDTH EXCLAMATION MARK (Po)
            case 0xFF1F: // FULLWIDTH QUESTION MARK (Po)
                cjk_type = cjkt_dividing_punct;
                break;
            case 0xFF0C: // FULLWIDTH COMMA (Po)
                cjk_type = cjkt_comma;
                break;
            case 0xFF0E: // FULLWIDTH FULL STOP (Po)
                cjk_type = cjkt_full_stop;
                break;
            case 0xFF1A: // FULLWIDTH COLON (Po)
            case 0xFF1B: // FULLWIDTH SEMICOLON (Po)
                cjk_type = cjkt_middle_dot;
                break;
            case 0xFF09: // FULLWIDTH RIGHT PARENTHESIS (Pe)
            case 0xFF3D: // FULLWIDTH RIGHT SQUARE BRACKET (Pe)
            case 0xFF5D: // FULLWIDTH RIGHT CURLY BRACKET (Pe)
            case 0xFF60: // FULLWIDTH RIGHT WHITE PARENTHESIS (Pe)
                cjk_type = cjkt_closing_bracket;
                break;
            case 0xFF08: // FULLWIDTH LEFT PARENTHESIS (Ps)
            case 0xFF3B: // FULLWIDTH LEFT SQUARE BRACKET (Ps)
            case 0xFF5B: // FULLWIDTH LEFT CURLY BRACKET (Ps)
            case 0xFF5F: // FULLWIDTH LEFT WHITE PARENTHESIS (Ps)
                cjk_type = cjkt_opening_bracket;
                break;
            case 0xFF02: // FULLWIDTH QUOTATION MARK (Po)
            case 0xFF07: // FULLWIDTH APOSTROPHE (Po)
                cjk_type = cjkt_ambiguous_quote;
                break;
            default:
                break;
        }
    }
    else if ( ch >= 0x2018 && ch <= 0x201D ) {
        // These are not CJK chars, but when using CJK fonts, they may get
        // a fullwidth glyph, and we would like to handle these like the ones
        // above. This funtion will be called when measureText() detects that
        // the glyph might be fullwidth, and there are other CJK glyphs around.
        // (We checked all the non-CJK punctuation ranges with various CJK
        // fonts, and found out only these 4 ones get a fullwidth glyph.)
        switch (ch) {
            case 0x2019: // RIGHT SINGLE QUOTATION MARK (Pf)
            case 0x201D: // RIGHT DOUBLE QUOTATION MARK (Pf)
                cjk_type = cjkt_closing_bracket;
                break;
            case 0x2018: // LEFT SINGLE QUOTATION MARK (Pi)
            case 0x201C: // LEFT DOUBLE QUOTATION MARK (Pi)
                cjk_type = cjkt_opening_bracket;
                break;
            default:
                break;
        }
    }
    return cjk_type;
}


class TextLangCfg;

class TextLangMan
{
    friend class TextLangCfg;
    static lString32 _main_lang;
    static bool _embedded_langs_enabled;
    static LVPtrVector<TextLangCfg> _lang_cfg_list;

    static bool _overridden_hyph_method; // (to avoid checking the 3 following bool)
    static bool _hyphenation_enabled;
    static bool _hyphenation_soft_hyphens_only;
    static bool _hyphenation_force_algorithmic;
    static HyphMethod * _no_hyph_method;       // instance of hyphman NoHyph
    static HyphMethod * _soft_hyphens_method;  // instance of hyphman SoftHyphensHyph
    static HyphMethod * _algo_hyph_method;     // instance of hyphman AlgoHyph

    static HyphMethod * getHyphMethodForLang( lString32 lang_tag ); // Used by TextLangCfg
public:
    static void uninit();
    static lUInt32 getHash();

    static void setMainLang( lString32 lang_tag ) { _main_lang = lang_tag; }
    static void setMainLangFromHyphDict( lString32 id ); // For HyphMan legacy methods
    static lString32 getMainLang() { return _main_lang; }

    static void setEmbeddedLangsEnabled( bool enabled ) { _embedded_langs_enabled = enabled; }
    static bool getEmbeddedLangsEnabled() { return _embedded_langs_enabled; }

    static bool getHyphenationEnabled() { return _hyphenation_enabled; }
    static void setHyphenationEnabled( bool enabled ) {
        _hyphenation_enabled = enabled;
        _overridden_hyph_method = !_hyphenation_enabled || _hyphenation_soft_hyphens_only || _hyphenation_force_algorithmic;
    }

    static bool getHyphenationSoftHyphensOnly() { return _hyphenation_soft_hyphens_only; }
    static void setHyphenationSoftHyphensOnly( bool enabled ) {
        _hyphenation_soft_hyphens_only = enabled;
        _overridden_hyph_method = !_hyphenation_enabled || _hyphenation_soft_hyphens_only || _hyphenation_force_algorithmic;
    }

    static bool getHyphenationForceAlgorithmic() { return _hyphenation_force_algorithmic; }
    static void setHyphenationForceAlgorithmic( bool enabled ) {
        _hyphenation_force_algorithmic = enabled;
        _overridden_hyph_method = !_hyphenation_enabled || _hyphenation_soft_hyphens_only || _hyphenation_force_algorithmic;
    }

    static TextLangCfg * getTextLangCfg(); // get LangCfg for _main_lang
    static TextLangCfg * getTextLangCfg( lString32 lang_tag, bool force=false );
        // (frontends can provide force=true to ignore _embedded_langs_enabled=false)
    static TextLangCfg * getTextLangCfg( ldomNode * node );
    static int getLangNodeIndex( ldomNode * node );

    static HyphMethod * getMainLangHyphMethod(); // For HyphMan::hyphenate()

    static void resetCounters();

    // For frontend info about TextLangMan status and seen langs
    static LVPtrVector<TextLangCfg> * getLangCfgList() {
        return &_lang_cfg_list;
    }

    TextLangMan();
    ~TextLangMan();
};

#define MAX_NB_LB_PROPS_ITEMS 20 // for our statically sized array (increase if needed)

#if USE_LIBUNIBREAK==1
typedef lChar32 (*lb_char_sub_func_t)(struct LineBreakContext *lbpCtx, const lChar32 * text, int pos, int next_usable);
#endif

class TextLangCfg
{
    friend class TextLangMan;
    lString32 _lang_tag;
    HyphMethod * _hyph_method;

    lString32 _open_quote1;
    lString32 _close_quote1;
    lString32 _open_quote2;
    lString32 _close_quote2;
    int _quote_nesting_level;

    #if USE_HARFBUZZ==1
    hb_language_t _hb_language;
    #endif

    #if USE_LIBUNIBREAK==1
    lb_char_sub_func_t _lb_char_sub_func;
    struct LineBreakProperties _lb_props[MAX_NB_LB_PROPS_ITEMS];
    #endif

    bool _duplicate_real_hyphen_on_next_line;

    bool _is_ja_zh;
    bool _is_ja;
    bool _is_zh_TC;
    bool _is_zh_SC;
    const cjk_width_adjustment_table_t * _cjk_width_adjustment_table;

    void resetCounters();

public:
    lString32 getLangTag() const { return _lang_tag; }

    HyphMethod * getHyphMethod() const {
        if ( !TextLangMan::_overridden_hyph_method )
            return _hyph_method;
        if ( !TextLangMan::_hyphenation_enabled )
            return TextLangMan::_no_hyph_method;
        if ( TextLangMan::_hyphenation_soft_hyphens_only )
            return TextLangMan::_soft_hyphens_method;
        if ( TextLangMan::_hyphenation_force_algorithmic )
            return TextLangMan::_algo_hyph_method;
        // Should not be reached
        return _hyph_method;
    }
    HyphMethod * getDefaultHyphMethod() const {
        return _hyph_method;
    }

    lString32 & getOpeningQuote( bool update_level=true );
    lString32 & getClosingQuote( bool update_level=true );

    int getHyphenHangingPercent();
    int getHangingPercent( bool right_hanging, bool rtl_line, bool & check_font, const lChar32 * text, int pos, int next_usable );

    #if USE_HARFBUZZ==1
    hb_language_t getHBLanguage() const { return _hb_language; }
    #endif

    #if USE_LIBUNIBREAK==1
    bool hasLBCharSubFunc() const { return _lb_char_sub_func != NULL; }
    lb_char_sub_func_t getLBCharSubFunc() const { return _lb_char_sub_func; }
    struct LineBreakProperties * getLBProps() const { return (struct LineBreakProperties *)_lb_props; }
    lChar32 getCssLbCharSub(css_line_break_t css_linebreak, css_word_break_t css_wordbreak,
                struct LineBreakContext *lbpCtx, const lChar32 * text, int pos, int next_usable, lChar32 tweaked_ch);
    #endif

    bool duplicateRealHyphenOnNextLine() const { return _duplicate_real_hyphen_on_next_line; }

    int getCJKWidthAdjustment( cjk_type_t current, cjk_type_t other ) const {
        return (int)(*_cjk_width_adjustment_table)[current][other];
    }
    bool isJapanese() const { return _is_ja; }
    bool isSimplifiedChinese() const { return _is_zh_SC; }
    bool isTraditionalChinese() const { return _is_zh_TC; }

    lString32 softHyphenateText( lString32 & text, bool use_default_hyph_method=false );

    TextLangCfg( lString32 lang_tag );
    ~TextLangCfg();
};


#endif
