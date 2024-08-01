/** \file hyphman.cpp
    \brief AlReader hyphenation manager

    (c) Alan, adapted TeX hyphenation dictionaries code: http://alreader.kms.ru/
    (c) Mark Lipsman -- hyphenation algorithm, modified my Mike & SeNS

    Adapted for CREngine by Vadim Lopatin

    This source code is distributed under the terms of
    GNU General Public License.

    See LICENSE file for details.

*/

// set to 1 for debug dump
#if 0
#define DUMP_HYPHENATION_WORDS 1
#define DUMP_PATTERNS 1
#else
#define DUMP_HYPHENATION_WORDS 0
#define DUMP_PATTERNS 0
#endif

#include "../include/crsetup.h"

#include <stdlib.h>
#include <string.h>
#include "../include/lvxml.h"

#if !defined(__SYMBIAN32__)
#include <stdio.h>
#include <wchar.h>
#endif

#include "../include/lvtypes.h"
#include "../include/lvstream.h"
#include "../include/hyphman.h"
#include "../include/lvfnt.h"
#include "../include/lvstring.h"
#include "../include/textlang.h"


#ifdef ANDROID

#define _32(x) lString32(x)

#else

#include "../include/cri18n.h"

#endif

int HyphMan::_LeftHyphenMin = HYPH_DEFAULT_HYPHEN_MIN;
int HyphMan::_RightHyphenMin = HYPH_DEFAULT_HYPHEN_MIN;
int HyphMan::_TrustSoftHyphens = HYPH_DEFAULT_TRUST_SOFT_HYPHENS;
LVHashTable<lString32, HyphMethod*, true> HyphMan::_loaded_hyph_methods(16);
HyphDataLoader* HyphMan::_dataLoader = NULL;


// Obsolete: now fetched from TextLangMan main lang TextLangCfg
// HyphDictionary * HyphMan::_selectedDictionary = NULL;

HyphDictionaryList * HyphMan::_dictList = NULL;

// MAX_PATTERN_SIZE is actually the max size of a word (pattern stripped
// from all the numbers that give the quality of a split after previous char)
// (35 is needed for German.pattern)
#define MAX_PATTERN_SIZE  35
#define PATTERN_HASH_SIZE 16384
class TexPattern;
class TexHyph : public HyphMethod
{
    TexPattern * table[PATTERN_HASH_SIZE];
    lUInt32 _hash;
    lUInt32 _pattern_count;
    lString32 _supported_modifiers;
public:
    int largest_overflowed_word;
    bool match( const lChar32 * str, char * mask );
    virtual bool hyphenate( const lChar32 * str, int len, lUInt16 * widths, lUInt8 * flags, lUInt16 hyphCharWidth, lUInt16 maxWidth, size_t flagSize );
    void addPattern( TexPattern * pattern );
    void checkForModifiers( lString32 str );
    TexHyph( lString32 id=HYPH_DICT_ID_DICTIONARY, int leftHyphenMin=HYPHMETHOD_DEFAULT_HYPHEN_MIN, int rightHyphenMin=HYPHMETHOD_DEFAULT_HYPHEN_MIN );
    virtual ~TexHyph();
    bool load( LVStreamRef stream );
    bool load( lString32 fileName );
    virtual lUInt32 getHash() { return _hash; }
    virtual lUInt32 getCount() { return _pattern_count; }
    virtual lUInt32 getSize();
};

class AlgoHyph : public HyphMethod
{
public:
    AlgoHyph(): HyphMethod(HYPH_DICT_ID_ALGORITHM) {};
    virtual bool hyphenate( const lChar32 * str, int len, lUInt16 * widths, lUInt8 * flags, lUInt16 hyphCharWidth, lUInt16 maxWidth, size_t flagSize );
    virtual ~AlgoHyph();
};

class SoftHyphensHyph : public HyphMethod
{
public:
    SoftHyphensHyph(): HyphMethod(HYPH_DICT_ID_SOFTHYPHENS) {};
    virtual bool hyphenate( const lChar32 * str, int len, lUInt16 * widths, lUInt8 * flags, lUInt16 hyphCharWidth, lUInt16 maxWidth, size_t flagSize );
    virtual ~SoftHyphensHyph();
};

class NoHyph : public HyphMethod
{
public:
    NoHyph(): HyphMethod(HYPH_DICT_ID_NONE) {};
    virtual bool hyphenate( const lChar32 * str, int len, lUInt16 * widths, lUInt8 * flags, lUInt16 hyphCharWidth, lUInt16 maxWidth, size_t flagSize )
    {
        CR_UNUSED6(str, len, widths, flags, hyphCharWidth, maxWidth);
        return false;
    }
    virtual ~NoHyph() { }
};

static NoHyph NO_HYPH;
static AlgoHyph ALGO_HYPH;
static SoftHyphensHyph SOFTHYPHENS_HYPH;

// Obsolete: provided by TextLangMan main lang
// HyphMethod * HyphMan::_method = &NO_HYPH;

#pragma pack(push, 1)
typedef struct {
    lUInt16         wl;
    lUInt16         wu;
    char            al;
    char            au;

    unsigned char   mask0[2];
    lUInt16         aux[256];

    lUInt16         len;
} thyph;

typedef struct {
    lUInt16 start;
    lUInt16 len;
} hyph_index_item_t;
#pragma pack(pop)

class HyphDataLoaderFromFile: public HyphDataLoader
{
public:
    HyphDataLoaderFromFile() : HyphDataLoader() {}
    virtual ~HyphDataLoaderFromFile() {}
    virtual LVStreamRef loadData(lString32 id) {
        HyphDictionaryList* dictList = HyphMan::getDictList();
        HyphDictionary * p = dictList->find(id);
        if ( !p )
            return LVStreamRef();
        if ( p->getType() == HDT_NONE ||
                p->getType() == HDT_ALGORITHM ||
                p->getType() == HDT_SOFTHYPHENS ||
                ( p->getType() != HDT_DICT_ALAN && p->getType() != HDT_DICT_TEX) )
            return LVStreamRef();
        lString32 filename = p->getFilename();
        return LVOpenFileStream( filename.c_str(), LVOM_READ );
    }
};



void HyphMan::uninit()
{
    // Avoid existing frontend code to have to call it:
    TextLangMan::uninit();
    if ( _dictList )
            delete _dictList;
    _dictList = NULL;
    if ( _dataLoader )
        delete _dataLoader;
    _dataLoader = NULL;
    /* Obsolete:
	_selectedDictionary = NULL;
    if ( HyphMan::_method != &ALGO_HYPH && HyphMan::_method != &NO_HYPH && HyphMan::_method != &SOFTHYPHENS_HYPH )
            delete HyphMan::_method;
    _method = &NO_HYPH;
    */
}

bool HyphMan::initDictionaries(lString32 dir, bool clear)
{
    if (clear && _dictList)
        delete _dictList;
    if (clear || !_dictList)
        _dictList = new HyphDictionaryList();
    if (NULL == _dataLoader)
        _dataLoader = new HyphDataLoaderFromFile;
    if (_dictList->open(dir, clear)) {
		if ( !_dictList->activate( lString32(DEF_HYPHENATION_DICT) ) )
    			_dictList->activate( lString32(HYPH_DICT_ID_ALGORITHM) );
		return true;
	} else {
		_dictList->activate( lString32(HYPH_DICT_ID_ALGORITHM) );
		return false;
	}
}

