// IMPORTANT : when making changes in language detection logic and per-language
// rules here, be sure to also bump FORMATTING_VERSION_ID in src/lvtinydom.cpp

#include "../include/lvtypes.h"
#include "../include/lvstring.h"
#include "../include/lvtinydom.h"
#include "../include/fb2def.h"
#include "../include/textlang.h"
#include "../include/hyphman.h"

#if (USE_UTF8PROC==1)
#include <utf8proc.h>
#endif

// Uncomment to see which lang_tags are seen and lang_cfg created
// #define DEBUG_LANG_USAGE

// Check a lang tag (or its first part) against a lang (without subparts)
static bool langStartsWith(const lString32 lang_tag, const char * prefix) {
    if (!prefix || !prefix[0])
        return true;
    int prefix_len = 0;
    for (const char * p=prefix; *p; p++)
        prefix_len++;
    int lang_len = lang_tag.length();
    if ( lang_len < prefix_len )
        return false;
    const lChar32 * s1 = lang_tag.c_str();
    const lChar8 * s2 = prefix;
    for ( int i=0; i<prefix_len; i++ )
        if (s1[i] != (lChar32)s2[i])
            return false;
    if ( lang_len == prefix_len ) // "en" starts with "en"
        return true;
    if ( s1[prefix_len] == '-' ) // "en-" starts with "en", but "eno" does not
        return true;
    return false;
}

// Support `langStartsWith(lang_tag, "fr", "es")` as a shortcut for:
// `langStartsWith(lang_tag, "fr") || langStartsWith(lang_tag, "es")`.
template<typename ...Args>
static inline bool langStartsWith(const lString32 lang_tag, const char *prefix, const Args &... prefix_list) {
    return langStartsWith(lang_tag, prefix) || langStartsWith(lang_tag, prefix_list...);
}

// (hyph_filename_prefix added because CoolReader may still have both
// current "Italian.pattern" and old "Italian_hyphen_(Alan).pdb".)
// (Romanian and Ukrainian have the prefix truncated because previous
// pattern files, still in CoolReader, had these truncated names.)
static struct {
    const char * lang_tag;
    const char * hyph_filename_prefix;
    const char * hyph_filename;
    int left_hyphen_min;
    int right_hyphen_min;
} _hyph_dict_table[] = {
    { "hy",    "Armenian",      "Armenian.pattern",      1, 2 },
    { "eu",    "Basque",        "Basque.pattern",        2, 2 },
    { "bg",    "Bulgarian",     "Bulgarian.pattern",     2, 2 },
    { "ca",    "Catalan",       "Catalan.pattern",       2, 2 },
    { "cs",    "Czech",         "Czech.pattern",         2, 3 },
    { "da",    "Danish",        "Danish.pattern",        2, 2 },
    { "nl",    "Dutch",         "Dutch.pattern",         2, 2 },
    { "en-GB", "English_GB",    "English_GB.pattern",    2, 3 },
    { "en",    "English_US",    "English_US.pattern",    2, 3 },
    { "eo",    "Esperanto",     "Esperanto.pattern",     2, 2 },
    { "et",    "Estonian",      "Estonian.pattern",      2, 3 },
    { "fi",    "Finnish",       "Finnish.pattern",       2, 2 },
    { "fr",    "French",        "French.pattern",        2, 1 }, // see French.pattern file for why right_hyphen_min=1
    { "fur",   "Friulian",      "Friulian.pattern",      2, 2 },
    { "gl",    "Galician",      "Galician.pattern",      2, 2 },
    { "ka",    "Georgian",      "Georgian.pattern",      1, 2 },
    { "de",    "German",        "German.pattern",        2, 2 },
    { "el",    "Greek",         "Greek.pattern",         1, 1 },
    { "hr",    "Croatian",      "Croatian.pattern",      2, 2 },
    { "hu",    "Hungarian",     "Hungarian.pattern",     2, 2 },
    { "is",    "Icelandic",     "Icelandic.pattern",     2, 2 },
    { "ga",    "Irish",         "Irish.pattern",         2, 3 },
    { "it",    "Italian",       "Italian.pattern",       2, 2 },
    { "la-lit","Latin_liturgical","Latin_liturgical.pattern",2, 2 },
    { "la",    "Latin",         "Latin.pattern",         2, 2 },
    { "lv",    "Latvian",       "Latvian.pattern",       2, 2 },
    { "lt",    "Lithuanian",    "Lithuanian.pattern",    2, 2 },
    { "mk",    "Macedonian",    "Macedonian.pattern",    2, 2 },
    { "no",    "Norwegian",     "Norwegian.pattern",     2, 2 },
    { "oc",    "Occitan",       "Occitan.pattern",       2, 2 },
    { "pms",   "Piedmontese",   "Piedmontese.pattern",   2, 2 },
    { "pl",    "Polish",        "Polish.pattern",        2, 2 },
    { "pt-BR", "Portuguese_BR", "Portuguese_BR.pattern", 2, 3 },
    { "pt",    "Portuguese",    "Portuguese.pattern",    2, 3 },
    { "ro",    "Roman",         "Romanian.pattern",      2, 2 }, // truncated prefix (see above)
    { "rm",    "Romansh",       "Romansh.pattern",       2, 2 },
    { "ru-GB", "Russian_EnGB",  "Russian_EnGB.pattern",  2, 2 },
    { "ru-US", "Russian_EnUS",  "Russian_EnUS.pattern",  2, 2 },
    { "ru",    "Russian",       "Russian.pattern",       2, 2 },
    { "sr",    "Serbian",       "Serbian.pattern",       2, 2 },
    { "sk",    "Slovak",        "Slovak.pattern",        2, 3 },
    { "sl",    "Slovenian",     "Slovenian.pattern",     2, 2 },
    { "es",    "Spanish",       "Spanish.pattern",       2, 2 },
    { "sv",    "Swedish",       "Swedish.pattern",       2, 2 },
    { "tr",    "Turkish",       "Turkish.pattern",       2, 2 },
    { "uk",    "Ukrain",        "Ukrainian.pattern",     2, 2 }, // truncated prefix (see above)
    { "cy",    "Welsh",         "Welsh.pattern",         2, 3 },
    { "zu",    "Zulu",          "Zulu.pattern",          2, 1 }, // defaulting to 2,1, left hyphenmin might need tweaking
    // No-lang hyph methods, for legacy HyphMan methods: other lang properties will be from English
    { "en#@none",        "@none",        "@none",        2, 2 },
    { "en#@softhyphens", "@softhyphens", "@softhyphens", 2, 2 },
    { "en#@algorithm",   "@algorithm",   "@algorithm",   2, 2 },
    { "en#@dictionary",  "@dictionary",  "@dictionary",  2, 2 }, // single instance of a dict created from
                                                                 // stream (by CoolReader on Android)
    { NULL, NULL, NULL, 0, 0 }
};

// Init global TextLangMan members
lString32 TextLangMan::_main_lang = TEXTLANG_DEFAULT_MAIN_LANG_32;
bool TextLangMan::_embedded_langs_enabled = TEXTLANG_DEFAULT_EMBEDDED_LANGS_ENABLED;
LVPtrVector<TextLangCfg> TextLangMan::_lang_cfg_list;

bool TextLangMan::_hyphenation_enabled = TEXTLANG_DEFAULT_HYPHENATION_ENABLED;
bool TextLangMan::_hyphenation_soft_hyphens_only = TEXTLANG_DEFAULT_HYPH_SOFT_HYPHENS_ONLY;
bool TextLangMan::_hyphenation_force_algorithmic = TEXTLANG_DEFAULT_HYPH_FORCE_ALGORITHMIC;
bool TextLangMan::_overridden_hyph_method =   !TEXTLANG_DEFAULT_HYPHENATION_ENABLED
                                            || TEXTLANG_DEFAULT_HYPH_SOFT_HYPHENS_ONLY
                                            || TEXTLANG_DEFAULT_HYPH_FORCE_ALGORITHMIC ;
// These will be set when we can
HyphMethod * TextLangMan::_no_hyph_method = NULL;
HyphMethod * TextLangMan::_algo_hyph_method = NULL;
HyphMethod * TextLangMan::_soft_hyphens_method = NULL;

TextLangMan::TextLangMan() {
}

TextLangMan::~TextLangMan() {
}

lUInt32 TextLangMan::getHash() {
    lUInt32 hash = _main_lang.getHash();
    hash = hash << 4;
    hash = hash + (_embedded_langs_enabled << 3);
    hash = hash + (_hyphenation_soft_hyphens_only << 2);
    hash = hash + (_hyphenation_force_algorithmic << 1);
    hash = hash + _hyphenation_enabled;
    // printf("TextLangMan::getHash %x\n", hash);
    return hash;
}

// No need to explicitely call this in frontend code.
// Calling HyphMan::uninit() will have this one called.
void TextLangMan::uninit() {
}

// For HyphMan legacy methods
void TextLangMan::setMainLangFromHyphDict( lString32 id ) {
    // When setting up TextlangMan thru HyphMan legacy methods,
    // disable embedded langs, for a consistent hyphenation.
    TextLangMan::setEmbeddedLangsEnabled( false );
    // Update flags if asked for @none, @softhyphens or @algorithm
    TextLangMan::setHyphenationEnabled( id != HYPH_DICT_ID_NONE );
    TextLangMan::setHyphenationSoftHyphensOnly( id == HYPH_DICT_ID_SOFTHYPHENS );
    TextLangMan::setHyphenationForceAlgorithmic( id == HYPH_DICT_ID_ALGORITHM );

    for (int i=0; _hyph_dict_table[i].lang_tag!=NULL; i++) {
        if ( id.startsWith( _hyph_dict_table[i].hyph_filename_prefix ) ) {
            TextLangMan::setMainLang( lString32(_hyph_dict_table[i].lang_tag) );
            #ifdef DEBUG_LANG_USAGE
            printf("TextLangMan::setMainLangFromHyphDict %s => %s\n",
                UnicodeToLocal(id).c_str(), UnicodeToLocal(TextLangMan::getMainLang()).c_str());
            #endif
            return;
        }
    }
    printf("CRE WARNING: lang not found for hyphenation dict: %s\n", UnicodeToLocal(id).c_str());
}

