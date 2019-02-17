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
#include FT_FREETYPE_H

#if USE_HARFBUZZ==1
#include <hb.h>
#include <hb-ft.h>
#include "lvhashtable.h"
#endif

#if (USE_FONTCONFIG==1)
    #include <fontconfig/fontconfig.h>
#endif

#endif


#if COLOR_BACKBUFFER==0
//#define USE_BITMAP_FONT
#endif

#define MAX_LINE_CHARS 2048
#define MAX_LINE_WIDTH 2048
#define MAX_LETTER_SPACING MAX_LINE_WIDTH/2


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
	lString16Collection faces;
	getFaceList(faces);
	// find first matched
	for (int i = 0; i < list.length(); i++) {
		lString8 wantFace = list[i];
		for (int j = 0; j < faces.length(); j++) {
			lString16 haveFace = faces[j];
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
//    gammaLevel = cr_ft_gamma_levels[GAMMA_LEVELS/2];
//    gammaIndex = GAMMA_LEVELS/2;
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
LVEmbeddedFontDef * LVEmbeddedFontList::findByUrl(lString16 url) {
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

bool LVEmbeddedFontList::add(lString16 url, lString8 face, bool bold, bool italic) {
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
int LVFont::getVisualAligmentWidth()
{
    FONT_GUARD
    if ( _visual_alignment_width==-1 ) {
        lChar16 chars[] = { getHyphChar(), ',', '.', '!', '?', ':', ';', (lChar16)L'，', (lChar16)L'。', (lChar16)L'！', 0 };
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

static lChar16 getReplacementChar( lUInt32 code ) {
    switch (code) {
    case UNICODE_SOFT_HYPHEN_CODE:
        return '-';
    case 0x0401: // CYRILLIC CAPITAL LETTER IO
        return 0x0415; //CYRILLIC CAPITAL LETTER IE
    case 0x0451: // CYRILLIC SMALL LETTER IO
        return 0x0435; // CYRILLIC SMALL LETTER IE
    case UNICODE_NO_BREAK_SPACE:
        return ' ';
    case UNICODE_ZERO_WIDTH_SPACE:
        // If the font lacks a zero-width breaking space glyph (like
        // some Kindle built-ins) substitute a different zero-width
        // character instead of one with width.
        return UNICODE_ZERO_WIDTH_NO_BREAK_SPACE;
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
        return '*';
    case 0x26AA: // css_lst_disc:
    case 0x25E6: // css_lst_disc:
    case 0x25CF: // css_lst_disc:
        return 'o';
    case 0x25CB: // css_lst_circle:
        return '*';
    case 0x25A0: // css_lst_square:
        return '-';
    }
    return 0;
}



/**
    \brief Font properties definition
*/
class LVFontDef
{
private:
    int               _size;
    int               _weight;
    int               _italic;
    css_font_family_t _family;
    lString8          _typeface;
    lString8          _name;
    int               _index;
    // for document font: _documentId, _buf, _name
    int               _documentId;
    LVByteArrayRef    _buf;
    int               _bias;
public:
    LVFontDef(const lString8 & name, int size, int weight, int italic, css_font_family_t family, const lString8 & typeface, int index=-1, int documentId=-1, LVByteArrayRef buf = LVByteArrayRef())
    : _size(size)
    , _weight(weight)
    , _italic(italic)
    , _family(family)
    , _typeface(typeface)
    , _name(name)
    , _index(index)
    , _documentId(documentId)
    , _buf(buf)
    , _bias(0)
    {
    }
    LVFontDef(const LVFontDef & def)
    : _size(def._size)
    , _weight(def._weight)
    , _italic(def._italic)
    , _family(def._family)
    , _typeface(def._typeface)
    , _name(def._name)
    , _index(def._index)
    , _documentId(def._documentId)
    , _buf(def._buf)
    , _bias(def._bias)
    {
    }

    /// returns true if definitions are equal
    bool operator == ( const LVFontDef & def ) const
    {
        return ( _size == def._size || _size == -1 || def._size == -1 )
            && ( _weight == def._weight || _weight==-1 || def._weight==-1 )
            && ( _italic == def._italic || _italic==-1 || def._italic==-1 )
            && _family == def._family
            && _typeface == def._typeface
            && _name == def._name
            && ( _index == def._index || def._index == -1 )
            && (_documentId == def._documentId || _documentId == -1)
            ;
    }

    lUInt32 getHash() {
        return ((((_size * 31) + _weight)*31  + _italic)*31 + _family)*31 + _name.getHash();
    }

    /// returns font file name
    lString8 getName() const { return _name; }
    void setName( lString8 name) {  _name = name; }
    int getIndex() const { return _index; }
    void setIndex( int index ) { _index = index; }
    int getSize() const { return _size; }
    void setSize( int size ) { _size = size; }
    int getWeight() const { return _weight; }
    void setWeight( int weight ) { _weight = weight; }
    bool getItalic() const { return _italic!=0; }
    bool isRealItalic() const { return _italic==1; }
    void setItalic( int italic ) { _italic=italic; }
    css_font_family_t getFamily() const { return _family; }
    void getFamily( css_font_family_t family ) { _family = family; }
    lString8 getTypeFace() const { return _typeface; }
    void setTypeFace(lString8 tf) { _typeface = tf; }
    int getDocumentId() { return _documentId; }
    void setDocumentId(int id) { _documentId = id; }
    LVByteArrayRef getBuf() { return _buf; }
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
    void clear() { _registered_list.clear(); _instance_list.clear(); }
    void gc(); // garbage collector
    void update( const LVFontDef * def, LVFontRef ref );
    void removeDocumentFonts(int documentId);
    int  length() { return _registered_list.length(); }
    void addInstance( const LVFontDef * def, LVFontRef ref );
    bool setAsPreferredFontWithBias( lString8 face, int bias, bool clearOthersBias );
    LVPtrVector< LVFontCacheItem > * getInstances() { return &_instance_list; }
    LVFontCacheItem * find( const LVFontDef * def, bool useBias=false );
    LVFontCacheItem * findFallback( lString8 face, int size );
    LVFontCacheItem * findDuplicate( const LVFontDef * def );
    LVFontCacheItem * findDocumentFontDuplicate(int documentId, lString8 name);
    /// get hash of installed fonts and fallback font
    virtual lUInt32 GetFontListHash(int documentId) {
        lUInt32 hash = 0;
        for ( int i=0; i<_registered_list.length(); i++ ) {
            int doc = _registered_list[i]->getDef()->getDocumentId();
            if (doc == -1 || doc == documentId) // skip document fonts
                hash = hash + _registered_list[i]->getDef()->getHash();
        }
        return 0;
    }
    virtual void getFaceList( lString16Collection & list )
    {
        list.clear();
        for ( int i=0; i<_registered_list.length(); i++ ) {
            if (_registered_list[i]->getDef()->getDocumentId() != -1)
                continue;
            lString16 name = Utf8ToUnicode( _registered_list[i]->getDef()->getTypeFace() );
            if ( !list.contains(name) )
                list.add( name );
        }
        list.sort();
    }
    virtual void getFontFileNameList(lString16Collection &list)
    {
        list.clear();
        for ( int i=0; i<_registered_list.length(); i++ ) {
            if (_registered_list[i]->getDef()->getDocumentId() == -1) {
                lString16 name = Utf8ToUnicode(_registered_list[i]->getDef()->getName());
                if (!list.contains(name))
                    list.add(name);
            }
        }
        list.sort();
    }
    virtual void clearFallbackFonts()
    {
        LVPtrVector< LVFontCacheItem > * fonts = getInstances();
        for ( int i=0; i<fonts->length(); i++ ) {
            fonts->get(i)->getFont()->setFallbackFont(LVFontRef());
        }
        for ( int i=0; i<_registered_list.length(); i++ ) {
            if (!_registered_list[i]->getFont().isNull())
                _registered_list[i]->getFont()->setFallbackFont(LVFontRef());
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
    lUInt16 get( lChar16 ch )
    {
        FONT_GLYPH_CACHE_GUARD
        int inx = (ch>>9) & 0x1ff;
        if (inx >= COUNT) return CACHED_UNSIGNED_METRIC_NOT_SET;
        lUInt16 * ptr = ptrs[inx];
        if ( !ptr )
            return CACHED_UNSIGNED_METRIC_NOT_SET;
        return ptr[ch & 0x1FF ];
    }
    void put( lChar16 ch, lUInt16 m )
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
    lInt16 get( lChar16 ch )
    {
        return (lInt16) ( LVFontGlyphUnsignedMetricCache::get(ch) - CACHED_SIGNED_METRIC_SHIFT );
    }
    void put( lChar16 ch, lInt16 m )
    {
        LVFontGlyphUnsignedMetricCache::put(ch, (lUInt16)( m + CACHED_SIGNED_METRIC_SHIFT) );
    }
};


class LVFreeTypeFace;
static LVFontGlyphCacheItem * newItem( LVFontLocalGlyphCache * local_cache, lChar16 ch, FT_GlyphSlot slot ) // , bool drawMonochrome
{
    FONT_LOCAL_GLYPH_CACHE_GUARD
    FT_Bitmap*  bitmap = &slot->bitmap;
    int w = bitmap->width;
    int h = bitmap->rows;
    LVFontGlyphCacheItem * item = LVFontGlyphCacheItem::newItem(local_cache, ch, w, h );
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
    } else {
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
        } else {
#endif
            memcpy( item->bmp, bitmap->buffer, w*h );
            // correct gamma
            if ( gammaIndex!=GAMMA_NO_CORRECTION_INDEX )
                cr_correct_gamma_buf(item->bmp, w*h, gammaIndex);
    }
    item->origin_x =   (lInt16)slot->bitmap_left;
    item->origin_y =   (lInt16)slot->bitmap_top;
    item->advance =    (lUInt16)(myabs(slot->metrics.horiAdvance) >> 6);
    return item;
}

#if USE_HARFBUZZ==1
static LVFontGlyphIndexCacheItem * newItem(lUInt32 index, FT_GlyphSlot slot )
{
	FONT_LOCAL_GLYPH_CACHE_GUARD
	FT_Bitmap*  bitmap = &slot->bitmap;
	int w = bitmap->width;
	int h = bitmap->rows;
	LVFontGlyphIndexCacheItem* item = LVFontGlyphIndexCacheItem::newItem(index, w, h );
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
	} else {
			memcpy( item->bmp, bitmap->buffer, w*h );
			// correct gamma
			if ( gammaIndex!=GAMMA_NO_CORRECTION_INDEX )
				cr_correct_gamma_buf(item->bmp, w*h, gammaIndex);
	}
	item->origin_x =   (lInt16)slot->bitmap_left;
	item->origin_y =   (lInt16)slot->bitmap_top;
	item->advance =    (lUInt16)(myabs(slot->metrics.horiAdvance) >> 6);
	return item;
}
#endif

void LVFontLocalGlyphCache::clear()
{
    FONT_LOCAL_GLYPH_CACHE_GUARD
    while ( head ) {
        LVFontGlyphCacheItem * ptr = head;
        remove( ptr );
        global_cache->remove( ptr );
        LVFontGlyphCacheItem::freeItem( ptr );
    }
}

LVFontGlyphCacheItem * LVFontLocalGlyphCache::get( lUInt32 ch )
{
    FONT_LOCAL_GLYPH_CACHE_GUARD
    LVFontGlyphCacheItem * ptr = head;
    for ( ; ptr; ptr = ptr->next_local ) {
        if ( ptr->ch == ch ) {
            global_cache->refresh( ptr );
            return ptr;
        }
    }
    return NULL;
}

void LVFontLocalGlyphCache::put( LVFontGlyphCacheItem * item )
{
    FONT_LOCAL_GLYPH_CACHE_GUARD
    global_cache->put( item );
    item->next_local = head;
    if ( head )
        head->prev_local = item;
    if ( !tail )
        tail = item;
    head = item;
}

/// remove from list, but don't delete
void LVFontLocalGlyphCache::remove( LVFontGlyphCacheItem * item )
{
    FONT_LOCAL_GLYPH_CACHE_GUARD
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
}

void LVFontGlobalGlyphCache::refresh( LVFontGlyphCacheItem * item )
{
    FONT_GLYPH_CACHE_GUARD
    if ( tail!=item ) {
        //move to head
        removeNoLock( item );
        putNoLock( item );
    }
}

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
    size -= item->getSize();
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
    if ( faceName == "Arial" && face->style_name && !strcmp(face->style_name, "Narrow") )
        faceName << " " << face->style_name;
    else if ( /*faceName == "Arial" &&*/ face->style_name && strstr(face->style_name, "Condensed") )
        faceName << " " << "Condensed";
    return faceName;
}

// The 2 slots with "LCHAR_IS_SPACE | LCHAR_ALLOW_WRAP_AFTER" on the 2nd line previously
// were: "LCHAR_IS_SPACE | LCHAR_IS_EOL | LCHAR_ALLOW_WRAP_AFTER".
// LCHAR_IS_EOL was not used by any code, and has been replaced by LCHAR_IS_LIGATURE_TAIL
// (as flags are usually lUInt8, and the 8 bits were used, one needed to be dropped).
static lUInt16 char_flags[] = {
    0, 0, 0, 0, 0, 0, 0, 0, // 0    00
    0, 0, LCHAR_IS_SPACE | LCHAR_ALLOW_WRAP_AFTER, 0, 0, LCHAR_IS_SPACE | LCHAR_ALLOW_WRAP_AFTER, 0, 0, // 8    08
    0, 0, 0, 0, 0, 0, 0, 0, // 16   10
    0, 0, 0, 0, 0, 0, 0, 0, // 24   18
    LCHAR_IS_SPACE | LCHAR_ALLOW_WRAP_AFTER, 0, 0, 0, 0, 0, 0, 0, // 32   20
    0, 0, 0, 0, 0, LCHAR_DEPRECATED_WRAP_AFTER, 0, 0, // 40   28
    0, 0, 0, 0, 0, 0, 0, 0, // 48   30
};

// removed, as soft hyphens are now exclusively handled by hyphman:
//      (ch==UNICODE_SOFT_HYPHEN_CODE?LCHAR_ALLOW_WRAP_AFTER:
#define GET_CHAR_FLAGS(ch) \
     (ch<48?char_flags[ch]: \
        (ch==UNICODE_NO_BREAK_SPACE?LCHAR_IS_SPACE: \
        (ch==UNICODE_HYPHEN?LCHAR_DEPRECATED_WRAP_AFTER: \
        (ch==UNICODE_ZERO_WIDTH_SPACE?LCHAR_ALLOW_WRAP_AFTER: \
        (ch==UNICODE_THIN_SPACE?LCHAR_ALLOW_WRAP_AFTER: \
         0)))))

struct LVCharTriplet
{
    lChar16 prevChar;
    lChar16 Char;
    lChar16 nextChar;
    bool operator==(const struct LVCharTriplet& other) {
        return prevChar == other.prevChar && Char == other.Char && nextChar == other.nextChar;
    }
};

struct LVCharPosInfo
{
    int offset;
    int width;
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

class LVFreeTypeFace : public LVFont
{
protected:
    LVMutex &      _mutex;
    lString8      _fileName;
    lString8      _faceName;
    css_font_family_t _fontFamily;
    FT_Library    _library;
    FT_Face       _face;
    FT_GlyphSlot  _slot;
    FT_Matrix     _matrix;                 /* transformation matrix */
    int           _size; // caracter height in pixels
    int           _height; // full line height in pixels
    int           _hyphen_width;
    int           _baseline;
    int            _weight;
    int            _italic;
    LVFontGlyphUnsignedMetricCache   _wcache;   // glyph width cache
    LVFontGlyphSignedMetricCache     _lsbcache; // glyph left side bearing cache
    LVFontGlyphSignedMetricCache     _rsbcache; // glyph right side bearing cache
    LVFontLocalGlyphCache _glyph_cache;
    bool          _drawMonochrome;
    hinting_mode_t _hintingMode;
    kerning_mode_t _kerningMode;
    bool          _fallbackFontIsSet;
    LVFontRef     _fallbackFont;
#if USE_HARFBUZZ==1
    hb_font_t* _hb_font;
    // For use with KERNING_MODE_HARFBUZZ:
    hb_buffer_t* _hb_buffer;
    hb_feature_t _hb_features[2];
    LVHashTable<lUInt32, LVFontGlyphIndexCacheItem*> _glyph_cache2;
    // For use with KERNING_MODE_HARFBUZZ_LIGHT:
    hb_buffer_t* _hb_light_buffer;
    hb_feature_t _hb_light_features[2];
    LVHashTable<struct LVCharTriplet, struct LVCharPosInfo> _width_cache2;
#endif
public:

    // fallback font support
    /// set fallback font for this font
    virtual void setFallbackFont( LVFontRef font ) {
        _fallbackFont = font;
        _fallbackFontIsSet = !font.isNull();
        clearCache();
    }

    /// get fallback font for this font
    LVFont * getFallbackFont() {
        if ( _fallbackFontIsSet )
            return _fallbackFont.get();
        if ( fontMan->GetFallbackFontFace()!=_faceName ) // to avoid circular link, disable fallback for fallback font
            _fallbackFont = fontMan->GetFallbackFont(_size);
        _fallbackFontIsSet = true;
        return _fallbackFont.get();
    }

    /// returns font weight
    virtual int getWeight() const { return _weight; }
    /// returns italic flag
    virtual int getItalic() const { return _italic; }
    /// sets face name
    virtual void setFaceName( lString8 face ) { _faceName = face; }

    LVMutex & getMutex() { return _mutex; }
    FT_Library getLibrary() { return _library; }

    LVFreeTypeFace( LVMutex &mutex, FT_Library  library, LVFontGlobalGlyphCache * globalCache )
    : _mutex(mutex), _fontFamily(css_ff_sans_serif), _library(library), _face(NULL), _size(0), _hyphen_width(0), _baseline(0)
    , _weight(400), _italic(0)
    , _glyph_cache(globalCache), _drawMonochrome(false), _kerningMode(KERNING_MODE_DISABLED), _hintingMode(HINTING_MODE_AUTOHINT), _fallbackFontIsSet(false)
#if USE_HARFBUZZ==1
    , _glyph_cache2(256)
    , _width_cache2(1024)
#endif
    {
        _matrix.xx = 0x10000;
        _matrix.yy = 0x10000;
        _matrix.xy = 0;
        _matrix.yx = 0;
        _hintingMode = fontMan->GetHintingMode();
#if USE_HARFBUZZ==1
        _hb_font = 0;
        _hb_buffer = hb_buffer_create();
        _hb_light_buffer = hb_buffer_create();
        // HarfBuzz features for full text shaping
        hb_feature_from_string("+kern", -1, &_hb_features[0]);      // font kerning
        hb_feature_from_string("+liga", -1, &_hb_features[1]);      // ligatures
        // HarfBuzz features for lighweight characters width calculating with caching
        hb_feature_from_string("+kern", -1, &_hb_light_features[0]);      // font kerning
        hb_feature_from_string("-liga", -1, &_hb_light_features[1]);      // no ligatures
#endif
    }

    virtual ~LVFreeTypeFace()
    {
#if USE_HARFBUZZ==1
        if (_hb_buffer)
            hb_buffer_destroy(_hb_buffer);
        if (_hb_light_buffer)
            hb_buffer_destroy(_hb_light_buffer);
#endif
        Clear();
    }

    void clearCache() {
        _glyph_cache.clear();
        _wcache.clear();
        _lsbcache.clear();
        _rsbcache.clear();
#if USE_HARFBUZZ==1
        LVHashTable<lUInt32, LVFontGlyphIndexCacheItem*>::pair* pair;
        LVHashTable<lUInt32, LVFontGlyphIndexCacheItem*>::iterator it = _glyph_cache2.forwardIterator();
        while ((pair = it.next())) {
            LVFontGlyphIndexCacheItem* item = pair->value;
            if (item)
                LVFontGlyphIndexCacheItem::freeItem(item);
        }
        _glyph_cache2.clear();
        _width_cache2.clear();
#endif
    }

    virtual int getHyphenWidth()
    {
        FONT_GUARD
        if ( !_hyphen_width ) {
            _hyphen_width = getCharWidth( UNICODE_SOFT_HYPHEN_CODE );
        }
        return _hyphen_width;
    }

    virtual kerning_mode_t getKerningMode() const { return _kerningMode; }

    virtual void setKerningMode( kerning_mode_t kerningMode ) {
        _kerningMode = kerningMode;
        _hash = 0; // Force lvstyles.cpp calcHash(font_ref_t) to recompute the hash
#if USE_HARFBUZZ==1
        // in cache may be found some ligatures, so clear it
        clearCache();
#endif
    }

    /// sets current hinting mode
    virtual void setHintingMode(hinting_mode_t mode) {
        if (_hintingMode == mode)
            return;
        _hintingMode = mode;
        _hash = 0; // Force lvstyles.cpp calcHash(font_ref_t) to recompute the hash
        clearCache();
    }
    /// returns current hinting mode
    virtual hinting_mode_t  getHintingMode() const { return _hintingMode; }

    /// get bitmap mode (true=bitmap, false=antialiased)
    virtual bool getBitmapMode() { return _drawMonochrome; }
    /// set bitmap mode (true=bitmap, false=antialiased)
    virtual void setBitmapMode( bool drawBitmap )
    {
        if ( _drawMonochrome == drawBitmap )
            return;
        _drawMonochrome = drawBitmap;
        clearCache();
    }

    bool loadFromBuffer(LVByteArrayRef buf, int index, int size, css_font_family_t fontFamily, bool monochrome, bool italicize )
    {
        FONT_GUARD
        _hintingMode = fontMan->GetHintingMode();
        _drawMonochrome = monochrome;
        _fontFamily = fontFamily;
        if (_face)
            FT_Done_Face(_face);
        int error = FT_New_Memory_Face( _library, buf->get(), buf->length(), index, &_face ); /* create face object */
        if (error)
            return false;
        if ( _fileName.endsWith(".pfb") || _fileName.endsWith(".pfa") ) {
            lString8 kernFile = _fileName.substr(0, _fileName.length()-4);
            if ( LVFileExists(Utf8ToUnicode(kernFile) + ".afm" ) ) {
                kernFile += ".afm";
            } else if ( LVFileExists(Utf8ToUnicode(kernFile) + ".pfm" ) ) {
                kernFile += ".pfm";
            } else {
                kernFile.clear();
            }
            if ( !kernFile.empty() )
                error = FT_Attach_File( _face, kernFile.c_str() );
        }
        //FT_Face_SetUnpatentedHinting( _face, 1 );
        _slot = _face->glyph;
        _faceName = familyName(_face);
        CRLog::debug("Loaded font %s [%d]: faceName=%s, ", _fileName.c_str(), index, _faceName.c_str() );
        //if ( !FT_IS_SCALABLE( _face ) ) {
        //    Clear();
        //    return false;
       // }
        error = FT_Set_Pixel_Sizes(
            _face,    /* handle to face object */
            0,        /* pixel_width           */
            size );  /* pixel_height          */
#if USE_HARFBUZZ==1
        if (FT_Err_Ok == error) {
            if (_hb_font)
                hb_font_destroy(_hb_font);
            _hb_font = hb_ft_font_create(_face, NULL);
            if (!_hb_font) {
                error = FT_Err_Invalid_Argument;
            } else {
                // NOTE: Commented out for now, it's prohibitively expensive. c.f., #230
                /*
                // Use the same load flags as we do when using FT directly, to avoid mismatching advances & raster
                int flags = FT_LOAD_DEFAULT;
                flags |= (!_drawMonochrome ? FT_LOAD_TARGET_NORMAL : FT_LOAD_TARGET_MONO);
                if (_hintingMode == HINTING_MODE_BYTECODE_INTERPRETOR) {
                    flags |= FT_LOAD_NO_AUTOHINT;
                } else if (_hintingMode == HINTING_MODE_AUTOHINT) {
                    flags |= FT_LOAD_FORCE_AUTOHINT;
                } else if (_hintingMode == HINTING_MODE_DISABLED) {
                    flags |= FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING;
                }
                hb_ft_font_set_load_flags(_hb_font, flags);
                */
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
        _height = _face->size->metrics.height >> 6;
        _size = size; //(_face->size->metrics.height >> 6);
        _baseline = _height + (_face->size->metrics.descender >> 6);
        _weight = _face->style_flags & FT_STYLE_FLAG_BOLD ? 700 : 400;
        _italic = _face->style_flags & FT_STYLE_FLAG_ITALIC ? 1 : 0;

        if ( !error && italicize && !_italic ) {
            _matrix.xy = 0x10000*3/10;
            FT_Set_Transform(_face, &_matrix, NULL);
            _italic = 2;
        }

        if ( error ) {
            // error
            return false;
        }
        return true;
    }

    bool loadFromFile( const char * fname, int index, int size, css_font_family_t fontFamily, bool monochrome, bool italicize )
    {
        FONT_GUARD
        _hintingMode = fontMan->GetHintingMode();
        _drawMonochrome = monochrome;
        _fontFamily = fontFamily;
        if ( fname )
            _fileName = fname;
        if ( _fileName.empty() )
            return false;
        if (_face)
            FT_Done_Face(_face);
        int error = FT_New_Face( _library, _fileName.c_str(), index, &_face ); /* create face object */
        if (error)
            return false;
        if ( _fileName.endsWith(".pfb") || _fileName.endsWith(".pfa") ) {
        	lString8 kernFile = _fileName.substr(0, _fileName.length()-4);
            if ( LVFileExists(Utf8ToUnicode(kernFile) + ".afm") ) {
        		kernFile += ".afm";
            } else if ( LVFileExists(Utf8ToUnicode(kernFile) + ".pfm" ) ) {
        		kernFile += ".pfm";
        	} else {
        		kernFile.clear();
        	}
        	if ( !kernFile.empty() )
        		error = FT_Attach_File( _face, kernFile.c_str() );
        }
        //FT_Face_SetUnpatentedHinting( _face, 1 );
        _slot = _face->glyph;
        _faceName = familyName(_face);
        CRLog::debug("Loaded font %s [%d]: faceName=%s, ", _fileName.c_str(), index, _faceName.c_str() );
        //if ( !FT_IS_SCALABLE( _face ) ) {
        //    Clear();
        //    return false;
       // }
        error = FT_Set_Pixel_Sizes(
            _face,    /* handle to face object */
            0,        /* pixel_width           */
            size );  /* pixel_height          */
#if USE_HARFBUZZ==1
        if (FT_Err_Ok == error) {
            if (_hb_font)
                hb_font_destroy(_hb_font);
            _hb_font = hb_ft_font_create(_face, NULL);
            if (!_hb_font) {
                error = FT_Err_Invalid_Argument;
            } else {
                // NOTE: Commented out for now, it's prohibitively expensive. c.f., #230
                /*
                // Use the same load flags as we do when using FT directly, to avoid mismatching advances & raster
                int flags = FT_LOAD_DEFAULT;
                flags |= (!_drawMonochrome ? FT_LOAD_TARGET_NORMAL : FT_LOAD_TARGET_MONO);
                if (_hintingMode == HINTING_MODE_BYTECODE_INTERPRETOR) {
                    flags |= FT_LOAD_NO_AUTOHINT;
                } else if (_hintingMode == HINTING_MODE_AUTOHINT) {
                    flags |= FT_LOAD_FORCE_AUTOHINT;
                } else if (_hintingMode == HINTING_MODE_DISABLED) {
                    flags |= FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING;
                }
                hb_ft_font_set_load_flags(_hb_font, flags);
                */
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
        _height = _face->size->metrics.height >> 6;
        _size = size; //(_face->size->metrics.height >> 6);
        _baseline = _height + (_face->size->metrics.descender >> 6);
        _weight = _face->style_flags & FT_STYLE_FLAG_BOLD ? 700 : 400;
        _italic = _face->style_flags & FT_STYLE_FLAG_ITALIC ? 1 : 0;

        if ( !error && italicize && !_italic ) {
            _matrix.xy = 0x10000*3/10;
            FT_Set_Transform(_face, &_matrix, NULL);
            _italic = 2;
        }

        if ( error ) {
            // error
            return false;
        }
        return true;
    }

#if USE_HARFBUZZ==1
    lChar16 filterChar(lChar16 code) {
        lChar16 res;
        if (code == '\t')
            code = ' ';
        FT_UInt ch_glyph_index = FT_Get_Char_Index(_face, code);
        if ( ch_glyph_index==0 && code >= 0xF000 && code <= 0xF0FF) {
            // If no glyph found and code is among the private unicode
            // area classically used by symbol fonts (range U+F020-U+F0FF),
            // try to switch to FT_ENCODING_MS_SYMBOL
            if (FT_Select_Charmap(_face, FT_ENCODING_MS_SYMBOL)) {
                ch_glyph_index = FT_Get_Char_Index( _face, code );
                // restore unicode charmap if there is one
                FT_Select_Charmap(_face, FT_ENCODING_UNICODE);
            }
        }
        if (0 != ch_glyph_index)
            res = code;
        else {
            res = getReplacementChar(code);
            if (0 == res)
                res = code;
        }
        return res;
    }

    bool hbCalcCharWidth(struct LVCharPosInfo* posInfo, const struct LVCharTriplet& triplet, lChar16 def_char) {
        if (!posInfo)
            return false;
        unsigned int segLen = 0;
        int cluster;
        hb_buffer_clear_contents(_hb_light_buffer);
        if (0 != triplet.prevChar) {
            hb_buffer_add(_hb_light_buffer, (hb_codepoint_t)triplet.prevChar, segLen);
            segLen++;
        }
        hb_buffer_add(_hb_light_buffer, (hb_codepoint_t)triplet.Char, segLen);
        cluster = segLen;
        segLen++;
        if (0 != triplet.nextChar) {
            hb_buffer_add(_hb_light_buffer, (hb_codepoint_t)triplet.nextChar, segLen);
            segLen++;
        }
        hb_buffer_set_content_type(_hb_light_buffer, HB_BUFFER_CONTENT_TYPE_UNICODE);
        hb_buffer_guess_segment_properties(_hb_light_buffer);
        hb_shape(_hb_font, _hb_light_buffer, _hb_light_features, 2);
        unsigned int glyph_count = hb_buffer_get_length(_hb_light_buffer);
        if (segLen == glyph_count) {
            hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(_hb_light_buffer, &glyph_count);
            hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(_hb_light_buffer, &glyph_count);
            if (0 != glyph_info[cluster].codepoint) {        // glyph found for this char in this font
                posInfo->offset = glyph_pos[cluster].x_offset >> 6;
                posInfo->width = glyph_pos[cluster].x_advance >> 6;
            } else {
                // hb_shape() failed or glyph omitted in this font, use fallback font
                glyph_info_t glyph;
                LVFont *fallback = getFallbackFont();
                if (fallback) {
                    if (fallback->getGlyphInfo(triplet.Char, &glyph, def_char)) {
                        posInfo->offset = 0;
                        posInfo->width = glyph.width;
                    }
                }
            }
        } else {
#ifdef _DEBUG
            CRLog::debug("hbCalcCharWidthWithKerning(): hb_buffer_get_length() return %d, must be %d, return value (-1)", glyph_count, segLen);
#endif
            return false;
        }
        return true;
    }
#endif

    FT_UInt getCharIndex( lChar16 code, lChar16 def_char ) {
        if ( code=='\t' )
            code = ' ';
        FT_UInt ch_glyph_index = FT_Get_Char_Index( _face, code );
        if ( ch_glyph_index==0 && code >= 0xF000 && code <= 0xF0FF) {
            // If no glyph found and code is among the private unicode
            // area classically used by symbol fonts (range U+F020-U+F0FF),
            // try to switch to FT_ENCODING_MS_SYMBOL
            if (FT_Select_Charmap(_face, FT_ENCODING_MS_SYMBOL)) {
                ch_glyph_index = FT_Get_Char_Index( _face, code );
                // restore unicode charmap if there is one
                FT_Select_Charmap(_face, FT_ENCODING_UNICODE);
            }
        }
        if ( ch_glyph_index==0 ) {
            lUInt32 replacement = getReplacementChar( code );
            if ( replacement )
                ch_glyph_index = FT_Get_Char_Index( _face, replacement );
            if ( ch_glyph_index==0 && def_char )
                ch_glyph_index = FT_Get_Char_Index( _face, def_char );
        }
        return ch_glyph_index;
    }

    /** \brief get glyph info
        \param glyph is pointer to glyph_info_t struct to place retrieved info
        \return true if glyh was found
    */
    virtual bool getGlyphInfo( lUInt32 code, glyph_info_t * glyph, lChar16 def_char=0 )
    {
        //FONT_GUARD
        int glyph_index = getCharIndex( code, 0 );
        if ( glyph_index==0 ) {
            LVFont * fallback = getFallbackFont();
            if ( !fallback ) {
                // No fallback
                glyph_index = getCharIndex( code, def_char );
                if ( glyph_index==0 )
                    return false;
            } else {
                // Fallback
                return fallback->getGlyphInfo(code, glyph, def_char);
            }
        }
        int flags = FT_LOAD_DEFAULT;
        flags |= (!_drawMonochrome ? FT_LOAD_TARGET_NORMAL : FT_LOAD_TARGET_MONO);
        if (_hintingMode == HINTING_MODE_BYTECODE_INTERPRETOR) {
            flags |= FT_LOAD_NO_AUTOHINT;
        } else if (_hintingMode == HINTING_MODE_AUTOHINT) {
            flags |= FT_LOAD_FORCE_AUTOHINT;
        } else if (_hintingMode == HINTING_MODE_DISABLED) {
            flags |= FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING;
        }
        updateTransform();
        int error = FT_Load_Glyph(
            _face,          /* handle to face object */
            glyph_index,   /* glyph index           */
            flags );  /* load flags, see below */
        if ( error )
            return false;
        glyph->blackBoxX = (lUInt16)(_slot->metrics.width >> 6);
        glyph->blackBoxY = (lUInt16)(_slot->metrics.height >> 6);
        glyph->originX =   (lInt16)(_slot->metrics.horiBearingX >> 6);
        glyph->originY =   (lInt16)(_slot->metrics.horiBearingY >> 6);
        glyph->width =     (lUInt16)(myabs(_slot->metrics.horiAdvance) >> 6);
        if (glyph->blackBoxX == 0) // If a glyph has no blackbox (a spacing
            glyph->rsb =   0;      // character), there is no bearing
        else
            glyph->rsb =   (lInt16)( (myabs(_slot->metrics.horiAdvance)
                            - _slot->metrics.horiBearingX - _slot->metrics.width) >> 6);
        // printf("%c: %d + %d + %d = %d (y: %d + %d)\n", code, glyph->originX, glyph->blackBoxX,
        //                            glyph->rsb, glyph->width, glyph->originY, glyph->blackBoxY);
        // Note: these >>6 on a negative number will floor() it, so we'll get
        // a ceil()'ed value when considering negative numbers as some overflow,
        // which is good when we're using it for adding some padding.
        //
        // Note: when the font does not provide italic glyphs (_italic = 2), some fake
        // italic/oblique is obtained with FreeType transformation (_matrix.xy and
        // FT_Set_Transform()). freetype.h states about it:
        //     Note that this also transforms the `face.glyph.advance' field,
        //     but *not* the values in `face.glyph.metrics'.
        // So, with such fake italic, the values just computed above are wrong,
        // and may cause some wrong glyphs positionning or advance.
        // Some possible attempt at guessing the transformed values can be found in
        // http://code.qt.io/cgit/qt/qtbase.git/tree/src/platformsupport/fontdatabases/freetype/qfontengine_ft.cpp
        // (transformBoundingBox) but a straightforward port here does not give
        // the expected rendering...

        return true;
    }
/*
  // USE GET_CHAR_FLAGS instead
    inline int calcCharFlags( lChar16 ch )
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
  */
    /** \brief measure text
        \param text is text string pointer
        \param len is number of characters to measure
        \return number of characters before max_width reached
    */
    virtual lUInt16 measureText(
                        const lChar16 * text,
                        int len,
                        lUInt16 * widths,
                        lUInt8 * flags,
                        int max_width,
                        lChar16 def_char,
                        int letter_spacing = 0,
                        bool allow_hyphenation = true
                     )
    {
        FONT_GUARD
        if ( len <= 0 || _face==NULL )
            return 0;
        if ( letter_spacing < 0 )
        {
            letter_spacing = 0;
        } else if ( letter_spacing > MAX_LETTER_SPACING )
        {
            letter_spacing = MAX_LETTER_SPACING;
        }

        int i;

        lUInt16 prev_width = 0;
        int lastFitChar = 0;
        updateTransform();
        // measure character widths

#if USE_HARFBUZZ==1
        if (_kerningMode == KERNING_MODE_HARFBUZZ) {

            unsigned int glyph_count;
            hb_glyph_info_t* glyph_info = 0;
            hb_glyph_position_t* glyph_pos = 0;
            hb_buffer_clear_contents(_hb_buffer);
            hb_buffer_set_replacement_codepoint(_hb_buffer, def_char);
            // fill HarfBuzz buffer with filtering
            for (i = 0; i < len; i++)
                hb_buffer_add(_hb_buffer, (hb_codepoint_t)filterChar(text[i]), i);
            hb_buffer_set_content_type(_hb_buffer, HB_BUFFER_CONTENT_TYPE_UNICODE);
            hb_buffer_guess_segment_properties(_hb_buffer);
            // shape
            hb_shape(_hb_font, _hb_buffer, _hb_features, 2);
            glyph_count = hb_buffer_get_length(_hb_buffer);
            glyph_info = hb_buffer_get_glyph_infos(_hb_buffer, 0);
            glyph_pos = hb_buffer_get_glyph_positions(_hb_buffer, 0);
#ifdef _DEBUG
            if ((int)glyph_count != len) {
                CRLog::debug(
                        "measureText(): glyph_count not equal source text length (ligature detected?), glyph_count=%d, len=%d",
                        glyph_count, len);
            }
#endif
            uint32_t j;
            uint32_t cluster;
            uint32_t prev_cluster = 0;
            int skipped_chars = 0; // to add to 'i' at end of loop, as 'i' is used later and should be accurate
            for (i = 0; i < (int)glyph_count; i++) {
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
                // Note: we use 'cluster' as an index similar to 'i' in 'text'
                // but the docs warn about this, as it may be wrong with some "level"
                // see https://harfbuzz.github.io/clusters.html for details
                // But it seems to work in our case.
                // Note: we deal with the "when more than one character gets merged in the
                // same glyph" case. Not sure we do things correctly for the "More than one
                // glyph can have the same cluster value, if they resulted from the same
                // character (one to many glyph substitution)"...

                cluster = glyph_info[i].cluster;
                lChar16 ch = text[cluster];
                // It seems each soft-hyphen is in its own cluster, of length 1 and width 0,
                // so HarfBuzz must already deal correctly with soft-hyphens.
                // No need to do what we do in the Freetype alternative code below.
                bool isHyphen = (ch == UNICODE_SOFT_HYPHEN_CODE);
                flags[cluster] = GET_CHAR_FLAGS(ch); //calcCharFlags( ch );
                hb_codepoint_t ch_glyph_index = glyph_info[i].codepoint;
                if (0 != ch_glyph_index)        // glyph found for this char in this font
                    widths[cluster] = prev_width + (glyph_pos[i].x_advance >> 6) + letter_spacing;
                else {
                    // hb_shape() failed or glyph skipped in this font, use fallback font
                    int w = _wcache.get(ch);
                    if ( w == CACHED_UNSIGNED_METRIC_NOT_SET ) {
                        glyph_info_t glyph;
                        LVFont *fallback = getFallbackFont();
                        if (fallback) {
                            if (fallback->getGlyphInfo(ch, &glyph, def_char)) {
                                w = glyph.width;
                                _wcache.put(ch, w);
                            } else        // ignore (skip) this char
                                widths[cluster] = prev_width;
                        } else            // ignore (skip) this char
                            widths[cluster] = prev_width;
                    }
                    widths[cluster] = prev_width + w + letter_spacing;
                }
                for (j = prev_cluster + 1; j < cluster; j++) {
                    // fill flags and widths for chars skipped (because they are part of a
                    // ligature and are accounted in the previous cluster - we didn't know
                    // how many until we processed the next cluster)
                    flags[j] = GET_CHAR_FLAGS(text[j]) | LCHAR_IS_LIGATURE_TAIL;
                    widths[j] = prev_width; // so 2nd char of a ligature has width=0
                    skipped_chars++;
                    // This will ensure we get (with word "afloat"):
                    //   glyph 14 cluster 14 flags=0:        glyph "a"
                    //     widths[14] = 150, flags[14]=0     char "a"
                    //   glyph 15 cluster 15 flags=0:        glyph "fl" (ligature)
                    //     widths[15] = 163, flags[15]=0     char "f"
                    //   glyph 16 cluster 17 flags=0:        glyph "o"
                    //     widths[17] = 174, flags[17]=0     char "o"
                    //       >widths[16] = 163, flags[16]=0  char "l"  (done here)
                    //   glyph 17 cluster 18 flags=0:        glyph "a"
                    //     widths[18] = 185, flags[18]=0     char "a"
                    //   glyph 18 cluster 19 flags=0:        glyph "t"
                    //     widths[19] = 192, flags[19]=0     char "t"
                }
                prev_cluster = cluster;
                if (!isHyphen) // avoid soft hyphens inside text string
                    prev_width = widths[cluster];
                if (prev_width > max_width) {
                    if (lastFitChar < cluster + 7)
                        break;
                } else {
                    lastFitChar = cluster + 1;
                }
            }
            // For case when ligature is the last glyph in measured text
            if (prev_cluster < (uint32_t)(len - 1) && prev_width < (lUInt16)max_width) {
                for (j = prev_cluster + 1; j < (uint32_t)len; j++) {
                    flags[j] = GET_CHAR_FLAGS(text[j]) | LCHAR_IS_LIGATURE_TAIL;
                    widths[j] = prev_width;
                    skipped_chars++;
                }
            }
            // i is used below to "fill props for rest of chars", so make it accurate
            i += skipped_chars;

        } // _kerningMode == KERNING_MODE_HARFBUZZ
        else if (_kerningMode == KERNING_MODE_HARFBUZZ_LIGHT) {
            unsigned int glyph_count;
            hb_glyph_info_t* glyph_info = 0;
            hb_glyph_position_t* glyph_pos = 0;
            struct LVCharTriplet triplet;
            struct LVCharPosInfo posInfo;
            triplet.Char = 0;
            for ( i=0; i<len; i++) {
                lChar16 ch = text[i];
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
                widths[i] = prev_width + posInfo.width + letter_spacing;
                if ( !isHyphen ) // avoid soft hyphens inside text string
                    prev_width = widths[i];
                if ( prev_width > max_width ) {
                    if ( lastFitChar < i + 7)
                        break;
                } else {
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
            lChar16 ch = text[i];
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
            int w = _wcache.get(ch);
            if ( w == CACHED_UNSIGNED_METRIC_NOT_SET ) {
                glyph_info_t glyph;
                if ( getGlyphInfo( ch, &glyph, def_char ) ) {
                    w = glyph.width;
                    _wcache.put(ch, w);
                } else {
                    widths[i] = prev_width;
                    lastFitChar = i + 1;
                    continue;  /* ignore errors */
                }
                if ( ch_glyph_index==(FT_UInt)-1 )
                    ch_glyph_index = getCharIndex( ch, 0 );
//                error = FT_Load_Glyph( _face,          /* handle to face object */
//                        ch_glyph_index,                /* glyph index           */
//                        FT_LOAD_DEFAULT );             /* load flags, see below */
//                if ( error ) {
//                    widths[i] = prev_width;
//                    continue;  /* ignore errors */
//                }
            }
            widths[i] = prev_width + w + (kerning >> 6) + letter_spacing;
            previous = ch_glyph_index;
            if ( !isHyphen ) // avoid soft hyphens inside text string
                prev_width = widths[i];
            if ( prev_width > max_width ) {
                if ( lastFitChar < i + 7)
                    break;
            } else {
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

        //maxFit = i;


        // find last word
        if ( allow_hyphenation ) {
            if ( !_hyphen_width )
                _hyphen_width = getCharWidth( UNICODE_SOFT_HYPHEN_CODE );
            if ( lastFitChar > 3 ) {
                int hwStart, hwEnd;
                lStr_findWordBounds( text, len, lastFitChar-1, hwStart, hwEnd );
                if ( hwStart < (int)(lastFitChar-1) && hwEnd > hwStart+3 ) {
                    //int maxw = max_width - (hwStart>0 ? widths[hwStart-1] : 0);
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
    virtual lUInt32 getTextWidth(
                        const lChar16 * text, int len
        )
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
                        L' ',  // def_char
                        0
                     );
        if ( res>0 && res<MAX_LINE_CHARS )
            return widths[res-1];
        return 0;
    }

    void updateTransform() {
//        static void * transformOwner = NULL;
//        if ( transformOwner!=this ) {
//            FT_Set_Transform(_face, &_matrix, NULL);
//            transformOwner = this;
//        }
    }

    /** \brief get glyph item
        \param code is unicode character
        \return glyph pointer if glyph was found, NULL otherwise
    */
    virtual LVFontGlyphCacheItem * getGlyph(lUInt32 ch, lChar16 def_char=0) {
        //FONT_GUARD
        FT_UInt ch_glyph_index = getCharIndex( ch, 0 );
        if ( ch_glyph_index==0 ) {
            LVFont * fallback = getFallbackFont();
            if ( !fallback ) {
                // No fallback
                ch_glyph_index = getCharIndex( ch, def_char );
                if ( ch_glyph_index==0 )
                    return NULL;
            } else {
                // Fallback
                return fallback->getGlyph(ch, def_char);
            }
        }
        LVFontGlyphCacheItem * item = _glyph_cache.get( ch );
        if ( !item ) {

            int rend_flags = FT_LOAD_RENDER | ( !_drawMonochrome ? FT_LOAD_TARGET_NORMAL : FT_LOAD_TARGET_MONO ); //|FT_LOAD_MONOCHROME|FT_LOAD_FORCE_AUTOHINT
            if (_hintingMode == HINTING_MODE_BYTECODE_INTERPRETOR) {
                rend_flags |= FT_LOAD_NO_AUTOHINT;
            } else if (_hintingMode == HINTING_MODE_AUTOHINT) {
                rend_flags |= FT_LOAD_FORCE_AUTOHINT;
            } else if (_hintingMode == HINTING_MODE_DISABLED) {
                rend_flags |= FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING;
            }
            /* load glyph image into the slot (erase previous one) */

            updateTransform();
            int error = FT_Load_Glyph( _face,          /* handle to face object */
                    ch_glyph_index,                /* glyph index           */
                    rend_flags );             /* load flags, see below */
            if ( error ) {
                return NULL;  /* ignore errors */
            }
            item = newItem( &_glyph_cache, ch, _slot ); //, _drawMonochrome
            _glyph_cache.put( item );
        }
        return item;
    }

#if USE_HARFBUZZ==1
    LVFontGlyphIndexCacheItem * getGlyphByIndex(lUInt32 index) {
        //FONT_GUARD
        LVFontGlyphIndexCacheItem * item = 0;
        //std::map<lUInt32, LVFontGlyphIndexCacheItem*>::const_iterator it;
        //it = _glyph_cache2.find(index);
        //if (it == _glyph_cache2.end()) {
        if (!_glyph_cache2.get(index, item)) {
            // glyph not found in cache, rendering...
            int rend_flags = FT_LOAD_RENDER | ( !_drawMonochrome ? FT_LOAD_TARGET_NORMAL : FT_LOAD_TARGET_MONO ); //|FT_LOAD_MONOCHROME|FT_LOAD_FORCE_AUTOHINT
            if (_hintingMode == HINTING_MODE_BYTECODE_INTERPRETOR) {
                rend_flags |= FT_LOAD_NO_AUTOHINT;
            } else if (_hintingMode == HINTING_MODE_AUTOHINT) {
                rend_flags |= FT_LOAD_FORCE_AUTOHINT;
            } else if (_hintingMode == HINTING_MODE_DISABLED) {
                rend_flags |= FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_HINTING;
            }
            /* load glyph image into the slot (erase previous one) */

            updateTransform();
            int error = FT_Load_Glyph( _face,          /* handle to face object */
                    index,                /* glyph index           */
                    rend_flags );             /* load flags, see below */
            if ( error ) {
                return NULL;  /* ignore errors */
            }
            item = newItem(index, _slot);
            if (item)
                //_glyph_cache2.insert(std::pair<lUInt32, LVFontGlyphIndexCacheItem*>(index, item));
                _glyph_cache2.set(index, item);
        }
        //else
        //    item = it->second;
        return item;
    }
#endif

//    /** \brief get glyph image in 1 byte per pixel format
//        \param code is unicode character
//        \param buf is buffer [width*height] to place glyph data
//        \return true if glyph was found
//    */
//    virtual bool getGlyphImage(lUInt16 ch, lUInt8 * bmp, lChar16 def_char=0)
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
    virtual int getCharWidth( lChar16 ch, lChar16 def_char='?' )
    {
        int w = _wcache.get(ch);
        if ( w == CACHED_UNSIGNED_METRIC_NOT_SET ) {
            glyph_info_t glyph;
            if ( getGlyphInfo( ch, &glyph, def_char ) ) {
                w = glyph.width;
            } else {
                w = 0;
            }
            _wcache.put(ch, w);
        }
        return w;
    }

    /// returns char glyph left side bearing
    virtual int getLeftSideBearing( lChar16 ch, bool negative_only=false, bool italic_only=false )
    {
        if ( italic_only && !getItalic() )
            return 0;
        int b = _lsbcache.get(ch);
        if ( b == CACHED_SIGNED_METRIC_NOT_SET ) {
            glyph_info_t glyph;
            if ( getGlyphInfo( ch, &glyph, '?' ) ) {
                b = glyph.originX;
            } else {
                b = 0;
            }
            _lsbcache.put(ch, b);
        }
        if (negative_only && b >= 0)
            return 0;
        return b;
    }

    /// returns char glyph right side bearing
    virtual int getRightSideBearing( lChar16 ch, bool negative_only=false, bool italic_only=false )
    {
        if ( italic_only && !getItalic() )
            return 0;
        int b = _rsbcache.get(ch);
        if ( b == CACHED_SIGNED_METRIC_NOT_SET ) {
            glyph_info_t glyph;
            if ( getGlyphInfo( ch, &glyph, '?' ) ) {
                b = glyph.rsb;
            } else {
                b = 0;
            }
            _rsbcache.put(ch, b);
        }
        if (negative_only && b >= 0)
            return 0;
        return b;
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

    /// draws text string
    virtual void DrawTextString( LVDrawBuf * buf, int x, int y,
                       const lChar16 * text, int len,
                       lChar16 def_char, lUInt32 * palette, bool addHyphen,
                       lUInt32 flags, int letter_spacing, int width )
    {
        FONT_GUARD
        if ( len <= 0 || _face==NULL )
            return;
        if ( letter_spacing < 0 )
        {
            letter_spacing = 0;
        } else if ( letter_spacing > MAX_LETTER_SPACING )
        {
            letter_spacing = MAX_LETTER_SPACING;
        }
        lvRect clip;
        buf->GetClipRect( &clip );
        updateTransform();
        if ( y + _height < clip.top || y >= clip.bottom )
            return;

        unsigned int i;
        //lUInt16 prev_width = 0;
        lChar16 ch;
        // measure character widths
        bool isHyphen = false;
        int x0 = x;
#if USE_HARFBUZZ==1
        if (_kerningMode == KERNING_MODE_HARFBUZZ) {
            hb_glyph_info_t *glyph_info = 0;
            hb_glyph_position_t *glyph_pos = 0;
            unsigned int glyph_count;
            int w;
            unsigned int len_new = 0;
            hb_buffer_clear_contents(_hb_buffer);
            hb_buffer_set_replacement_codepoint(_hb_buffer, 0);
            // fill HarfBuzz buffer with filtering
            for (i = 0; i < (unsigned int)len; i++) {
                ch = text[i];
                // don't draw any soft hyphens inside text string
                bool isHyphen = (ch == UNICODE_SOFT_HYPHEN_CODE);
                if (!isHyphen) { // skip any soft-hyphen
                    // Also replace any chars to similar if glyph not found
                    hb_buffer_add(_hb_buffer, (hb_codepoint_t)filterChar(ch), i);
                    len_new++;
                }
            }
            hb_buffer_set_content_type(_hb_buffer, HB_BUFFER_CONTENT_TYPE_UNICODE);
            hb_buffer_guess_segment_properties(_hb_buffer);
            // shape
            hb_shape(_hb_font, _hb_buffer, _hb_features, 2);
            glyph_count = hb_buffer_get_length(_hb_buffer);
            glyph_info = hb_buffer_get_glyph_infos(_hb_buffer, 0);
            glyph_pos = hb_buffer_get_glyph_positions(_hb_buffer, 0);
#ifdef _DEBUG
            if (glyph_count != len_new) {
                CRLog::debug(
                        "DrawTextString(): glyph_count not equal source text length, glyph_count=%d, len=%d",
                        glyph_count, len_new);
            }
#endif
            for (i = 0; i < glyph_count; i++) {
                if (0 == glyph_info[i].codepoint) {
                    // If HarfBuzz can't find glyph in current font
                    // using fallback font that used in getGlyph()
                    ch = text[glyph_info[i].cluster];
                    LVFontGlyphCacheItem *item = getGlyph(ch, def_char);
                    if (item) {
                        w = item->advance;
                        buf->Draw(x + item->origin_x,
                              y + _baseline - item->origin_y,
                              item->bmp,
                              item->bmp_width,
                              item->bmp_height,
                              palette);
                        x += w + letter_spacing;
                    }
                }
                else {
                    LVFontGlyphIndexCacheItem *item = getGlyphByIndex(glyph_info[i].codepoint);
                    if (item) {
                        w = glyph_pos[i].x_advance >> 6;
                        buf->Draw(x + item->origin_x + (glyph_pos[i].x_offset >> 6),
                                  y + _baseline - item->origin_y + (glyph_pos[i].y_offset >> 6),
                                  item->bmp,
                                  item->bmp_width,
                                  item->bmp_height,
                                  palette);
                        x += w + letter_spacing;
                        // Wondered if item->origin_x and glyph_pos[i].x_offset must really
                        // be added (harfbuzz' x_offset correcting Freetype's origin_x),
                        // or are the same thing (harfbuzz' x_offset=0 replacing and
                        // cancelling FreeType's origin_x) ?
                        // Comparing screenshots seems to indicate they must be added.
                    }
                }
            }
            if (addHyphen) {
                ch = UNICODE_SOFT_HYPHEN_CODE;
                LVFontGlyphCacheItem *item = getGlyph(ch, def_char);
                if (item) {
                    w = item->advance;
                    buf->Draw( x + item->origin_x,
                               y + _baseline - item->origin_y,
                               item->bmp,
                               item->bmp_width,
                               item->bmp_height,
                               palette);
                    x  += w + letter_spacing;
                }
            }

        } // _kerningMode == KERNING_MODE_HARFBUZZ
        else if (_kerningMode == KERNING_MODE_HARFBUZZ_LIGHT) {
            hb_glyph_info_t *glyph_info = 0;
            hb_glyph_position_t *glyph_pos = 0;
            unsigned int glyph_count;
            int w;
            unsigned int len_new = 0;
            struct LVCharTriplet triplet;
            struct LVCharPosInfo posInfo;
            triplet.Char = 0;
            for ( i=0; i<=(unsigned int)len; i++) {
                if ( i==len && !addHyphen )
                    break;
                if ( i<len ) {
                    ch = text[i];
                    if ( ch=='\t' )
                        ch = ' ';
                    // don't draw any soft hyphens inside text string
                    isHyphen = (ch==UNICODE_SOFT_HYPHEN_CODE);
                } else {
                    ch = UNICODE_SOFT_HYPHEN_CODE;
                    isHyphen = false; // an hyphen, but not one to not draw
                }
                FT_UInt ch_glyph_index = getCharIndex( ch, def_char );
                LVFontGlyphCacheItem * item = getGlyph(ch, def_char);
                if ( !item )
                    continue;
                if ( (item && !isHyphen) || i==len ) { // only draw soft hyphens at end of string
                    triplet.prevChar = triplet.Char;
                    triplet.Char = ch;
                    if (i < (unsigned int)(len - 1))
                        triplet.nextChar = text[i + 1];
                    else
                        triplet.nextChar = 0;
                    if (!_width_cache2.get(triplet, posInfo)) {
                        if (!hbCalcCharWidth(&posInfo, triplet, def_char)) {
                            posInfo.offset = 0;
                            posInfo.width = item->advance;
                        }
                        _width_cache2.set(triplet, posInfo);
                    }
                    buf->Draw(x + item->origin_x + posInfo.offset,
                        y + _baseline - item->origin_y,
                        item->bmp,
                        item->bmp_width,
                        item->bmp_height,
                        palette);

                    x += posInfo.width + letter_spacing;
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
        for ( i=0; i<=(unsigned int)len; i++) {
            if ( i==len && !addHyphen )
                break;
            if ( i<len ) {
                ch = text[i];
                if ( ch=='\t' )
                    ch = ' ';
                // don't draw any soft hyphens inside text string
                isHyphen = (ch==UNICODE_SOFT_HYPHEN_CODE);
            } else {
                ch = UNICODE_SOFT_HYPHEN_CODE;
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
            LVFontGlyphCacheItem * item = getGlyph(ch, def_char);
            if ( !item )
                continue;
            if ( (item && !isHyphen) || i==len ) { // only draw soft hyphens at end of string
                int w = item->advance + (kerning >> 6);
                buf->Draw( x + (kerning>>6) + item->origin_x,
                    y + _baseline - item->origin_y,
                    item->bmp,
                    item->bmp_width,
                    item->bmp_height,
                    palette);

                x  += w + letter_spacing;
                previous = ch_glyph_index;
            }
        }
#if USE_HARFBUZZ==1
    } // else fallback to the non harfbuzz code
#endif
        if ( flags & LTEXT_TD_MASK ) {
            // text decoration: underline, etc.
            // Don't overflow the provided width (which may be lower than our
            // pen x if last glyph was a space not accounted in word width)
            if ( width >= 0 && x > x0 + width)
                x = x0 + width;
            int h = _size > 30 ? 2 : 1;
            lUInt32 cl = buf->GetTextColor();
            if ( (flags & LTEXT_TD_UNDERLINE) || (flags & LTEXT_TD_BLINK) ) {
                int liney = y + _baseline + h;
                buf->FillRect( x0, liney, x, liney+h, cl );
            }
            if ( flags & LTEXT_TD_OVERLINE ) {
                int liney = y + h;
                buf->FillRect( x0, liney, x, liney+h, cl );
            }
            if ( flags & LTEXT_TD_LINE_THROUGH ) {
//                int liney = y + _baseline - _size/4 - h/2;
                int liney = y + _baseline - _size*2/7;
                buf->FillRect( x0, liney, x, liney+h, cl );
            }
        }
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
    virtual lChar16 getHyphChar() { return UNICODE_SOFT_HYPHEN_CODE; }

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
    virtual bool getGlyphInfo( lUInt32 code, glyph_info_t * glyph, lChar16 def_char=0  )
    {
        bool res = _baseFont->getGlyphInfo( code, glyph, def_char );
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
                        const lChar16 * text, int len,
                        lUInt16 * widths,
                        lUInt8 * flags,
                        int max_width,
                        lChar16 def_char,
                        int letter_spacing=0,
                        bool allow_hyphenation=true
                     )
    {
        CR_UNUSED(allow_hyphenation);
        lUInt16 res = _baseFont->measureText(
                        text, len,
                        widths,
                        flags,
                        max_width,
                        def_char,
                        letter_spacing
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
    virtual lUInt32 getTextWidth(
                        const lChar16 * text, int len
        )
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
                        L' ',  // def_char
                        0
                     );
        if ( res>0 && res<MAX_LINE_CHARS )
            return widths[res-1];
        return 0;
    }

    /** \brief get glyph item
        \param code is unicode character
        \return glyph pointer if glyph was found, NULL otherwise
    */
    virtual LVFontGlyphCacheItem * getGlyph(lUInt32 ch, lChar16 def_char=0) {

        LVFontGlyphCacheItem * item = _glyph_cache.get( ch );
        if ( item )
            return item;

        LVFontGlyphCacheItem * olditem = _baseFont->getGlyph( ch, def_char );
        if ( !olditem )
            return NULL;

        int oldx = olditem->bmp_width;
        int oldy = olditem->bmp_height;
        int dx = oldx ? oldx + _hShift : 0;
        int dy = oldy ? oldy + _vShift : 0;

        item = LVFontGlyphCacheItem::newItem( &_glyph_cache, ch, dx, dy ); //, _drawMonochrome
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
        return item;
    }

    /** \brief get glyph image in 1 byte per pixel format
        \param code is unicode character
        \param buf is buffer [width*height] to place glyph data
        \return true if glyph was found
    */
//    virtual bool getGlyphImage(lUInt16 code, lUInt8 * buf, lChar16 def_char=0 )
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
    virtual int getCharWidth( lChar16 ch, lChar16 def_char=0 )
    {
        int w = _baseFont->getCharWidth( ch, def_char ) + _hShift;
        return w;
    }

    /// returns char glyph left side bearing
    virtual int getLeftSideBearing( lChar16 ch, bool negative_only=false, bool italic_only=false )
    {
        return _baseFont->getLeftSideBearing( ch, negative_only, italic_only );
    }

    /// returns char glyph right side bearing
    virtual int getRightSideBearing( lChar16 ch, bool negative_only=false, bool italic_only=false )
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

    /// draws text string
    virtual void DrawTextString( LVDrawBuf * buf, int x, int y,
                       const lChar16 * text, int len,
                       lChar16 def_char, lUInt32 * palette, bool addHyphen,
                       lUInt32 flags, int letter_spacing, int width )
    {
        if ( len <= 0 )
            return;
        if ( letter_spacing < 0 )
        {
            letter_spacing = 0;
        } else if ( letter_spacing > MAX_LETTER_SPACING )
        {
            letter_spacing = MAX_LETTER_SPACING;
        }
        lvRect clip;
        buf->GetClipRect( &clip );
        if ( y + _height < clip.top || y >= clip.bottom )
            return;

        //int error;

        int i;

        //lUInt16 prev_width = 0;
        lChar16 ch;
        // measure character widths
        bool isHyphen = false;
        int x0 = x;
        for ( i=0; i<=len; i++) {
            if ( i==len && !addHyphen )
                break;
            if ( i<len ) {
                ch = text[i];
                isHyphen = (ch==UNICODE_SOFT_HYPHEN_CODE);
            } else {
                ch = UNICODE_SOFT_HYPHEN_CODE;
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
        if ( flags & LTEXT_TD_MASK ) {
            // text decoration: underline, etc.
            // Don't overflow the provided width (which may be lower than our
            // pen x if last glyph was a space not accounted in word width)
            if ( width >= 0 && x > x0 + width)
                x = x0 + width;
            int h = _size > 30 ? 2 : 1;
            lUInt32 cl = buf->GetTextColor();
            if ( (flags & LTEXT_TD_UNDERLINE) || (flags & LTEXT_TD_BLINK) ) {
                int liney = y + _baseline + h;
                buf->FillRect( x0, liney, x, liney+h, cl );
            }
            if ( flags & LTEXT_TD_OVERLINE ) {
                int liney = y + h;
                buf->FillRect( x0, liney, x, liney+h, cl );
            }
            if ( flags & LTEXT_TD_LINE_THROUGH ) {
                int liney = y + _height/2 - h/2;
                buf->FillRect( x0, liney, x, liney+h, cl );
            }
        }
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
    lString8    _fallbackFontFace;
    LVFontCache _cache;
    FT_Library  _library;
    LVFontGlobalGlyphCache _globalCache;
    lString16 _requiredChars;
    #if (DEBUG_FONT_MAN==1)
    FILE * _log;
    #endif
    LVMutex   _lock;
public:

    /// get hash of installed fonts and fallback font
    virtual lUInt32 GetFontListHash(int documentId) {
        FONT_MAN_GUARD
        return _cache.GetFontListHash(documentId) * 75 + _fallbackFontFace.getHash();
    }

    /// set fallback font
    virtual bool SetFallbackFontFace( lString8 face ) {
        FONT_MAN_GUARD
        if ( face!=_fallbackFontFace ) {
            CRLog::trace("Looking for fallback font %s", face.c_str());
            LVFontCacheItem * item = _cache.findFallback( face, -1 );
            if ( !item ) {
                face.clear();
                // Don't reset previous fallback if this one is not found/valid
                return false;
            }
            _cache.clearFallbackFonts();
            _fallbackFontFace = face;
        }
        return !_fallbackFontFace.empty();
    }

    /// set as preferred font with the given bias to add in CalcMatch algorithm
    virtual bool SetAsPreferredFontWithBias( lString8 face, int bias, bool clearOthersBias ) {
        FONT_MAN_GUARD
        return _cache.setAsPreferredFontWithBias(face, bias, clearOthersBias);
    }


    /// get fallback font face (returns empty string if no fallback font is set)
    virtual lString8 GetFallbackFontFace() { return _fallbackFontFace; }

    /// returns fallback font for specified size
    virtual LVFontRef GetFallbackFont(int size) {
        FONT_MAN_GUARD
        if ( _fallbackFontFace.empty() )
            return LVFontRef();
        // reduce number of possible distinct sizes for fallback font
        if ( size>40 )
            size &= 0xFFF8;
        else if ( size>28 )
            size &= 0xFFFC;
        else if ( size>16 )
            size &= 0xFFFE;
        LVFontCacheItem * item = _cache.findFallback( _fallbackFontFace, size );
        if ( !item->getFont().isNull() )
            return item->getFont();
        return GetFont(size, 400, false, css_ff_sans_serif, _fallbackFontFace, -1);
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
            lString16Collection fonts;

            int facesFound = 0;

            FcFontSet *fontset;

            FcObjectSet *os = FcObjectSetBuild(FC_FILE, FC_WEIGHT, FC_FAMILY,
                                               FC_SLANT, FC_SPACING, FC_INDEX,
                                               FC_STYLE, NULL);
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
                lString16 fn16( fn.c_str() );
                fn16.lowercase();
                if (!fn16.endsWith(".ttf") && !fn16.endsWith(".odf") && !fn16.endsWith(".otf") && !fn16.endsWith(".pfb") && !fn16.endsWith(".pfa")  ) {
                    continue;
                }
                int weight = FC_WEIGHT_MEDIUM;
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
                //case FC_WEIGHT_ULTRALIGHT        FC_WEIGHT_EXTRALIGHT
                    weight = 200;
                    break;
                case FC_WEIGHT_LIGHT:         //    50
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
                    weight = 900;
                    break;
#endif
                default:
                    weight = 400;
                    break;
                }
                FcBool scalable = 0;
                res = FcPatternGetBool(fontset->fonts[i], FC_SCALABLE, 0, &scalable);
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
//                int cr_weight;
//                switch(weight) {
//                    case FC_WEIGHT_LIGHT: cr_weight = 200; break;
//                    case FC_WEIGHT_MEDIUM: cr_weight = 300; break;
//                    case FC_WEIGHT_DEMIBOLD: cr_weight = 500; break;
//                    case FC_WEIGHT_BOLD: cr_weight = 700; break;
//                    case FC_WEIGHT_BLACK: cr_weight = 800; break;
//                    default: cr_weight=300; break;
//                }
                css_font_family_t fontFamily = css_ff_sans_serif;
                lString16 face16((const char *)family);
                face16.lowercase();
                if ( spacing==FC_MONO )
                    fontFamily = css_ff_monospace;
                else if (face16.pos("sans") >= 0)
                    fontFamily = css_ff_sans_serif;
                else if (face16.pos("serif") >= 0)
                    fontFamily = css_ff_serif;

                //css_ff_inherit,
                //css_ff_serif,
                //css_ff_sans_serif,
                //css_ff_cursive,
                //css_ff_fantasy,
                //css_ff_monospace,
                bool italic = (slant!=FC_SLANT_ROMAN);

                lString8 face((const char*)family);
                lString16 style16((const char*)style);
                style16.lowercase();
                if (style16.pos("condensed") >= 0)
                    face << " Condensed";
                else if (style16.pos("extralight") >= 0)
                    face << " Extra Light";

                LVFontDef def(
                    lString8((const char*)s),
                    -1, // height==-1 for scalable fonts
                    weight,
                    italic,
                    fontFamily,
                    face,
                    index
                );

                CRLog::debug("FONTCONFIG: Font family:%s style:%s weight:%d slant:%d spacing:%d file:%s", family, style, weight, slant, spacing, s);
                if ( _cache.findDuplicate( &def ) ) {
                    CRLog::debug("is duplicate, skipping");
                    continue;
                }
                _cache.update( &def, LVFontRef(NULL) );

                if ( scalable && !def.getItalic() ) {
                    LVFontDef newDef( def );
                    newDef.setItalic(2); // can italicize
                    if ( !_cache.findDuplicate( &newDef ) )
                        _cache.update( &newDef, LVFontRef(NULL) );
                }

                facesFound++;


            }

            FcFontSetDestroy(fontset);
            CRLog::info("FONTCONFIG: %d fonts registered", facesFound);

            const char * fallback_faces [] = {
                "Arial Unicode MS",
                "AR PL ShanHeiSun Uni",
                "Liberation Sans",
                NULL
            };

            for ( int i=0; fallback_faces[i]; i++ )
                if ( SetFallbackFontFace(lString8(fallback_faces[i])) ) {
                    CRLog::info("Fallback font %s is found", fallback_faces[i]);
                    break;
                } else {
                    CRLog::trace("Fallback font %s is not found", fallback_faces[i]);
                }

            return facesFound > 0;
        }
        #else
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
        _log = fopen(DEBUG_FONT_MAN_LOG_FILE, "at");
        if ( _log ) {
            fprintf(_log, "=========================== LOGGING STARTED ===================\n");
        }
    #endif
        _requiredChars = L"azAZ09";//\x0410\x042F\x0430\x044F";
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
    virtual void getFaceList( lString16Collection & list )
    {
        FONT_MAN_GUARD
        _cache.getFaceList( list );
    }

    /// returns registered font files
    virtual void getFontFileNameList( lString16Collection & list )
    {
        FONT_MAN_GUARD
        _cache.getFontFileNameList(list);
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
            css_ff_inherit,
            alias,
            -1,
            id
    );

                int index = 0;

            FT_Face face = NULL;

            // for all faces in file
            for ( ;; index++ ) {
                int error = FT_New_Face( _library, item->getDef()->getName().c_str(), index, &face ); /* create face object */
                if ( error ) {
                    if (index == 0) {
                        CRLog::error("FT_New_Face returned error %d", error);
                    }
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

                bool boldFlag = !facename.empty() ? bold : (face->style_flags & FT_STYLE_FLAG_BOLD) != 0;
                bool italicFlag = !facename.empty() ? italic : (face->style_flags & FT_STYLE_FLAG_ITALIC) != 0;

                LVFontDef def2(
                        item->getDef()->getName(),
                        -1, // height==-1 for scalable fonts
                        boldFlag ? 700 : 400,
                        italicFlag,
                        fontFamily,
                        alias,
                        index,
                        id
                );

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
        if (item->getDef()->getTypeFace()==alias ){
            return true;
        }
    else
        {
            return false;
        }
}
    virtual LVFontRef GetFont(int size, int weight, bool italic, css_font_family_t family, lString8 typeface, int documentId, bool useBias=false)
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
                item->getDef()->getName().c_str(), item->getDef()->getIndex(), item->getDef()->getSize(), item->getDef()->getWeight(), item->getDef()->getItalic()?1:0,
                (int)item->getDef()->getFamily(), item->getDef()->getTypeFace().c_str(),
                weight - item->getDef()->getWeight(), item->getFont().isNull()?0:item->getFont()->getHeight()
            );
        }
    #endif
        bool italicize = false;

        LVFontDef newDef(*item->getDef());
        // printf("  got %s\n", newDef.getTypeFace().c_str());

        if (!item->getFont().isNull())
        {
            int deltaWeight = weight - item->getDef()->getWeight();
            if ( deltaWeight >= 200 ) {
                // embolden
                CRLog::debug("font: apply Embolding to increase weight from %d to %d", newDef.getWeight(), newDef.getWeight() + 200 );
                newDef.setWeight( newDef.getWeight() + 200 );
                LVFontRef ref = LVFontRef( new LVFontBoldTransform( item->getFont(), &_globalCache ) );
                _cache.update( &newDef, ref );
                return ref;
            } else {
                //fprintf(_log, "    : fount existing\n");
                return item->getFont();
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
            newDef.setItalic(true);
            italicize = true;
        }

        //printf("going to load font file %s\n", fname.c_str());
        bool loaded = false;
        // Use the family of the font we found in the cache (it may be different
        // from the requested family).
        // Assigning the requested familly to this new font could be wrong, and
        // may cause a style or font mismatch when loading from cache, forcing a
        // full re-rendering).
        family = item->getDef()->getFamily();
        if (item->getDef()->getBuf().isNull())
            loaded = font->loadFromFile( pathname.c_str(), item->getDef()->getIndex(), size, family, isBitmapModeForSize(size), italicize );
        else
            loaded = font->loadFromBuffer(item->getDef()->getBuf(), item->getDef()->getIndex(), size, family, isBitmapModeForSize(size), italicize );
        if (loaded) {
            //fprintf(_log, "    : loading from file %s : %s %d\n", item->getDef()->getName().c_str(),
            //    item->getDef()->getTypeFace().c_str(), item->getDef()->getSize() );
            LVFontRef ref(font);
            font->setKerningMode( GetKerningMode() );
            font->setFaceName( item->getDef()->getTypeFace() );
            newDef.setSize( size );
            //item->setFont( ref );
            //_cache.update( def, ref );
            _cache.update( &newDef, ref );
            int deltaWeight = weight - newDef.getWeight();
            if ( 1 && deltaWeight >= 200 ) {
                // embolden
                CRLog::debug("font: apply Embolding to increase weight from %d to %d", newDef.getWeight(), newDef.getWeight() + 200 );
                newDef.setWeight( newDef.getWeight() + 200 );
                ref = LVFontRef( new LVFontBoldTransform( ref, &_globalCache ) );
                _cache.update( &newDef, ref );
            }
//            int rsz = ref->getSize();
//            if ( rsz!=size ) {
//                size++;
//            }
            //delete def;
            return ref;
        }
        else
        {
            //printf("    not found!\n");
        }
        //delete def;
        delete font;
        return LVFontRef(NULL);
    }

    bool checkCharSet( FT_Face face )
    {
        // TODO: check existance of required characters (e.g. cyrillic)
        if (face==NULL)
            return false; // invalid face
        for ( int i=0; i<_requiredChars.length(); i++ ) {
            lChar16 ch = _requiredChars[i];
            FT_UInt ch_glyph_index = FT_Get_Char_Index( face, ch );
            if ( ch_glyph_index==0 ) {
                CRLog::debug("Required char not found in font: %04x", ch);
                return false; // no required char!!!
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
        lChar16 ch1 = 'i';
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

        lChar16 ch2 = 'W';
        FT_UInt ch_glyph_index2 = FT_Get_Char_Index( face, ch2 );
        if ( ch_glyph_index2==0 )
            return false; // no required char!!!
        return w1==w2;
    }
    */

    /// registers document font
    virtual bool RegisterDocumentFont(int documentId, LVContainerRef container, lString16 name, lString8 faceName, bool bold, bool italic) {
        FONT_MAN_GUARD
        lString8 name8 = UnicodeToUtf8(name);
        CRLog::debug("RegisterDocumentFont(documentId=%d, path=%s)", documentId, name8.c_str());
        if (_cache.findDocumentFontDuplicate(documentId, name8)) {
            return false;
        }
        LVStreamRef stream = container->OpenStream(name.c_str(), LVOM_READ);
        if (stream.isNull())
            return false;
        lUInt32 size = (lUInt32)stream->GetSize();
        if (size < 100 || size > 5000000)
            return false;
        LVByteArrayRef buf(new LVByteArray(size, 0));
        lvsize_t bytesRead = 0;
        if (stream->Read(buf->get(), size, &bytesRead) != LVERR_OK || bytesRead != size)
            return false;
        bool res = false;

        int index = 0;

        FT_Face face = NULL;

        // for all faces in file
        for ( ;; index++ ) {
            int error = FT_New_Memory_Face( _library, buf->get(), buf->length(), index, &face ); /* create face object */
            if ( error ) {
                if (index == 0) {
                    CRLog::error("FT_New_Memory_Face returned error %d", error);
                }
                break;
            }
//            bool scal = FT_IS_SCALABLE( face );
//            bool charset = checkCharSet( face );
//            //bool monospaced = isMonoSpaced( face );
//            if ( !scal || !charset ) {
//    //#if (DEBUG_FONT_MAN==1)
//     //           if ( _log ) {
//                CRLog::debug("    won't register font %s: %s",
//                    name.c_str(), !charset?"no mandatory characters in charset" : "font is not scalable"
//                    );
//    //            }
//    //#endif
//                if ( face ) {
//                    FT_Done_Face( face );
//                    face = NULL;
//                }
//                break;
//            }
            int num_faces = face->num_faces;

            css_font_family_t fontFamily = css_ff_sans_serif;
            if ( face->face_flags & FT_FACE_FLAG_FIXED_WIDTH )
                fontFamily = css_ff_monospace;
            lString8 familyName(!faceName.empty() ? faceName : ::familyName(face));
            /*
            if ( familyName=="Times" || familyName=="Times New Roman" )
                fontFamily = css_ff_serif;
            */

            bool boldFlag = !faceName.empty() ? bold : (face->style_flags & FT_STYLE_FLAG_BOLD) != 0;
            bool italicFlag = !faceName.empty() ? italic : (face->style_flags & FT_STYLE_FLAG_ITALIC) != 0;

            LVFontDef def(
                name8,
                -1, // height==-1 for scalable fonts
                boldFlag ? 700 : 400,
                italicFlag,
                fontFamily,
                familyName,
                index,
                documentId,
                buf
            );
    #if (DEBUG_FONT_MAN==1)
        if ( _log ) {
            fprintf(_log, "registering font: (file=%s[%d], size=%d, weight=%d, italic=%d, family=%d, typeface=%s)\n",
                def.getName().c_str(), def.getIndex(), def.getSize(), def.getWeight(), def.getItalic()?1:0, (int)def.getFamily(), def.getTypeFace().c_str()
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

    virtual bool RegisterExternalFont( lString16 name, lString8 family_name, bool bold, bool italic) {
		if (name.startsWithNoCase(lString16("res://")))
			name = name.substr(6);
		else if (name.startsWithNoCase(lString16("file://")))
			name = name.substr(7);
		lString8 fname = UnicodeToUtf8(name);

        bool res = false;

        int index = 0;

        FT_Face face = NULL;

        // for all faces in file
        for ( ;; index++ ) {
            int error = FT_New_Face( _library, fname.c_str(), index, &face ); /* create face object */
            if ( error ) {
                if (index == 0) {
                    CRLog::error("FT_New_Face returned error %d", error);
                }
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
    //#if (DEBUG_FONT_MAN==1)
     //           if ( _log ) {
                CRLog::debug("    won't register font %s: %s",
                    name.c_str(), !charset?"no mandatory characters in charset" : "font is not scalable"
                    );
    //            }
    //#endif
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
                bold?700:400,
                italic?true:false,
                fontFamily,
                family_name,
                index
            );
    #if (DEBUG_FONT_MAN==1)
        if ( _log ) {
            fprintf(_log, "registering font: (file=%s[%d], size=%d, weight=%d, italic=%d, family=%d, typeface=%s)\n",
                def.getName().c_str(), def.getIndex(), def.getSize(), def.getWeight(), def.getItalic()?1:0, (int)def.getFamily(), def.getTypeFace().c_str()
            );
        }
    #endif
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

            if ( face ) {
                FT_Done_Face( face );
                face = NULL;
            }

            if ( index>=num_faces-1 )
                break;
        }

        return res;
	}

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
            fprintf(_log, "RegisterFont( %s ) path=%s\n",
                name.c_str(), fname.c_str()
            );
        }
    #endif
        bool res = false;

        int index = 0;

        FT_Face face = NULL;

        // for all faces in file
        for ( ;; index++ ) {
            int error = FT_New_Face( _library, fname.c_str(), index, &face ); /* create face object */
            if ( error ) {
                if (index == 0) {
                    CRLog::error("FT_New_Face returned error %d", error);
                }
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
    //#if (DEBUG_FONT_MAN==1)
     //           if ( _log ) {
                CRLog::debug("    won't register font %s: %s",
                    name.c_str(), !charset?"no mandatory characters in charset" : "font is not scalable"
                    );
    //            }
    //#endif
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
                ( face->style_flags & FT_STYLE_FLAG_BOLD ) ? 700 : 400,
                ( face->style_flags & FT_STYLE_FLAG_ITALIC ) ? true : false,
                fontFamily,
                familyName,
                index
            );
    #if (DEBUG_FONT_MAN==1)
        if ( _log ) {
            fprintf(_log, "registering font: (file=%s[%d], size=%d, weight=%d, italic=%d, family=%d, typeface=%s)\n",
                def.getName().c_str(), def.getIndex(), def.getSize(), def.getWeight(), def.getItalic()?1:0, (int)def.getFamily(), def.getTypeFace().c_str()
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
#endif

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
        //_log = fopen( "fonts.log", "wt" );
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
    virtual LVFontRef GetFont(int size, int weight, bool italic, css_font_family_t family, lString8 typeface, int documentId, bool useBias=false)
    {
        LVFontDef * def = new LVFontDef(
            lString8::empty_str,
            size,
            weight,
            italic,
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
    virtual void getFontFileNameList( lString16Collection & list )
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
#endif


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
        //_log = fopen( "fonts.log", "wt" );
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
        LOGFONTA lf;
        memset(&lf, 0, sizeof(lf));
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

    virtual void getFaceList( lString16Collection & list )
    {
        _cache.getFaceList(list);
    }
    /// returns registered font files
    virtual void getFontFileNameList( lString16Collection & list )
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
            static lChar16 chars[] = {0, 0xBF, 0xE9, 0x106, 0x410, 0x44F, 0 };
            for (int i=0; chars[i]; i++)
            {
                LVFont::glyph_info_t glyph;
                if (!fnt.getGlyphInfo( chars[i], &glyph, L' ' )) //def_char
                    return 1;
            }
            fontman->RegisterFont( lf ); //&lpelfe->elfLogFont
        }
    }
    return 1;
}
#endif

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

#endif

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

int LVFontDef::CalcDuplicateMatch( const LVFontDef & def ) const
{
    if (def._documentId != -1 && _documentId != def._documentId)
        return false;
    bool size_match = (_size==-1 || def._size==-1) ? true
        : (def._size == _size);
    bool weight_match = (_weight==-1 || def._weight==-1) ? true
        : (def._weight == _weight);
    bool italic_match = (_italic == def._italic || _italic==-1 || def._italic==-1);
    bool family_match = (_family==css_ff_inherit || def._family==css_ff_inherit || def._family == _family);
    bool typeface_match = (_typeface == def._typeface);
    return size_match && weight_match && italic_match && family_match && typeface_match;
}

int LVFontDef::CalcMatch( const LVFontDef & def, bool useBias ) const
{
    if (_documentId != -1 && _documentId != def._documentId)
        return 0;
    int size_match = (_size==-1 || def._size==-1) ? 256
        : (def._size>_size ? _size*256/def._size : def._size*256/_size );
    int weight_diff = def._weight - _weight;
    if ( weight_diff<0 )
        weight_diff = -weight_diff;
    if ( weight_diff > 800 )
        weight_diff = 800;
    int weight_match = (_weight==-1 || def._weight==-1) ? 256
        : ( 256 - weight_diff * 256 / 800 );
    int italic_match = (_italic == def._italic || _italic==-1 || def._italic==-1) ? 256 : 0;
    if ( (_italic==2 || def._italic==2) && _italic>0 && def._italic>0 )
        italic_match = 128;
    int family_match = (_family==css_ff_inherit || def._family==css_ff_inherit || def._family == _family)
        ? 256
        : ( (_family==css_ff_monospace)==(def._family==css_ff_monospace) ? 64 : 0 );
    int typeface_match = (_typeface == def._typeface) ? 256 : 0;
    int bias = useBias ? _bias : 0;
    int score = bias
        + (size_match     * 100)
        + (weight_match   * 5)
        + (italic_match   * 5)
        + (family_match   * 100)
        + (typeface_match * 1000);
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
    int size_match = (_size==-1 || size==-1 || _size==size) ? 256 : 0;
    int weight_match = (_weight==-1) ? 256 : ( 256 - _weight * 256 / 800 );
    int italic_match = _italic == 0 ? 256 : 0;
    return
        + (size_match     * 100)
        + (weight_match   * 5)
        + (italic_match   * 5);
}








void LVBaseFont::DrawTextString( LVDrawBuf * buf, int x, int y,
                   const lChar16 * text, int len,
                   lChar16 def_char, lUInt32 * palette, bool addHyphen, lUInt32 , int , int )
{
    //static lUInt8 glyph_buf[16384];
    //LVFont::glyph_info_t info;
    int baseline = getBaseline();
    while (len>=(addHyphen?0:1))
    {
      if (len<=1 || *text != UNICODE_SOFT_HYPHEN_CODE)
      {
          lChar16 ch = ((len==0)?UNICODE_SOFT_HYPHEN_CODE:*text);

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

//          if ( !getGlyphInfo( ch, &info, def_char ) )
//          {
//              ch = def_char;
//              if ( !getGlyphInfo( ch, &info, def_char ) )
//                  ch = 0;
//          }
//          if (ch && getGlyphImage( ch, glyph_buf, def_char ))
//          {
//              if (info.blackBoxX && info.blackBoxY)
//              {
//                  buf->Draw( x + info.originX,
//                      y + baseline - info.originY,
//                      glyph_buf,
//                      info.blackBoxX,
//                      info.blackBoxY,
//                      palette);
//              }
//              x += info.width;
//          }
      }
      else if (*text != UNICODE_SOFT_HYPHEN_CODE)
      {
          //len = len;
      }
      len--;
      text++;
    }
}

#if (USE_BITMAP_FONTS==1)
bool LBitmapFont::getGlyphInfo( lUInt32 code, LVFont::glyph_info_t * glyph, lChar16 def_char )
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
                    const lChar16 * text, int len,
                    lUInt16 * widths,
                    lUInt8 * flags,
                    int max_width,
                    lChar16 def_char,
                    int letter_spacing,
                    bool allow_hyphenation
                 )
{
    return lvfontMeasureText( m_font, text, len, widths, flags, max_width, def_char );
}

lUInt32 LBitmapFont::getTextWidth( const lChar16 * text, int len )
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
                    L' '  // def_char
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
bool LBitmapFont::getGlyphImage(lUInt32 code, lUInt8 * buf, lChar16 def_char)
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
#endif

LVFontCacheItem * LVFontCache::findDuplicate( const LVFontDef * def )
{
    for (int i=0; i<_registered_list.length(); i++)
    {
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
    for (i=0; i<_instance_list.length(); i++)
    {
        int match = _instance_list[i]->_def.CalcFallbackMatch( face, size );
        if (match > best_instance_match)
        {
            best_instance_match = match;
            best_instance_index = i;
        }
    }
    for (i=0; i<_registered_list.length(); i++)
    {
        int match = _registered_list[i]->_def.CalcFallbackMatch( face, size );
        if (match > best_match)
        {
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
    for (int nindex=0; nindex==0 || nindex<list.length(); nindex++)
    {
        if ( nindex<list.length() )
            def.setTypeFace( list[nindex] );
        else
            def.setTypeFace(lString8::empty_str);
        for (i=0; i<_instance_list.length(); i++)
        {
            int match = _instance_list[i]->_def.CalcMatch( def, useBias );
            if (match > best_instance_match)
            {
                best_instance_match = match;
                best_instance_index = i;
            }
        }
        for (i=0; i<_registered_list.length(); i++)
        {
            int match = _registered_list[i]->_def.CalcMatch( def, useBias );
            if (match > best_match)
            {
                best_match = match;
                best_index = i;
            }
        }
    }
    if (best_index<0)
        return NULL;
    if (best_instance_match >= best_match)
        return _instance_list[best_instance_index];
    return _registered_list[best_index];
}

bool LVFontCache::setAsPreferredFontWithBias( lString8 face, int bias, bool clearOthersBias )
{
    bool found = false;
    int i;
    for (i=0; i<_instance_list.length(); i++)
    {
        if (_instance_list[i]->_def.setBiasIfNameMatch( face, bias, clearOthersBias ))
            found = true;
    }
    for (i=0; i<_registered_list.length(); i++)
    {
        if (_registered_list[i]->_def.setBiasIfNameMatch( face, bias, clearOthersBias ))
            found = true;
    }
    return found;
}

void LVFontCache::addInstance( const LVFontDef * def, LVFontRef ref )
{
    if ( ref.isNull() )
        printf("Adding null font instance!");
    LVFontCacheItem * item = new LVFontCacheItem(*def);
    item->_fnt = ref;
    _instance_list.add( item );
}

void LVFontCache::update( const LVFontDef * def, LVFontRef ref )
{
    int i;
    if ( !ref.isNull() ) {
        for (i=0; i<_instance_list.length(); i++)
        {
            if ( _instance_list[i]->_def == *def )
            {
                if (ref.isNull())
                {
                    _instance_list.erase(i, 1);
                }
                else
                {
                    _instance_list[i]->_fnt = ref;
                }
                return;
            }
        }
        // add new
        //LVFontCacheItem * item;
        //item = new LVFontCacheItem(*def);
        addInstance( def, ref );
    } else {
        for (i=0; i<_registered_list.length(); i++)
        {
            if ( _registered_list[i]->_def == *def )
            {
                return;
            }
        }
        // add new
        LVFontCacheItem * item;
        item = new LVFontCacheItem(*def);
        _registered_list.add( item );
    }
}

void LVFontCache::removeDocumentFonts(int documentId)
{
    int i;
    for (i=_instance_list.length()-1; i>=0; i--) {
        if (_instance_list[i]->_def.getDocumentId() == documentId)
            delete _instance_list.remove(i);
    }
    for (i=_registered_list.length()-1; i>=0; i--) {
        if (_registered_list[i]->_def.getDocumentId() == documentId)
            delete _registered_list.remove(i);
    }
}

// garbage collector
void LVFontCache::gc()
{
    int droppedCount = 0;
    int usedCount = 0;
    for (int i=_instance_list.length()-1; i>=0; i--)
    {
        if ( _instance_list[i]->_fnt.getRefCount()<=1 )
        {
            if ( CRLog::isTraceEnabled() )
                CRLog::trace("dropping font instance %s[%d] by gc()", _instance_list[i]->getDef()->getTypeFace().c_str(), _instance_list[i]->getDef()->getSize() );
            _instance_list.erase(i,1);
            droppedCount++;
        } else {
            usedCount++;
        }
    }
    if ( CRLog::isDebugEnabled() )
        CRLog::debug("LVFontCache::gc() : %d fonts still used, %d fonts dropped", usedCount, droppedCount );
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
    LOGFONTA lf;
    memset(&lf, 0, sizeof(LOGFONTA));
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
bool LVWin32DrawFont::getGlyphInfo( lUInt16 code, glyph_info_t * glyph, lChar16 def_char )
{
    return false;
}

/// returns char width
int LVWin32DrawFont::getCharWidth( lChar16 ch, lChar16 def_char )
{
    if (_hfont==NULL)
        return 0;
    // measure character widths
    GCP_RESULTSW gcpres;
    memset( &gcpres, 0, sizeof(gcpres) );
    gcpres.lStructSize = sizeof(gcpres);
    lChar16 str[2];
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

lUInt32 LVWin32DrawFont::getTextWidth( const lChar16 * text, int len )
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
                    L' '  // def_char
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
                    const lChar16 * text, int len,
                    lUInt16 * widths,
                    lUInt8 * flags,
                    int max_width,
                    lChar16 def_char,
                    int letter_spacing,
                    bool allow_hyphenation
                 )
{
    if (_hfont==NULL)
        return 0;

    lString16 str(text, len);
    assert(str[len]==0);
    //str += L"         ";
    lChar16 * pstr = str.modify();
    assert(pstr[len]==0);
    // substitute soft hyphens with zero width spaces
    for (int i=0; i<len; i++)
    {
        if (pstr[i]==UNICODE_SOFT_HYPHEN_CODE)
            pstr[i] = UNICODE_ZERO_WIDTH_SPACE;
    }
    assert(pstr[len]==0);
    // measure character widths
    GCP_RESULTSW gcpres;
    memset( &gcpres, 0, sizeof(gcpres) );
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
    lChar16 ch;
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
        lChar16 ch = text[hwEnd];
        if (lvfontIsUnicodeSpace(ch))
            break;
        if (flags[hwEnd-1]&LCHAR_ALLOW_WRAP_AFTER)
            break;
        if (ch=='.' || ch==',' || ch=='!' || ch=='?' || ch=='?' || ch==':' || ch==';')
            break;

    }
    HyphMan::hyphenate(text+hwStart, hwEnd-hwStart, widths+hwStart, flags+hwStart, _hyphen_width, max_width);

    return nchars;
}

/// draws text string
void LVWin32DrawFont::DrawTextString( LVDrawBuf * buf, int x, int y,
                   const lChar16 * text, int len,
                   lChar16 def_char, lUInt32 * palette, bool addHyphen,
                   lUInt32 flags, int letter_spacing, int width )
{
    if (_hfont==NULL)
        return;

    lString16 str(text, len);
    // substitute soft hyphens with zero width spaces
    if (addHyphen)
        str += UNICODE_SOFT_HYPHEN_CODE;
    //str += L"       ";
    lChar16 * pstr = str.modify();
    for (int i=0; i<len-1; i++)
    {
        if (pstr[i]==UNICODE_SOFT_HYPHEN_CODE)
            pstr[i] = UNICODE_ZERO_WIDTH_SPACE;
    }

    lvRect clip;
    buf->GetClipRect(&clip);
    if (y > clip.bottom || y+_height < clip.top)
        return;
    if (buf->GetBitsPerPixel()<16)
    {
        // draw using backbuffer
        SIZE sz;
        if ( !GetTextExtentPoint32W(_drawbuf.GetDC(),
                str.c_str(), str.length(), &sz) )
            return;
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
}

/** \brief get glyph image in 1 byte per pixel format
    \param code is unicode character
    \param buf is buffer [width*height] to place glyph data
    \return true if glyph was found
*/
bool LVWin32DrawFont::getGlyphImage(lUInt16 code, lUInt8 * buf, lChar16 def_char)
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


glyph_t * LVWin32Font::GetGlyphRec( lChar16 ch )
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
bool LVWin32Font::getGlyphInfo( lUInt16 code, glyph_info_t * glyph, lChar16 def_char )
{
    if (_hfont==NULL)
        return false;
    glyph_t * p = GetGlyphRec( code );
    if (!p)
        return false;
    *glyph = p->gi;
    return true;
}

lUInt32 LVWin32Font::getTextWidth( const lChar16 * text, int len )
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
                    L' '  // def_char
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
                    const lChar16 * text, int len,
                    lUInt16 * widths,
                    lUInt8 * flags,
                    int max_width,
                    lChar16 def_char,
                    int letter_spacing,
                    bool allow_hyphenation
                 )
{
    if (_hfont==NULL)
        return 0;

    lUInt16 wsum = 0;
    lUInt16 nchars = 0;
    glyph_t * glyph; //GetGlyphRec( lChar16 ch )
    lUInt16 gwidth = 0;
    lUInt16 hyphwidth = 0;
    lUInt8 bflags;
    int isSpace;
    lChar16 ch;
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
        lChar16 ch = text[hwEnd];
        if (lvfontIsUnicodeSpace(ch))
            break;
        if (flags[hwEnd-1]&LCHAR_ALLOW_WRAP_AFTER)
            break;
        if (ch=='.' || ch==',' || ch=='!' || ch=='?' || ch=='?')
            break;

    }
    HyphMan::hyphenate(text+hwStart, hwEnd-hwStart, widths+hwStart, flags+hwStart, hyphwidth, max_width);

    return nchars;
}

/** \brief get glyph image in 1 byte per pixel format
    \param code is unicode character
    \param buf is buffer [width*height] to place glyph data
    \return true if glyph was found
*/
bool LVWin32Font::getGlyphImage(lUInt16 code, lUInt8 * buf, lChar16 def_char)
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

#endif

/// to compare two fonts
bool operator == (const LVFont & r1, const LVFont & r2)
{
    if ( &r1 == &r2 )
        return true;
    return r1.getSize()==r2.getSize()
            && r1.getWeight()==r2.getWeight()
            && r1.getItalic()==r2.getItalic()
            && r1.getFontFamily()==r2.getFontFamily()
            && r1.getTypeFace()==r2.getTypeFace()
            && r1.getKerningMode()==r2.getKerningMode()
            && r1.getHintingMode()==r2.getHintingMode()
            ;
}