// for android
bool HyphMan::addDictionaryItem(HyphDictionary* dict)
{
    if (_dictList->find(dict->getId()))
        return false;
    _dictList->add(dict);
    return true;
}

void HyphMan::setDataLoader(HyphDataLoader* loader) {
    if (_dataLoader)
        delete _dataLoader;
    _dataLoader = loader;
}

bool HyphMan::setLeftHyphenMin( int left_hyphen_min ) {
    if (left_hyphen_min >= HYPH_MIN_HYPHEN_MIN && left_hyphen_min <= HYPH_MAX_HYPHEN_MIN) {
        HyphMan::_LeftHyphenMin = left_hyphen_min;
        return true;
    }
    return false;
}

bool HyphMan::setRightHyphenMin( int right_hyphen_min ) {
    if (right_hyphen_min >= HYPH_MIN_HYPHEN_MIN && right_hyphen_min <= HYPH_MAX_HYPHEN_MIN) {
        HyphMan::_RightHyphenMin = right_hyphen_min;
        return true;
    }
    return false;
}

bool HyphMan::setTrustSoftHyphens( int trust_soft_hyphens ) {
    HyphMan::_TrustSoftHyphens = trust_soft_hyphens;
    return true;
}

bool HyphMan::isEnabled() {
    return TextLangMan::getHyphenationEnabled();
    /* Obsolete:
    return _selectedDictionary != NULL && _selectedDictionary->getId() != HYPH_DICT_ID_NONE;
    */
}

bool HyphMan::hyphenate( const lChar32 * str, int len, lUInt16 * widths, lUInt8 * flags, lUInt16 hyphCharWidth, lUInt16 maxWidth, size_t flagSize )
{
    return TextLangMan::getMainLangHyphMethod()->hyphenate( str, len, widths, flags, hyphCharWidth, maxWidth, flagSize );
    /* Obsolete:
    return _method->hyphenate( str, len, widths, flags, hyphCharWidth, maxWidth, flagSize );
    */
}

HyphDictionary * HyphMan::getSelectedDictionary() {
    lString32 id = TextLangMan::getTextLangCfg()->getHyphMethod()->getId();
    HyphDictionary * dict = _dictList->find( id );
    return dict;
}

HyphMethod * HyphMan::getHyphMethodForDictionary( lString32 id, int leftHyphenMin, int rightHyphenMin ) {
    if ( id.empty() || NULL == _dataLoader)
        return &NO_HYPH;
    HyphDictionary * p = _dictList->find(id);
    if ( !p || p->getType() == HDT_NONE )
        return &NO_HYPH;
    if ( p->getType() == HDT_ALGORITHM )
        return &ALGO_HYPH;
    if ( p->getType() == HDT_SOFTHYPHENS )
        return &SOFTHYPHENS_HYPH;
    if ( p->getType() != HDT_DICT_ALAN && p->getType() != HDT_DICT_TEX )
        return &NO_HYPH;
    HyphMethod * method;
    if ( _loaded_hyph_methods.get(id, method) ) {
        // printf("getHyphMethodForDictionary reusing cached %s\n", UnicodeToUtf8(p->getFilename()).c_str());
        return method;
    }
    LVStreamRef stream = _dataLoader->loadData(id);
    if ( stream.isNull() ) {
        CRLog::error("Cannot open hyphenation dictionary %s", UnicodeToUtf8(id).c_str() );
        return &NO_HYPH;
    }
    TexHyph * newmethod = new TexHyph(id, leftHyphenMin, rightHyphenMin);
    if ( !newmethod->load( stream ) ) {
        CRLog::error("Cannot open hyphenation dictionary %s", UnicodeToUtf8(id).c_str() );
        delete newmethod;
        return &NO_HYPH;
    }
    // printf("CRE: loaded hyphenation dict %s\n", UnicodeToUtf8(id).c_str());
    if ( newmethod->largest_overflowed_word )
        printf("CRE WARNING: %s: some hyphenation patterns were too long and have been ignored: increase MAX_PATTERN_SIZE from %d to %d\n", UnicodeToUtf8(id).c_str(), MAX_PATTERN_SIZE, newmethod->largest_overflowed_word);
    _loaded_hyph_methods.set(id, newmethod);
    return newmethod;
}

bool HyphDictionary::activate()
{
    TextLangMan::setMainLangFromHyphDict( getId() );
    return true;
    /* Obsolete:
    if (HyphMan::_selectedDictionary == this)
        return true; // already active
	if ( getType() == HDT_ALGORITHM ) {
		CRLog::info("Turn on algorythmic hyphenation" );
        if ( HyphMan::_method != &ALGO_HYPH ) {
            if ( HyphMan::_method != &SOFTHYPHENS_HYPH && HyphMan::_method != &NO_HYPH )
                delete HyphMan::_method;
            HyphMan::_method = &ALGO_HYPH;
        }
	} else if ( getType() == HDT_SOFTHYPHENS ) {
		CRLog::info("Turn on soft-hyphens hyphenation" );
        if ( HyphMan::_method != &SOFTHYPHENS_HYPH ) {
            if ( HyphMan::_method != &ALGO_HYPH && HyphMan::_method != &NO_HYPH )
                delete HyphMan::_method;
            HyphMan::_method = &SOFTHYPHENS_HYPH;
        }
	} else if ( getType() == HDT_NONE ) {
		CRLog::info("Disabling hyphenation" );
        if ( HyphMan::_method != &NO_HYPH ) {
            if ( HyphMan::_method != &ALGO_HYPH && HyphMan::_method != &SOFTHYPHENS_HYPH )
                delete HyphMan::_method;
            HyphMan::_method = &NO_HYPH;
        }
	} else if ( getType() == HDT_DICT_ALAN || getType() == HDT_DICT_TEX ) {
        if ( HyphMan::_method != &NO_HYPH && HyphMan::_method != &ALGO_HYPH && HyphMan::_method != &SOFTHYPHENS_HYPH ) {
            delete HyphMan::_method;
            HyphMan::_method = &NO_HYPH;
        }
		CRLog::info("Selecting hyphenation dictionary %s", UnicodeToUtf8(_filename).c_str() );
		LVStreamRef stream = LVOpenFileStream( getFilename().c_str(), LVOM_READ );
		if ( stream.isNull() ) {
			CRLog::error("Cannot open hyphenation dictionary %s", UnicodeToUtf8(_filename).c_str() );
			return false;
		}
        TexHyph * method = new TexHyph();
        if ( !method->load( stream ) ) {
			CRLog::error("Cannot open hyphenation dictionary %s", UnicodeToUtf8(_filename).c_str() );
            delete method;
            return false;
        }
        if (method->largest_overflowed_word)
            printf("CRE WARNING: %s: some hyphenation patterns were too long and have been ignored: increase MAX_PATTERN_SIZE from %d to %d\n", UnicodeToUtf8(_filename).c_str(), MAX_PATTERN_SIZE, method->largest_overflowed_word);
        HyphMan::_method = method;
	}
	HyphMan::_selectedDictionary = this;
	return true;
    */
}

bool HyphDictionaryList::activate( lString32 id )
{
    CRLog::trace("HyphDictionaryList::activate(%s)", LCSTR(id));
	HyphDictionary * p = find(id);
	if ( p )
		return p->activate();
	else
		return false;
}