// Used only by TextLangCfg
HyphMethod * TextLangMan::getHyphMethodForLang( lString32 lang_tag ) {
    // Look for full lang_tag
    for (int i=0; _hyph_dict_table[i].lang_tag!=NULL; i++) {
        if ( lang_tag == lString32(_hyph_dict_table[i].lang_tag).lowercase() ) {
            return HyphMan::getHyphMethodForDictionary( lString32(_hyph_dict_table[i].hyph_filename),
                        _hyph_dict_table[i].left_hyphen_min, _hyph_dict_table[i].right_hyphen_min);
        }
    }
    // Look for lang_tag initial subpart
    int m_pos = lang_tag.pos("-");
    if ( m_pos > 0 ) {
        lString32 lang_tag2 = lang_tag.substr(0, m_pos);
        for (int i=0; _hyph_dict_table[i].lang_tag!=NULL; i++) {
            if ( lang_tag2 == lString32(_hyph_dict_table[i].lang_tag).lowercase() ) {
                return HyphMan::getHyphMethodForDictionary( lString32(_hyph_dict_table[i].hyph_filename),
                            _hyph_dict_table[i].left_hyphen_min, _hyph_dict_table[i].right_hyphen_min);
            }
        }
    }
    // Fallback to English_US, as other languages are more likely to get mixed
    // with english text (it feels better than using @algorithm)
    return HyphMan::getHyphMethodForDictionary(TEXTLANG_FALLBACK_HYPH_DICT_ID);

}

// Return the (single and cached) TextLangCfg for the provided lang_tag
TextLangCfg * TextLangMan::getTextLangCfg( lString32 lang_tag, bool force ) {
    if ( !_embedded_langs_enabled && !force ) {
        // Drop provided lang_tag: always return main lang TextLangCfg
        lang_tag = _main_lang;
    }
    // Not sure if we can lowercase lang_tag and avoid duplicate (Harfbuzz might
    // need the proper lang tag with some parts starting with some uppercase letter)
    for ( int i=0; i<_lang_cfg_list.length(); i++ ) {
        if ( _lang_cfg_list[i]->_lang_tag == lang_tag ) {
            // printf("TextLangCfg %s reused\n", UnicodeToLocal(lang_tag).c_str());
            // There should rarely be more than 3 lang in a document, so move
            // any requested far down in the list at top to shorten next loops.
            if ( i > 2 ) {
                _lang_cfg_list.move(0, i);
                return _lang_cfg_list[0];
            }
            return _lang_cfg_list[i];
        }
    }
    // Not found in cache: create it
    TextLangCfg * lang_cfg = new TextLangCfg( lang_tag );
    _lang_cfg_list.add( lang_cfg ); // and cache it
    return lang_cfg;
}

TextLangCfg * TextLangMan::getTextLangCfg() {
    // No lang_tag specified: return main lang one
    return TextLangMan::getTextLangCfg( _main_lang );
}

TextLangCfg * TextLangMan::getTextLangCfg( ldomNode * node ) {
    if ( !_embedded_langs_enabled || !node ) {
        // No need to look at nodes: return main lang one
        return TextLangMan::getTextLangCfg( _main_lang );
    }
    if ( node->isText() )
        node = node->getParentNode();
    // We are usually called from renderFinalBlock() with a node that
    // we know has a lang= attribute.
    // But we may be called in other contexts (e.g. writeNodeEx) with
    // any node: so, look at this node parents for that lang= attribute.
    for ( ; !node->isRoot(); node = node->getParentNode() ) {
        if ( node->hasAttribute( attr_lang ) ) {
            lString32 lang_tag = node->getAttributeValue( attr_lang );
            if ( !lang_tag.empty() )
                return TextLangMan::getTextLangCfg( lang_tag );
        }
    }
    // No parent with lang= attribute: return main lang one
    return TextLangMan::getTextLangCfg( _main_lang );
}

int TextLangMan::getLangNodeIndex( ldomNode * node ) {
    if ( !_embedded_langs_enabled || !node ) {
        // No need to look up if !_embedded_langs_enabled
        return 0;
    }
    if ( node->isText() )
        node = node->getParentNode();
    for ( ; !node->isRoot(); node = node->getParentNode() ) {
        if ( node->hasAttribute( attr_lang ) ) {
            if ( !node->getAttributeValue( attr_lang ).empty() ) {
                return node->getDataIndex();
            }
        }
    }
    return 0;
}

// For HyphMan::hyphenate()
HyphMethod * TextLangMan::getMainLangHyphMethod() {
    return getTextLangCfg()->getHyphMethod();
}

void TextLangMan::resetCounters() {
    for ( int i=0; i<_lang_cfg_list.length(); i++ ) {
        _lang_cfg_list[i]->resetCounters();
    }
}

// TextLangCfg object: per language holder of language specificities

// For CSS "content: open-quote / close-quote"
typedef struct quotes_spec {
    const char * lang_tag;
    const lChar32 *  open_quote_level_1;
    const lChar32 * close_quote_level_1;
    const lChar32 *  open_quote_level_2;
    const lChar32 * close_quote_level_2;
} quotes_spec;