void HyphDictionaryList::addDefault()
{
	if ( !find( lString32( HYPH_DICT_ID_NONE ) ) ) {
		_list.add( new HyphDictionary( HDT_NONE, _32("[No Hyphenation]"), lString32(HYPH_DICT_ID_NONE), lString32(HYPH_DICT_ID_NONE) ) );
	}
	if ( !find( lString32( HYPH_DICT_ID_ALGORITHM ) ) ) {
		_list.add( new HyphDictionary( HDT_ALGORITHM, _32("[Algorithmic Hyphenation]"), lString32(HYPH_DICT_ID_ALGORITHM), lString32(HYPH_DICT_ID_ALGORITHM) ) );
	}
	if ( !find( lString32( HYPH_DICT_ID_SOFTHYPHENS ) ) ) {
		_list.add( new HyphDictionary( HDT_SOFTHYPHENS, _32("[Soft-hyphens Hyphenation]"), lString32(HYPH_DICT_ID_SOFTHYPHENS), lString32(HYPH_DICT_ID_SOFTHYPHENS) ) );
	}

}

HyphDictionary * HyphDictionaryList::find( const lString32& id )
{
	for ( int i=0; i<_list.length(); i++ ) {
		if ( _list[i]->getId() == id )
			return _list[i];
	}
	return NULL;
}

static int HyphDictionary_comparator(const HyphDictionary ** item1, const HyphDictionary ** item2)
{
    if ( ( (*item1)->getType() == HDT_DICT_ALAN || (*item1)->getType() == HDT_DICT_TEX) &&
         ( (*item2)->getType() == HDT_DICT_ALAN || (*item2)->getType() == HDT_DICT_TEX) )
        return (*item1)->getTitle().compare((*item2)->getTitle());
    return (int)((*item1)->getType() - (*item2)->getType());
}

bool HyphDictionaryList::open(lString32 hyphDirectory, bool clear)
{
    CRLog::info("HyphDictionaryList::open(%s)", LCSTR(hyphDirectory) );
    if (clear) {
        _list.clear();
        addDefault();
    }
    if ( hyphDirectory.empty() )
        return true;
    //LVAppendPathDelimiter( hyphDirectory );
    LVContainerRef container;
    LVStreamRef stream;
    if ( (hyphDirectory.endsWith("/") || hyphDirectory.endsWith("\\")) && LVDirectoryExists(hyphDirectory) ) {
        container = LVOpenDirectory( hyphDirectory.c_str(), U"*.*" );
    } else if ( LVFileExists(hyphDirectory) ) {
        stream = LVOpenFileStream( hyphDirectory.c_str(), LVOM_READ );
        if ( !stream.isNull() )
            container = LVOpenArchieve( stream );
    }

	if ( !container.isNull() ) {
		int len = container->GetObjectCount();
        int count = 0;
        CRLog::info("%d items found in hyph directory", len);
		for ( int i=0; i<len; i++ ) {
			const LVContainerItemInfo * item = container->GetObjectInfo( i );
			lString32 name = item->GetName();
            lString32 suffix;
            lString32 suffix2add;
            HyphDictType t = HDT_NONE;
            if ( name.endsWith("_hyphen_(Alan).pdb") ) {
                suffix = "_hyphen_(Alan).pdb";
                suffix2add = " (Alan)";
                t = HDT_DICT_ALAN;
            } else if ( name.endsWith(".pattern") ) {
                suffix = ".pattern";
                t = HDT_DICT_TEX;
            } else
                continue;



			lString32 filename = hyphDirectory + name;
			lString32 id = name;
			lString32 title = name;
			if ( title.endsWith( suffix ) )
				title.erase( title.length() - suffix.length(), suffix.length() );
			if (!suffix2add.empty())
				title.append(suffix2add);
			_list.add( new HyphDictionary( t, title, id, filename ) );
            count++;
		}
        _list.sort(HyphDictionary_comparator);
		CRLog::info("%d dictionaries added to list", _list.length());
		return true;
	} else {
        CRLog::info("no hyphenation dictionary items found in hyph directory %s", LCSTR(hyphDirectory));
	}
	return false;
}

HyphMan::HyphMan()
{
}

HyphMan::~HyphMan()
{
}

// Used by SoftHyphensHyph::hyphenate(), but also possibly (when
// TrustSoftHyphens is true) as a first step by TexHyph::hyphenate()
// and AlgoHyph::hyphenate(): if soft hyphens are found in the
// provided word, trust and use them; don't do the regular patterns
// and algorithm matching.
static bool softhyphens_hyphenate( const lChar32 * str, int len, lUInt16 * widths, lUInt8 * flags, lUInt16 hyphCharWidth, lUInt16 maxWidth, size_t flagSize )
{
    bool soft_hyphens_found = false;
    for ( int i = 0; i<len; i++ ) {
        if ( widths[i] + hyphCharWidth > maxWidth )
            break;
        if ( str[i] == UNICODE_SOFT_HYPHEN_CODE ) {
            if ( flagSize == 2 ) {
                lUInt16* flags16 = (lUInt16*) flags;
                flags16[i] |= LCHAR_ALLOW_HYPH_WRAP_AFTER;
            }
            else {
                flags[i] |= LCHAR_ALLOW_HYPH_WRAP_AFTER;
            }
            soft_hyphens_found = true;
        }
    }
    return soft_hyphens_found;
}

bool SoftHyphensHyph::hyphenate( const lChar32 * str, int len, lUInt16 * widths, lUInt8 * flags, lUInt16 hyphCharWidth, lUInt16 maxWidth, size_t flagSize )
{
    if ( UserHyphDict::hasWords() ) {
        if ( UserHyphDict::hyphenate(str, len, widths, flags, hyphCharWidth, maxWidth, flagSize) )
            return true;
    }

    return softhyphens_hyphenate(str, len, widths, flags, hyphCharWidth, maxWidth, flagSize);
}

SoftHyphensHyph::~SoftHyphensHyph()
{
}

struct tPDBHdr
{
    char filename[36];
    lUInt32 dw1;
    lUInt32 dw2;
    lUInt32 dw4[4];
    char type[8];
    lUInt32 dw44;
    lUInt32 dw48;
    lUInt16 numrec;
};

static int isCorrectHyphFile(LVStream * stream)
{
    if (!stream)
        return false;
    lvsize_t   dw;
    int    w = 0;
    tPDBHdr    HDR;
    stream->SetPos(0);
    stream->Read( &HDR, 78, &dw);
    stream->SetPos(0);
    lvByteOrderConv cnv;
    w=cnv.msf(HDR.numrec);
    if (dw!=78 || w>0xff)
        w = 0;

    if (strncmp((const char*)&HDR.type, "HypHAlR4", 8) != 0)
        w = 0;

    return w;
}

class TexPattern {
public:
    lChar32 word[MAX_PATTERN_SIZE+1];
    char attr[MAX_PATTERN_SIZE+2];
    int overflowed; // 0, or size of complete word if larger than MAX_PATTERN_SIZE
    TexPattern * next;

    int cmp( TexPattern * v )
    {
        return lStr_cmp( word, v->word );
    }

    static int hash( const lChar32 * s )
    {
        return ((lUInt32)(((s[0] *31 + s[1])*31 + s[2]) * 31 + s[3])) % PATTERN_HASH_SIZE;
    }

    static int hash3( const lChar32 * s )
    {
        return ((lUInt32)(((s[0] *31 + s[1])*31 + s[2]) * 31 + 0)) % PATTERN_HASH_SIZE;
    }

    static int hash2( const lChar32 * s )
    {
        return ((lUInt32)(((s[0] *31 + s[1])*31 + 0) * 31 + 0)) % PATTERN_HASH_SIZE;
    }

    static int hash1( const lChar32 * s )
    {
        return ((lUInt32)(((s[0] *31 + 0)*31 + 0) * 31 + 0)) % PATTERN_HASH_SIZE;
    }

    int hash()
    {
        return ((lUInt32)(((word[0] *31 + word[1])*31 + word[2]) * 31 + word[3])) % PATTERN_HASH_SIZE;
    }

    bool match( const lChar32 * s, char * mask )
    {
        TexPattern * p = this;
        bool found = false;
        while ( p ) {
            bool res = true;
            for ( int i=2; p->word[i]; i++ )
                if ( p->word[i]!=s[i] ) {
                    res = false;
                    break;
                }
            if ( res ) {
                if ( p->word[0]==s[0] && (p->word[1]==0 || p->word[1]==s[1]) ) {
#if DUMP_PATTERNS==1
                    CRLog::debug("Pattern matched: %s %s on %s %s", LCSTR(lString32(p->word)), p->attr, LCSTR(lString32(s)), mask);
#endif
                    p->apply(mask);
                    found = true;
                }
            }
            p = p->next;
        }
        return found;
    }

    void apply( char * mask )
    {
        ;
        for ( char * p = attr; *p && *mask; p++, mask++ ) {
            if ( *mask < *p )
                *mask = *p;
        }
    }

    TexPattern( const lString32 &s ) : next( NULL )
    {
        overflowed = 0;
        memset( word, 0, sizeof(word) );
        memset( attr, '0', sizeof(attr) );
        attr[sizeof(attr)-1] = 0;
        int n = 0;
        for ( int i=0; i<(int)s.length(); i++ ) {
            lChar32 ch = s[i];
            if (n > MAX_PATTERN_SIZE) {
                if ( ch<'0' || ch>'9' ) {
                    overflowed = n++;
                }
                continue;
            }
            if ( ch>='0' && ch<='9' ) {
                attr[n] = (char)ch;
//                if (n>0)
//                    attr[n-1] = (char)ch;
            } else {
                if (n == MAX_PATTERN_SIZE) { // we previously reached max word size
                    // Let the last 0 (string termination) in
                    // word[MAX_PATTERN_SIZE] and mark it as overflowed
                    overflowed = n++;
                }
                else {
                    word[n++] = ch;
                }
            }
        }
        // if n==MAX_PATTERN_SIZE (or >), attr[MAX_PATTERN_SIZE] is either the
        // memset '0', or a 0-9 we got on next iteration, and
        // attr[MAX_PATTERN_SIZE+1] is the 0 set by attr[sizeof(attr)-1] = 0
        if (n < MAX_PATTERN_SIZE)
            attr[n+1] = 0;

        if (overflowed)
            overflowed = overflowed + 1; // convert counter to number of things counted
    }

    TexPattern( const unsigned char * s, int sz, const lChar32 * charMap )
    {
        overflowed = 0;
        if ( sz > MAX_PATTERN_SIZE ) {
            overflowed = sz;
            sz = MAX_PATTERN_SIZE;
        }
        memset( word, 0, sizeof(word) );
        memset( attr, 0, sizeof(attr) );
        for ( int i=0; i<sz; i++ )
            word[i] = charMap[ s[i] ];
        memcpy( attr, s+sz, sz+1 );
    }
};

class HyphPatternReader : public LVXMLParserCallback
{
protected:
    bool insidePatternTag;
    lString32Collection & data;
public:
    HyphPatternReader(lString32Collection & result) : insidePatternTag(false), data(result)
    {
        result.clear();
    }
    /// called on parsing end
    virtual void OnStop() { }
    /// called on opening tag end
    virtual void OnTagBody() {}
    /// called on opening tag
    virtual ldomNode * OnTagOpen( const lChar32 * nsname, const lChar32 * tagname)
    {
        CR_UNUSED(nsname);
        if (!lStr_cmp(tagname, "pattern")) {
            insidePatternTag = true;
        }
        return NULL;
    }
    /// called on closing
    virtual void OnTagClose( const lChar32 * nsname, const lChar32 * tagname, bool self_closing_tag=false )
    {
        CR_UNUSED2(nsname, tagname);
        insidePatternTag = false;
    }
    /// called on element attribute
    virtual void OnAttribute( const lChar32 * nsname, const lChar32 * attrname, const lChar32 * attrvalue )
    {
        CR_UNUSED3(nsname, attrname, attrvalue);
    }
    /// called on text
    virtual void OnText( const lChar32 * text, int len, lUInt32 flags )
    {
        CR_UNUSED(flags);
        if ( insidePatternTag )
            data.add( lString32(text, len) );
    }
    /// add named BLOB data to document
    virtual bool OnBlob(lString32 name, const lUInt8 * data, int size) {
        CR_UNUSED3(name, data, size);
        return false;
    }

};

TexHyph::TexHyph(lString32 id, int leftHyphenMin, int rightHyphenMin) : HyphMethod(id, leftHyphenMin, rightHyphenMin)
{
    memset( table, 0, sizeof(table) );
    _hash = 123456;
    _pattern_count = 0;
    largest_overflowed_word = 0;
}

TexHyph::~TexHyph()
{
    for ( int i=0; i<PATTERN_HASH_SIZE; i++ ) {
        TexPattern * p = table[i];
        while (p) {
            TexPattern * tmp = p;
            p = p->next;
            delete tmp;
        }
    }
}

void TexHyph::addPattern( TexPattern * pattern )
{
    int h = pattern->hash();
    TexPattern * * p = &table[h];
    while ( *p && pattern->cmp(*p)<0 )
        p = &((*p)->next);
    pattern->next = *p;
    *p = pattern;
    _pattern_count++;
}

void TexHyph::checkForModifiers( lString32 str )
{
    int len = str.length();
    for ( int i=0; i<len; i++ ) {
        if ( lGetCharProps(str[i]) & CH_PROP_MODIFIER ) {
            if ( _supported_modifiers.pos(str[i]) < 0 ) {
                 _supported_modifiers << str[i];
            }
        }
    }
}

lUInt32 TexHyph::getSize() {
    return _pattern_count * sizeof(TexPattern);
}