// List built 20200601 from https://html.spec.whatwg.org/multipage/rendering.html#quotes
// 2nd part of lang_tag lowercased for easier comparison, and if multiple
// lang_tag with the same starting chars, put the longest first.
// Small issue: 3-letters lang tag not specified here might match
// a 2-letter lang tag specified here ("ito" will get those from "it").
static quotes_spec _quotes_spec_table[] = {
    { "af",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "agq",      U"\x201e", U"\x201d", U"\x201a", U"\x2019" }, /* „ ” ‚ ’ */
    { "ak",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "am",       U"\x00ab", U"\x00bb", U"\x2039", U"\x203a" }, /* « » ‹ › */
    { "ar",       U"\x201d", U"\x201c", U"\x2019", U"\x2018" }, /* ” “ ’ ‘ */
    { "asa",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "ast",      U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */
    { "az-cyrl",  U"\x00ab", U"\x00bb", U"\x2039", U"\x203a" }, /* « » ‹ › */
    { "az",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "bas",      U"\x00ab", U"\x00bb", U"\x201e", U"\x201c" }, /* « » „ “ */
    { "bem",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "bez",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "be",       U"\x00ab", U"\x00bb", U"\x201e", U"\x201c" }, /* « » „ “ */
    { "bg",       U"\x201e", U"\x201c", U"\x2018", U"\x2019" }, /* „ “ ‘ ’ */
    { "bm",       U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */
    { "bn",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "brx",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "br",       U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */
    { "bs-cyrl",  U"\x201e", U"\x201c", U"\x201a", U"\x2018" }, /* „ “ ‚ ‘ */
    { "bs",       U"\x201e", U"\x201d", U"\x2018", U"\x2019" }, /* „ ” ‘ ’ */
    { "ca",       U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */
    { "cgg",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "chr",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "cs",       U"\x201e", U"\x201c", U"\x201a", U"\x2018" }, /* „ “ ‚ ‘ */
    { "cy",       U"\x2018", U"\x2019", U"\x201c", U"\x201d" }, /* ‘ ’ “ ” */
    { "dav",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "da",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "de",       U"\x201e", U"\x201c", U"\x201a", U"\x2018" }, /* „ “ ‚ ‘ */
    { "dje",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "dsb",      U"\x201e", U"\x201c", U"\x201a", U"\x2018" }, /* „ “ ‚ ‘ */
    { "dua",      U"\x00ab", U"\x00bb", U"\x2018", U"\x2019" }, /* « » ‘ ’ */
    { "dyo",      U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */
    { "dz",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "ebu",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "ee",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "el",       U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */
    { "en",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "eo",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "es",       U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */
    { "et",       U"\x201e", U"\x201c", U"\x00ab", U"\x00bb" }, /* „ “ « » */
    { "eu",       U"\x00ab", U"\x00bb", U"\x2039", U"\x203a" }, /* « » ‹ › */
    { "ewo",      U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */
    { "fa",       U"\x00ab", U"\x00bb", U"\x2039", U"\x203a" }, /* « » ‹ › */
    { "ff",       U"\x201e", U"\x201d", U"\x201a", U"\x2019" }, /* „ ” ‚ ’ */
    { "fil",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "fi",       U"\x201d", U"\x201d", U"\x2019", U"\x2019" }, /* ” ” ’ ’ */
    { "fo",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "fr-ch",    U"\x00ab", U"\x00bb", U"\x2039", U"\x203a" }, /* « » ‹ › */
    // { "fr",    U"\x00ab", U"\x00bb", U"\x00ab", U"\x00bb" }, /* « » « » */  /* Same pair for both level, bit sad... */
    { "fr",       U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */  /* Better to have "fr" just as "it" */
    { "fur",      U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */  /* Defaulting to "it", needs verification */
    { "ga",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "gd",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "gl",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "gsw",      U"\x00ab", U"\x00bb", U"\x2039", U"\x203a" }, /* « » ‹ › */
    { "guz",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "gu",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "ha",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "he",       U"\x201d", U"\x201d", U"\x2019", U"\x2019" }, /* ” ” ’ ’ */
    { "hi",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "hr",       U"\x201e", U"\x201c", U"\x201a", U"\x2018" }, /* „ “ ‚ ‘ */
    { "hsb",      U"\x201e", U"\x201c", U"\x201a", U"\x2018" }, /* „ “ ‚ ‘ */
    { "hu",       U"\x201e", U"\x201d", U"\x00bb", U"\x00ab" }, /* „ ” » « */
    { "hy",       U"\x00ab", U"\x00bb", U"\x00ab", U"\x00bb" }, /* « » « » */
    { "id",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "ig",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "is",       U"\x201e", U"\x201c", U"\x201a", U"\x2018" }, /* „ “ ‚ ‘ */
    { "it",       U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */
    { "ja",       U"\x300c", U"\x300d", U"\x300e", U"\x300f" }, /* 「 」 『 』 */
    { "jgo",      U"\x00ab", U"\x00bb", U"\x2039", U"\x203a" }, /* « » ‹ › */
    { "jmc",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "kab",      U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */
    { "kam",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "ka",       U"\x201e", U"\x201c", U"\x2018", U"\x2019" }, /* „ “ “ ” */
    { "kde",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "kea",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "khq",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "ki",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "kkj",      U"\x00ab", U"\x00bb", U"\x2039", U"\x203a" }, /* « » ‹ › */
    { "kk",       U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */
    { "kln",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "km",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "kn",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "ko",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "ksb",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "ksf",      U"\x00ab", U"\x00bb", U"\x2018", U"\x2019" }, /* « » ‘ ’ */
    { "ky",       U"\x00ab", U"\x00bb", U"\x201e", U"\x201c" }, /* « » „ “ */
    { "la-lit",   U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */  /* "la" just as "it" */
    { "la",       U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */  /* "la" just as "it" */
    { "lag",      U"\x201d", U"\x201d", U"\x2019", U"\x2019" }, /* ” ” ’ ’ */
    { "lb",       U"\x201e", U"\x201c", U"\x201a", U"\x2018" }, /* „ “ ‚ ‘ */
    { "lg",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "ln",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "lo",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "lrc",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "lt",       U"\x201e", U"\x201c", U"\x201a", U"\x2018" }, /* „ “ ‚ ‘ */
    { "luo",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "luy",      U"\x201e", U"\x201c", U"\x201a", U"\x2018" }, /* „ “ ‚ ‘ */
    { "lu",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "lv",       U"\x201c", U"\x201d", U"\x201e", U"\x201d" }, /* “ ” „ ” */
    { "mas",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "mer",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "mfe",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "mgo",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "mg",       U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */
    { "mk",       U"\x201e", U"\x201c", U"\x2019", U"\x2018" }, /* „ “ ’ ‘ */
    { "ml",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "mn",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "mr",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "ms",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "mt",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "mua",      U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */
    { "my",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "mzn",      U"\x00ab", U"\x00bb", U"\x2039", U"\x203a" }, /* « » ‹ › */
    { "naq",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "nb",       U"\x00ab", U"\x00bb", U"\x2018", U"\x2019" }, /* « » ‘ ’ */
    { "nd",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "ne",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "nl",       U"\x2018", U"\x2019", U"\x201c", U"\x201d" }, /* ‘ ’ “ ” */
    { "nmg",      U"\x201e", U"\x201d", U"\x00ab", U"\x00bb" }, /* „ ” « » */
    { "nnh",      U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */
    { "nn",       U"\x00ab", U"\x00bb", U"\x2018", U"\x2019" }, /* « » ‘ ’ */
    { "nus",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "nyn",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "oc",       U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */
    { "pa",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "pl",       U"\x201e", U"\x201d", U"\x00ab", U"\x00bb" }, /* „ ” « » */
    { "pms",      U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */  /* Defaulting to "it", needs verification */
    { "pt-br",    U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "pt-pt",    U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */
    { "pt",       U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */
    { "rm",       U"\x00ab", U"\x00bb", U"\x2039", U"\x203a" }, /* « » ‹ › */
    { "rn",       U"\x201d", U"\x201d", U"\x2019", U"\x2019" }, /* ” ” ’ ’ */
    { "rof",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "ro",       U"\x201e", U"\x201d", U"\x00ab", U"\x00bb" }, /* „ ” « » */
    { "ru",       U"\x00ab", U"\x00bb", U"\x201e", U"\x201c" }, /* « » „ “ */
    { "rwk",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "rw",       U"\x00ab", U"\x00bb", U"\x2018", U"\x2019" }, /* « » ‘ ’ */
    { "sah",      U"\x00ab", U"\x00bb", U"\x201e", U"\x201c" }, /* « » „ “ */
    { "saq",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "sbp",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "seh",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "ses",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "sg",       U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */
    { "shi-latn", U"\x00ab", U"\x00bb", U"\x201e", U"\x201d" }, /* « » „ ” */
    { "shi",      U"\x00ab", U"\x00bb", U"\x201e", U"\x201d" }, /* « » „ ” */
    { "si",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "sk",       U"\x201e", U"\x201c", U"\x201a", U"\x2018" }, /* „ “ ‚ ‘ */
    { "sl",       U"\x201e", U"\x201c", U"\x201a", U"\x2018" }, /* „ “ ‚ ‘ */
    { "sn",       U"\x201d", U"\x201d", U"\x2019", U"\x2019" }, /* ” ” ’ ’ */
    { "so",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "sq",       U"\x00ab", U"\x00bb", U"\x201c", U"\x201d" }, /* « » “ ” */
    { "sr-latn",  U"\x201e", U"\x201c", U"\x2018", U"\x2018" }, /* „ “ ‘ ‘ */
    { "sr",       U"\x201e", U"\x201d", U"\x2019", U"\x2019" }, /* „ ” ’ ’ */
    { "sv",       U"\x201d", U"\x201d", U"\x2019", U"\x2019" }, /* ” ” ’ ’ */
    { "sw",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "ta",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "teo",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "te",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "th",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "ti-er",    U"\x2018", U"\x2019", U"\x201c", U"\x201d" }, /* ‘ ’ “ ” */
    { "tk",       U"\x201c", U"\x201d", U"\x201c", U"\x201d" }, /* “ ” “ ” */
    { "to",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "tr",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "twq",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "tzm",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "uk",       U"\x00ab", U"\x00bb", U"\x201e", U"\x201c" }, /* « » „ “ */
    { "ur",       U"\x201d", U"\x201c", U"\x2019", U"\x2018" }, /* ” “ ’ ‘ */
    { "uz-cyrl",  U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "uz",       U"\x201c", U"\x201d", U"\x2019", U"\x2018" }, /* “ ” ’ ‘ */
    { "vai-latn", U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "vai",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "vi",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "vun",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "xog",      U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "yav",      U"\x00ab", U"\x00bb", U"\x00ab", U"\x00bb" }, /* « » « » */
    { "yo",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "yue-hans", U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "yue",      U"\x300c", U"\x300d", U"\x300e", U"\x300f" }, /* 「 」 『 』 */
    { "zgh",      U"\x00ab", U"\x00bb", U"\x201e", U"\x201d" }, /* « » „ ” */
    { "zh-hant",  U"\x300c", U"\x300d", U"\x300e", U"\x300f" }, /* 「 」 『 』 */
    { "zh",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { "zu",       U"\x201c", U"\x201d", U"\x2018", U"\x2019" }, /* “ ” ‘ ’ */
    { NULL, NULL, NULL, NULL, NULL }
};
// Default to quotes for English
static quotes_spec _quotes_spec_default = { "", U"\x201c", U"\x201d", U"\x2018", U"\x2019" };

#if USE_LIBUNIBREAK==1
lChar32 lb_char_sub_func_english(struct LineBreakContext *lbpCtx, const lChar32 * text, int pos, int next_usable) {
    // https://github.com/koreader/crengine/issues/364
    // Normally, line breaks are allowed at both sides of an em-dash.
    // When an em-dash is at the "end of a word" (or beginning), we want to avoid separating it from its word,
    // this is detected by looking for letters/numbers at both sides of the dash, if on any side a space
    // is closer than any letter/number, treat it as a non-breakable dash.
    // The current implementation does not allow examining the following characters beyond the current node,
    // so the detection is not perfect and we replace the dash with "opening" or "closing" characters
    // (or "ambiguous), to play safer (note that "}" allows a break after, while ")" doesn't).
    //
    // The intent is the following:
    //   blah—blah                     ->  —  (break before or after)
    //   blah “—blah , <p>—blah        ->  {  (do not break after)
    //   blah—” Blah , blah—”</p>      ->  }  (do not break before)
    //   blah — blah , blah —<em>blah  ->  "  (break only at spaces)
    switch ( text[pos] ) {
        case 0x2014:  // em dash
        case 0x2E3A:  // two-em dash
        case 0x2E3B:  // three-em dash
            {
                // The variable "replacement" will be the output char,
                // we start by setting it to the actual input char.
                // It will be '{' if no-break on right,
                //            '}' if no-break on left,
                //            '"' if no-break on both.
                lChar32 replacement = text[pos];
                int new_pos;
                enum LineBreakClass new_lbc;
                // 1. Detect no-break on right (scan left of dash)
                //
                // already at the beginning of text
                if ( pos == 0 ) {
                    replacement = '{';
                }
                else {
                    // inspect preceding characters
                    new_pos = pos;
                    while ( new_pos > 0) {
                        new_pos--;
                        new_lbc = lb_get_char_class(lbpCtx, text[new_pos]);
                        if ( new_lbc == LBP_AL || new_lbc == LBP_NU ) {
                            // found word / number
                            break;
                        }
                        else if ( new_lbc == LBP_SP || new_pos == 0 ) {
                            // found space or beginning
                            replacement = '{';
                            break;
                        }
                    }
                }
                // 2. Detect no-break on left (scan right of dash)
                //    If already no-break on right, replacement will be '"'
                //
                // already at the end of text
                if ( next_usable == 0 ) {
                    replacement = ( replacement == '{' ) ? '"' : '}';
                }
                else {
                    // inspect following characters
                    new_pos = pos;
                    while ( new_pos < pos+next_usable ) {
                        new_pos++;
                        new_lbc = lb_get_char_class(lbpCtx, text[new_pos]);
                        if ( new_lbc == LBP_AL || new_lbc == LBP_NU ) {
                            // found word / number
                            break;
                        }
                        else if ( new_lbc == LBP_SP || new_pos == pos+next_usable ) {
                            // found space or end (of the current text node, there could be letters beyond)
                            replacement = ( replacement == '{' ) ? '"' : '}';
                            break;
                        }
                    }
                }
                return replacement;
            }
            break;
        default:
            break;
    }
    return text[pos];
}

lChar32 lb_char_sub_func_polish(struct LineBreakContext *lbpCtx, const lChar32 * text, int pos, int next_usable) {
    // https://github.com/koreader/koreader/issues/5645#issuecomment-559193057
    // Letters aiouwzAIOUWS are prepositions that should not be left at the
    // end of a line.
    // Make them behave (for libunibreak) just like a opening paren (which
    // being LBC_OP, will prevent a line break after it, even if followed
    // by a space).
    if ( pos >= 1 && text[pos-1] == ' ' ) {
        switch ( text[pos] ) {
            case 'A':
            case 'I':
            case 'O':
            case 'U':
            case 'W':
            case 'Z': // Meaning in english:
            case 'a': // and
            case 'i': // and
            case 'o': // about
            case 'u': // at
            case 'w': // in
            case 'z': // with
                return '(';
                break;
            default:
                break;
        }
    }
    return text[pos];
}

lChar32 lb_char_sub_func_czech_slovak(struct LineBreakContext *lbpCtx, const lChar32 * text, int pos, int next_usable) {
    // Same for Czech and Slovak : AIiVvOoUuSsZzKk
    // https://tex.stackexchange.com/questions/27780/one-letter-word-at-the-end-of-line
    // https://github.com/michal-h21/luavlna
    if ( pos >= 1 && text[pos-1] == ' ' ) {
        switch ( text[pos] ) {
            case 'A':
            case 'I':
            case 'K':
            case 'O':
            case 'S':
            case 'U':
            case 'V':
            case 'Z':
            case 'i':
            case 'k':
            case 'o':
            case 's':
            case 'u':
            case 'v':
            case 'z':
                return '(';
                break;
            default:
                break;
        }
    }
    return text[pos];
}

lChar32 lb_char_sub_func_russian(struct LineBreakContext *lbpCtx, const lChar32 * text, int pos, int next_usable) {
    // Russian typography doesn't recommend one letter prepositions and conjunctions at the end of the line.
    // https://www.artlebedev.ru/kovodstvo/sections/62/
    if ( pos >= 1 && text[pos-1] == ' ' ) {
        switch ( text[pos] ) {
            case 0x0410: // "А"
            case 0x0412: // "В"
            case 0x0418: // "И"
            case 0x041a: // "К"
            case 0x041e: // "О"
            case 0x0421: // "С"
            case 0x0423: // "У" (Meaning in English:)
            case 0x0430: // "а" ("but")
            case 0x0432: // "в" ("in")
            case 0x0438: // "и" ("and")
            case 0x043a: // "к" ("towards")
            case 0x043e: // "о" ("about")
            case 0x0441: // "с" ("with")
            case 0x0443: // "у" ("at")
                return '(';
                break;
            default:
                break;
        }
    }
    return text[pos];
}

// (Mostly) non-language specific char substitution to ensure CSS line-break and word-break properties
//
// Note: the (hardcoded in many places) default behaviour (without these tweaks) in crengine
// resembles "word-break: break-word; overflow-wrap: break-word", that is: we don't let text
// overflow their container. So, we don't differentiate "word-break: normal" from "break-word",
// and we don't handle "overflow-wrap" (normal/break-word/anywhere).
//
// Specs:
//   https://drafts.csswg.org/css-text-3/#word-break-property
//   https://drafts.csswg.org/css-text-3/#line-break-property
// Also see:
//   https://florian.rivoal.net/talks/line-breaking/
// Rust implementation of UAX#14 and these CSS properties:
//   https://github.com/makotokato/uax14_rs
// WebKit:
//   https://bugs.webkit.org/show_bug.cgi?id=89235
//   https://trac.webkit.org/changeset/176473/webkit
//   https://trac.webkit.org/wiki/LineBreakingCSS3Mapping
// Firefox:
//   https://bugzilla.mozilla.org/show_bug.cgi?id=1011369
//   https://bugzilla.mozilla.org/show_bug.cgi?id=1531715
//   https://hg.mozilla.org/integration/autoland/rev/436e3199c386
//   https://hg.mozilla.org/releases/mozilla-esr78/file/tip/intl/lwbrk/LineBreaker.cpp
//
// Most of these implementation handles line breaking themselves, which we do not
// exactly do here: we just masquerade an original char with another one with
// different line-breaking properties, before invoking libunibreak pure UAX#14
// implementation, expecting this to exhibit the wished behaviour.
// Below, we mostly always use a random LBP_ID (CJK ideographic char) that should
// break on both sides: this allows breaking on the unbreakable side, but it may
// change the original LB class behaviour on the other side...
//
lChar32 TextLangCfg::getCssLbCharSub(css_line_break_t css_linebreak, css_word_break_t css_wordbreak,
                struct LineBreakContext *lbpCtx, const lChar32 * text, int pos, int next_usable, lChar32 tweaked_ch) {
    // "line-break: anywhere" has precedence over everything
    if ( css_linebreak == css_lb_anywhere ) {
        return 0x5000; // Random CJK ideographic character LBP_ID
                       // Everything becoming ID, it can break anywhere
    }
    lChar32 ch = tweaked_ch ? tweaked_ch : text[pos];
    enum LineBreakClass lbc = lb_get_char_class(lbpCtx, ch);
    if ( css_wordbreak == css_wb_break_all ) {
        if ( lbc == LBP_AI || lbc == LBP_AL || lbc == LBP_NU || lbc == LBP_SA ) {
            // Alphabetic, ambiguous (ID in CJK lang, AL otherwise) numeric, south-east asian:
            // treated as ideographic
            // (Note: Firefox includes others: CJ H2 H3 JL JT JV
            return 0x5000; // Random CJK ideographic character LBP_ID
        }
    }
    else if ( css_wordbreak == css_wb_keep_all ) {
	// A char of classes AI AL ID NU HY H2 H3 JL JV JT CJ shouldn't break
        // between another char of any of these class.
        // (Note: uax14_rs includes LBP_HY but Firefox does not)
        // Feels like treating a char of these classes just as AL
        if ( lbc == LBP_AI || lbc == LBP_AL || lbc == LBP_ID || lbc == LBP_NU || lbc == LBP_HY || lbc == LBP_H2 ||
                    lbc == LBP_H3 || lbc == LBP_JL || lbc == LBP_JV || lbc == LBP_JT || lbc == LBP_CJ ) {
            return 0x41; // 'A' LBP_AL
        }
    }
    if ( css_linebreak > css_lb_auto ) {
        // Following rules from https://drafts.csswg.org/css-text-3/#line-break-property:

        // "The following breaks are forbidden in strict line breaking and allowed in normal and loose:
        // - breaks before Japanese small kana or the Katakana-Hiragana prolonged sound mark,
        // i.e. character from the Unicode line breaking class CJ [UAX14]."
        if ( css_linebreak == css_lb_strict && lbc == LBP_CJ) {
            // Conditional Japanese Starter => Nonstarter (as done by libunibreak itself
            // when the lang tag ends with "-strict')
            return 0x2047; // '⁇' LBP_NS
        }

        // "The following breaks are allowed for normal and loose line breaking if the writing
        // system is Chinese or Japanese, and are otherwise forbidden:
        // - breaks before certain CJK hyphen-like characters: U+301C,  U+30A0"
        if ( _is_ja_zh && css_linebreak != css_lb_strict && ( ch==0x301C || ch==0x30A0 ) ) {
            // By default, libunibreak considers them LBP_NS (Nonstarter, non breakable before).
            // https://unicode.org/reports/tr14/#NS: "Optionally, the NS restriction may be relaxed
            // by tailoring, with some or all characters treated like ID to achieve a more permissive
            // style of line breaking, especially in some East Asian document styles"
            return 0x5000; // Random CJK ideographic character LBP_ID
        }

        // "The following breaks are allowed for loose line breaking if the preceding character
        // belongs to the Unicode line breaking class ID [UAX14] (including when the preceding
        // character is treated as ID due to word-break: break-all), and are otherwise forbidden:
        // - breaks before hyphens: U+2010, U+2013"
        if ( css_linebreak == css_lb_loose && pos > 1 && ( ch==0x2010 || ch==0x2013 ) ) {
            // By default, libunibreak considers them LBP_NS (Nonstarter, non breakable before).
            enum LineBreakClass plbc = lb_get_char_class(lbpCtx, text[pos-1]);
            if ( plbc == LBP_ID || (css_wordbreak == css_wb_break_all &&
                                    ( plbc == LBP_AI || plbc == LBP_AL || plbc == LBP_NU || plbc == LBP_SA )) ) {
                return 0x5000; // Random CJK ideographic character LBP_ID
            }
        }

        // "The following breaks are forbidden for normal and strict line breaking and allowed in loose:
        // - breaks before iteration marks: U+3005, U+303B, U+309D, U+309E, U+30FD, U+30FE
        // - breaks between inseparable characters (such as U+2025, U+2026) i.e. characters
        //   from the Unicode line breaking class IN [UAX14]. "
        if ( css_linebreak == css_lb_loose && ( ch==0x3005 || ch==0x303B || ch==0x309D ||
                                                ch==0x309E || ch==0x30FD || ch==0x30FE ||
                                                lbc == LBP_IN ) ) {
            // By default, libunibreak considers these codepoints LBP_NS (Nonstarter, non breakable before).
            // LBP_IN are inseparable from other of the same class, so making them ID should allow breaking.
            return 0x5000; // Random CJK ideographic character LBP_ID
        }

        // "The following breaks are allowed for loose if the writing system is Chinese or Japanese
        // and are otherwise forbidden:
        // - breaks before certain centered punctuation marks: U+30FB, U+FF1A, U+FF1B, U+FF65, U+203C,
        //   U+2047, U+2048, U+2049, U+FF01, U+FF1F
        // - breaks before suffixes: Characters with the Unicode line breaking class PO [UAX14]
        //   and the East Asian Width property [UAX11] Ambiguous, Fullwidth, or Wide.
        // - breaks after prefixes: Characters with the Unicode line breaking class PR [UAX14]
        //   and the East Asian Width property [UAX11] Ambiguous, Fullwidth, or Wide."
        if ( _is_ja_zh && css_linebreak == css_lb_loose ) {
            // By default, libunibreak considers these codepoints LBP_NS (Nonstarter, non breakable before)
            // or (the last 2 ones) LBP_EX (Exclamation, Interrogation, Prohibit line breaks before)
            if ( ch==0x30FB || ch==0xFF1A || ch==0xFF1B || ch==0xFF65 ||
                 ch==0x203C || ch==0x2047 || ch==0x2048 || ch==0x2049 ||
                 ch==0xFF01 || ch==0xFF1F ) {
                return 0x5000; // Random CJK ideographic character LBP_ID
            }
            // For the 2 last cases, we need utf8proc to know the East Asian Width property
            #if (USE_UTF8PROC==1)
            if ( lbc == LBP_PO || lbc == LBP_PR ) {
                // LBP_PO are Postfix Numeric ("do not break following a numeric expression")
                // LBP_PR are Prefix Numeric ("do not break in front of a numeric expression")
                if ( utf8proc_charwidth(ch) == 2 ) {
                    // Note: utf8proc returns 2 for Fullwidth and Wide, 1 or 0 otherwise.
                    // It may return 1 for "Ambiguous", so we may not handle this case well.
                    return 0x5000; // Random CJK ideographic character LBP_ID
                }
            }
            #endif
        }

        // Private keyword: "line-break: -cr-loose", to ignore no-break-space (some books
        // may abuse &nbsp; between words like article and noun, possibly to make it easier
        // for people with reading difficulties)
        if ( css_linebreak == css_lb_cr_loose ) {
            if ( ch==0x00A0 ) return ' ';    // non-breaking space => space
            if ( ch==0x2011 ) return 0x2010; // non-breaking hyphen => hyphen
        }
    }
    return ch;
}
#endif

// CJK fullwidth chars' width adjustment tables, allowing to reduce some punctuations'
// width depending on neighbours and context, and the language typography rules.
// We try to ensure the spirit of these specs, while doing it in much more limited
// and simple ways:
// https://www.w3.org/TR/clreq/
// https://www.w3.org/TR/jlreq/
//
// (https://www.w3.org/TR/klreq/ Korean uses mostly western punctuation, so does
// not need any specific handling - the default Simplified Chinese rules will be
// used in case CJK punctuation is met)
//

// These tables Define the width adjustment possible for a char (CURRENT) depending on
// its cjk_type_t and the one of the following char (NEXT) - except for opening_bracket
// which depends on the type of the preceding char.
// The unit is in 1/8em. 8 means no change to nominal width.
// A positive value means the nominal width (8) *should* be reduced to this value.
// A negative value means the nominal width (8) *can* be reduced to this value (made positive)
// if needed to add more chars on a line and avoid text justification to spread out glyphs
// too much.
// (0 means not applicable: no chance to meet this combination.)
// For all categories, the 2nd indice is the category of the next char, EXCEPT for opening_bracket
// where what should be checked is the previous char.
// Note: in the tables below, we keep 8 and 0 (width unchanged) shifted a bit away to make
// it easier to see what combinations are tweaked from the default.

// These tables assume the glyphs obtained from the font will be for the language, either
// via OpenType locl feature, or by using a non-OpenType font for the language.
// Larger TC glyphs (like '?') may look compressed if SC typography make them all be halfwidth.
// However, lvfntman doing what's best from the glyph (ie. keeping TC punctuations centered)
// may help making a mismatch bearable.
// Users wanting another kind of typography rules (ie. while reading SC, prefering to have
// larger spacing as provided by TC) can use a one-language font (without OpenType locl
// features) and chose the other language for typography.

// Simplified Chinese (SC), and the default for all other languages when meeting CJK.
// Specificities:
// - all SC glyphs in these categories are left aligned in their glyphs (except opening_bracket
//   which are right aligned), and when cutting a glyph by half, we won't change the position of
//   the glyph vs. the preceding character, which allows us to be quite aggressive (4 or -4).
// - all these are allowed to be halfwidth, and should be halfwidth at start and end of line.
// - consecutive opening punctuations should be halfwidth (except the first, that can be halfwidth)
// - consecutive non-opening punctuations should be halfwidth (except the last, that can be halfwidth)
// Implementation:
// - we make non-opening punctuation followed by an opening bracket be 6. We ensure the 1/2em blank
//   between them by making the opening bracket also be 6. First idea was to make the closing 4 and
//   the opening 8, but it may happen that one of the bracket/quote is not fullwidth, and would then
//   not ensure its part of the 1/2em blank: with 6, we ensure that at least 1/4em blank is there.
// - for ambiguous quotes, which have their glyph centered, we keep them full width, but make a preceding
//   punctuation forced halfwidth, as 1/4em space will be ensured by the fullwidth ambiguous space
//   on both sides (as we don't know which side it opens or closes). Note that line breaking won't be
//   allowed on any side of it, as the UAX14 algorithm also don't know which side it opens/closes.
static const cjk_width_adjustment_table_t simplified_chinese_cjk_width_adjustment_table = {
/* CURRENT\/  NEXT >     other start  end  ambiguous" opening[ closing] dividing? middle: fullstop. comma, fullspace */
/* other             */ {   8 ,   0 ,  8 ,        8 ,      8 ,      8 ,       8 ,     8 ,       8 ,    8 ,        8  }, // never checked
/* start_of_line     */ {   0 ,   0 ,  0 ,        0 ,      0 ,      0 ,       0 ,     0 ,       0 ,    0 ,        0  }, // n/a
/* end_of_line       */ {   0 ,   0 ,  0 ,        0 ,      0 ,      0 ,       0 ,     0 ,       0 ,    0 ,        0  }, // n/a
/* ambiguous_quote " */ {   8 ,   0 ,  8 ,        8 ,      8 ,      8 ,       8 ,     8 ,       8 ,    8 ,        8  },
/* opening_bracket [ */ {   -4,    4,  0 ,         4,       4,       6,        6,      6,        6,     6,        -4 }, // vs. PREVIOUS
/* closing_bracket ] */ {   -4,   0 ,   4,         4,       6,       4,        4,      4,        4,     4,        -4 },
/* dividing_punct !? */ {   -4,   0 ,   4,         4,       6,       4,        4,      4,        4,     4,        -4 },
/* middle_dot     :; */ {   -4,   0 ,   4,         4,       6,       4,        4,      4,        4,     4,        -4 },
/* full_stop       . */ {   -4,   0 ,   4,         4,       6,       4,        4,      4,        4,     4,        -4 },
/* comma           , */ {   -4,   0 ,   4,         4,       6,       4,        4,      4,        4,     4,        -4 },
/* fullwidth_space   */ {   8 ,   0 ,  8 ,        8 ,      8 ,      8 ,       8 ,     8 ,       8 ,    8 ,        8  },
//                               n/a
};

// Traditional Chinese (TC)
// Specificities:
// - clreq does not say much about how different things should be from SC
// - all TC glyphs (except opening and closing brackets/quotation marks) are centered in their glyph,
//   and are better kept fullwidth to keep this feeling of centering (if reduced, they would still
//   be centered, but compressed in a rectangle instead of a square)
// Implementation:
// - we could do the same things as SC and just be less radical with some glyphs (like ? or fullstop),
//   but let's take a more "traditional" approach: keep all glyphs fullwidth (even consecutive opening
//   or closing punctuations), also at start and end of line, but allow most to be -6 and a tad smaller
//   only when required to fit more chars to allow nicer text justification.
// - we allow -4 (a bit more reduction than -6) on left/right-aligned glyphs (closing/opening_bracket)
//   at start or end or when followed by another punctuation (but not near "other" to keep some spacing)
static const cjk_width_adjustment_table_t traditional_chinese_cjk_width_adjustment_table = {
/* CURRENT\/  NEXT >     other start  end  ambiguous" opening[ closing] dividing? middle: fullstop. comma, fullspace */
/* other             */ {   8 ,   0 ,  8 ,        8 ,      8 ,      8 ,       8 ,     8 ,       8 ,    8 ,        8  }, // never checked
/* start_of_line     */ {   0 ,   0 ,  0 ,        0 ,      0 ,      0 ,       0 ,     0 ,       0 ,    0 ,        0  }, // n/a
/* end_of_line       */ {   0 ,   0 ,  0 ,        0 ,      0 ,      0 ,       0 ,     0 ,       0 ,    0 ,        0  }, // n/a
/* ambiguous_quote " */ {   -6,   0 ,  -6,        -6,      -6,      -6,       -6,     -6,       -6,    -6,        -6 },
/* opening_bracket [ */ {   -6,   -4,  0 ,        -4,      -4,      -6,       -4,     -4,       -4,    -4,        -4 }, // vs. PREVIOUS
/* closing_bracket ] */ {   -6,   0 ,  -4,        -4,      -6,      -4,       -4,     -4,       -4,    -4,        -4 },
/* dividing_punct !? */ {   -6,   0 ,  -6,        -6,      -6,      -6,       -6,     -6,       -6,    -6,        -6 },
/* middle_dot     :; */ {   -6,   0 ,  -6,        -6,      -6,      -6,       -6,     -6,       -6,    -6,        -6 },
/* full_stop       . */ {   -6,   0 ,  -6,        -6,      -6,      -6,       -6,     -6,       -6,    -6,        -6 },
/* comma           , */ {   -6,   0 ,  -6,        -6,      -6,      -6,       -6,     -6,       -6,    -6,        -6 },
/* fullwidth_space   */ {   8 ,   0 ,  8 ,        8 ,      8 ,      8 ,       8 ,     8 ,       8 ,    8 ,        8  },
//                               n/a
};

// Japanese
// Specificities:
// - jlreq has many rules (and tables more complex than ours) to define what/how things
//   can or should be reduced - and even different and contradictory set of rules
//   (W3C Table 3, JISX4051 Table 4, "as on books" Table 5)
// - jlreq and its tables work with the idea that these characters are haldwidth (except ?!),
//   and define how much space (0=solid, 1/4em, 1/2em) can be added on each side.
//   We work (as do the fonts) with fullwidth glyphs, and should decide how much we can
//   reduce their width. We also don't handle each side independantly: the shifting of the
//   fullwidth glyph inside the reduced width is handled by lvfntman's DrawTextString(),
//   and we'll get a glyph left or center or right aligned in the reduced width whether
//   it is originally left or center or right aligned in its original glyph (for a
//   centered glyph, we'll have the same spacing on each side, unlike the different
//   fine tuning jlreq may advise for each side).
//   It is *really hard* to map from these tables to our way of doing it...
// - full stop and commas are left-aligned, but ;:?! are centered in their glyph
// - full stop should stay fullwidth at end of line (but reduction allowed in some spec),
//   but commas should be halfwidth (in some rules) or can be halfwidth (in some other rules)
// - full stop and commas should be halfwidth when followed by a closing bracket
// - ; and ; are centered aligned, but considered halfwidth with reduction allowed on both sides
// - ? and ! are centered aligned, but consideded fullwidth (no space reduction allowed)
// - jlreq says reduction (removing the blank at the right of left aligned punctuation) should first
//   happen at end of line, and only then, if more is needed, in the middle of a line.
// Implementation:
// - fullstop stays fullwidth in the middle of line (except when followed by a closing bracket):
//   we set it -6 to allow stealing some space if required; -4 at end of line as it is allowed
//   to be halfwidth by some specs
// - comma and closing brackets at end of line can either be made forced halfwidth or variable:
//   one specs (JIS X 4051) advises always halfwidth (which feels more natural to the westerner
//   reader), while others advise "1/2 or 0" added spacing, with explicitely no value in between.
//   Let's go with "1/2 or 0" by using '-4'.
//     NOTE: 'comma' and 'closing_bracket' vs. 'end' (3rd column) could be made '4' instead of '-4'
//     if the holes it causes near the right margin really feel odd to Japanese readers (as they
//     do to me) and it feels better to go with the JIS X 4051 rules.
// - For these and the "1/2 or 0 but nothing in between", we can use -4 or -6: some bits of code
//   in lvtextfm.cpp's alignLine() will ensure the "nothing in between" when reduction is needed
//   for the line and when the last word is a flexible CJK with a min_width smaller than width.
// - ?! stays fullwidth: we yet set them -6 to allow stealing a bit of space if needed.
// - ;: stays fullwitdh but are allowed to be halfwidth, even at end of line. Also, their reduction
//   has a higher priority than others, so we can go with -4.
// - The spacing before an opening bracket is allowed to be 0 (except after fullstop), or 1/4em in
//   the "books" spec: we set them to -6 to get a visible blank of 1/4em.
// - The spacing after a closing bracket is allowed to be 0 (except after fullstop), or 1/4em in
//   the "books" spec: we set them to -6 to get a visible blank of 1/4em.
//   Except before another closing bracket, a comma or fullstop, where it is solid (4).
// - We make fullstop and comma before opening bracket be -6, as it's possible the opening bracket
//   is not CJK (especially with Japanese where some CJK quotation marks glyphs are not fullwidth,
//   so the opening bracket -6 would not trigger and not add the leading 1/4em)
static const cjk_width_adjustment_table_t japanese_cjk_width_adjustment_table = {
/* CURRENT\/  NEXT >     other start  end  ambiguous" opening[ closing] dividing? middle: fullstop. comma, fullspace */
/* other             */ {   8 ,   0 ,  8 ,        8 ,      8 ,      8 ,       8 ,     8 ,       8 ,    8 ,        8  }, // never checked
/* start_of_line     */ {   0 ,   0 ,  0 ,        0 ,      0 ,      0 ,       0 ,     0 ,       0 ,    0 ,        0  }, // n/a
/* end_of_line       */ {   0 ,   0 ,  0 ,        0 ,      0 ,      0 ,       0 ,     0 ,       0 ,    0 ,        0  }, // n/a
/* ambiguous_quote " */ {   8 ,   0 ,  8 ,        8 ,      8 ,      8 ,       8 ,     8 ,       8 ,    8 ,        8  },
/* opening_bracket [ */ {   -6,    4,  0 ,         4,       4,       6,       -6,     -6,        4,     4,        -6 }, // vs. PREVIOUS
/* closing_bracket ] */ {   -6,   0 ,  -4,         4,       6,       4,       -6,      4,        4,     4,        -6 },
/* dividing_punct !? */ {   -6,   0 ,  -6,        -6,      -6,      -6,       -6,     -6,       -6,    -6,        -6 },
/* middle_dot     :; */ {   -4,   0 ,  -4,        -4,      -4,      -4,       -4,     -4,       -4,    -4,        -4 },
/* full_stop       . */ {   -6,   0 ,  -4,        -6,      -6,       4,       -6,      4,        4,     4,        -4 },
/* comma           , */ {   -4,   0 ,  -4,         4,      -6,       4,       -4,      4,        4,     4,        -4 },
/* fullwidth_space   */ {   8 ,   0 ,  8 ,        8 ,      8 ,      8 ,       8 ,     8 ,       8 ,    8 ,        8  },
//                               n/a
};

// Note: we may not do well with cjkt_ambiguous_quote (fullwidth or non-fullwidth ' and ").
// May be we can do better by tweaking/adding some conditions or with heuristics
// (ie. '"' in ':"' is probably opening, while in '"。' it is closing;
// but in ' 。" ', we wouldn't know (it might close the previous sentence,
// or start a new quote after the full stop...)


// Instantiate a new TextLangCfg with properties adequate to the provided lang_tag
TextLangCfg::TextLangCfg( lString32 lang_tag ) {
    if ( TextLangMan::_no_hyph_method == NULL ) {
        // We need to init static TextLangMan::_no_hyph_method and friends after
        // HyphMan is set up. Do that here, even if unrelated, as TextLangCfg
        // creation is called less often that every other methods around here.
        TextLangMan::_no_hyph_method = HyphMan::getHyphMethodForDictionary(HYPH_DICT_ID_NONE);
        TextLangMan::_soft_hyphens_method = HyphMan::getHyphMethodForDictionary(HYPH_DICT_ID_SOFTHYPHENS);
        TextLangMan::_algo_hyph_method = HyphMan::getHyphMethodForDictionary(HYPH_DICT_ID_ALGORITHM);
    }

    // Keep as our id the provided and non-lowercase'd lang_tag (with possibly bogus #@algorithm)
    _lang_tag = lang_tag;
    // Harfbuzz may know more than us about exotic/complex lang tags,
    // so let it deal the the provided one as-is.
    lString32 hb_lang_tag = lang_tag;
    // Lowercase it for our tests
    lang_tag.lowercase(); // (used by langStartsWith(…) macros)

    // Get hyph method/dictionary from _hyph_dict_table
    _hyph_method = TextLangMan::getHyphMethodForLang(lang_tag);

    // Cleanup if we got "en#@something" from legacy HyphMan methods
    int h_pos = lang_tag.pos("#");
    if ( h_pos > 0 ) {
        lang_tag = lang_tag.substr(0, h_pos);
        hb_lang_tag = hb_lang_tag.substr(0, h_pos); // Also clean the one for HB
    }
    #ifdef DEBUG_LANG_USAGE
    printf("TextLangCfg %s created (%s %s)\n", UnicodeToLocal(_lang_tag).c_str(),
                    UnicodeToLocal(lang_tag).c_str(), UnicodeToLocal(_hyph_method->getId()).c_str());
    #endif

    // https://drafts.csswg.org/css-text-3/#script-tagging
    // We might need to check for the script subpart (optional 2nd
    // subpart) Lant, Hant, Hrkt... and make some non latin language
    // with a Lant script behave more like latin languages...

    // Note that Harfbuzz seems to do the right same thing with
    // either "zh-TW" and "zh-Hant".

    // See for more clever/complex handling of lang tags:
    // https://android.googlesource.com/platform/frameworks/minikin/+/refs/heads/master/libs/minikin/Locale.cpp

    // We thought about adding a 2nd fallback font per-language, but it feels
    // a bit wrong to limit this feature to documents with lang tags.
    // Better to implement a generic font fallback chain independant of language.

    // https://unicode.org/reports/tr14/#Hyphen : in Polish and Portuguese,
    // a real hyphen at end of line must be duplicated at start of next line.
    _duplicate_real_hyphen_on_next_line = false;

    // getCssLbCharSub(), possibly called on each glyph, has some different behaviours with 'ja' and 'zh'
    _is_ja_zh = false;
    // These may be used to tweak CJK typography. Default typography for CJK segments will be
    // Simplified Chinese, which should be fine for embedded CJK in western text, Simplified
    // Chinese being the variant that feels the most like western typography (condensed
    // punctuation, including at start and end of lines).
    _is_ja = false;
    _is_zh_SC = false;
    _is_zh_TC = false;
    if ( langStartsWith(lang_tag, "ja") ) {
        _is_ja_zh = true;
        _is_ja = true;
    }
    else if ( langStartsWith(lang_tag, "zh") ) {
        _is_ja_zh = true;
        if ( lang_tag.pos("-hant") > 0 ) {
            _is_zh_TC = true;
        }
        else if ( (lang_tag.pos("-hans") < 0) ) { // Not explicitely Simplified Chinse
            if ( lang_tag.pos("-hk") > 0 ) // Hong Kong (zh-HK)
                _is_zh_TC = true;
            else if ( lang_tag.pos("-mo") > 0 ) // Macao (zh-MO)
                _is_zh_TC = true;
            else if ( lang_tag.pos("-tw") > 0 ) // Taiwan (zh-TW)
                _is_zh_TC = true;
        }
        if ( !_is_zh_TC ) {
            // (We set this even if not -Hans and it uses non-CJK scripts)
            _is_zh_SC = true;
        }
    }
    else if ( langStartsWith(lang_tag, "lzh") ) { // Literary/Classical Chinese
        _is_ja_zh = true;
        _is_zh_TC = true;
    }
    // CJK width adjustment tables
    if ( _is_ja ) {
        _cjk_width_adjustment_table = &japanese_cjk_width_adjustment_table;
    }
    else if ( _is_zh_TC ) {
        _cjk_width_adjustment_table = &traditional_chinese_cjk_width_adjustment_table;
    }
    else { // Otherwise, default to Simplified Chinese
        _cjk_width_adjustment_table = &simplified_chinese_cjk_width_adjustment_table;
    }

#if USE_HARFBUZZ==1
    _hb_language = hb_language_from_string(UnicodeToLocal(hb_lang_tag).c_str(), -1);
#endif

#if USE_LIBUNIBREAK==1
    // libunibreak per-language LineBreakProperties extensions
    //
    // Rules extracted from libunibreak/src/linebreakdef.c, so we can adapt
    // them and build LineBreakProperties adequately for more languages.
    // See https://en.wikipedia.org/wiki/Quotation_mark
    // These are mostly need only for languages that may add a space between
    // the quote and its content - otherwise, the quote will be part of the
    // word it sticks to, and break will be allowed on the other side which
    // probably is a space.
    // When a language allows the use of unpaired quotes (same quote on both
    // sides), it seems best to not specify anything.
    bool has_left_single_quotation_mark_opening = false;   // U+2018 ‘
    bool has_left_single_quotation_mark_closing = false;
    bool has_right_single_quotation_mark_opening = false;  // U+2019 ’
    bool has_right_single_quotation_mark_closing = false;
    bool has_right_single_quotation_mark_glue = false;
    bool has_left_double_quotation_mark_opening = false;   // U+201C “
    bool has_left_double_quotation_mark_closing = false;
    bool has_right_double_quotation_mark_opening = false;  // U+201D ”
    bool has_right_double_quotation_mark_closing = false;
    bool has_left_single_angle_quotation_mark_opening = false;   // U+2039 ‹
    bool has_left_single_angle_quotation_mark_closing = false;
    bool has_right_single_angle_quotation_mark_opening = false;  // U+203A ›
    bool has_right_single_angle_quotation_mark_closing = false;
    bool has_left_double_angle_quotation_mark_opening = false;   // U+00AB «
    bool has_left_double_angle_quotation_mark_closing = false;
    bool has_right_double_angle_quotation_mark_opening = false;  // U+00BB »
    bool has_right_double_angle_quotation_mark_closing = false;
    // Additional rule for treating em-dashes as e.g. "horizontal bar"
    // This is appropriate for languages that typically have a space at a
    // breakable side of the dash
    bool has_em_dash_alphabetic = false; // U+2014 —, U+2E3A ⸺, U+2E3B ⸻

    // Note: these macros use 'lang_tag'.
    if ( langStartsWith(lang_tag, "en") ) { // English
        has_left_single_quotation_mark_opening = true; // no right..closing in linebreakdef.c
        has_left_double_quotation_mark_opening = true;
        has_right_double_quotation_mark_closing = true;
    }
    else if ( langStartsWith(lang_tag, "fr", "es") ) { // French, Spanish
        has_left_single_quotation_mark_opening = true; // no right..closing in linebreakdef.c
        has_left_double_quotation_mark_opening = true;
        has_right_double_quotation_mark_closing = true;
        has_left_single_angle_quotation_mark_opening = true;
        has_right_single_angle_quotation_mark_closing = true;
        has_left_double_angle_quotation_mark_opening = true;
        has_right_double_angle_quotation_mark_closing = true;
        has_em_dash_alphabetic = true;
    }
    else if ( langStartsWith(lang_tag, "de") ) { // German
        has_left_single_quotation_mark_closing = true;
        has_right_single_quotation_mark_glue = true;
        has_left_double_quotation_mark_closing = true;
        /* Next ones commented out, as non-inverted usage of these
         * quotation marks can be found in pure "de" text - and
         * generally, these quotations marks are stuck to their
         * quoted first or last word and have only a space on the
         * other side, and so should be fine with just being "QU"
         * for libunibreak.
         * See https://github.com/koreader/koreader/issues/6717
        has_left_single_angle_quotation_mark_closing = true;
        has_right_single_angle_quotation_mark_opening = true;
        has_left_double_angle_quotation_mark_closing = true;
        has_right_double_angle_quotation_mark_opening = true;
        */
    }
    else if ( langStartsWith(lang_tag, "ru") ) { // Russian
        // The following rule is disabled because Russian texts often
        // use quotation marks from Word (“”)
        // has_left_double_quotation_mark_closing = true;
        has_left_double_angle_quotation_mark_opening = true;
        has_right_double_angle_quotation_mark_closing = true;
    }
    else if ( langStartsWith(lang_tag, "zh") ) { // Chinese
        has_left_single_quotation_mark_opening = true;
        has_right_single_quotation_mark_closing = true;
        has_left_double_quotation_mark_opening = true;
        has_right_double_quotation_mark_closing = true;
    }
    // Add languages rules here, or reuse previous one with other languages if needed.

    // Set up _lb_props.
    // Important: the unicode indices must be in strict ascending order (or libunibreak
    // might abort checking them all)
    int n = 0;
    if ( has_left_double_angle_quotation_mark_opening )  _lb_props[n++] = { 0x00AB, 0x00AB, LBP_OP };
    if ( has_left_double_angle_quotation_mark_closing )  _lb_props[n++] = { 0x00AB, 0x00AB, LBP_CL };
    // Soft-Hyphens are handled by Hyphman hyphenate(), have them handled as Zero-Width-Joiner by
    // libunibreak so they don't allow any break and don't prevent hyphenate() to handle them correctly.
    _lb_props[n++] = { 0x00AD, 0x00AD, LBP_ZWJ };
    if ( has_right_double_angle_quotation_mark_opening ) _lb_props[n++] = { 0x00BB, 0x00BB, LBP_OP };
    if ( has_right_double_angle_quotation_mark_closing ) _lb_props[n++] = { 0x00BB, 0x00BB, LBP_CL };
    if ( has_em_dash_alphabetic )                        _lb_props[n++] = { 0x2014, 0x2014, LBP_AL };
    if ( has_left_single_quotation_mark_opening )        _lb_props[n++] = { 0x2018, 0x2018, LBP_OP };
    if ( has_left_single_quotation_mark_closing )        _lb_props[n++] = { 0x2018, 0x2018, LBP_CL };
    if ( has_right_single_quotation_mark_opening )       _lb_props[n++] = { 0x2019, 0x2019, LBP_OP };
    if ( has_right_single_quotation_mark_closing )       _lb_props[n++] = { 0x2019, 0x2019, LBP_CL };
    if ( has_right_single_quotation_mark_glue )          _lb_props[n++] = { 0x2019, 0x2019, LBP_GL };
    if ( has_left_double_quotation_mark_opening )        _lb_props[n++] = { 0x201C, 0x201C, LBP_OP };
    if ( has_left_double_quotation_mark_closing )        _lb_props[n++] = { 0x201C, 0x201C, LBP_CL };
    if ( has_right_double_quotation_mark_opening )       _lb_props[n++] = { 0x201D, 0x201D, LBP_OP };
    if ( has_right_double_quotation_mark_closing )       _lb_props[n++] = { 0x201D, 0x201D, LBP_CL };
    if ( has_left_single_angle_quotation_mark_opening )  _lb_props[n++] = { 0x2039, 0x2039, LBP_OP };
    if ( has_left_single_angle_quotation_mark_closing )  _lb_props[n++] = { 0x2039, 0x2039, LBP_CL };
    if ( has_right_single_angle_quotation_mark_opening ) _lb_props[n++] = { 0x203A, 0x203A, LBP_OP };
    if ( has_right_single_angle_quotation_mark_closing ) _lb_props[n++] = { 0x203A, 0x203A, LBP_CL };
    if ( has_em_dash_alphabetic )                        _lb_props[n++] = { 0x2E3A, 0x2E3B, LBP_AL };
    // End of list
    _lb_props[n++] = { 0, 0, LBP_Undefined };
        // When adding properties, be sure combinations for all languages
        // do fit in _lb_props[MAX_NB_LB_PROPS_ITEMS] (MAX_NB_LB_PROPS_ITEMS
        // is defined in textlang.h, currently at 20).
    // Done with libunibreak per-language LineBreakProperties extensions

    // Other line breaking and text layout tweaks
    _lb_char_sub_func = NULL;
    if ( langStartsWith(lang_tag, "en") ) { // English
        _lb_char_sub_func = &lb_char_sub_func_english;
    }
    else if ( langStartsWith(lang_tag, "pl") ) { // Polish
        _lb_char_sub_func = &lb_char_sub_func_polish;
        _duplicate_real_hyphen_on_next_line = true;
    }
    else if ( langStartsWith(lang_tag, "cs", "sk") ) { // Czech, Slovak
        _lb_char_sub_func = &lb_char_sub_func_czech_slovak;
    }
    else if ( langStartsWith(lang_tag, "pt", "sr") ) { // Portuguese, Serbian
        _duplicate_real_hyphen_on_next_line = true;
    }
    else if ( langStartsWith(lang_tag, "ru") ) { // Russian
        _lb_char_sub_func = &lb_char_sub_func_russian;
    }
#endif

    // Language default opening and closing quotes, for CSS
    //   "q::before { content: open-quote }" and
    //   "q::after  { content: close-quote }"
    quotes_spec * quotes = &_quotes_spec_default;
    for (int i=0; _quotes_spec_table[i].lang_tag!=NULL; i++) {
        if ( langStartsWith(lang_tag, _quotes_spec_table[i].lang_tag ) ) {
            quotes = &_quotes_spec_table[i];
            break;
        }
    }
    // Avoid a wrap after/before an opening/close quote.
    const lChar32 * quote_joiner = U"\x2060";
        // (Zero width, equivalent to deprecated ZERO WIDTH NO-BREAK SPACE)
        // We might want with some languages to use a non-breaking thin space instead.

    _open_quote1  << quotes->open_quote_level_1    << quote_joiner;
    _close_quote1 << quote_joiner   << quotes->close_quote_level_1;
    _open_quote2  << quotes->open_quote_level_2    << quote_joiner;
    _close_quote2 << quote_joiner   << quotes->close_quote_level_2;

    resetCounters();
}

TextLangCfg::~TextLangCfg() {
    // NOTE: _hyph_method may be dangling now, not *quite* sure what it points to,
    //       and how it relates to HyphMan::uninit & TextLangMan::uninit
}

void TextLangCfg::resetCounters() {
    _quote_nesting_level = 0;
}

lString32 & TextLangCfg::getOpeningQuote( bool update_level ) {
    if ( !update_level )
        return _open_quote1;
    _quote_nesting_level++;
    return (_quote_nesting_level % 2) ? _open_quote1 : _open_quote2;
}

lString32 & TextLangCfg::getClosingQuote( bool update_level ) {
    if ( !update_level )
        return _close_quote1;
    _quote_nesting_level--;
    return ((_quote_nesting_level+1) % 2) ? _close_quote1 : _close_quote2;
}

int TextLangCfg::getHyphenHangingPercent() {
    return 70; // 70%
}

int TextLangCfg::getHangingPercent( bool right_hanging, bool rtl_line, bool & check_font, const lChar32 * text, int pos, int next_usable ) {
    // We get provided with the BiDi re-ordered m_text (so, visually
    // ordered) and the index of char: if needed, we can look at
    // previous or next chars for context to decide how much to hang
    // (i.e. consecutive punctuations).

    // If we ever need to tweak this per language, try to avoid checks
    // for the lang_tag in here:
    // - either set bool members to enable or disable some checks and tweaks
    // - or make this hanging_percent_func_generic, and add dedicated
    //   functions per language, hanging_percent_func_french, that
    //   could fallback to calling hanging_percent_func_generic after
    //   some checks - and have TextLangCfg::getHangingPercent() call
    //   the dedicated function pointer stored as a member.

    // We might want to prevent any hanging with Chinese and Japanese
    // as the text might be mostly full-width glyphs, and this might
    // break the grid. This is less risky if the main font is a CJK
    // font, but if it is not, punctuation might be picked from the
    // main non-CJK font and won't be full-width.
    // Or we could round any value to 0 or 100%  (and/or tweak any
    // glyph in lvtextfm.cpp so it looks like it is full-width).

    // We may return a greater ratio if some char happens at end of
    // line (hanging in the right margin in a LTR paragraph, or in
    // the left margin in a RTL one) as it may have more hanging
    // company on this side.
    bool line_end_hanging = right_hanging != rtl_line;

    lChar32 ch = text[pos];
    int ratio = 0;

    /* Commented out, as currently no longer needed :
    // In French, there's usually a space before and after guillemets,
    // or before a quotation mark. Having them hanging, and then a
    // space, looks like there's a hole in the margin.
    // So, for some chars, we'll avoid hanging or reduce the hanging
    // ratio if the next/prev char is a space char.
    // This might not happen in other languages, so let's do that
    // prevention generically. If needed, make that dependant on
    // a boolean member, set to true if langStartsWith(…, "fr").
    bool space_alongside = false;
    if ( right_hanging ) {
        if ( pos > 0 ) {
            lChar32 prev_ch = text[pos-1];
            if ( prev_ch == 0x0020 || prev_ch == 0x00A0 || (prev_ch >= 0x2000 && prev_ch <= 0x200A ) ) {
                // Normal space, no-break space, and other unicode spaces (except zero-width ones)
                space_alongside = true;
            }
        }
    }
    else {
        if ( next_usable > 0 ) {
            lChar32 next_ch = text[pos+1];
            if ( next_ch == 0x0020 || next_ch == 0x00A0 || (next_ch >= 0x2000 && next_ch <= 0x200A ) ) {
                // Normal space, no-break space, and other unicode spaces (except zero-width ones)
                space_alongside = true;
            }
        }
    }
    */

    // For the common punctuations, parens and quotes, we check and
    // return the same value whether asked for left or right hanging.
    // Normally, libunibreak has prevented them from happening on
    // one of the sides - but with RTL text, they may happen on
    // the other side. Also, some BiDi mirrorable chars "([])" might
    // be mirrored in the provided *text when not-using HarfBuzz, but
    // won't be mirrored when using HarfBuzz - so let's handle
    // all of them no matter the hanging side asked for.
    // Also, because in some languages, quotation marks and guillemets
    // are used reverted, we include left and right ones in both sets.

    // Most values taken from the "protusion" section in:
    // https://source.contextgarden.net/tex/context/base/mkiv/font-imp-quality.lua
    // https://www.w3.org/Mail/flatten/index?subject=Amending+hanging-punctuation+for+Western+typography&list=www-style
    // and the microtypography thesis: http://www.pragma-ade.nl/pdftex/thesis.pdf
    // (screenshot at https://github.com/koreader/koreader/issues/6235#issuecomment-639307634)

    switch (ch) {
        case 0x0027: // ' single quote
        case 0x002C: // , comma
        case 0x002D: // - minus
        case 0x002E: // . period
        case 0x0060: // ` back quote
        // case 0x00AD: // soft hyphen (we don't draw them, so don't handle them)
        case 0x060C: // ، arabic comma
        case 0x06D4: // ۔ arabic full stop
        case 0x2010: // ‐ hyphen
        case 0x2018: // ‘ left single quotation mark
        case 0x2019: // ’ right single quotation mark
        case 0x201A: // ‚ single low-9 quotation mark
        case 0x201B: // ‛ single high-reversed-9 quotation mark
            ratio = 70;
            break;
        case 0x0022: // " double quote
        case 0x003A: // : colon
        case 0x003B: // ; semicolon
        case 0x061B: // ؛ arabic semicolon
        case 0x201C: // “ left double quotation mark
        case 0x201D: // ” right double quotation mark
        case 0x201E: // „ double low-9 quotation mark
        case 0x201F: // ‟ double high-reversed-9 quotation mark
            ratio = 50;
            break;
        case 0x2013: // – endash
            // Should have enough body inside (with only 30% hanging)
            ratio = 30;
            break;
        /* This early idea feels not the best: these have a side taller than
         * the other, and the glyph may have some strong body with some fonts:
        case 0x2039: // ‹ left single guillemet
        case 0x203A: // › right single guillemet
            // These are wider than the previous ones, and hanging by 70% with a space
            // alongside can give a feeling of bad justification. So, hang less.
            ratio = space_alongside ? 20 : 70;
            break;
        case 0x00AB: // « left guillemet
        case 0x00BB: // » right guillemet
            ratio = space_alongside ? 20 : 50;
            break;
        */
        // If feels better to not bother about any space alongside and use smaller values.
        // It also feels among German and French users that the orientation of the guillemet
        // doesn't matter, and we can hang the same ratio whether the tall or the small side
        // will be hanging.
        // We also go with a tad larger value on the line-end margin (right margin if LTR)
        // so it blends better with the more probable other punctuation handling there.
        case 0x2039: // ‹ left single guillemet
        case 0x203A: // › right single guillemet
            ratio = line_end_hanging ? 40 : 35;
            break;
        case 0x00AB: // « left guillemet
        case 0x00BB: // » right guillemet
            ratio = line_end_hanging ? 20 : 15;
            break;
        case 0x0021: // !
        case 0x003F: // ?
        case 0x00A1: // ¡
        case 0x00BF: // ¿
        case 0x061F: // ؟
        case 0x2014: // — emdash
        case 0x2026: // … ellipsis
            // These will have enough body inside (with only 20% hanging),
            // so they shouldn't hurt when space_alongside.
            ratio = 20;
            break;
        case 0x0028: // (
        case 0x0029: // )
        case 0x005B: // [
        case 0x005D: // ]
        case 0x007B: // {
        case 0x007D: // }
            ratio  = 5;
            break;
        default:
            break;
    }
    if ( ratio ) {
        check_font = false;
        return ratio;
    }
    // Other are non punctuation but slight adjustment for some letters,
    // that might be ignored if the font already include some negative
    // left side bearing.
    // The hanging ratio is small, so no need to correct if space_alongside.
    check_font = true;
    if ( right_hanging ) {
        switch (ch) {
            case 'A':
            case 'F':
            case 'K':
            case 'L':
            case 'T':
            case 'V':
            case 'W':
            case 'X':
            case 'Y':
            case 'k':
            case 'r':
            case 't':
            case 'v':
            case 'w':
            case 'x':
            case 'y':
                ratio  = 5;
                break;
            default:
                break;
        }
    }
    else { // left hanging
        switch (ch) {
            case 'A':
            case 'J':
            case 'T':
            case 'V':
            case 'W':
            case 'X':
            case 'Y':
            case 'v':
            case 'w':
            case 'x':
            case 'y':
                ratio  = 5;
                break;
            default:
                break;
        }
    }
    return ratio;
}

lString32 TextLangCfg::softHyphenateText( lString32 & text, bool use_default_hyph_method ) {
    #define HYPH_MIN_WORD_LEN_TO_HYPHENATE 4
    #define HYPH_MAX_WORD_SIZE 64
    const lChar32 * text32 = text.c_str();
    int txtlen = text.length();
    lUInt8 * flags = (lUInt8*)calloc(txtlen, sizeof(*flags));
    lUInt16 widths[HYPH_MAX_WORD_SIZE] = { 0 }; // array needed by hyphenate()
    // Lookup words starting from the end, just because lStr_findWordBounds()
    // will ensure the iteration that way.
    int wordpos = txtlen;
    while ( wordpos > 0 ) {
        // lStr_findWordBounds() will find the word contained at wordpos
        // (or the previous word if wordpos happens to be a space or some
        // punctuation) by looking only for alpha chars in m_text.
        int start, end;
        bool has_rtl;
        lStr_findWordBounds( text32, txtlen, wordpos, start, end, has_rtl );
        if ( end <= HYPH_MIN_WORD_LEN_TO_HYPHENATE ) {
            // Too short word at start, we're done
            break;
        }
        int len = end - start;
        if ( len < HYPH_MIN_WORD_LEN_TO_HYPHENATE || has_rtl ) {
            // Too short word found, or word containing RTL: skip it
            wordpos = start - 1;
            continue;
        }
        if ( start >= wordpos ) {
            // Shouldn't happen, but let's be sure we don't get stuck
            wordpos = wordpos - HYPH_MIN_WORD_LEN_TO_HYPHENATE;
            continue;
        }
        // We have a valid word to look for hyphenation
        if ( len > HYPH_MAX_WORD_SIZE ) // hyphenate() stops/truncates at 64 chars
            len = HYPH_MAX_WORD_SIZE;
        // Have hyphenate() set flags inside 'flags'
        if ( use_default_hyph_method )
            getDefaultHyphMethod()->hyphenate(text32+start, len, widths, flags+start, 0, 0xFFFF, 1);
        else
            getHyphMethod()->hyphenate(text32+start, len, widths, flags+start, 0, 0xFFFF, 1);
        // Continue with previous word
        wordpos = start - 1;
    }
    // Return text, adding a soft-hyphen where there are flags
    lString32 res;
    for ( int i=0; i<txtlen; i++ ) {
        res << text.substr(i, 1);
        if ( flags[i] & LCHAR_ALLOW_HYPH_WRAP_AFTER )
            res << U'\x00AD';
    }
    free(flags);
    return res;
}