bool TexHyph::load( LVStreamRef stream )
{
    int w = isCorrectHyphFile(stream.get());
    int patternCount = 0;
    if (w) {
        _hash = stream->getcrc32();
        int        i;
        lvsize_t   dw;

        lvByteOrderConv cnv;

        int hyph_count = w;
        thyph hyph;

        lvpos_t p = 78 + (hyph_count * 8 + 2);
        stream->SetPos(p);
        if ( stream->SetPos(p)!=p )
            return false;
        lChar32 charMap[256] = { 0 };
        unsigned char buf[0x10000];
        // make char map table
        for (i=0; i<hyph_count; i++)
        {
            if ( stream->Read( &hyph, 522, &dw )!=LVERR_OK || dw!=522 )
                return false;
            cnv.msf( &hyph.len ); //rword(_main_hyph[i].len);
            lvpos_t newPos;
            if ( stream->Seek( hyph.len, LVSEEK_CUR, &newPos )!=LVERR_OK )
                return false;

            cnv.msf( hyph.wl );
            cnv.msf( hyph.wu );
            charMap[ (unsigned char)hyph.al ] = hyph.wl;
            charMap[ (unsigned char)hyph.au ] = hyph.wu;
//            lChar32 ch = hyph.wl;
//            CRLog::debug("wl=%s mask=%c%c", LCSTR(lString32(&ch, 1)), hyph.mask0[0], hyph.mask0[1]);
            if (hyph.mask0[0]!='0'||hyph.mask0[1]!='0') {
                unsigned char pat[4];
                pat[0] = hyph.al;
                pat[1] = hyph.mask0[0];
                pat[2] = hyph.mask0[1];
                pat[3] = 0;
                TexPattern * pattern = new TexPattern(pat, 1, charMap);
#if DUMP_PATTERNS==1
                CRLog::debug("Pattern: '%s' - %s", LCSTR(lString32(pattern->word)), pattern->attr );
#endif
                if (pattern->overflowed) {
                    // don't use truncated words
                    CRLog::warn("Pattern overflowed (%d > %d) and ignored: '%s'", pattern->overflowed, MAX_PATTERN_SIZE, LCSTR(lString32(pattern->word)));
                    if (pattern->overflowed > largest_overflowed_word)
                        largest_overflowed_word = pattern->overflowed;
                    delete pattern;
                }
                else {
                    addPattern( pattern );
                    patternCount++;
                }
            }
        }

        if ( stream->SetPos(p)!=p )
            return false;

        for (i=0; i<hyph_count; i++)
        {
            stream->Read( &hyph, 522, &dw );
            if (dw!=522)
                return false;
            cnv.msf( &hyph.len );

            stream->Read(buf, hyph.len, &dw);
            if (dw!=hyph.len)
                return false;

            unsigned char * p = buf;
            unsigned char * end_p = p + hyph.len;
            while ( p < end_p ) {
                lUInt8 sz = *p++;
                if ( p + sz > end_p )
                    break;
                TexPattern * pattern = new TexPattern( p, sz, charMap );
#if DUMP_PATTERNS==1
                CRLog::debug("Pattern: '%s' - %s", LCSTR(lString32(pattern->word)), pattern->attr);
#endif
                if (pattern->overflowed) {
                    // don't use truncated words
                    CRLog::warn("Pattern overflowed (%d > %d) and ignored: '%s'", pattern->overflowed, MAX_PATTERN_SIZE, LCSTR(lString32(pattern->word)));
                    if (pattern->overflowed > largest_overflowed_word)
                        largest_overflowed_word = pattern->overflowed;
                    delete pattern;
                }
                else {
                    addPattern( pattern );
                    patternCount++;
                }
                p += sz + sz + 1;
            }
        }
        // Note: support for diacritics/modifiers with checkForModifiers() not implemented

        return patternCount>0;
    } else {
        // tex xml format as for FBReader
        lString32Collection data;
        HyphPatternReader reader( data );
        LVXMLParser parser( stream, &reader );
        if ( !parser.CheckFormat() )
            return false;
        if ( !parser.Parse() )
            return false;
        if ( !data.length() )
            return false;
        for ( int i=0; i<(int)data.length(); i++ ) {
            data[i].lowercase();
            TexPattern * pattern = new TexPattern( data[i] );
#if DUMP_PATTERNS==1
            CRLog::debug("Pattern: (%s) '%s' - %s", LCSTR(data[i]), LCSTR(lString32(pattern->word)), pattern->attr);
#endif
            if (pattern->overflowed) {
                // don't use truncated words
                CRLog::warn("Pattern overflowed (%d > %d) and ignored: (%s) '%s'", pattern->overflowed, MAX_PATTERN_SIZE, LCSTR(data[i]), LCSTR(lString32(pattern->word)));
                if (pattern->overflowed > largest_overflowed_word)
                    largest_overflowed_word = pattern->overflowed;
                delete pattern;
            }
            else {
                addPattern( pattern );
                patternCount++;
                // Check for and remember diacritics found in patterns, so we don't ignore
                // them when present in word to hyphenate
                checkForModifiers( data[i] );
            }
        }
        return patternCount>0;
    }
}

bool TexHyph::load( lString32 fileName )
{
    LVStreamRef stream = LVOpenFileStream( fileName.c_str(), LVOM_READ );
    if ( stream.isNull() )
        return false;
    return load( stream );
}


bool TexHyph::match( const lChar32 * str, char * mask )
{
    bool found = false;
    TexPattern * res = table[ TexPattern::hash( str ) ];
    if ( res ) {
        found = res->match( str, mask ) || found;
    }
    res = table[ TexPattern::hash3( str ) ];
    if ( res ) {
        found = res->match( str, mask ) || found;
    }
    res = table[ TexPattern::hash2( str ) ];
    if ( res ) {
        found = res->match( str, mask ) || found;
    }
    res = table[ TexPattern::hash1( str ) ];
    if ( res ) {
        found = res->match( str, mask ) || found;
    }
    return found;
}

//TODO: do we need it?
///// returns false if there is rule disabling hyphenation at specified point
//static bool checkHyphenRules( const lChar32 * str, int len, int pos )
//{
//    if ( pos<1 || pos>len-3 )
//        return false;
//    lUInt16 props[2] = { 0, 0 };
//    lStr_getCharProps( str+pos+1, 1, props);
//    if ( props[0]&CH_PROP_ALPHA_SIGN )
//        return false;
//    if ( pos==len-3 ) {
//        lStr_getCharProps( str+len-2, 2, props);
//        return (props[0]&CH_PROP_VOWEL) || (props[1]&CH_PROP_VOWEL);
//    }
//    if ( pos==1 ) {
//        lStr_getCharProps( str, 2, props);
//        return (props[0]&CH_PROP_VOWEL) || (props[1]&CH_PROP_VOWEL);
//    }
//    return true;
//}

bool TexHyph::hyphenate( const lChar32 * str, int len, lUInt16 * widths, lUInt8 * flags, lUInt16 hyphCharWidth, lUInt16 maxWidth, size_t flagSize )
{
    if ( UserHyphDict::hasWords() ) {
        if ( UserHyphDict::hyphenate(str, len, widths, flags, hyphCharWidth, maxWidth, flagSize) )
            return true;
    }
    if ( HyphMan::_TrustSoftHyphens ) {
        if ( softhyphens_hyphenate(str, len, widths, flags, hyphCharWidth, maxWidth, flagSize) )
            return true;
    }
    if ( len<=3 )
        return false;
    if ( len>=WORD_LENGTH )
        len = WORD_LENGTH - 2;
    lChar32 word[WORD_LENGTH+4] = { 0 };
    char mask[WORD_LENGTH+4] = { 0 };

    // Make word from str, with soft-hyphens and modifiers (combining diacritics) stripped out.
    // Prepend and append a space so patterns can match word boundaries.
    bool has_ignorables = false;
    int ignorables_at_right = 0;
    int wlen;
    word[0] = ' ';
    int w = 1;
    for ( int i=0; i<len; i++ ) {
        if ( (lGetCharProps(str[i]) & (CH_PROP_MODIFIER|CH_PROP_HYPHEN)) ) {
            // Ignore combining diacritics and soft hyphens in the word we will check.
            // (We might want to do NFC composition, but handling/restoring the shifts is complicated.)
            has_ignorables = true;
            ignorables_at_right++; // Keep track of how many to ensure right_hyphen_min
            // But keep the ones present in the pattern file (assuming they are supported fully) in the word to be matched
            if (_supported_modifiers.pos(str[i]) >= 0) {
                word[w++] = str[i];
            }
        }
        else {
            word[w++] = str[i];
            ignorables_at_right = 0;
        }
    }
    wlen = w-1;
    word[w++] = ' ';
    if ( wlen<=3 )
        return false;
    lStr_lowercase(word+1, wlen);
    // printf("word:%s => #%s# (%d => %d)\n", LCSTR(lString32(str, len)), LCSTR(lString32(word)), len, wlen);

#if DUMP_HYPHENATION_WORDS==1
    CRLog::trace("word to hyphenate: '%s'", LCSTR(lString32(word)));
#endif

    // Find matches from dict patterns, at any position in word.
    // Places where hyphenation is allowed are put into 'mask'.
    memset( mask, '0', wlen+3 );	// 0x30!
    bool found = false;
    for ( int i=0; i<=wlen; i++ ) {
        found = match( word + i, mask + i ) || found;
    }
    if ( !found )
        return false;

#if DUMP_HYPHENATION_WORDS==1
    lString32 buf;
    lString32 buf2;
    bool boundFound = false;
    for ( int i=0; i<wlen; i++ ) {
        buf << word[i+1];
        buf2 << word[i+1];
        buf2 << (lChar32)mask[i+2];
        // This maxWidth check may be wrong here (in the dump only) because
        // of a +1 shift and possible more shifts due to soft-hyphens.
        int nw = widths[i]+hyphCharWidth;
        if ( (mask[i+2]&1) ) {
            buf << (lChar32)'-';
            buf2 << (lChar32)'-';
        }
        if ( nw>maxWidth && !boundFound ) {
            buf << (lChar32)'|';
            buf2 << (lChar32)'|';
            boundFound = true;
//            buf << (lChar32)'-';
//            buf2 << (lChar32)'-';
        }
    }
    CRLog::trace("Hyphenate: %s  %s", LCSTR(buf), LCSTR(buf2) );
#endif

    // Use HyphMan global left/right hyphen min, unless set to 0 (the default)
    // which means we should use the HyphMethod specific values.
    int left_hyphen_min = HyphMan::_LeftHyphenMin ? HyphMan::_LeftHyphenMin : _left_hyphen_min;
    int right_hyphen_min = HyphMan::_RightHyphenMin ? HyphMan::_RightHyphenMin : _right_hyphen_min;

    // Moves allowed hyphenation positions from 'mask' to the provided 'flags',
    // taking soft-hyphen shifts into account
    int ignorables_skipped = 0;
    bool res = false;
    for ( int p=0 ; p<=len-2; p++ ) {
        // printf(" char %c\n", str[p]);
        if ( has_ignorables && (lGetCharProps(str[p]) & (CH_PROP_MODIFIER|CH_PROP_HYPHEN)) && (_supported_modifiers.pos(str[p]) < 0) ) {
            ignorables_skipped++;
            continue;
        }
        if (p - ignorables_skipped < left_hyphen_min - 1)
            continue;
        if (p > len - ignorables_at_right - right_hyphen_min - 1)
            continue;
        // hyphenate
        //00010030100
        int nw = widths[p]+hyphCharWidth;
        // printf(" word %c\n", word[p+1-ignorables_skipped]);
        // p+2 because: +1 because word has a space prepended, and +1 because
        // mask[] holds the flag for char n on slot n+1
        if ( (mask[p + 2 - ignorables_skipped] & 1) && nw <= maxWidth ) {
            if ( has_ignorables ) {
                // Move over any diacritics (whether or not present in the patterns) so ALLOW_HYPH_WRAP_AFTER
                // is set after all of them
                while ( p<=len-2 && (lGetCharProps(str[p+1]) & (CH_PROP_MODIFIER|CH_PROP_HYPHEN)) ) {
                    if ( _supported_modifiers.pos(str[p]) < 0 ) {
                        ignorables_skipped++;
                    }
                    p++;
                    if (p > len - ignorables_at_right - right_hyphen_min - 1)
                        return res;
                }
            }
            if ( flagSize == 2 ) {
                lUInt16* flags16 = (lUInt16*) flags;
                flags16[p] |= LCHAR_ALLOW_HYPH_WRAP_AFTER;
            }
            else {
                flags[p] |= LCHAR_ALLOW_HYPH_WRAP_AFTER;
            }
            // printf(" allowed after %c\n", str[p]);
            res = true;
        }
    }
    return res;
}

bool AlgoHyph::hyphenate( const lChar32 * str, int len, lUInt16 * widths, lUInt8 * flags, lUInt16 hyphCharWidth, lUInt16 maxWidth, size_t flagSize )
{
    if ( UserHyphDict::hasWords() ) {
        if ( UserHyphDict::hyphenate(str, len, widths, flags, hyphCharWidth, maxWidth, flagSize) )
            return true;
    }
    if ( HyphMan::_TrustSoftHyphens ) {
        if ( softhyphens_hyphenate(str, len, widths, flags, hyphCharWidth, maxWidth, flagSize) )
            return true;
    }

    // Use HyphMan global left/right hyphen min, unless set to 0 (the default)
    // which means we should use the HyphMethod specific values.
    int left_hyphen_min = HyphMan::_LeftHyphenMin ? HyphMan::_LeftHyphenMin : _left_hyphen_min;
    int right_hyphen_min = HyphMan::_RightHyphenMin ? HyphMan::_RightHyphenMin : _right_hyphen_min;

    if (len < left_hyphen_min + right_hyphen_min)
        return false; // too small

    lUInt16 chprops[WORD_LENGTH];
    if ( len > WORD_LENGTH-2 )
        len = WORD_LENGTH - 2;
    lStr_getCharProps( str, len, chprops );

    int min = len+1;
    int nb_visible = 0;
    for (int i=0; i <len; i++) {
        if (chprops[i] & CH_PROP_ALPHA)
            nb_visible++;
        if (nb_visible >= left_hyphen_min) {
            min = i; // We can't allow-wrap-after before this car
            break;
        }
    }
    int max = -1;
    nb_visible = 0;
    bool vowel_seen = false;
    for (int i=len-1; i >=0; i--) {
        if ( chprops[i] & CH_PROP_ALPHA )
            nb_visible++;
        bool is_vowel = CH_PROP_IS_VOWEL(chprops[i]);
        if ( is_vowel )
            vowel_seen = true;
        if ( nb_visible >= right_hyphen_min ) {
            max = i-1; // We can't allow-wrap-after after this car
            if ( vowel_seen ) // We wan't at least one vowel on the right part of hyphenation
                break;
        }
    }
    if ( min > max )
        return false;

    // We may walk multiple times over some parts of the word.
    // On each walk, we skip diacritic/modifiers and soft hyphens.
    // We stop on a vowel (this, and the above 'max' check, ensure we will have a vowel on each side of the hyphenation)
    // If it is followed by 2 consonants, we allow hyph wran between these 2 consonants.
    // If it is followed by 1 consonant and a russian alpha-sign, we allow hyph wran between these.
    // Otherwise, we allow wrap after the vowel.
    for ( int i=0; i <= max; i++) {
        if ( widths[i] > maxWidth )
            break;
        if ( CH_PROP_IS_VOWEL(chprops[i]) ) { // Go see what's after
            while ( i+1 < len && (chprops[i+1] & (CH_PROP_MODIFIER|CH_PROP_HYPHEN)) ) {
                i++; // position on the last of any diacritic/hyphen combining with this vowel
            }
            if (i > max)
                break;
            int next = i+1; // not a modifier/hyphen
            int next2 = next+1; // might be a modifier/hyphen
            while ( next2 < len && (chprops[next2] & (CH_PROP_MODIFIER|CH_PROP_HYPHEN)) ) {
                next2++;
            }
            if ( CH_PROP_IS_CONSONANT(chprops[next]) && CH_PROP_IS_CONSONANT(chprops[next2]) )
                i = next;
            else if ( CH_PROP_IS_CONSONANT(chprops[next]) && CH_PROP_IS_ALPHA_SIGN(chprops[next2]) )
                i = next2;
            // otherwise, allow wrap after this vowel
            if (i > max)
                break;
            if ( i >= min ) {
                // insert hyphenation mark
                lUInt16 nw = widths[i] + hyphCharWidth;
                if ( nw<maxWidth ) {
                    // We prevent hyphenation between these consonants
                    // (should we check that earlier, and break after the preceding vowel
                    // instead of not at all?)
                    bool forbidden = false;
                    const lChar32 * dblSequences[] = {
                        U"sh", U"th", U"ph", U"ch", NULL
                    };
                    next = i+1;
                    while ( next < len && (chprops[next] & (CH_PROP_MODIFIER|CH_PROP_HYPHEN)) ) {
                        next++;
                    }
                    for (int k=0; dblSequences[k]; k++) {
                        if ( str[i]==dblSequences[k][0] && str[next]==dblSequences[k][1] ) {
                            forbidden = true;
                            break;
                        }
                    }
                    if ( !forbidden ) {
                        if ( flagSize == 2 ) {
                            lUInt16* flags16 = (lUInt16*) flags;
                            flags16[i] |= LCHAR_ALLOW_HYPH_WRAP_AFTER;
                        }
                        else {
                            flags[i] |= LCHAR_ALLOW_HYPH_WRAP_AFTER;
                        }
                    }
                }
            }
        }
    }
    return true;
}

AlgoHyph::~AlgoHyph()
{
}

/*
 * -------------------------------------------
 *               UserHyphDict
 * -------------------------------------------
*/

#define HYPH_HASH_MULT 7

lString32 UserHyphDict::_filename = U"";
size_t UserHyphDict::_filesize = 0;
lUInt32 UserHyphDict::_hash_value = 0;

lUInt32 UserHyphDict::words_in_memory = 0;
lString32* UserHyphDict::words = 0;
char** UserHyphDict::masks = 0;

UserHyphDict::UserHyphDict()
{
}

UserHyphDict::~UserHyphDict()
{
    release();
}

// free memory used by user hyphenation dictionary
void UserHyphDict::release()
{
    for ( size_t i = 0; i<words_in_memory; ++i ) {
        free(masks[i]);
        words[i].clear();
    }

    if ( words_in_memory>0 ) {
        free(masks);
        delete[] words;
    }

    words_in_memory = 0;
    _filename = U"";
    _filesize = 0;
    _hash_value = 0;
}

lUInt8 UserHyphDict::addEntry(const char *word, const char* hyphenation)
{
    // copy word to user dict, lowercase
    words[words_in_memory] = Utf8ToUnicode(word).lowercase();
    size_t word_len = words[words_in_memory].length();

    lString32 hyphenation_lower = Utf8ToUnicode(hyphenation).lowercase();
    size_t hyphenation_len = hyphenation_lower.length();

    // generate mask
    masks[words_in_memory] = (char*) malloc((word_len+1) * sizeof(char)); // +1 for termination

    size_t hyphenation_pos = 1;
    size_t i = hyphenation_pos;
    for ( ; i < word_len && hyphenation_pos < hyphenation_len; ++i, ++hyphenation_pos ) {
        if ( hyphenation_lower[hyphenation_pos] != '-' ) {
            masks[words_in_memory][i-1] = '0';
        } else {
            masks[words_in_memory][i-1] = '1';
            ++hyphenation_pos;
        }
    }
    masks[words_in_memory][word_len-1]= '0';
    masks[words_in_memory][word_len] = '\0';

    // check if alphabetically sorted
    if ( words_in_memory > 0 && words[words_in_memory].compare(words[words_in_memory-1]) <= 0 ) {
        printf("CRE WARNING: UserHyphDict dictionary not sorted %s/%s\n", LCSTR(words[words_in_memory-1]), LCSTR(words[words_in_memory]));
        return USER_HYPH_DICT_ERROR_NOT_SORTED;
    }

    // sanity check, if entry is ok
    if ( hyphenation_pos != hyphenation_len || i != word_len) {
        printf("CRE WARNING: UserHyphDict malformed entry %s;%s\n", word, hyphenation);
        free(masks[words_in_memory]);
        words[words_in_memory].clear();
        return USER_HYPH_DICT_MALFORMED;
    }

    ++words_in_memory;
    return true;
}

// (Re)initializes the user hyphen dict, if the filename and/or filesize have changed.
// filename ... filename of the user hyphen dictionary. An empty string releases the dict.
//              format: one entry per line, no spaces: hyphenation;hyph-en-ation
//                                                     sauerstoffflasche;sauer-stoff-fla-sche
// reload==true -> do also check if the hash of the requested dict matches the loaded one
// returns    USER_HYPH_DICT_RELOAD
//            USER_HYPH_DICT_NOCHANGE
//            USER_HYPH_DICT_MALFORMED
//            USER_HYPH_DICT_ERROR_NOT_SORTED
lUInt8 UserHyphDict::init(lString32 filename, bool reload)
{
    if ( filename.length() == 0 ) {
        release();
        return USER_HYPH_DICT_RELOAD;
    }
    LVStreamRef instream = LVOpenFileStream( filename.c_str(), LVOM_READ );
    if ( !instream ) {
        release();
        printf("CRE WARNING: UserHyphDict cannot open file: %s\n", LCSTR(filename));
        return USER_HYPH_DICT_RELOAD;
    }

    size_t filesize = instream->GetSize();
    if ( _filename.compare(filename)==0 && _filesize == filesize && not reload ) {
        return USER_HYPH_DICT_NOCHANGE;
    }

    // buffer to hold user hyphenation file
    char *buf = (char*) malloc(filesize * sizeof(char));

    lvsize_t count = 0;
    instream->Read(buf, filesize, &count);

    lUInt32 hash_value = 0;
    unsigned words_in_file = 0;
    for ( lvsize_t i=0; i<count; ++i) {
        hash_value = ( hash_value * HYPH_HASH_MULT ) ^ buf[i];
        if ( buf[i] == '\r' &&  i+1<filesize && buf[i+1] == '\n' ) {
            ++i;
            ++words_in_file;
        } else if ( buf[i] == '\n' || buf[i] == '\r' )
            ++words_in_file;
    }

    if (words_in_file == 0) {
        free(buf);
        release();
        return USER_HYPH_DICT_RELOAD;
    }

    // do fast thourogh check if requested dictionary matches the loaded one
    if ( _filename.compare(filename)==0 && _filesize == filesize && _hash_value == hash_value ) {
        free(buf);
        return USER_HYPH_DICT_NOCHANGE;
    }
    else
        release();

    _filename = filename;
    _filesize = filesize;
    _hash_value = hash_value;

    lUInt8 return_value = USER_HYPH_DICT_RELOAD;

    words = new lString32[words_in_file];
    masks = (char**) calloc(words_in_file, sizeof(char*) );

    #define HYPHENATION_LENGTH (WORD_LENGTH*2+2)  // hyphenation can get longer than word
    char word[WORD_LENGTH];
    char mask[HYPHENATION_LENGTH];
    lvsize_t pos = 0; // pos in puffer
    while (pos < count ) {
        unsigned i;
        for ( i = 0; i < WORD_LENGTH-1; ++i ) { // -1 because of trailing NULL
            if ( buf[pos] == ';' ) {
                ++pos;
                break;
            }
            word[i] = buf[pos++];   //todo check case
        }
        word[i] = '\0';
        if ( i == WORD_LENGTH )
            printf("CRE WARNING: UserHyphDict dictionary word too long: '%s'\n", word);

        for ( i = 0; i<HYPHENATION_LENGTH-1; ++i ) { // -1 because of tailling NULL
            if ( buf[i] == '\r' &&  i+1<filesize && buf[i+1] == '\n' ) {
                pos += 2;
                break;
            }
            else if ( buf[pos] == '\n' || buf[pos] == '\r' ) {
                ++pos;
                break;
            }
            mask[i] = buf[pos++];
        }
        mask[i] = '\0';
        if ( i == HYPHENATION_LENGTH )
            printf("CRE WARNING: UserHyphDict hyphenation too long: '%s'\n", mask);

        lUInt8 tmp = addEntry(word, mask);
        if ( tmp == USER_HYPH_DICT_MALFORMED )
            return_value = USER_HYPH_DICT_MALFORMED;
        else if ( tmp == USER_HYPH_DICT_ERROR_NOT_SORTED ) {
            free(buf);
            release();
            return USER_HYPH_DICT_ERROR_NOT_SORTED;
        }
    }
    if ( words_in_memory != words_in_file )
        printf("CRE WARNING: UserHyphDict %d words ignored\n", words_in_file - words_in_memory);

    free(buf);
    return return_value;
}

bool UserHyphDict::getMask(lChar32 *word, char *mask)
{
    if ( words_in_memory == 0 )
        return false; // no dictionary, or dictionary not initialized

    // dictionary should be alphabetically sorted
    // so don't search the whole dict. -> binarySearch is faster
#if 1 == 0  // use this only for tests as this might get really slow on big dictionaries
    size_t i = 0;
    while ( i < words_in_memory && words[i].compare(word) < 0 )
        ++i;
    if ( i < words_in_memory && words[i].compare(word) == 0) {
        lStr_cpy(mask, masks[i]);
        return true;
    }
    return false;
#endif

    size_t left = 0;
    size_t right = words_in_memory-1;
    size_t mid;
    while ( left <= right )
    {
        mid = left + (right-left)/2;
        int cmp = words[mid].compare(word);
        if ( cmp == 0 ) {
            lStr_cpy(mask, masks[mid]);
            return true;
        }
        else if ( cmp < 0 )
            left = mid + 1;
        else {
            if ( mid == 0 )
                break; // as right is unsigned and cannot be -1!
            right = mid - 1;
        }
    }
    return false;
}

// get the hyphenation for word; shows all hyphenation positions, don't obey _xxx_hyphen_min
// use lStr_findWordBounds to trim the word
// return: hyphenated word
// e.g.: Danger -> Dan-ger
// This is not performance critical, as it is done only for single words on user interaction.
lString32 UserHyphDict::getHyphenation(const char *word)
{
    lString32 orig_word_str(word);
    int orig_len = orig_word_str.length();

    // Given some combined words like stairway2heaven, we want to get the first potential part
    // as the candidate for hyphenation for a clearer layout, with smaller gaps.
    // Imagine the following lines:
    //
    // |<--      page width       -->|
    // bla bla bla bla bla bla stairway2heaven
    // bla bla bla bla bla stairway2heaven
    //
    // A hypenation at stairway would give us a hyphenation in both cases,
    // whereas hyphenating at heaven will only hyphenate the later case.
    // -> So hyphenation at stairway will yield smaller gaps.

    // As lStr_findWordBounds() will only look on the left side of the provided "pos" (our f_start here),
    // and as orig_word may start with some non-alpha that would be ignored, if we want to find the first part
    // of a combined word, we need to increase f_start until some start of word is found (which will make start != end)
    bool is_rtl = false;
    int start = 0;
    int end = 0;
    int f_start = 1;
    while (start == end && f_start <= orig_len) {
        lStr_findWordBounds( orig_word_str.c_str(), orig_len, f_start, start, end, is_rtl);
        f_start++;
    }

    lString32 word_str(orig_word_str.c_str() + start, end - start);
    size_t len = word_str.length();
    lUInt16 widths[len+2]; // NOLINT(clang-diagnostic-vla-cxx-extension)
    lUInt8 flags[len+2]; // NOLINT(clang-diagnostic-vla-cxx-extension)

    for ( size_t i = 0; i < len; ++i ) {
        widths[i] = 0;
        flags[i] = 0;
    }

    TextLangMan::getTextLangCfg()->getHyphMethod()->hyphenate(word_str.c_str(), len, widths, flags, 0, 0xFFFF, 1);

    lString32 hyphenation;
    size_t i;
    for ( i=0; i<len; ++i ) {
        hyphenation += word_str[i];
        if (flags[i] & LCHAR_ALLOW_HYPH_WRAP_AFTER )
            hyphenation += "-";
    }
    return hyphenation;
}

bool UserHyphDict::hyphenate( const lChar32 * str, int len, lUInt16 * widths, lUInt8 * flags, lUInt16 hyphCharWidth, lUInt16 maxWidth, size_t flagSize )
{
    if ( !UserHyphDict::hasWords() ) {
        return false;
    }
    if ( len<=3 )
        return false;

    if ( len>=WORD_LENGTH )
        len = WORD_LENGTH - 2;
    lChar32 word[WORD_LENGTH+4] = { 0 };
    char mask[WORD_LENGTH+4] = { 0 };

    // Make word from str, with soft-hyphens stripped out.
    int wlen;
    int w = 0;
    for ( int i=0; i<len; i++ ) {
        if ( str[i] != UNICODE_SOFT_HYPHEN_CODE ) {
            word[w++] = str[i];
        }
    }
    wlen = w-1;
    if ( wlen<3 ) // don't hyphenate words with three letters
        return false;
    lStr_lowercase(word, wlen);
    // printf("word:%s => #%s# (%d => %d)\n", LCSTR(lString32(str, len)), LCSTR(lString32(word)), len, wlen);
    memset( mask, '0', wlen+3 );

    if ( !UserHyphDict::getMask(word, mask) ) {
        return false;
    }

    for ( int i = 0 ; i<len ; ++i ) {
        if ( widths[i] + hyphCharWidth > maxWidth )
            break;
        if ( mask[i] == '1' ) {
            if ( flagSize == 2 ) {
                lUInt16* flags16 = (lUInt16*) flags;
                flags16[i] |= LCHAR_ALLOW_HYPH_WRAP_AFTER;
            }
            else {
                flags[i] |= LCHAR_ALLOW_HYPH_WRAP_AFTER;
            }
        }
    }
    return true;
}
