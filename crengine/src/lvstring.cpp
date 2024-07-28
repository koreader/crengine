/*******************************************************

   CoolReader Engine

   lvstring.cpp:  string classes implementation

   (c) Vadim Lopatin, 2000-2006
   This source code is distributed under the terms of
   GNU General Public License
   See LICENSE file for details

*******************************************************/

#include "../include/lvstring.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <time.h>
#ifdef LINUX
#include <sys/time.h>
#if !defined(__APPLE__)
#include <malloc.h>
#endif
#endif

#if (USE_ZLIB==1)
#include <zlib.h>
#endif

#if (USE_UTF8PROC==1)
#include <utf8proc.h>
#endif

#if !defined(__SYMBIAN32__) && defined(_WIN32)
extern "C" {
#include <windows.h>
}
#endif

#define LS_DEBUG_CHECK

// set to 1 to enable debugging
#define DEBUG_STATIC_STRING_ALLOC 0


static lChar8 empty_str_8[] = {0};
static lstring_chunk_t empty_chunk_8(empty_str_8);
lstring_chunk_t * lString8::EMPTY_STR_8 = &empty_chunk_8;

static lChar32 empty_str_32[] = {0};
static lstring_chunk_t empty_chunk_32(empty_str_32);
lstring_chunk_t * lString32::EMPTY_STR_32 = &empty_chunk_32;

//================================================================================
// atomic string storages for string literals
//================================================================================

static const void * const_ptrs_8[CONST_STRING_BUFFER_SIZE] = {NULL};
static lString8 values_8[CONST_STRING_BUFFER_SIZE];
static int size_8 = 0;

/// get reference to atomic constant string for string literal e.g. cs8("abc") -- fast and memory effective
const lString8 & cs8(const char * str) {
    unsigned int index =  (unsigned int)(((ptrdiff_t)str * CONST_STRING_BUFFER_HASH_MULT) & CONST_STRING_BUFFER_MASK);
    for (;;) {
        const void * p = const_ptrs_8[index];
        if (p == str) {
            return values_8[index];
        } else if (p == NULL) {
#if DEBUG_STATIC_STRING_ALLOC == 1
            CRLog::trace("allocating static string8 %s", str);
#endif
            const_ptrs_8[index] = str;
            size_8++;
            values_8[index] = lString8(str);
            values_8[index].addref();
            return values_8[index];
        }
        if (size_8 > CONST_STRING_BUFFER_SIZE / 4) {
            crFatalError(-1, "out of memory for const string8");
        }
        index = (index + 1) & CONST_STRING_BUFFER_MASK;
    }
    return lString8::empty_str;
}

static const void * const_ptrs_32[CONST_STRING_BUFFER_SIZE] = {NULL};
static lString32 values_32[CONST_STRING_BUFFER_SIZE];
static int size_32 = 0;

/// get reference to atomic constant wide string for string literal e.g. cs32("abc") -- fast and memory effective
const lString32 & cs32(const char * str) {
    unsigned int index =  (unsigned int)(((ptrdiff_t)str * CONST_STRING_BUFFER_HASH_MULT) & CONST_STRING_BUFFER_MASK);
    for (;;) {
        const void * p = const_ptrs_32[index];
        if (p == str) {
            return values_32[index];
        } else if (p == NULL) {
#if DEBUG_STATIC_STRING_ALLOC == 1
            CRLog::trace("allocating static string32 %s", str);
#endif
            const_ptrs_32[index] = str;
            size_32++;
            values_32[index] = lString32(str);
            values_32[index].addref();
            return values_32[index];
        }
        if (size_32 > CONST_STRING_BUFFER_SIZE / 4) {
            crFatalError(-1, "out of memory for const string8");
        }
        index = (index + 1) & CONST_STRING_BUFFER_MASK;
    }
    return lString32::empty_str;
}

/// get reference to atomic constant wide string for string literal e.g. cs32(U"abc") -- fast and memory effective
const lString32 & cs32(const lChar32 * str) {
    unsigned int index = (((unsigned int)((ptrdiff_t)str)) * CONST_STRING_BUFFER_HASH_MULT) & CONST_STRING_BUFFER_MASK;
    for (;;) {
        const void * p = const_ptrs_32[index];
        if (p == str) {
            return values_32[index];
        } else if (p == NULL) {
#if DEBUG_STATIC_STRING_ALLOC == 1
            CRLog::trace("allocating static string32 %s", LCSTR(str));
#endif
            const_ptrs_32[index] = str;
            size_32++;
            values_32[index] = lString32(str);
            values_32[index].addref();
            return values_32[index];
        }
        if (size_32 > CONST_STRING_BUFFER_SIZE / 4) {
            crFatalError(-1, "out of memory for const string8");
        }
        index = (index + 1) & CONST_STRING_BUFFER_MASK;
    }
    return lString32::empty_str;
}



//================================================================================
// memory allocation slice
//================================================================================
struct lstring_chunk_slice_t {
    lstring_chunk_t * pChunks; // first chunk
    lstring_chunk_t * pEnd;    // first free byte after last chunk
    lstring_chunk_t * pFree;   // first free chunk
    int used;
    lstring_chunk_slice_t( int size )
    {
        pChunks = (lstring_chunk_t *) malloc(sizeof(lstring_chunk_t) * size);
        pEnd = pChunks + size;
        pFree = pChunks;
        for (lstring_chunk_t * p = pChunks; p<pEnd; ++p)
        {
            p->buf8 = (char*)(p+1);
            p->size = 0;
        }
        (pEnd-1)->buf8 = NULL;
    }
    ~lstring_chunk_slice_t()
    {
        free( pChunks );
    }
    inline lstring_chunk_t * alloc_chunk()
    {
        lstring_chunk_t * res = pFree;
        pFree = (lstring_chunk_t *)res->buf8;
        return res;
    }
    inline bool free_chunk( lstring_chunk_t * pChunk )
    {
        if (pChunk < pChunks || pChunk >= pEnd)
            return false; // chunk does not belong to this slice
/*
#ifdef LS_DEBUG_CHECK
        if (!pChunk->size)
        {
            crFatalError(); // already freed!!!
        }
        pChunk->size = 0;
#endif
*/
        pChunk->buf8 = (char *)pFree;
        pFree = pChunk;
        return true;
    }
};

//#define FIRST_SLICE_SIZE 256
//#define MAX_SLICE_COUNT  20
#if (LDOM_USE_OWN_MEM_MAN == 1)
static lstring_chunk_slice_t * slices[MAX_SLICE_COUNT];
static int slices_count = 0;
static bool slices_initialized = false;
#endif

#if (LDOM_USE_OWN_MEM_MAN == 1)
static void init_ls_storage()
{
    slices[0] = new lstring_chunk_slice_t( FIRST_SLICE_SIZE );
    slices_count = 1;
    slices_initialized = true;
}

void free_ls_storage()
{
    if (!slices_initialized)
        return;
    for (int i=0; i<slices_count; i++)
    {
        delete slices[i];
    }
    slices_count = 0;
    slices_initialized = false;
}

lstring_chunk_t * lstring_chunk_t::alloc()
{
    if (!slices_initialized)
        init_ls_storage();
    // search for existing slice
    for (int i=slices_count-1; i>=0; --i)
    {
        if (slices[i]->pFree != NULL)
            return slices[i]->alloc_chunk();
    }
    // alloc new slice
    if (slices_count >= MAX_SLICE_COUNT)
        crFatalError();
    lstring_chunk_slice_t * new_slice = new lstring_chunk_slice_t( FIRST_SLICE_SIZE << (slices_count+1) );
    slices[slices_count++] = new_slice;
    return slices[slices_count-1]->alloc_chunk();
}

void lstring_chunk_t::free( lstring_chunk_t * pChunk )
{
    for (int i=slices_count-1; i>=0; --i)
    {
        if (slices[i]->free_chunk(pChunk))
            return;
    }
    crFatalError(); // wrong pointer!!!
}
#endif

////////////////////////////////////////////////////////////////////////////
// Utility functions
////////////////////////////////////////////////////////////////////////////

inline int _lStr_len(const lChar32 * str)
{
    int len;
    for (len=0; *str; str++)
        len++;
    return len;
}

inline int _lStr_len(const lChar8 * str)
{
    int len;
    for (len=0; *str; str++)
        len++;
    return len;
}

inline int _lStr_nlen(const lChar32 * str, int maxcount)
{
    int len;
    for (len=0; len<maxcount && *str; str++)
        len++;
    return len;
}

inline int _lStr_nlen(const lChar8 * str, int maxcount)
{
    int len;
    for (len=0; len<maxcount && *str; str++)
        len++;
    return len;
}

inline int _lStr_cpy(lChar32 * dst, const lChar32 * src)
{
    int count;
    for ( count=0; (*dst++ = *src++); count++ )
        ;
    return count;
}

inline int _lStr_cpy(lChar8 * dst, const lChar8 * src)
{
    int count;
    for ( count=0; (*dst++ = *src++); count++ )
        ;
    return count;
}

inline int _lStr_cpy(lChar32 * dst, const lChar8 * src)
{
    int count;
    for ( count=0; (*dst++ = *src++); count++ )
        ;
    return count;
}

inline int _lStr_cpy(lChar8 * dst, const lChar32 * src)
{
    int count;
    for ( count=0; (*dst++ = (lChar8)*src++); count++ )
        ;
    return count;
}

inline int _lStr_ncpy(lChar32 * dst, const lChar32 * src, int maxcount)
{
    int count = 0;
    do
    {
        if (++count > maxcount)
        {
            *dst = 0;
            return count;
        }
    } while ((*dst++ = *src++));
    return count;
}

inline int _lStr_ncpy(lChar32 * dst, const lChar8 * src, int maxcount)
{
    int count = 0;
    do
    {
        if (++count > maxcount)
        {
            *dst = 0;
            return count;
        }
    } while ((*dst++ = (unsigned char)*src++));
    return count;
}

inline int _lStr_ncpy(lChar8 * dst, const lChar8 * src, int maxcount)
{
    int count = 0;
    do
    {
        if (++count > maxcount)
        {
            *dst = 0;
            return count;
        }
    } while ((*dst++ = *src++));
    return count;
}

inline void _lStr_memcpy(lChar32 * dst, const lChar32 * src, int count)
{
    while ( count-- > 0)
        (*dst++ = *src++);
}

inline void _lStr_memcpy(lChar8 * dst, const lChar8 * src, int count)
{
    memcpy(dst, src, count);
}

inline void _lStr_memset(lChar32 * dst, lChar32 value, int count)
{
    while ( count-- > 0)
        *dst++ = value;
}

inline void _lStr_memset(lChar8 * dst, lChar8 value, int count)
{
    memset(dst, value, count);
}

int lStr_len(const lChar32 * str)
{
    return _lStr_len(str);
}

int lStr_len(const lChar8 * str)
{
    return _lStr_len(str);
}

int lStr_nlen(const lChar32 * str, int maxcount)
{
    return _lStr_nlen(str, maxcount);
}

int lStr_nlen(const lChar8 * str, int maxcount)
{
    return _lStr_nlen(str, maxcount);
}

int lStr_cpy(lChar32 * dst, const lChar32 * src)
{
    return _lStr_cpy(dst, src);
}

int lStr_cpy(lChar8 * dst, const lChar8 * src)
{
    return _lStr_cpy(dst, src);
}

int lStr_cpy(lChar32 * dst, const lChar8 * src)
{
    return _lStr_cpy(dst, src);
}

int lStr_ncpy(lChar32 * dst, const lChar32 * src, int maxcount)
{
    return _lStr_ncpy(dst, src, maxcount);
}

int lStr_ncpy(lChar8 * dst, const lChar8 * src, int maxcount)
{
    return _lStr_ncpy(dst, src, maxcount);
}

void lStr_memcpy(lChar32 * dst, const lChar32 * src, int count)
{
    _lStr_memcpy(dst, src, count);
}

void lStr_memcpy(lChar8 * dst, const lChar8 * src, int count)
{
    _lStr_memcpy(dst, src, count);
}

void lStr_memset(lChar32 * dst, lChar32 value, int count)
{
    _lStr_memset(dst, value, count);
}

void lStr_memset(lChar8 * dst, lChar8 value, int count)
{
    _lStr_memset(dst, value, count);
}

int lStr_cmp(const lChar32 * dst, const lChar32 * src)
{
    while ( *dst == *src)
    {
        if (! *dst )
            return 0;
        ++dst;
        ++src;
    }
    if ( *dst > *src )
        return 1;
    else
        return -1;
}

int lStr_cmp(const lChar8 * dst, const lChar8 * src)
{
    while ( *dst == *src)
    {
        if (! *dst )
            return 0;
        ++dst;
        ++src;
    }
    if ( *dst > *src )
        return 1;
    else
        return -1;
}

int lStr_cmp(const lChar32 * dst, const lChar8 * src)
{
    while ( *dst == (lChar32)*src)
    {
        if (! *dst )
            return 0;
        ++dst;
        ++src;
    }
    if ( *dst > (lChar32)*src )
        return 1;
    else
        return -1;
}

int lStr_cmp(const lChar8 * dst, const lChar32 * src)
{
    while ( (lChar32)*dst == *src)
    {
        if (! *dst )
            return 0;
        ++dst;
        ++src;
    }
    if ( (lChar32)*dst > *src )
        return 1;
    else
        return -1;
}

////////////////////////////////////////////////////////////////////////////
// lString32
////////////////////////////////////////////////////////////////////////////

void lString32::free()
{
    if ( pchunk==EMPTY_STR_32 )
        return;
    //assert(pchunk->buf32[pchunk->len]==0);
    ::free(pchunk->buf32);
#if (LDOM_USE_OWN_MEM_MAN == 1)
    for (int i=slices_count-1; i>=0; --i)
    {
        if (slices[i]->free_chunk(pchunk))
            return;
    }
    crFatalError(); // wrong pointer!!!
#else
    ::free(pchunk);
#endif
}

void lString32::alloc(int sz)
{
#if (LDOM_USE_OWN_MEM_MAN == 1)
    pchunk = lstring_chunk_t::alloc();
#else
    pchunk = (lstring_chunk_t*)::malloc(sizeof(lstring_chunk_t));
#endif
    pchunk->buf32 = (lChar32*) ::malloc( sizeof(lChar32) * (sz+1) );
    assert( pchunk->buf32!=NULL );
    pchunk->size = sz;
    pchunk->nref = 1;
}

lString32::lString32(const lChar32 * str)
{
    if (!str || !(*str))
    {
        pchunk = EMPTY_STR_32;
        addref();
        return;
    }
    size_type len = _lStr_len(str);
    alloc( len );
    pchunk->len = len;
    _lStr_cpy( pchunk->buf32, str );
}

lString32::lString32(const lChar8 * str)
{
    if (!str || !(*str))
    {
        pchunk = EMPTY_STR_32;
        addref();
        return;
    }
    pchunk = EMPTY_STR_32;
    addref();
	*this = Utf8ToUnicode( str );
}

/// constructor from utf8 character array fragment
lString32::lString32(const lChar8 * str, size_type count)
{
    if (!str || !(*str))
    {
        pchunk = EMPTY_STR_32;
        addref();
        return;
    }
    pchunk = EMPTY_STR_32;
    addref();
	*this = Utf8ToUnicode( str, count );
}


lString32::lString32(const value_type * str, size_type count)
{
    if ( !str || !(*str) || count<=0 )
    {
        pchunk = EMPTY_STR_32; addref();
    }
    else
    {
        size_type len = _lStr_nlen(str, count);
        alloc(len);
        _lStr_ncpy( pchunk->buf32, str, len );
        pchunk->len = len;
    }
}

lString32::lString32(const lString32 & str, size_type offset, size_type count)
{
    if ( count > str.length() - offset )
        count = str.length() - offset;
    if (count<=0)
    {
        pchunk = EMPTY_STR_32; addref();
    }
    else
    {
        alloc(count);
        _lStr_memcpy( pchunk->buf32, str.pchunk->buf32+offset, count );
        pchunk->buf32[count]=0;
        pchunk->len = count;
    }
}

lString32 & lString32::assign(const lChar32 * str)
{
    if (!str || !(*str))
    {
        clear();
    }
    else
    {
        size_type len = _lStr_len(str);
        if (pchunk->nref==1)
        {
            if (pchunk->size < len)
            {
                // resize is necessary
                pchunk->buf32 = (lChar32*) ::realloc( pchunk->buf32, sizeof(lChar32)*(len+1) );
                pchunk->size = len;
            }
        }
        else
        {
            release();
            alloc(len);
        }
        _lStr_cpy( pchunk->buf32, str );
        pchunk->len = len;
    }
    return *this;
}

lString32 & lString32::assign(const lChar8 * str)
{
    if (!str || !(*str))
    {
        clear();
    }
    else
    {
        size_type len = _lStr_len(str);
        if (pchunk->nref==1)
        {
            if (pchunk->size < len)
            {
                // resize is necessary
                pchunk->buf32 = (lChar32*) ::realloc( pchunk->buf32, sizeof(lChar32)*(len+1) );
                pchunk->size = len;
            }
        }
        else
        {
            release();
            alloc(len);
        }
        _lStr_cpy( pchunk->buf32, str );
        pchunk->len = len;
    }
    return *this;
}

lString32 & lString32::assign(const lChar32 * str, size_type count)
{
    if ( !str || !(*str) || count<=0 )
    {
        clear();
    }
    else
    {
        size_type len = _lStr_nlen(str, count);
        if (pchunk->nref==1)
        {
            if (pchunk->size < len)
            {
                // resize is necessary
                pchunk->buf32 = (lChar32*) ::realloc( pchunk->buf32, sizeof(lChar32)*(len+1) );
                pchunk->size = len;
            }
        }
        else
        {
            release();
            alloc(len);
        }
        _lStr_ncpy( pchunk->buf32, str, count );
        pchunk->len = len;
    }
    return *this;
}

lString32 & lString32::assign(const lChar8 * str, size_type count)
{
    if ( !str || !(*str) || count<=0 )
    {
        clear();
    }
    else
    {
        size_type len = _lStr_nlen(str, count);
        if (pchunk->nref==1)
        {
            if (pchunk->size < len)
            {
                // resize is necessary
                pchunk->buf32 = (lChar32*) ::realloc( pchunk->buf32, sizeof(lChar32)*(len+1) );
                pchunk->size = len;
            }
        }
        else
        {
            release();
            alloc(len);
        }
        _lStr_ncpy( pchunk->buf32, str, count );
        pchunk->len = len;
    }
    return *this;
}

lString32 & lString32::assign(const lString32 & str, size_type offset, size_type count)
{
    if ( count > str.length() - offset )
        count = str.length() - offset;
    if (count<=0)
    {
        clear();
    }
    else
    {
        if (pchunk==str.pchunk)
        {
            if (&str != this)
            {
                release();
                alloc(count);
            }
            if (offset>0)
            {
                _lStr_memcpy( pchunk->buf32, str.pchunk->buf32+offset, count );
            }
            pchunk->buf32[count]=0;
        }
        else
        {
            if (pchunk->nref==1)
            {
                if (pchunk->size < count)
                {
                    // resize is necessary
                    pchunk->buf32 = (lChar32*) ::realloc( pchunk->buf32, sizeof(lChar32)*(count+1) );
                    pchunk->size = count;
                }
            }
            else
            {
                release();
                alloc(count);
            }
            _lStr_memcpy( pchunk->buf32, str.pchunk->buf32+offset, count );
            pchunk->buf32[count]=0;
        }
        pchunk->len = count;
    }
    return *this;
}

lString32 & lString32::erase(size_type offset, size_type count)
{
    if ( count > length() - offset )
        count = length() - offset;
    if (count<=0)
    {
        clear();
    }
    else
    {
        size_type newlen = length()-count;
        if (pchunk->nref==1)
        {
            _lStr_memcpy( pchunk->buf32+offset, pchunk->buf32+offset+count, newlen-offset+1 );
        }
        else
        {
            lstring_chunk_t * poldchunk = pchunk;
            release();
            alloc( newlen );
            _lStr_memcpy( pchunk->buf32, poldchunk->buf32, offset );
            _lStr_memcpy( pchunk->buf32+offset, poldchunk->buf32+offset+count, newlen-offset+1 );
        }
        pchunk->len = newlen;
        pchunk->buf32[newlen]=0;
    }
    return *this;
}

void lString32::reserve(size_type n)
{
    if (pchunk->nref==1)
    {
        if (pchunk->size < n)
        {
            pchunk->buf32 = (lChar32*) ::realloc( pchunk->buf32, sizeof(lChar32)*(n+1) );
            pchunk->size = n;
        }
    }
    else
    {
        lstring_chunk_t * poldchunk = pchunk;
        release();
        alloc( n );
        _lStr_memcpy( pchunk->buf32, poldchunk->buf32, poldchunk->len+1 );
        pchunk->len = poldchunk->len;
    }
}

void lString32::lock( size_type newsize )
{
    if (pchunk->nref>1)
    {
        lstring_chunk_t * poldchunk = pchunk;
        release();
        alloc( newsize );
        size_type len = newsize;
        if (len>poldchunk->len)
            len = poldchunk->len;
        _lStr_memcpy( pchunk->buf32, poldchunk->buf32, len );
        pchunk->buf32[len]=0;
        pchunk->len = len;
    }
}

// lock string, allocate buffer and reset length to 0
void lString32::reset( size_type size )
{
    if (pchunk->nref>1 || pchunk->size<size)
    {
        release();
        alloc( size );
    }
    pchunk->buf32[0] = 0;
    pchunk->len = 0;
}

void lString32::resize(size_type n, lChar32 e)
{
    lock( n );
    if (pchunk->size < n)
    {
        pchunk->buf32 = (lChar32*) ::realloc( pchunk->buf32, sizeof(lChar32)*(n+1) );
        pchunk->size = n;
    }
    // fill with data if expanded
    for (size_type i=pchunk->len; i<n; i++)
        pchunk->buf32[i] = e;
    pchunk->buf32[pchunk->len] = 0;
}

lString32 & lString32::append(const lChar32 * str)
{
    size_type len = _lStr_len(str);
    reserve( pchunk->len+len );
    _lStr_memcpy(pchunk->buf32+pchunk->len, str, len+1);
    pchunk->len += len;
    return *this;
}

lString32 & lString32::append(const lChar32 * str, size_type count)
{
    reserve(pchunk->len + count);
    _lStr_ncpy(pchunk->buf32 + pchunk->len, str, count);
    pchunk->len += count;
    return *this;
}

lString32 & lString32::append(const lChar8 * str)
{
    size_type len = _lStr_len(str);
    reserve( pchunk->len+len );
    _lStr_ncpy(pchunk->buf32+pchunk->len, str, len+1);
    pchunk->len += len;
    return *this;
}

lString32 & lString32::append(const lChar8 * str, size_type count)
{
    reserve(pchunk->len + count);
    _lStr_ncpy(pchunk->buf32+pchunk->len, str, count);
    pchunk->len += count;
    return *this;
}

lString32 & lString32::append(const lString32 & str)
{
    size_type len2 = pchunk->len + str.pchunk->len;
    reserve( len2 );
    _lStr_memcpy( pchunk->buf32+pchunk->len, str.pchunk->buf32, str.pchunk->len+1 );
    pchunk->len = len2;
    return *this;
}

lString32 & lString32::append(const lString32 & str, size_type offset, size_type count)
{
    if ( str.pchunk->len>offset )
    {
        if ( offset + count > str.pchunk->len )
            count = str.pchunk->len - offset;
        reserve( pchunk->len+count );
        _lStr_ncpy(pchunk->buf32 + pchunk->len, str.pchunk->buf32 + offset, count);
        pchunk->len += count;
        pchunk->buf32[pchunk->len] = 0;
    }
    return *this;
}

lString32 & lString32::append(size_type count, lChar32 ch)
{
    reserve( pchunk->len+count );
    _lStr_memset(pchunk->buf32+pchunk->len, ch, count);
    pchunk->len += count;
    pchunk->buf32[pchunk->len] = 0;
    return *this;
}

lString32 & lString32::insert(size_type p0, size_type count, lChar32 ch)
{
    if (p0>pchunk->len)
        p0 = pchunk->len;
    reserve( pchunk->len+count );
    for (size_type i=pchunk->len-1; i>=p0; i--)
        pchunk->buf32[i+count] = pchunk->buf32[i];
    _lStr_memset(pchunk->buf32+p0, ch, count);
    pchunk->len += count;
    pchunk->buf32[pchunk->len] = 0;
    return *this;
}

lString32 & lString32::insert(size_type p0, const lString32 & str)
{
    if (p0>pchunk->len)
        p0 = pchunk->len;
    int count = str.length();
    reserve( pchunk->len+count );
    for (size_type i=pchunk->len-1; i>=p0; i--)
        pchunk->buf32[i+count] = pchunk->buf32[i];
    _lStr_memcpy(pchunk->buf32 + p0, str.c_str(), count);
    pchunk->len += count;
    pchunk->buf32[pchunk->len] = 0;
    return *this;
}

lString32 lString32::substr(size_type pos, size_type n) const
{
    if (pos>=length())
        return lString32::empty_str;
    if (pos+n>length())
        n = length() - pos;
    return lString32( pchunk->buf32+pos, n );
}

lString32 & lString32::pack()
{
    if (pchunk->len < pchunk->size)
    {
        if (pchunk->nref>1)
        {
            lock(pchunk->len);
        }
        else
        {
            pchunk->buf32 = cr_realloc( pchunk->buf32, pchunk->len+1 );
            pchunk->size = pchunk->len;
        }
    }
    return *this;
}

bool isAlNum(lChar32 ch) {
    lUInt32 props = lGetCharProps(ch);
    return (props & (CH_PROP_ALPHA | CH_PROP_DIGIT)) != 0;
}

/// trims non alpha at beginning and end of string
lString32 & lString32::trimNonAlpha()
{
    int firstns;
    for (firstns = 0; firstns<pchunk->len &&
        !isAlNum(pchunk->buf32[firstns]); ++firstns)
        ;
    if (firstns >= pchunk->len)
    {
        clear();
        return *this;
    }
    int lastns;
    for (lastns = pchunk->len-1; lastns>0 &&
        !isAlNum(pchunk->buf32[lastns]); --lastns)
        ;
    int newlen = lastns-firstns+1;
    if (newlen == pchunk->len)
        return *this;
    if (pchunk->nref == 1)
    {
        if (firstns>0)
            lStr_memcpy( pchunk->buf32, pchunk->buf32+firstns, newlen );
        pchunk->buf32[newlen] = 0;
        pchunk->len = newlen;
    }
    else
    {
        lstring_chunk_t * poldchunk = pchunk;
        release();
        alloc( newlen );
        _lStr_memcpy( pchunk->buf32, poldchunk->buf32+firstns, newlen );
        pchunk->buf32[newlen] = 0;
        pchunk->len = newlen;
    }
    return *this;
}

lString32 & lString32::trim()
{
    //
    int firstns;
    for (firstns = 0; firstns<pchunk->len &&
        (pchunk->buf32[firstns]==' ' || pchunk->buf32[firstns]=='\t'); ++firstns)
        ;
    if (firstns >= pchunk->len)
    {
        clear();
        return *this;
    }
    int lastns;
    for (lastns = pchunk->len-1; lastns>0 &&
        (pchunk->buf32[lastns]==' ' || pchunk->buf32[lastns]=='\t'); --lastns)
        ;
    int newlen = lastns-firstns+1;
    if (newlen == pchunk->len)
        return *this;
    if (pchunk->nref == 1)
    {
        if (firstns>0)
            lStr_memcpy( pchunk->buf32, pchunk->buf32+firstns, newlen );
        pchunk->buf32[newlen] = 0;
        pchunk->len = newlen;
    }
    else
    {
        lstring_chunk_t * poldchunk = pchunk;
        release();
        alloc( newlen );
        _lStr_memcpy( pchunk->buf32, poldchunk->buf32+firstns, newlen );
        pchunk->buf32[newlen] = 0;
        pchunk->len = newlen;
    }
    return *this;
}

int lString32::atoi() const
{
    int n = 0;
    bool res = atoi(n); // cppcheck-suppress nullPointer
    return res ? n : 0;
}

static const char * hex_digits = "0123456789abcdef";
// converts 0..15 to 0..f
char toHexDigit( int c )
{
    return hex_digits[c&0xf];
}

// returns 0..15 if c is hex digit, -1 otherwise
int hexDigit( int c )
{
    if ( c>='0' && c<='9')
        return c-'0';
    if ( c>='a' && c<='f')
        return c-'a'+10;
    if ( c>='A' && c<='F')
        return c-'A'+10;
    return -1;
}

// decode LEN hex digits, return decoded number, -1 if invalid
int decodeHex( const lChar32 * str, int len ) {
    int n = 0;
    for ( int i=0; i<len; i++ ) {
        if ( !str[i] )
            return -1;
        int d = hexDigit(str[i]);
        if ( d==-1 )
            return -1;
        n = (n<<4) | d;
    }
    return n;
}

// decode LEN decimal digits, return decoded number, -1 if invalid
int decodeDecimal( const lChar32 * str, int len ) {
    int n = 0;
    for ( int i=0; i<len; i++ ) {
        if ( !str[i] )
            return -1;
        int d = str[i] - '0';
        if ( d<0 || d>9 )
            return -1;
        n = n*10 + d;
    }
    return n;
}

bool lString32::atoi( int &n ) const
{
	n = 0;
    int sgn = 1;
    const lChar32 * s = c_str();
    while (*s == ' ' || *s == '\t')
        s++;
    if ( s[0]=='0' && s[1]=='x') {
        s+=2;
        for (;*s;) {
            int d = hexDigit(*s++);
            if ( d>=0 )
                n = (n<<4) | d;
        }
        return true;
    }
    if (*s == '-')
    {
        sgn = -1;
        s++;
    }
    else if (*s == '+')
    {
        s++;
    }
    if ( !(*s>='0' && *s<='9') )
        return false;
    while (*s>='0' && *s<='9')
    {
        if (n > INT_MAX/10) {
            return false;
        }
        n = n * 10 + ( (*s++)-'0' );
    }
    if ( sgn<0 )
        n = -n;
    return *s=='\0' || *s==' ' || *s=='\t';
}

bool lString32::atoi( lInt64 &n ) const
{
    n = 0;
    int sgn = 1;
    const lChar32 * s = c_str();
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '-')
    {
        sgn = -1;
        s++;
    }
    else if (*s == '+')
    {
        s++;
    }
    if ( !(*s>='0' && *s<='9') )
        return false;
    while (*s>='0' && *s<='9')
    {
        if (n > __INT64_MAX__/10)
            return false;
        n = n * 10 + ( (*s++)-'0' );
    }
    if ( sgn<0 )
        n = -n;
    return *s=='\0' || *s==' ' || *s=='\t';
}

double lString32::atod() const {
    double d = 0.0;
    bool res = atod(d, '.');
    return res ? d : 0.0;
}

bool lString32::atod( double &d, char dp ) const {
    // Simplified implementation without overflow checking
    int sign = 1;
    unsigned long intg = 0;
    unsigned long frac = 0;
    unsigned long frac_div = 1;
    unsigned int exp = 0;
    int exp_sign = 1;
    bool res = false;
    const value_type * s = c_str();
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '-') {
        sign = -1;
        s++;
    }
    else if (*s == '+') {
        s++;
    }
    if (*s>='0' && *s<='9') {
        res = true;
        while (*s>='0' && *s<='9') {
            intg = intg * 10 + ( (*s)-'0' );
            s++;
        }
    }
    if (res && *s == (value_type)dp) {
        // decimal point found
        s++;
        res = false;
        if (*s>='0' && *s<='9') {
            res = true;
            while (*s>='0' && *s<='9') {
                frac = frac * 10 + ( (*s)-'0' );
                s++;
                frac_div *= 10;
            }
        }
    }
    if (res && (*s == 'e' || *s == 'E')) {
        // exponent part
        s++;
        if (*s == '-') {
            exp_sign = -1;
            s++;
        }
        else if (*s == '+') {
            s++;
        }
        res = false;
        if (*s>='0' && *s<='9') {
            res = true;
            while (*s>='0' && *s<='9') {
                exp = exp * 10 + ( (*s)-'0' );
                s++;
            }
        }
    }
    if (res && (*s != '\0' && *s != ' ' && *s != '\t')) {
        // unprocessed characters left
        res = false;
    }
    d = (double)intg;
    if (frac_div > 1)
        d += ((double)frac)/((double)frac_div);
    if (exp > 1) {
        double pwr = exp_sign > 0 ? 10.0 : 0.1;
        for (unsigned int i = 0; i < exp; i++) {
            d *= pwr;
        }
    }
    if (sign < 0)
        d = -d;
    return res;
}

#define STRING_HASH_MULT 31

lUInt32 lString32::getHash(const lChar32 *begin, const lChar32 *end) {
    lUInt32 res = 0;
    for (; begin < end; ++begin)
        res = res * STRING_HASH_MULT + *begin;
    return res;
}

lUInt32 lString32::getHash() const
{
    lUInt32 res = 0;
    for (lInt32 i=0; i<pchunk->len; i++)
        res = res * STRING_HASH_MULT + pchunk->buf32[i];
    return res;
}



void lString32Collection::reserve(int space)
{
    if ( count + space > size )
    {
        size = count + space + 64;
        chunks = cr_realloc( chunks, size );
    }
}

static int (str32_comparator)(const void * n1, const void * n2)
{
    lstring_chunk_t ** s1 = (lstring_chunk_t **)n1;
    lstring_chunk_t ** s2 = (lstring_chunk_t **)n2;
    return lStr_cmp( (*s1)->data32(), (*s2)->data32() );
}

static int(*custom_lstr32_comparator_ptr)(lString32 & s1, lString32 & s2);
static int (str32_custom_comparator)(const void * n1, const void * n2)
{
    lString32 s1(*((lstring_chunk_t **)n1));
    lString32 s2(*((lstring_chunk_t **)n2));
    return custom_lstr32_comparator_ptr(s1, s2);
}

void lString32Collection::sort(int(comparator)(lString32 & s1, lString32 & s2))
{
    custom_lstr32_comparator_ptr = comparator;
    qsort(chunks,count,sizeof(lstring_chunk_t*), str32_custom_comparator);
}

void lString32Collection::sort()
{
    qsort(chunks,count,sizeof(lstring_chunk_t*), str32_comparator);
}

int lString32Collection::add( const lString32 & str )
{
    reserve( 1 );
    chunks[count] = str.pchunk;
    str.addref();
    return count++;
}
int lString32Collection::insert( int pos, const lString32 & str )
{
    if (pos<0 || pos>=count)
        return add(str);
    reserve( 1 );
    for (int i=count; i>pos; --i)
        chunks[i] = chunks[i-1];
    chunks[pos] = str.pchunk;
    str.addref();
    return count++;
}
void lString32Collection::clear()
{
    if (chunks) {
        for (int i=0; i<count; i++)
        {
            ((lString32 *)chunks)[i].release();
        }
        free(chunks);
        chunks = NULL;
    }
    count = 0;
    size = 0;
}

void lString32Collection::erase(int offset, int cnt)
{
    if (count<=0)
        return;
    if (offset < 0 || offset + cnt > count)
        return;
    int i;
    for (i = offset; i < offset + cnt; i++)
    {
        ((lString32 *)chunks)[i].release();
    }
    for (i = offset + cnt; i < count; i++)
    {
        chunks[i-cnt] = chunks[i];
    }
    count -= cnt;
    if (!count)
        clear();
}

void lString8Collection::split( const lString8 & str, const lString8 & delimiter )
{
    if (str.empty())
        return;
    for (int startpos = 0; startpos < str.length(); ) {
        int pos = str.pos(delimiter, startpos);
        if (pos < 0)
            pos = str.length();
        add(str.substr(startpos, pos - startpos));
        startpos = pos + delimiter.length();
    }
}

void lString32Collection::split( const lString32 & str, const lString32 & delimiter )
{
    if (str.empty())
        return;
    for (int startpos = 0; startpos < str.length(); ) {
        int pos = str.pos(delimiter, startpos);
        if (pos < 0)
            pos = str.length();
        add(str.substr(startpos, pos - startpos));
        startpos = pos + delimiter.length();
    }
}

void lString8Collection::erase(int offset, int cnt)
{
    if (count <= 0)
        return;
    if (offset < 0 || offset + cnt > count)
        return;
    int i;
    for (i = offset; i < offset + cnt; i++)
    {
        ((lString8 *)chunks)[i].release();
    }
    for (i = offset + cnt; i < count; i++)
    {
        chunks[i-cnt] = chunks[i];
    }
    count -= cnt;
    if (!count)
        clear();
}

void lString8Collection::reserve(int space)
{
    if ( count + space > size )
    {
        size = count + space + 64;
        chunks = cr_realloc( chunks, size );
    }
}

int lString8Collection::add( const lString8 & str )
{
    reserve( 1 );
    chunks[count] = str.pchunk;
    str.addref();
    return count++;
}
void lString8Collection::clear()
{
    for (int i=0; i<count; i++)
    {
        ((lString8 *)chunks)[i].release();
    }
    if (chunks)
        free(chunks);
    chunks = NULL;
    count = 0;
    size = 0;
}

lUInt32 calcStringHash( const lChar32 * s )
{
    lUInt32 a = 2166136261u;
    while (*s)
    {
        a = a * 16777619 ^ (*s++);
    }
    return a;
}

static const char * str_hash_magic="STRS";

/// serialize to byte array (pointer will be incremented by number of bytes written)
void lString32HashedCollection::serialize( SerialBuf & buf )
{
    if ( buf.error() )
        return;
    int start = buf.pos();
    buf.putMagic( str_hash_magic );
    lUInt32 count = length();
    buf << count;
    for ( int i=0; i<length(); i++ )
    {
        buf << at(i);
    }
    buf.putCRC( buf.pos() - start );
}

/// calculates CRC32 for buffer contents
lUInt32 lStr_crc32( lUInt32 prevValue, const void * buf, int size )
{
#if (USE_ZLIB==1)
    return crc32( prevValue, (const lUInt8 *)buf, size );
#else
    // TODO:
    return 0;
#endif
}

/// add CRC32 for last N bytes
void SerialBuf::putCRC( int size )
{
    if ( error() )
        return;
    if ( size>_pos ) {
        *this << (lUInt32)0;
        seterror();
    }
    lUInt32 n = 0;
    n = lStr_crc32( n, _buf + _pos-size, size );
    *this << n;
}

/// get CRC32 for the whole buffer
lUInt32 SerialBuf::getCRC()
{
    if (error())
        return 0;
    lUInt32 n = 0;
    n = lStr_crc32( n, _buf, _pos );
    return n;
}

/// read crc32 code, comapare with CRC32 for last N bytes
bool SerialBuf::checkCRC( int size )
{
    if ( error() )
        return false;
    if ( size>_pos ) {
        seterror();
        return false;
    }
    lUInt32 n0 = 0;
    n0 = lStr_crc32(n0, _buf + _pos-size, size);
    lUInt32 n = 0;
    *this >> n;
    if ( error() )
        return false;
    if ( n!=n0 )
        seterror();
    return !error();
}

/// deserialize from byte array (pointer will be incremented by number of bytes read)
bool lString32HashedCollection::deserialize( SerialBuf & buf )
{
    if ( buf.error() )
        return false;
    clear();
    int start = buf.pos();
    buf.putMagic( str_hash_magic );
    lInt32 count = 0;
    buf >> count;
    for ( int i=0; i<count; i++ ) {
        lString32 s;
        buf >> s;
        if ( buf.error() )
            break;
        add( s.c_str() );
    }
    buf.checkCRC( buf.pos() - start );
    return !buf.error();
}

lString32HashedCollection::lString32HashedCollection( lString32HashedCollection & v )
: lString32Collection( v )
, hashSize( v.hashSize )
, hash( NULL )
{
    hash = (HashPair *)malloc( sizeof(HashPair) * hashSize );
    for ( int i=0; i<hashSize; i++ ) {
        hash[i].clear();
        hash[i].index = v.hash[i].index;
        HashPair * next = v.hash[i].next;
        while ( next ) {
            addHashItem( i, next->index );
            next = next->next;
        }
    }
}

void lString32HashedCollection::addHashItem( int hashIndex, int storageIndex )
{
    if ( hash[ hashIndex ].index == -1 ) {
        hash[hashIndex].index = storageIndex;
    } else {
        HashPair * np = (HashPair *)malloc(sizeof(HashPair));
        np->index = storageIndex;
        np->next = hash[hashIndex].next;
        hash[hashIndex].next = np;
    }
}

void lString32HashedCollection::clearHash()
{
    if ( hash ) {
        for ( int i=0; i<hashSize; i++) {
            HashPair * p = hash[i].next;
            while ( p ) {
                HashPair * tmp = p->next;
                free( p );
                p = tmp;
            }
        }
        free( hash );
    }
    hash = NULL;
}

lString32HashedCollection::lString32HashedCollection( lUInt32 hash_size )
: hashSize(hash_size), hash(NULL)
{

    hash = (HashPair *)malloc( sizeof(HashPair) * hashSize );
    for ( int i=0; i<hashSize; i++ )
        hash[i].clear();
}

lString32HashedCollection::~lString32HashedCollection()
{
    clearHash();
}

int lString32HashedCollection::find( const lChar32 * s )
{
    if ( !hash || !length() )
        return -1;
    lUInt32 h = calcStringHash( s );
    lUInt32 n = h % hashSize;
    if ( hash[n].index!=-1 )
    {
        const lString32 & str = at( hash[n].index );
        if ( str == s )
            return hash[n].index;
        HashPair * p = hash[n].next;
        for ( ;p ;p = p->next ) {
            const lString32 & str = at( p->index );
            if ( str==s )
                return p->index;
        }
    }
    return -1;
}

void lString32HashedCollection::reHash( int newSize )
{
    if (hashSize == newSize)
        return;
    clearHash();
    hashSize = newSize;
    if (hashSize > 0) {
        hash = (HashPair *)malloc( sizeof(HashPair) * hashSize );
        for ( int i=0; i<hashSize; i++ )
            hash[i].clear();
    }
    for ( int i=0; i<length(); i++ ) {
        lUInt32 h = calcStringHash( at(i).c_str() );
        lUInt32 n = h % hashSize;
        addHashItem( n, i );
    }
}

int lString32HashedCollection::add( const lChar32 * s )
{
    if ( !hash || hashSize < length()*2 ) {
        int sz = 16;
        while ( sz<length() )
            sz <<= 1;
        sz <<= 1;
        reHash( sz );
    }
    lUInt32 h = calcStringHash( s );
    lUInt32 n = h % hashSize;
    if ( hash[n].index!=-1 )
    {
        const lString32 & str = at( hash[n].index );
        if ( str == s )
            return hash[n].index;
        HashPair * p = hash[n].next;
        for ( ;p ;p = p->next ) {
            const lString32 & str = at( p->index );
            if ( str==s )
                return p->index;
        }
    }
    lUInt32 i = lString32Collection::add( lString32(s) );
    addHashItem( n, i );
    return i;
}

const lString32 lString32::empty_str;


////////////////////////////////////////////////////////////////////////////
// lString8
////////////////////////////////////////////////////////////////////////////

void lString8::free()
{
    if ( pchunk==EMPTY_STR_8 )
        return;
    ::free(pchunk->buf8);
#if (LDOM_USE_OWN_MEM_MAN == 1)
    for (int i=slices_count-1; i>=0; --i)
    {
        if (slices[i]->free_chunk(pchunk))
            return;
    }
    crFatalError(); // wrong pointer!!!
#else
    ::free(pchunk);
#endif
}

void lString8::alloc(int sz)
{
#if (LDOM_USE_OWN_MEM_MAN == 1)
    pchunk = lstring_chunk_t::alloc();
#else
    pchunk = (lstring_chunk_t*)::malloc(sizeof(lstring_chunk_t));
#endif
    pchunk->buf8 = (lChar8*) ::malloc( sizeof(lChar8) * (sz+1) );
    assert( pchunk->buf8!=NULL );
    pchunk->size = sz;
    pchunk->nref = 1;
}

lString8::lString8(const lChar8 * str)
{
    if (!str || !(*str))
    {
        pchunk = EMPTY_STR_8;
        addref();
        return;
    }
    size_type len = _lStr_len(str);
    alloc( len );
    pchunk->len = len;
    _lStr_cpy( pchunk->buf8, str );
}

lString8::lString8(const lChar32 * str)
{
    if (!str || !(*str))
    {
        pchunk = EMPTY_STR_8;
        addref();
        return;
    }
    size_type len = _lStr_len(str);
    alloc( len );
    pchunk->len = len;
    _lStr_cpy( pchunk->buf8, str );
}

lString8::lString8(const value_type * str, size_type count)
{
    if ( !str || !(*str) || count<=0 )
    {
        pchunk = EMPTY_STR_8; addref();
    }
    else
    {
        size_type len = _lStr_nlen(str, count);
        alloc(len);
        _lStr_ncpy( pchunk->buf8, str, len );
        pchunk->len = len;
    }
}

lString8::lString8(const lString8 & str, size_type offset, size_type count)
{
    if ( count > str.length() - offset )
        count = str.length() - offset;
    if (count<=0)
    {
        pchunk = EMPTY_STR_8; addref();
    }
    else
    {
        alloc(count);
        _lStr_memcpy( pchunk->buf8, str.pchunk->buf8+offset, count );
        pchunk->buf8[count]=0;
        pchunk->len = count;
    }
}

lString8 & lString8::assign(const lChar8 * str)
{
    if (!str || !(*str))
    {
        clear();
    }
    else
    {
        size_type len = _lStr_len(str);
        if (pchunk->nref==1)
        {
            if (pchunk->size < len)
            {
                // resize is necessary
                pchunk->buf8 = (lChar8*) ::realloc( pchunk->buf8, sizeof(lChar8)*(len+1) );
                pchunk->size = len;
            }
        }
        else
        {
            release();
            alloc(len);
        }
        _lStr_cpy( pchunk->buf8, str );
        pchunk->len = len;
    }
    return *this;
}

lString8 & lString8::assign(const lChar8 * str, size_type count)
{
    if ( !str || !(*str) || count<=0 )
    {
        clear();
    }
    else
    {
        size_type len = _lStr_nlen(str, count);
        if (pchunk->nref==1)
        {
            if (pchunk->size < len)
            {
                // resize is necessary
                pchunk->buf8 = (lChar8*) ::realloc( pchunk->buf8, sizeof(lChar8)*(len+1) );
                pchunk->size = len;
            }
        }
        else
        {
            release();
            alloc(len);
        }
        _lStr_ncpy( pchunk->buf8, str, count );
        pchunk->len = len;
    }
    return *this;
}

lString8 & lString8::assign(const lString8 & str, size_type offset, size_type count)
{
    if ( count > str.length() - offset )
        count = str.length() - offset;
    if (count<=0)
    {
        clear();
    }
    else
    {
        if (pchunk==str.pchunk)
        {
            if (&str != this)
            {
                release();
                alloc(count);
            }
            if (offset>0)
            {
                _lStr_memcpy( pchunk->buf8, str.pchunk->buf8+offset, count );
            }
            pchunk->buf8[count]=0;
        }
        else
        {
            if (pchunk->nref==1)
            {
                if (pchunk->size < count)
                {
                    // resize is necessary
                    pchunk->buf8 = (lChar8*) ::realloc( pchunk->buf8, sizeof(lChar8)*(count+1) );
                    pchunk->size = count;
                }
            }
            else
            {
                release();
                alloc(count);
            }
            _lStr_memcpy( pchunk->buf8, str.pchunk->buf8+offset, count );
            pchunk->buf8[count]=0;
        }
        pchunk->len = count;
    }
    return *this;
}

lString8 & lString8::erase(size_type offset, size_type count)
{
    if ( count > length() - offset )
        count = length() - offset;
    if (count<=0)
    {
        clear();
    }
    else
    {
        size_type newlen = length()-count;
        if (pchunk->nref==1)
        {
            _lStr_memcpy( pchunk->buf8+offset, pchunk->buf8+offset+count, newlen-offset+1 );
        }
        else
        {
            lstring_chunk_t * poldchunk = pchunk;
            release();
            alloc( newlen );
            _lStr_memcpy( pchunk->buf8, poldchunk->buf8, offset );
            _lStr_memcpy( pchunk->buf8+offset, poldchunk->buf8+offset+count, newlen-offset+1 );
        }
        pchunk->len = newlen;
        pchunk->buf8[newlen]=0;
    }
    return *this;
}

void lString8::reserve(size_type n)
{
    if (pchunk->nref==1)
    {
        if (pchunk->size < n)
        {
            pchunk->buf8 = (lChar8*) ::realloc( pchunk->buf8, sizeof(lChar8)*(n+1) );
            pchunk->size = n;
        }
    }
    else
    {
        lstring_chunk_t * poldchunk = pchunk;
        release();
        alloc( n );
        _lStr_memcpy( pchunk->buf8, poldchunk->buf8, poldchunk->len+1 );
        pchunk->len = poldchunk->len;
    }
}

void lString8::lock( size_type newsize )
{
    if (pchunk->nref>1)
    {
        lstring_chunk_t * poldchunk = pchunk;
        release();
        alloc( newsize );
        size_type len = newsize;
        if (len>poldchunk->len)
            len = poldchunk->len;
        _lStr_memcpy( pchunk->buf8, poldchunk->buf8, len );
        pchunk->buf8[len]=0;
        pchunk->len = len;
    }
}

// lock string, allocate buffer and reset length to 0
void lString8::reset( size_type size )
{
    if (pchunk->nref>1 || pchunk->size<size)
    {
        release();
        alloc( size );
    }
    pchunk->buf8[0] = 0;
    pchunk->len = 0;
}

void lString8::resize(size_type n, lChar8 e)
{
    lock( n );
    if (pchunk->size < n)
    {
        pchunk->buf8 = (lChar8*) ::realloc( pchunk->buf8, sizeof(lChar8)*(n+1) );
        pchunk->size = n;
    }
    // fill with data if expanded
    for (size_type i=pchunk->len; i<n; i++)
        pchunk->buf8[i] = e;
    pchunk->buf8[pchunk->len] = 0;
}

lString8 & lString8::append(const lChar8 * str)
{
    size_type len = _lStr_len(str);
    reserve( pchunk->len+len );
    _lStr_memcpy(pchunk->buf8+pchunk->len, str, len+1);
    pchunk->len += len;
    return *this;
}

lString8 & lString8::appendDecimal(lInt64 n)
{
    lChar8 buf[24];
    int i=0;
    int negative = 0;
    if (n==0)
        return append(1, '0');
    else if (n<0)
    {
        negative = 1;
        n = -n;
    }
    for ( ; n; n/=10 )
    {
        buf[i++] = '0' + (n % 10);
    }
    reserve(length() + i + negative);
    if (negative)
        append(1, '-');
    for (int j=i-1; j>=0; j--)
        append(1, buf[j]);
    return *this;
}

lString8 & lString8::appendHex(lUInt64 n)
{
    if (n == 0)
        return append(1, '0');
    reserve(length() + 16);
    bool foundNz = false;
    for (int i=0; i<16; i++) {
        int digit = (n >> 60) & 0x0F;
        if (digit)
            foundNz = true;
        if (foundNz)
            append(1, (lChar8)toHexDigit(digit));
        n <<= 4;
    }
    return *this;
}

lString32 & lString32::appendDecimal(lInt64 n)
{
    lChar32 buf[24];
    int i=0;
    int negative = 0;
    if (n==0)
        return append(1, '0');
    else if (n<0)
    {
        negative = 1;
        n = -n;
    }
    for ( ; n; n/=10 )
    {
        buf[i++] = '0' + (n % 10);
    }
    reserve(length() + i + negative);
    if (negative)
        append(1, '-');
    for (int j=i-1; j>=0; j--)
        append(1, buf[j]);
    return *this;
}

lString32 & lString32::appendHex(lUInt64 n)
{
    if (n == 0)
        return append(1, '0');
    reserve(length() + 16);
    bool foundNz = false;
    for (int i=0; i<16; i++) {
        int digit = (n >> 60) & 0x0F;
        if (digit)
            foundNz = true;
        if (foundNz)
            append(1, toHexDigit(digit));
        n <<= 4;
    }
    return *this;
}

lString8 & lString8::append(const lChar8 * str, size_type count)
{
    size_type len = _lStr_nlen(str, count);
    reserve( pchunk->len+len );
    _lStr_ncpy(pchunk->buf8+pchunk->len, str, len);
    pchunk->len += len;
    return *this;
}

lString8 & lString8::append(const lString8 & str)
{
    size_type len2 = pchunk->len + str.pchunk->len;
    reserve( len2 );
    _lStr_memcpy( pchunk->buf8+pchunk->len, str.pchunk->buf8, str.pchunk->len+1 );
    pchunk->len = len2;
    return *this;
}

lString8 & lString8::append(const lString8 & str, size_type offset, size_type count)
{
    if ( str.pchunk->len>offset )
    {
        if ( offset + count > str.pchunk->len )
            count = str.pchunk->len - offset;
        reserve( pchunk->len+count );
        _lStr_ncpy(pchunk->buf8 + pchunk->len, str.pchunk->buf8 + offset, count);
        pchunk->len += count;
        pchunk->buf8[pchunk->len] = 0;
    }
    return *this;
}

lString8 & lString8::append(size_type count, lChar8 ch)
{
    reserve( pchunk->len+count );
    memset( pchunk->buf8+pchunk->len, ch, count );
    //_lStr_memset(pchunk->buf8+pchunk->len, ch, count);
    pchunk->len += count;
    pchunk->buf8[pchunk->len] = 0;
    return *this;
}

lString8 & lString8::insert(size_type p0, size_type count, lChar8 ch)
{
    if (p0>pchunk->len)
        p0 = pchunk->len;
    reserve( pchunk->len+count );
    for (size_type i=pchunk->len-1; i>=p0; i--)
        pchunk->buf8[i+count] = pchunk->buf8[i];
    //_lStr_memset(pchunk->buf8+p0, ch, count);
    memset(pchunk->buf8+p0, ch, count);
    pchunk->len += count;
    pchunk->buf8[pchunk->len] = 0;
    return *this;
}

lString8 lString8::substr(size_type pos, size_type n) const
{
    if (pos>=length())
        return lString8::empty_str;
    if (pos+n>length())
        n = length() - pos;
    return lString8( pchunk->buf8+pos, n );
}

int lString8::pos(lChar8 ch) const
{
    for (int i = 0; i < length(); i++)
    {
        if (pchunk->buf8[i] == ch)
        {
            return i;
        }
    }
    return -1;
}

int lString8::pos(lChar8 ch, int start) const
{
    if (length() - start < 1)
        return -1;
    for (int i = start; i < length(); i++)
    {
        if (pchunk->buf8[i] == ch)
        {
            return i;
        }
    }
    return -1;
}

int lString8::pos(const lString8 & subStr) const
{
    if (subStr.length()>length())
        return -1;
    int l = subStr.length();
    int dl = length() - l;
    for (int i=0; i<=dl; i++)
    {
        int flg = 1;
        for (int j=0; j<l; j++)
            if (pchunk->buf8[i+j]!=subStr.pchunk->buf8[j])
            {
                flg = 0;
                break;
            }
        if (flg)
            return i;
    }
    return -1;
}

/// find position of substring inside string starting from right, -1 if not found
int lString8::rpos(const char * subStr) const
{
    if (!subStr || !subStr[0])
        return -1;
    int l = lStr_len(subStr);
    if (l > length())
        return -1;
    int dl = length() - l;
    for (int i=dl; i>=0; i--)
    {
        int flg = 1;
        for (int j=0; j<l; j++)
            if (pchunk->buf8[i+j] != subStr[j])
            {
                flg = 0;
                break;
            }
        if (flg)
            return i;
    }
    return -1;
}

/// find position of substring inside string, -1 if not found
int lString8::pos(const char * subStr) const
{
    if (!subStr || !subStr[0])
        return -1;
    int l = lStr_len(subStr);
    if (l > length())
        return -1;
    int dl = length() - l;
    for (int i=0; i<=dl; i++)
    {
        int flg = 1;
        for (int j=0; j<l; j++)
            if (pchunk->buf8[i+j] != subStr[j])
            {
                flg = 0;
                break;
            }
        if (flg)
            return i;
    }
    return -1;
}

int lString8::pos(const lString8 & subStr, int startPos) const
{
    if (subStr.length() > length() - startPos)
        return -1;
    int l = subStr.length();
    int dl = length() - l;
    for (int i = startPos; i <= dl; i++) {
        int flg = 1;
        for (int j=0; j<l; j++)
            if (pchunk->buf8[i+j]!=subStr.pchunk->buf8[j])
            {
                flg = 0;
                break;
            }
        if (flg)
            return i;
    }
    return -1;
}

int lString32::pos(lChar32 ch) const {
    for (int i = 0; i < length(); i++)
    {
        if (pchunk->buf32[i] == ch)
        {
            return i;
        }
    }
    return -1;
}

int lString32::pos(lChar32 ch, int start) const
{
    if (length() - start < 1)
        return -1;
    for (int i = start; i < length(); i++)
    {
        if (pchunk->buf32[i] == ch)
        {
            return i;
        }
    }
    return -1;
}

int lString32::pos(const lString32 & subStr, int startPos) const
{
    if (subStr.length() > length() - startPos)
        return -1;
    int l = subStr.length();
    int dl = length() - l;
    for (int i = startPos; i <= dl; i++) {
        int flg = 1;
        for (int j=0; j<l; j++)
            if (pchunk->buf32[i+j]!=subStr.pchunk->buf32[j])
            {
                flg = 0;
                break;
            }
        if (flg)
            return i;
    }
    return -1;
}

/// find position of substring inside string, -1 if not found
int lString8::pos(const char * subStr, int startPos) const
{
    if (!subStr || !subStr[0])
        return -1;
    int l = lStr_len(subStr);
    if (l > length() - startPos)
        return -1;
    int dl = length() - l;
    for (int i = startPos; i <= dl; i++) {
        int flg = 1;
        for (int j=0; j<l; j++)
            if (pchunk->buf8[i+j] != subStr[j])
            {
                flg = 0;
                break;
            }
        if (flg)
            return i;
    }
    return -1;
}

/// find position of substring inside string, -1 if not found
int lString32::pos(const lChar32 * subStr, int startPos) const
{
    if (!subStr || !subStr[0])
        return -1;
    int l = lStr_len(subStr);
    if (l > length() - startPos)
        return -1;
    int dl = length() - l;
    for (int i = startPos; i <= dl; i++) {
        int flg = 1;
        for (int j=0; j<l; j++)
            if (pchunk->buf32[i+j] != subStr[j])
            {
                flg = 0;
                break;
            }
        if (flg)
            return i;
    }
    return -1;
}

/// find position of substring inside string, right to left, return -1 if not found
int lString32::rpos(lString32 subStr) const
{
    if (subStr.length()>length())
        return -1;
    int l = subStr.length();
    int dl = length() - l;
    for (int i=dl; i>=0; i++)
    {
        int flg = 1;
        for (int j=0; j<l; j++)
            if (pchunk->buf32[i+j]!=subStr.pchunk->buf32[j])
            {
                flg = 0;
                break;
            }
        if (flg)
            return i;
    }
    return -1;
}

/// find position of substring inside string, -1 if not found
int lString32::pos(const lChar32 * subStr) const
{
    if (!subStr)
        return -1;
    int l = lStr_len(subStr);
    if (l > length())
        return -1;
    int dl = length() - l;
    for (int i=0; i <= dl; i++)
    {
        int flg = 1;
        for (int j=0; j<l; j++)
            if (pchunk->buf32[i+j] != subStr[j])
            {
                flg = 0;
                break;
            }
        if (flg)
            return i;
    }
    return -1;
}

/// find position of substring inside string, -1 if not found
int lString32::pos(const lChar8 * subStr) const
{
    if (!subStr)
        return -1;
    int l = lStr_len(subStr);
    if (l > length())
        return -1;
    int dl = length() - l;
    for (int i=0; i <= dl; i++)
    {
        int flg = 1;
        for (int j=0; j<l; j++)
            if (pchunk->buf32[i+j] != (value_type)subStr[j])
            {
                flg = 0;
                break;
            }
        if (flg)
            return i;
    }
    return -1;
}

/// find position of substring inside string, -1 if not found
int lString32::pos(const lChar8 * subStr, int start) const
{
    if (!subStr)
        return -1;
    int l = lStr_len(subStr);
    if (l > length() - start)
        return -1;
    int dl = length() - l;
    for (int i = start; i <= dl; i++)
    {
        int flg = 1;
        for (int j=0; j<l; j++)
            if (pchunk->buf32[i+j] != (value_type)subStr[j])
            {
                flg = 0;
                break;
            }
        if (flg)
            return i;
    }
    return -1;
}

int lString32::pos(lString32 subStr) const
{
    if (subStr.length()>length())
        return -1;
    int l = subStr.length();
    int dl = length() - l;
    for (int i=0; i<=dl; i++)
    {
        int flg = 1;
        for (int j=0; j<l; j++)
            if (pchunk->buf32[i+j]!=subStr.pchunk->buf32[j])
            {
                flg = 0;
                break;
            }
        if (flg)
            return i;
    }
    return -1;
}

lString8 & lString8::pack()
{
    if (pchunk->len < pchunk->size)
    {
        if (pchunk->nref>1)
        {
            lock(pchunk->len);
        }
        else
        {
            pchunk->buf8 = cr_realloc( pchunk->buf8, pchunk->len+1 );
            pchunk->size = pchunk->len;
        }
    }
    return *this;
}

lString8 & lString8::trim()
{
    //
    int firstns;
    for (firstns = 0;
            firstns < pchunk->len &&
            (pchunk->buf8[firstns] == ' ' ||
            pchunk->buf8[firstns] == '\t');
            ++firstns)
        ;
    if (firstns >= pchunk->len)
    {
        clear();
        return *this;
    }
    size_t lastns;
    for (lastns = pchunk->len-1;
            lastns>0 &&
            (pchunk->buf8[lastns]==' ' || pchunk->buf8[lastns]=='\t');
            --lastns)
        ;
    int newlen = (int)(lastns - firstns + 1);
    if (newlen == pchunk->len)
        return *this;
    if (pchunk->nref == 1)
    {
        if (firstns>0)
            lStr_memcpy( pchunk->buf8, pchunk->buf8+firstns, newlen );
        pchunk->buf8[newlen] = 0;
        pchunk->len = newlen;
    }
    else
    {
        lstring_chunk_t * poldchunk = pchunk;
        release();
        alloc( newlen );
        _lStr_memcpy( pchunk->buf8, poldchunk->buf8+firstns, newlen );
        pchunk->buf8[newlen] = 0;
        pchunk->len = newlen;
    }
    return *this;
}

int lString8::atoi() const
{
    int sgn = 1;
    int n = 0;
    const lChar8 * s = c_str();
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '-')
    {
        sgn = -1;
        s++;
    }
    else if (*s == '+')
    {
        s++;
    }
    while (*s>='0' && *s<='9')
    {
        n = n * 10 + ( (*s)-'0' );
        s++;
    }
    return (sgn>0)?n:-n;
}

lInt64 lString8::atoi64() const
{
    int sgn = 1;
    lInt64 n = 0;
    const lChar8 * s = c_str();
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '-')
    {
        sgn = -1;
        s++;
    }
    else if (*s == '+')
    {
        s++;
    }
    while (*s>='0' && *s<='9')
    {
        n = n * 10 + ( (*s)-'0' );
        s++;
    }
    return (sgn>0)?n:-n;
}

// constructs string representation of integer
lString8 lString8::itoa( int n )
{
    lChar8 buf[16];
    int i=0;
    int negative = 0;
    if (n==0)
        return cs8("0");
    else if (n<0)
    {
        negative = 1;
        n = -n;
    }
    for ( ; n; n/=10 )
    {
        buf[i++] = '0' + (n%10);
    }
    lString8 res;
    res.reserve(i+negative);
    if (negative)
        res.append(1, '-');
    for (int j=i-1; j>=0; j--)
        res.append(1, buf[j]);
    return res;
}

// constructs string representation of integer
lString8 lString8::itoa( unsigned int n )
{
    lChar8 buf[16];
    int i=0;
    if (n==0)
        return cs8("0");
    for ( ; n; n/=10 )
    {
        buf[i++] = '0' + (n%10);
    }
    lString8 res;
    res.reserve(i);
    for (int j=i-1; j>=0; j--)
        res.append(1, buf[j]);
    return res;
}

// constructs string representation of integer
lString8 lString8::itoa( lInt64 n )
{
    lChar8 buf[32];
    int i=0;
    int negative = 0;
    if (n==0)
        return cs8("0");
    else if (n<0)
    {
        negative = 1;
        n = -n;
    }
    for ( ; n; n/=10 )
    {
        buf[i++] = '0' + (n%10);
    }
    lString8 res;
    res.reserve(i+negative);
    if (negative)
        res.append(1, '-');
    for (int j=i-1; j>=0; j--)
        res.append(1, buf[j]);
    return res;
}

// constructs string representation of integer
lString32 lString32::itoa( int n )
{
    return itoa( (lInt64)n );
}

// constructs string representation of integer
lString32 lString32::itoa( lInt64 n )
{
    lChar32 buf[32];
    int i=0;
    int negative = 0;
    if (n==0)
        return cs32("0");
    else if (n<0)
    {
        negative = 1;
        n = -n;
    }
    for ( ; n && i<30; n/=10 )
    {
        buf[i++] = (lChar32)('0' + (n%10));
    }
    lString32 res;
    res.reserve(i+negative);
    if (negative)
        res.append(1, U'-');
    for (int j=i-1; j>=0; j--)
        res.append(1, buf[j]);
    return res;
}

bool lvUnicodeIsAlpha( lChar32 ch )
{
    if ( ch<128 ) {
        if ( (ch>='a' && ch<='z') || (ch>='A' && ch<='Z') )
            return true;
    } else if ( ch>=0xC0 && ch<=0x1ef9) {
        return true;
    }
    return false;
}

lString8 & lString8::uppercase()
{
    lStr_uppercase( modify(), length() );
    return *this;
}

lString8 & lString8::lowercase()
{
    lStr_lowercase( modify(), length() );
    return *this;
}

lString32 & lString32::uppercase()
{
    lStr_uppercase( modify(), length() );
    return *this;
}

lString32 & lString32::lowercase()
{
    lStr_lowercase( modify(), length() );
    return *this;
}

lString32 & lString32::capitalize()
{
    lStr_capitalize( modify(), length() );
    return *this;
}

lString32 & lString32::fullWidthChars()
{
    lStr_fullWidthChars( modify(), length() );
    return *this;
}

void lStr_uppercase( lChar8 * str, int len )
{
    for ( int i=0; i<len; i++ ) {
        lChar32 ch = str[i];
        if ( ch>='a' && ch<='z' ) {
            str[i] = ch - 0x20;
        } else if ( ch>=0xE0 && ch<=0xFF ) {
            str[i] = ch - 0x20;
        }
    }
}

void lStr_lowercase( lChar8 * str, int len )
{
    for ( int i=0; i<len; i++ ) {
        lChar32 ch = str[i];
        if ( ch>='A' && ch<='Z' ) {
            str[i] = ch + 0x20;
        } else if ( ch>=0xC0 && ch<=0xDF ) {
            str[i] = ch + 0x20;
        }
    }
}

void lStr_uppercase( lChar32 * str, int len )
{
    for ( int i=0; i<len; i++ ) {
        lChar32 ch = str[i];
#if (USE_UTF8PROC==1)
        str[i] = utf8proc_toupper(ch);
#else
        if ( ch>='a' && ch<='z' ) {
            str[i] = ch - 0x20;
        } else if ( ch>=0xE0 && ch<=0xFF ) {
            str[i] = ch - 0x20;
        } else if ( ch>=0x430 && ch<=0x44F ) {
            str[i] = ch - 0x20;
        } else if ( ch>=0x3b0 && ch<=0x3cF ) {
            str[i] = ch - 0x20;
        } else if ( (ch >> 8)==0x1F ) { // greek
            lChar32 n = ch & 255;
            if (n<0x70) {
                str[i] = ch | 8;
            } else if (n<0x80) {

            } else if (n<0xF0) {
                str[i] = ch | 8;
            }
        }
#endif
    }
}

void lStr_lowercase( lChar32 * str, int len )
{
    for ( int i=0; i<len; i++ ) {
        lChar32 ch = str[i];
#if (USE_UTF8PROC==1)
        str[i] = utf8proc_tolower(ch);
#else
        if ( ch>='A' && ch<='Z' ) {
            str[i] = ch + 0x20;
        } else if ( ch>=0xC0 && ch<=0xDF ) {
            str[i] = ch + 0x20;
        } else if ( ch>=0x410 && ch<=0x42F ) {
            str[i] = ch + 0x20;
        } else if ( ch>=0x390 && ch<=0x3aF ) {
            str[i] = ch + 0x20;
        } else if ( (ch >> 8)==0x1F ) { // greek
            lChar32 n = ch & 255;
            if (n<0x70) {
                str[i] = ch & (~8);
            } else if (n<0x80) {

            } else if (n<0xF0) {
                str[i] = ch & (~8);
            }
        }
#endif
    }
}

void lStr_fullWidthChars( lChar32 * str, int len )
{
    for ( int i=0; i<len; i++ ) {
        lChar32 ch = str[i];
        if ( ch>=0x21 && ch<=0x7E ) {
            // full-width versions of ascii chars 0x21-0x7E are at 0xFF01-0Xff5E
            str[i] = ch + UNICODE_ASCII_FULL_WIDTH_OFFSET;
        } else if ( ch==0x20 ) {
            str[i] = UNICODE_CJK_IDEOGRAPHIC_SPACE; // full-width space
        }
    }
}

void lStr_capitalize( lChar32 * str, int len )
{
    bool prev_is_word_sep = true; // first char of string will be capitalized
    for ( int i=0; i<len; i++ ) {
        lChar32 ch = str[i];
        if (prev_is_word_sep) {
            // as done as in lStr_uppercase()
#if (USE_UTF8PROC==1)
            str[i] = utf8proc_toupper(ch);
#else
            if ( ch>='a' && ch<='z' ) {
                str[i] = ch - 0x20;
            } else if ( ch>=0xE0 && ch<=0xFF ) {
                str[i] = ch - 0x20;
            } else if ( ch>=0x430 && ch<=0x44F ) {
                str[i] = ch - 0x20;
            } else if ( ch>=0x3b0 && ch<=0x3cF ) {
                str[i] = ch - 0x20;
            } else if ( (ch >> 8)==0x1F ) { // greek
                lChar32 n = ch & 255;
                if (n<0x70) {
                    str[i] = ch | 8;
                } else if (n<0x80) {

                } else if (n<0xF0) {
                    str[i] = ch | 8;
                }
            }
#endif
        }
        // update prev_is_word_sep for next char
        prev_is_word_sep = lStr_isWordSeparator(ch);
    }
}

void lString32Collection::parse( lString32 string, lChar32 delimiter, bool flgTrim )
{
    int wstart=0;
    for ( int i=0; i<=string.length(); i++ ) {
        if ( i==string.length() || string[i]==delimiter ) {
            lString32 s( string.substr( wstart, i-wstart) );
            if ( flgTrim )
                s.trimDoubleSpaces(false, false, false);
            if ( !flgTrim || !s.empty() )
                add( s );
            wstart = i+1;
        }
    }
}

void lString32Collection::parse( lString32 string, lString32 delimiter, bool flgTrim )
{
    if ( delimiter.empty() || string.pos(delimiter)<0 ) {
        lString32 s( string );
        if ( flgTrim )
            s.trimDoubleSpaces(false, false, false);
        add(s);
        return;
    }
    int wstart=0;
    for ( int i=0; i<=string.length(); i++ ) {
        bool matched = true;
        for ( int j=0; j<delimiter.length() && i+j<string.length(); j++ ) {
            if ( string[i+j]!=delimiter[j] ) {
                matched = false;
                break;
            }
        }
        if ( matched ) {
            lString32 s( string.substr( wstart, i-wstart) );
            if ( flgTrim )
                s.trimDoubleSpaces(false, false, false);
            if ( !flgTrim || !s.empty() )
                add( s );
            wstart = i+delimiter.length();
            i+= delimiter.length()-1;
        }
    }
}

int TrimDoubleSpaces(lChar32 * buf, int len,  bool allowStartSpace, bool allowEndSpace, bool removeEolHyphens)
{
    lChar32 * psrc = buf;
    lChar32 * pdst = buf;
    int state = 0; // 0=beginning, 1=after space, 2=after non-space
    while ((len--) > 0) {
        lChar32 ch = *psrc++;
        if (ch == ' ' || ch == '\t') {
            if ( state==2 ) {
                if ( *psrc || allowEndSpace ) // if not last
                    *pdst++ = ' ';
            } else if ( state==0 && allowStartSpace ) {
                *pdst++ = ' ';
            }
            state = 1;
        } else if ( ch=='\r' || ch=='\n' ) {
            if ( state==2 ) {
                if ( removeEolHyphens && pdst>(buf+1) && *(pdst-1)=='-' && lvUnicodeIsAlpha(*(pdst-2)) )
                    pdst--; // remove hyphen at end of line
                if ( *psrc || allowEndSpace ) // if not last
                    *pdst++ = ' ';
            } else if ( state==0 && allowStartSpace ) {
                *pdst++ = ' ';
            }
            state = 1;
        } else {
            *pdst++ = ch;
            state = 2;
        }
    }
    return (int)(pdst - buf);
}

lString32 & lString32::trimDoubleSpaces( bool allowStartSpace, bool allowEndSpace, bool removeEolHyphens )
{
    if ( empty() )
        return *this;
    lChar32 * buf = modify();
    int len = length();
    int nlen = TrimDoubleSpaces(buf, len,  allowStartSpace, allowEndSpace, removeEolHyphens);
    if (nlen < len)
        limit(nlen);
    return *this;
}

// constructs string representation of integer
lString32 lString32::itoa( unsigned int n )
{
    return itoa( (lUInt64) n );
}

// constructs string representation of integer
lString32 lString32::itoa( lUInt64 n )
{
    lChar32 buf[24];
    int i=0;
    if (n==0)
        return cs32("0");
    for ( ; n; n/=10 )
    {
        buf[i++] = (lChar32)('0' + (n%10));
    }
    lString32 res;
    res.reserve(i);
    for (int j=i-1; j>=0; j--)
        res.append(1, buf[j]);
    return res;
}


lUInt32 lString8::getHash() const
{
    lUInt32 res = 0;
    for (int i=0; i < pchunk->len; i++)
        res = res * STRING_HASH_MULT + pchunk->buf8[i];
    return res;
}

const lString8 lString8::empty_str;

int Utf8CharCount( const lChar8 * str )
{
    int count = 0;
    lUInt8 ch;
    while ( (ch=*str++) ) {
        if ( (ch & 0x80) == 0 ) {
        } else if ( (ch & 0xE0) == 0xC0 ) {
            if ( !(*str++) )
                break;
        } else if ( (ch & 0xF0) == 0xE0 ) {
            if ( !(*str++) )
                break;
            if ( !(*str++) )
                break;
        } else if ( (ch & 0xF8) == 0xF0 ) {
            if ( !(*str++) )
                break;
            if ( !(*str++) )
                break;
            if ( !(*str++) )
                break;
        } else {
            // In Unicode standard maximum length of UTF-8 sequence is 4 byte!
            // invalid first byte in UTF-8 sequence, just leave as is
            ;
        }
        count++;
    }
    return count;
}

int Utf8CharCount( const lChar8 * str, int len )
{
    if (len == 0)
        return 0;
    int count = 0;
    lUInt8 ch;
    const lChar8 * endp = str + len;
    while ((ch=*str++)) {
        if ( (ch & 0x80) == 0 ) {
        } else if ( (ch & 0xE0) == 0xC0 ) {
            str++;
        } else if ( (ch & 0xF0) == 0xE0 ) {
            str+=2;
        } else if ( (ch & 0xF8) == 0xF0 ) {
            str+=3;
        } else {
            // invalid first byte of UTF-8 sequence, just leave as is
            ;
        }
        if (str > endp)
            break;
        count++;
    }
    return count;
}

inline int charUtf8ByteCount(int ch) {
    if (!(ch & ~0x7F))
        return 1;
    if (!(ch & ~0x7FF))
        return 2;
    if (!(ch & ~0xFFFF))
        return 3;
    if (!(ch & ~0x1FFFFF))
        return 4;
    // In Unicode Standard codepoint must be in range U+0000..U+10FFFF
    // return invalid codepoint as one byte
    return 1;
}

int Utf8ByteCount(const lChar32 * str)
{
    int count = 0;
    lUInt32 ch;
    while ( (ch=*str++) ) {
        count += charUtf8ByteCount(ch);
    }
    return count;
}

int Utf8ByteCount(const lChar32 * str, int len)
{
    int count = 0;
    lUInt32 ch;
    while ((len--) > 0) {
        ch = *str++;
        count += charUtf8ByteCount(ch);
    }
    return count;
}

lString32 Utf8ToUnicode( const lString8 & str )
{
	return Utf8ToUnicode( str.c_str() );
}

#define CONT_BYTE(index,shift) (((lChar32)(s[index]) & 0x3F) << shift)

static void DecodeUtf8(const char * s,  lChar32 * p, int len)
{
    lChar32 * endp = p + len;
    lUInt32 ch;
    while (p < endp) {
        ch = *s++;
        if ( (ch & 0x80) == 0 ) {
            *p++ = (char)ch;
        } else if ( (ch & 0xE0) == 0xC0 ) {
            *p++ = ((ch & 0x1F) << 6)
                    | CONT_BYTE(0,0);
            s++;
        } else if ( (ch & 0xF0) == 0xE0 ) {
            *p++ = ((ch & 0x0F) << 12)
                | CONT_BYTE(0,6)
                | CONT_BYTE(1,0);
            s += 2;
        } else if ( (ch & 0xF8) == 0xF0 ) {
            *p++ = ((ch & 0x07) << 18)
                | CONT_BYTE(0,12)
                | CONT_BYTE(1,6)
                | CONT_BYTE(2,0);
            s += 3;
        } else {
            // Invalid first byte in UTF-8 sequence
            // Pass with mask 0x7F, to resolve exception around env->NewStringUTF()
            *p++ = (char) (ch & 0x7F);
        }
    }
}

// Top two bits are 10, i.e. original & 11000000(2) == 10000000(2)
#define IS_FOLLOWING(index) ((s[index] & 0xC0) == 0x80)

void Utf8ToUnicode(const lUInt8 * src,  int &srclen, lChar32 * dst, int &dstlen)
{
    const lUInt8 * s = src;
    const lUInt8 * ends = s + srclen;
    lChar32 * p = dst;
    lChar32 * endp = p + dstlen;
    lUInt32 ch;
    while (p < endp && s < ends) {
        ch = *s;
        if ( (ch & 0x80) == 0 ) {
            *p++ = (char)ch;
            s++;
            continue;
        } else if ( (ch & 0xE0) == 0xC0 ) {
            if (s + 2 > ends)
                break;
            if (IS_FOLLOWING(1)) {
                *p++ = ((ch & 0x1F) << 6)
                        | CONT_BYTE(1,0);
                s += 2;
                continue;
            }
        } else if ( (ch & 0xF0) == 0xE0 ) {
            if (s + 3 > ends)
                break;
            if (IS_FOLLOWING(1) && IS_FOLLOWING(2)) {
                *p++ = ((ch & 0x0F) << 12)
                    | CONT_BYTE(1,6)
                    | CONT_BYTE(2,0);
                s += 3;
                // Supports WTF-8 : https://en.wikipedia.org/wiki/UTF-8#WTF-8
                // a superset of UTF-8, that includes UTF-16 surrogates
                // in UTF-8 bytes (forbidden in well-formed UTF-8).
                // We may get that from bad producers or converters.
                // As these shouldn't be there in UTF-8, if we find
                // these surrogates in the right sequence, we might as well
                // convert the char they represent to the right Unicode
                // codepoint and display it instead of a '?'.
                //   Surrogates are code points from two special ranges of
                //   Unicode values, reserved for use as the leading, and
                //   trailing values of paired code units in UTF-16. Leading,
                //   also called high, surrogates are from D800 to DBFF, and
                //   trailing, or low, surrogates are from DC00 to DFFF. They
                //   are called surrogates, since they do not represent
                //   characters directly, but only as a pair.
                if (*(p-1) >= 0xD800 && *(p-1) <= 0xDBFF && s+2 < ends) { // what we wrote is a high surrogate,
                    lUInt32 next = *s;                            // and there's room next for a low surrogate
                    if ( (next & 0xF0) == 0xE0 && IS_FOLLOWING(1) && IS_FOLLOWING(2)) { // is a valid 3-bytes sequence
                        next = ((next & 0x0F) << 12) | CONT_BYTE(1,6) | CONT_BYTE(2,0);
                        if (next >= 0xDC00 && next <= 0xDFFF) { // is a low surrogate: valid surrogates sequence
                            ch = 0x10000 + ((*(p-1) & 0x3FF)<<10) + (next & 0x3FF);
                            p--; // rewind to override what we wrote
                            *p++ = ch;
                            s += 3;
                        }
                    }
                }
                continue;
            }
        } else if ( (ch & 0xF8) == 0xF0 ) {
            if (s + 4 > ends)
                break;
            if (IS_FOLLOWING(1) && IS_FOLLOWING(2) && IS_FOLLOWING(3)) {
                *p++ = ((ch & 0x07) << 18)
                    | CONT_BYTE(1,12)
                    | CONT_BYTE(2,6)
                    | CONT_BYTE(3,0);
                s += 4;
                continue;
            }
        } else {
            // Invalid first byte in UTF-8 sequence
            // Pass with mask 0x7F, to resolve exception around env->NewStringUTF()
            *p++ = (char) (ch & 0x7F);
            s++;
            continue;
        }
        // unexpected character
        *p++ = '?';
        s++;
    }
    srclen = (int)(s - src);
    dstlen = (int)(p - dst);
}

lString32 Utf8ToUnicode( const char * s ) {
    if (!s || !s[0])
      return lString32::empty_str;
    int len = Utf8CharCount( s );
    if (!len)
      return lString32::empty_str;
    lString32 dst;
    dst.append(len, (lChar32)0);
    lChar32 * p = dst.modify();
    DecodeUtf8(s, p, len);
    return dst;
}

lString32 Utf8ToUnicode( const char * s, int sz ) {
    if (!s || !s[0] || sz <= 0)
      return lString32::empty_str;
    int len = Utf8CharCount( s, sz );
    if (!len)
      return lString32::empty_str;
    lString32 dst;
    dst.append(len, 0);
    lChar32 * p = dst.modify();
    DecodeUtf8(s, p, len);
    return dst;
}


lString8 UnicodeToUtf8(const lChar32 * s, int count)
{
    if (count <= 0)
      return lString8::empty_str;
    lString8 dst;
    int len = Utf8ByteCount(s, count);
    if (len <= 0)
      return lString8::empty_str;
    dst.append( len, ' ' );
    lChar8 * buf = dst.modify();
    {
        lUInt32 ch;
        while ((count--) > 0) {
            ch = *s++;
            if (!(ch & ~0x7F)) {
                *buf++ = ( (lUInt8)ch );
            } else if (!(ch & ~0x7FF)) {
                *buf++ = ( (lUInt8) ( ((ch >> 6) & 0x1F) | 0xC0 ) );
                *buf++ = ( (lUInt8) ( ((ch ) & 0x3F) | 0x80 ) );
            } else if (!(ch & ~0xFFFF)) {
                *buf++ = ( (lUInt8) ( ((ch >> 12) & 0x0F) | 0xE0 ) );
                *buf++ = ( (lUInt8) ( ((ch >> 6) & 0x3F) | 0x80 ) );
                *buf++ = ( (lUInt8) ( ((ch ) & 0x3F) | 0x80 ) );
            } else if (!(ch & ~0x1FFFFF)) {
                *buf++ = ( (lUInt8) ( ((ch >> 18) & 0x07) | 0xF0 ) );
                *buf++ = ( (lUInt8) ( ((ch >> 12) & 0x3F) | 0x80 ) );
                *buf++ = ( (lUInt8) ( ((ch >> 6) & 0x3F) | 0x80 ) );
                *buf++ = ( (lUInt8) ( ((ch ) & 0x3F) | 0x80 ) );
            } else {
                // invalid codepoint
                // In Unicode Standard codepoint must be in range U+0000 .. U+10FFFF
                *buf++ = '?';
            }
        }
    }
    return dst;
}

lString8 UnicodeToUtf8( const lString32 & str )
{
    return UnicodeToUtf8(str.c_str(), str.length());
}

lString8 UnicodeTo8Bit( const lString32 & str, const lChar8 * * table )
{
    lString8 buf;
    buf.reserve( str.length() );
    for (int i=0; i < str.length(); i++) {
        lChar32 ch = str[i];
        const lChar8 * p = table[ (ch>>8) & 255 ];
        if ( p ) {
            buf += p[ ch&255 ];
        } else {
            buf += '?';
        }
    }
    return buf;
}

lString32 ByteToUnicode( const lString8 & str, const lChar32 * table )
{
    lString32 buf;
    buf.reserve( str.length() );
    for (int i=0; i < str.length(); i++) {
        lChar32 ch = (unsigned char)str[i];
        lChar32 ch32 = ((ch & 0x80) && table) ? table[ (ch&0x7F) ] : ch;
        buf += ch32;
    }
    return buf;
}


#if !defined(__SYMBIAN32__) && defined(_WIN32)

lString8 UnicodeToLocal( const lString32 & str )
{
   lString8 dst;
   if (str.empty())
      return dst;
   CHAR def_char = '?';
   BOOL usedDefChar = FALSE;
   int len = WideCharToMultiByte(
      CP_ACP,
      WC_COMPOSITECHECK | WC_DISCARDNS
       | WC_SEPCHARS | WC_DEFAULTCHAR,
      str.c_str(),
      str.length(),
      NULL,
      0,
      &def_char,
      &usedDefChar
      );
   if (len)
   {
      dst.insert(0, len, ' ');
      WideCharToMultiByte(
         CP_ACP,
         WC_COMPOSITECHECK | WC_DISCARDNS
          | WC_SEPCHARS | WC_DEFAULTCHAR,
         str.c_str(),
         str.length(),
         dst.modify(),
         len,
         &def_char,
         &usedDefChar
         );
   }
   return dst;
}

lString32 LocalToUnicode( const lString8 & str )
{
   lString32 dst;
   if (str.empty())
      return dst;
   int len = MultiByteToWideChar(
      CP_ACP,
      0,
      str.c_str(),
      str.length(),
      NULL,
      0
      );
   if (len)
   {
      dst.insert(0, len, ' ');
      MultiByteToWideChar(
         CP_ACP,
         0,
         str.c_str(),
         str.length(),
         dst.modify(),
         len
         );
   }
   return dst;
}

#else

lString8 UnicodeToLocal( const lString32 & str )
{
    return UnicodeToUtf8( str );
}

lString32 LocalToUnicode( const lString8 & str )
{
    return Utf8ToUnicode( str );
}

#endif

//0x410
static const char * russian_capital[32] =
{
"A", "B", "V", "G", "D", "E", "ZH", "Z", "I", "j", "K", "L", "M", "N", "O", "P", "R",
"S", "T", "U", "F", "H", "TS", "CH", "SH", "SH", "\'", "Y", "\'", "E", "YU", "YA"
};
static const char * russian_small[32] =
{
"a", "b", "v", "g", "d", "e", "zh", "z", "i", "j", "k", "l", "m", "n", "o", "p", "r",
"s", "t", "u", "f", "h", "ts", "ch", "sh", "sh", "\'", "y", "\'", "e", "yu", "ya"
};

static const char * latin_1[64] =
{
"A", // U+00C0	LATIN CAPITAL LETTER A WITH GRAVE
"A", // U+00C1	LATIN CAPITAL LETTER A WITH ACUTE
"A", // U+00C2	LATIN CAPITAL LETTER A WITH CIRCUMFLEX
"A", // U+00C3	LATIN CAPITAL LETTER A WITH TILDE
"AE",// U+00C4	LATIN CAPITAL LETTER A WITH DIAERESIS
"A", // U+00C5	LATIN CAPITAL LETTER A WITH RING ABOVE
"AE",// U+00C6	LATIN CAPITAL LETTER AE
"C", // U+00C7	LATIN CAPITAL LETTER C WITH CEDILLA
"E", // U+00C8	LATIN CAPITAL LETTER E WITH GRAVE
"E", // U+00C9	LATIN CAPITAL LETTER E WITH ACUTE
"E", // U+00CA	LATIN CAPITAL LETTER E WITH CIRCUMFLEX
"E", // U+00CB	LATIN CAPITAL LETTER E WITH DIAERESIS
"I", // U+00CC	LATIN CAPITAL LETTER I WITH GRAVE
"I", // U+00CD	LATIN CAPITAL LETTER I WITH ACUTE
"I", // U+00CE	LATIN CAPITAL LETTER I WITH CIRCUMFLEX
"I", // U+00CF	LATIN CAPITAL LETTER I WITH DIAERESIS
"D", // U+00D0	LATIN CAPITAL LETTER ETH
"N", // U+00D1	LATIN CAPITAL LETTER N WITH TILDE
"O", // U+00D2	LATIN CAPITAL LETTER O WITH GRAVE
"O", // U+00D3	LATIN CAPITAL LETTER O WITH ACUTE
"O", // U+00D4	LATIN CAPITAL LETTER O WITH CIRCUMFLEX
"O", // U+00D5	LATIN CAPITAL LETTER O WITH TILDE
"OE",// U+00D6	LATIN CAPITAL LETTER O WITH DIAERESIS
"x", // U+00D7	MULTIPLICATION SIGN
"O", // U+00D8	LATIN CAPITAL LETTER O WITH STROKE
"U", // U+00D9	LATIN CAPITAL LETTER U WITH GRAVE
"U", // U+00DA	LATIN CAPITAL LETTER U WITH ACUTE
"U", // U+00DB	LATIN CAPITAL LETTER U WITH CIRCUMFLEX
"UE",// U+00DC	LATIN CAPITAL LETTER U WITH DIAERESIS
"Y", // U+00DD	LATIN CAPITAL LETTER Y WITH ACUTE
"p", // U+00DE	LATIN CAPITAL LETTER THORN
"SS",// U+00DF	LATIN SMALL LETTER SHARP S
"a", // U+00E0	LATIN SMALL LETTER A WITH GRAVE
"a", // U+00E1	LATIN SMALL LETTER A WITH ACUTE
"a", // U+00E2	LATIN SMALL LETTER A WITH CIRCUMFLEX
"a", // U+00E3	LATIN SMALL LETTER A WITH TILDE
"ae",// U+00E4	LATIN SMALL LETTER A WITH DIAERESIS
"a", // U+00E5	LATIN SMALL LETTER A WITH RING ABOVE
"ae",// U+00E6	LATIN SMALL LETTER AE
"c", // U+00E7	LATIN SMALL LETTER C WITH CEDILLA
"e", // U+00E8	LATIN SMALL LETTER E WITH GRAVE
"e", // U+00E9	LATIN SMALL LETTER E WITH ACUTE
"e", // U+00EA	LATIN SMALL LETTER E WITH CIRCUMFLEX
"e", // U+00EB	LATIN SMALL LETTER E WITH DIAERESIS
"i", // U+00EC	LATIN SMALL LETTER I WITH GRAVE
"i", // U+00ED	LATIN SMALL LETTER I WITH ACUTE
"i", // U+00EE	LATIN SMALL LETTER I WITH CIRCUMFLEX
"i", // U+00EF	LATIN SMALL LETTER I WITH DIAERESIS
"d", // U+00F0	LATIN SMALL LETTER ETH
"n", // U+00F1	LATIN SMALL LETTER N WITH TILDE
"o", // U+00F2	LATIN SMALL LETTER O WITH GRAVE
"o", // U+00F3	LATIN SMALL LETTER O WITH ACUTE
"o", // U+00F4	LATIN SMALL LETTER O WITH CIRCUMFLEX
"oe",// U+00F5	LATIN SMALL LETTER O WITH TILDE
"o", // U+00F6	LATIN SMALL LETTER O WITH DIAERESIS
"x", // U+00F7	DIVISION SIGN
"o", // U+00F8	LATIN SMALL LETTER O WITH STROKE
"u", // U+00F9	LATIN SMALL LETTER U WITH GRAVE
"u", // U+00FA	LATIN SMALL LETTER U WITH ACUTE
"u", // U+00FB	LATIN SMALL LETTER U WITH CIRCUMFLEX
"ue",// U+00FC	LATIN SMALL LETTER U WITH DIAERESIS
"y", // U+00FD	LATIN SMALL LETTER Y WITH ACUTE
"p", // U+00FE	LATIN SMALL LETTER THORN
"y", // U+00FF	LATIN SMALL LETTER Y WITH DIAERESIS
};

static const char * getCharTranscript( lChar32 ch )
{
    if ( ch>=0x410 && ch<0x430 )
        return russian_capital[ch-0x410];
    else if (ch>=0x430 && ch<0x450)
        return russian_small[ch-0x430];
    else if (ch>=0xC0 && ch<0xFF)
        return latin_1[ch-0xC0];
    else if (ch==0x450)
        return "E";
    else if ( ch==0x451 )
        return "e";
    return "?";
}


lString8  UnicodeToTranslit( const lString32 & str )
{
    lString8 buf;
	if ( str.empty() )
		return buf;
    buf.reserve( str.length()*5/4 );
    for ( int i=0; i<str.length(); i++ ) {
		lChar32 ch = str[i];
        if ( ch>=32 && ch<=127 ) {
            buf.append( 1, (lChar8)ch );
        } else {
            const char * trans = getCharTranscript(ch);
            buf.append( trans );
        }
	}
    buf.pack();
    return buf;
}


// Note:
// CH_PROP_UPPER and CH_PROP_LOWER make out CH_PROP_ALPHA, which is,
// with CH_PROP_CONSONANT, CH_PROP_VOWEL and CH_PROP_ALPHA_SIGN,
// used only for detecting a word candidate to hyphenation.
// CH_PROP_MODIFIER is used also only for detecting a word candidate
// to hyphenation, and makes it get the behaviour of its preceding
// non-modifier character.
// CH_PROP_PUNCT is used in some obscure place, and CH_PROP_SIGN,
// CH_PROP_DIGIT, CH_PROP_SPACE are not used, except to flag a char
// as word separator.
// CH_PROP_AVOID_WRAP_BEFORE and CH_PROP_AVOID_WRAP_AFTER are used
// for line breaking when not using libunibreak.
static lUInt16 char_props[] = {
// 0x0000:
0,0,0,0, 0,0,0,0, CH_PROP_SPACE,CH_PROP_SPACE,CH_PROP_SPACE,0, CH_PROP_SPACE,CH_PROP_SPACE,0,0,
0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
// 0x0020:
CH_PROP_SPACE, // ' '
CH_PROP_PUNCT | CH_PROP_AVOID_WRAP_BEFORE, // '!'
CH_PROP_PUNCT, // '\"'
CH_PROP_SIGN, // '#' (Unicode Po, but considered as symbol)
CH_PROP_SIGN | CH_PROP_AVOID_WRAP_BEFORE | CH_PROP_AVOID_WRAP_AFTER, // '$'
CH_PROP_SIGN | CH_PROP_AVOID_WRAP_BEFORE, // '%' (Unicode Po, but considered as symbol)
CH_PROP_SIGN, // '&' (Unicode Po, but considered as symbol)
CH_PROP_PUNCT, // '\''
CH_PROP_PUNCT_OPEN | CH_PROP_AVOID_WRAP_AFTER, // '('
CH_PROP_PUNCT_CLOSE | CH_PROP_AVOID_WRAP_BEFORE, // ')'
CH_PROP_SIGN | CH_PROP_AVOID_WRAP_BEFORE | CH_PROP_AVOID_WRAP_AFTER, // '*' (Unicode Po, but considered as symbol)
CH_PROP_SIGN | CH_PROP_AVOID_WRAP_BEFORE | CH_PROP_AVOID_WRAP_AFTER, // '+'
CH_PROP_PUNCT | CH_PROP_AVOID_WRAP_BEFORE, // ','
CH_PROP_SIGN | CH_PROP_AVOID_WRAP_BEFORE, // '-' (Unicode Pd, but considered as symbol, for consistency with +)
CH_PROP_PUNCT | CH_PROP_AVOID_WRAP_BEFORE, // '.'
CH_PROP_SIGN | CH_PROP_AVOID_WRAP_BEFORE, // '/' (Unicode Po, but considered as symbol)
// 0x0030:
CH_PROP_DIGIT, // '0'
CH_PROP_DIGIT, // '1'
CH_PROP_DIGIT, // '2'
CH_PROP_DIGIT, // '3'
CH_PROP_DIGIT, // '4'
CH_PROP_DIGIT, // '5'
CH_PROP_DIGIT, // '6'
CH_PROP_DIGIT, // '7'
CH_PROP_DIGIT, // '8'
CH_PROP_DIGIT, // '9'
CH_PROP_PUNCT | CH_PROP_AVOID_WRAP_BEFORE, // ':'
CH_PROP_PUNCT | CH_PROP_AVOID_WRAP_BEFORE, // ';'
CH_PROP_SIGN  | CH_PROP_AVOID_WRAP_BEFORE | CH_PROP_AVOID_WRAP_AFTER,  // '<'
CH_PROP_SIGN | CH_PROP_AVOID_WRAP_BEFORE | CH_PROP_AVOID_WRAP_AFTER,  // '='
CH_PROP_SIGN | CH_PROP_AVOID_WRAP_BEFORE | CH_PROP_AVOID_WRAP_AFTER,  // '>'
CH_PROP_PUNCT | CH_PROP_AVOID_WRAP_BEFORE, // '?'
// 0x0040:
CH_PROP_SIGN,  // '@' (Unicode Po, but considered as symbol)
CH_PROP_UPPER | CH_PROP_VOWEL,     // 'A'
CH_PROP_UPPER | CH_PROP_CONSONANT, // 'B'
CH_PROP_UPPER | CH_PROP_CONSONANT, // 'C'
CH_PROP_UPPER | CH_PROP_CONSONANT, // 'D'
CH_PROP_UPPER | CH_PROP_VOWEL, // 'E'
CH_PROP_UPPER | CH_PROP_CONSONANT, // 'F'
CH_PROP_UPPER | CH_PROP_CONSONANT, // 'G'
CH_PROP_UPPER | CH_PROP_CONSONANT, // 'H'
CH_PROP_UPPER | CH_PROP_VOWEL, // 'I'
CH_PROP_UPPER | CH_PROP_CONSONANT, // 'J'
CH_PROP_UPPER | CH_PROP_CONSONANT, // 'K'
CH_PROP_UPPER | CH_PROP_CONSONANT, // 'L'
CH_PROP_UPPER | CH_PROP_CONSONANT, // 'M'
CH_PROP_UPPER | CH_PROP_CONSONANT, // 'N'
CH_PROP_UPPER | CH_PROP_VOWEL, // 'O'
CH_PROP_UPPER | CH_PROP_CONSONANT, // 'P'
CH_PROP_UPPER | CH_PROP_CONSONANT, // 'Q'
CH_PROP_UPPER | CH_PROP_CONSONANT, // 'R'
CH_PROP_UPPER | CH_PROP_CONSONANT, // 'S'
CH_PROP_UPPER | CH_PROP_CONSONANT, // 'T'
CH_PROP_UPPER | CH_PROP_VOWEL, // 'U'
CH_PROP_UPPER | CH_PROP_CONSONANT, // 'V'
CH_PROP_UPPER | CH_PROP_CONSONANT, // 'W'
CH_PROP_UPPER | CH_PROP_CONSONANT, // 'X'
CH_PROP_UPPER | CH_PROP_VOWEL, // 'Y'
CH_PROP_UPPER | CH_PROP_CONSONANT, // 'Z'
CH_PROP_PUNCT_OPEN | CH_PROP_AVOID_WRAP_AFTER, // '['
CH_PROP_SIGN, // '\' (Unicode Po, but considered as symbol)
CH_PROP_PUNCT_CLOSE | CH_PROP_AVOID_WRAP_BEFORE, // ']'
CH_PROP_MODIFIER, // 005E (Sk) CIRCUMFLEX ACCENT '^'
CH_PROP_SIGN, // '_' (Unicode Pc, but considered as symbol)
// 0x0060:
CH_PROP_MODIFIER, // 0060 (Sk) GRAVE ACCENT '`'
CH_PROP_LOWER | CH_PROP_VOWEL,     // 'a'
CH_PROP_LOWER | CH_PROP_CONSONANT, // 'b'
CH_PROP_LOWER | CH_PROP_CONSONANT, // 'c'
CH_PROP_LOWER | CH_PROP_CONSONANT, // 'd'
CH_PROP_LOWER | CH_PROP_VOWEL, // 'e'
CH_PROP_LOWER | CH_PROP_CONSONANT, // 'f'
CH_PROP_LOWER | CH_PROP_CONSONANT, // 'g'
CH_PROP_LOWER | CH_PROP_CONSONANT, // 'h'
CH_PROP_LOWER | CH_PROP_VOWEL, // 'i'
CH_PROP_LOWER | CH_PROP_CONSONANT, // 'j'
CH_PROP_LOWER | CH_PROP_CONSONANT, // 'k'
CH_PROP_LOWER | CH_PROP_CONSONANT, // 'l'
CH_PROP_LOWER | CH_PROP_CONSONANT, // 'm'
CH_PROP_LOWER | CH_PROP_CONSONANT, // 'n'
CH_PROP_LOWER | CH_PROP_VOWEL, // 'o'
CH_PROP_LOWER | CH_PROP_CONSONANT, // 'p'
CH_PROP_LOWER | CH_PROP_CONSONANT, // 'q'
CH_PROP_LOWER | CH_PROP_CONSONANT, // 'r'
CH_PROP_LOWER | CH_PROP_CONSONANT, // 's'
CH_PROP_LOWER | CH_PROP_CONSONANT, // 't'
CH_PROP_LOWER | CH_PROP_VOWEL, // 'u'
CH_PROP_LOWER | CH_PROP_CONSONANT, // 'v'
CH_PROP_LOWER | CH_PROP_CONSONANT, // 'w'
CH_PROP_LOWER | CH_PROP_CONSONANT, // 'x'
CH_PROP_LOWER | CH_PROP_VOWEL, // 'y'
CH_PROP_LOWER | CH_PROP_CONSONANT, // 'z'
CH_PROP_PUNCT_OPEN | CH_PROP_AVOID_WRAP_AFTER, // '{'
CH_PROP_SIGN | CH_PROP_AVOID_WRAP_BEFORE | CH_PROP_AVOID_WRAP_AFTER, // '|'
CH_PROP_PUNCT_CLOSE | CH_PROP_AVOID_WRAP_BEFORE, // '}'
CH_PROP_SIGN, // '~'
0,
// 0x0080:
0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
// 0x0090:
0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
// 0x00A0:
CH_PROP_SPACE, // 00A0 nbsp
CH_PROP_PUNCT, // 00A1 inverted !
CH_PROP_SIGN,  // 00A2 (Sc) CENT SIGN
CH_PROP_SIGN,  // 00A3 (Sc) POUND SIGN
CH_PROP_SIGN,  // 00A4 (Sc) CURRENCY SIGN
CH_PROP_SIGN,  // 00A5 (Sc) YEN SIGN
CH_PROP_SIGN,  // 00A6 (So) BROKEN BAR
CH_PROP_SIGN,  // 00A7 (Po) SECTION SIGN (considered as symbol)
CH_PROP_MODIFIER, // 00A8 (Sk) DIAERESIS
CH_PROP_SIGN,  // 00A9 (So) COPYRIGHT SIGN
CH_PROP_LOWER, // 00AA (Lo) FEMININE ORDINAL INDICATOR
CH_PROP_PUNCT_OPEN | CH_PROP_AVOID_WRAP_AFTER,  // 00AB «
CH_PROP_SIGN,  // 00AC (Sm) NOT SIGN
CH_PROP_HYPHEN,// 00AD soft-hyphen (UNICODE_SOFT_HYPHEN_CODE)
CH_PROP_SIGN,  // 00AE (So) REGISTERED SIGN
CH_PROP_MODIFIER, // 00AF (Sk) MACRON
// 0x00B0:
CH_PROP_SIGN,  // 00B0 (So) DEGREE SIGN
CH_PROP_SIGN,  // 00B1 (Sm) PLUS-MINUS SIGN
CH_PROP_DIGIT, // 00B2 (No) SUPERSCRIPT TWO
CH_PROP_DIGIT, // 00B3 (No) SUPERSCRIPT THREE
CH_PROP_MODIFIER, // 00B4 (Sk) ACUTE ACCENT
CH_PROP_LOWER, // 00B5 (Ll) MICRO SIGN
CH_PROP_PUNCT, // 00B6 (Po) PILCROW SIGN
CH_PROP_PUNCT, // 00B7 (Po) MIDDLE DOT
CH_PROP_MODIFIER, // 00B8 (Sk) CEDILLA
CH_PROP_DIGIT, // 00B9 (No) SUPERSCRIPT ONE
CH_PROP_LOWER, // 00BA (Lo) MASCULINE ORDINAL INDICATOR
CH_PROP_PUNCT_CLOSE | CH_PROP_AVOID_WRAP_BEFORE,  // 00BB »
CH_PROP_LOWER, // 00BC (No) VULGAR FRACTION ONE QUARTER
CH_PROP_LOWER, // 00BD (No) VULGAR FRACTION ONE HALF
CH_PROP_LOWER, // 00BE (No) VULGAR FRACTION THREE QUARTERS
CH_PROP_PUNCT, // 00BF (Po) INVERTED QUESTION MARK
// 0x00C0:
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00C0 A`
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00C1 A'
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00C2 A^
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00C3 A"
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00C4 A:
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00C5 Ao
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00C6 AE
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 00C7 C~
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00C8 E`
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00C9 E'
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00CA E^
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00CB E:
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00CC I`
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00CD I'
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00CE I^
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00CF I:
// 0x00D0:
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 00D0 D-
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 00D1 N-
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00D2 O`
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00D3 O'
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00D4 O^
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00D5 O"
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00D6 O:
CH_PROP_SIGN | CH_PROP_AVOID_WRAP_BEFORE | CH_PROP_AVOID_WRAP_AFTER,  // 00D7 x (multiplication sign)
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00D8 O/
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00D9 U`
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00DA U'
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00DB U^
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00DC U:
CH_PROP_UPPER | CH_PROP_VOWEL,  // 00DD Y'
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 00DE P thorn
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 00DF ss
// 0x00E0:
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00E0 a`
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00E1 a'
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00E2 a^
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00E3 a"
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00E4 a:
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00E5 ao
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00E6 ae
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 00E7 c~
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00E8 e`
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00E9 e'
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00EA e^
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00EB e:
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00EC i`
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00ED i'
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00EE i^
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00EF i:
// 0x00F0:
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 00F0 eth
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 00F1 n~
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00F2 o`
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00F3 o'
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00F4 o^
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00F5 o"
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00F6 o:
CH_PROP_SIGN | CH_PROP_AVOID_WRAP_BEFORE | CH_PROP_AVOID_WRAP_AFTER,  // 00F7 (division sign %)
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00F8 o/
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00F9 u`
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00FA u'
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00FB u^
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00FC u:
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00FD y'
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 00FE p thorn
CH_PROP_LOWER | CH_PROP_VOWEL,  // 00FF y:
// 0x0100:
CH_PROP_UPPER | CH_PROP_VOWEL,  // 0100 A_
CH_PROP_LOWER | CH_PROP_VOWEL,  // 0101 a_
CH_PROP_UPPER | CH_PROP_VOWEL,  // 0102 Au
CH_PROP_LOWER | CH_PROP_VOWEL,  // 0103 au
CH_PROP_UPPER | CH_PROP_VOWEL,  // 0104 A,
CH_PROP_LOWER | CH_PROP_VOWEL,  // 0105 a,
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0106 C'
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0107 c'
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0108 C^
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0109 c^
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 010A C.
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 010B c.
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 010C Cu
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 010D cu
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 010E Du
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 010F d'

CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0110 D-
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0111 d-
CH_PROP_UPPER | CH_PROP_VOWEL,  // 0112 E_
CH_PROP_LOWER | CH_PROP_VOWEL,  // 0113 e_
CH_PROP_UPPER | CH_PROP_VOWEL,  // 0114 Eu
CH_PROP_LOWER | CH_PROP_VOWEL,  // 0115 eu
CH_PROP_UPPER | CH_PROP_VOWEL,  // 0116 E.
CH_PROP_LOWER | CH_PROP_VOWEL,  // 0117 e.
CH_PROP_UPPER | CH_PROP_VOWEL,  // 0118 E,
CH_PROP_LOWER | CH_PROP_VOWEL,  // 0119 e,
CH_PROP_UPPER | CH_PROP_VOWEL,  // 011A Ev
CH_PROP_LOWER | CH_PROP_VOWEL,  // 011B ev
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 011C G^
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 011D g^
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 011E Gu
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 011F Gu

CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0120 G.
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0121 g.
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0122 G,
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0123 g,
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0124 H^
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0125 h^
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0126 H-
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0127 h-
CH_PROP_UPPER | CH_PROP_VOWEL,  // 0128 I~
CH_PROP_LOWER | CH_PROP_VOWEL,  // 0129 i~
CH_PROP_UPPER | CH_PROP_VOWEL,  // 012A I_
CH_PROP_LOWER | CH_PROP_VOWEL,  // 012B i_
CH_PROP_UPPER | CH_PROP_VOWEL,  // 012C Iu
CH_PROP_LOWER | CH_PROP_VOWEL,  // 012D iu
CH_PROP_UPPER | CH_PROP_VOWEL,  // 012E I,
CH_PROP_LOWER | CH_PROP_VOWEL,  // 012F i,

CH_PROP_UPPER | CH_PROP_VOWEL,  // 0130 I.
CH_PROP_LOWER | CH_PROP_VOWEL,  // 0131 i-.
CH_PROP_UPPER | CH_PROP_VOWEL,  // 0132 IJ
CH_PROP_LOWER | CH_PROP_VOWEL,  // 0133 ij
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0134 J^
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0135 j^
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0136 K,
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0137 k,
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0138 k (kra)
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0139 L'
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 013A l'
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 013B L,
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 013C l,
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 013D L'
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 013E l'
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 013F L.

CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0140 l.
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0141 L/
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0142 l/
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0143 N'
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0144 n'
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0145 N,
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0146 n,
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0147 Nv
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0148 nv
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0149 `n
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 014A Ng
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 014B ng
CH_PROP_UPPER | CH_PROP_VOWEL,  // 014C O_
CH_PROP_LOWER | CH_PROP_VOWEL,  // 014D o-.
CH_PROP_UPPER | CH_PROP_VOWEL,  // 014E Ou
CH_PROP_LOWER | CH_PROP_VOWEL,  // 014F ou

CH_PROP_UPPER | CH_PROP_VOWEL,  // 0150 O"
CH_PROP_LOWER | CH_PROP_VOWEL,  // 0151 o"
CH_PROP_UPPER | CH_PROP_VOWEL,  // 0152 Oe
CH_PROP_LOWER | CH_PROP_VOWEL,  // 0153 oe
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0154 R'
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0155 r'
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0156 R,
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0157 r,
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0158 Rv
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0159 rv
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 015A S'
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 015B s'
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 015C S^
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 015D s^
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 015E S,
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 015F s,

CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0160 Sv
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0161 sv
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0162 T,
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0163 T,
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0164 Tv
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0165 Tv
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0166 T-
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0167 T-
CH_PROP_UPPER | CH_PROP_VOWEL,  // 0168 U~
CH_PROP_LOWER | CH_PROP_VOWEL,  // 0169 u~
CH_PROP_UPPER | CH_PROP_VOWEL,  // 016A U_
CH_PROP_LOWER | CH_PROP_VOWEL,  // 016B u_
CH_PROP_UPPER | CH_PROP_VOWEL,  // 016C Uu
CH_PROP_LOWER | CH_PROP_VOWEL,  // 016D uu
CH_PROP_UPPER | CH_PROP_VOWEL,  // 016E Uo
CH_PROP_LOWER | CH_PROP_VOWEL,  // 016F uo

CH_PROP_UPPER | CH_PROP_VOWEL,  // 0170 U"
CH_PROP_LOWER | CH_PROP_VOWEL,  // 0171 u"
CH_PROP_UPPER | CH_PROP_VOWEL,  // 0172 U,
CH_PROP_LOWER | CH_PROP_VOWEL,  // 0173 u,
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0174 W^
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0175 w^
CH_PROP_UPPER | CH_PROP_VOWEL,  // 0176 Y,
CH_PROP_LOWER | CH_PROP_VOWEL,  // 0177 y,
CH_PROP_UPPER | CH_PROP_VOWEL,  // 0178 Y:
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0179 Z'
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 017A z'
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 017B Z.
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 017C z.
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 017D Zv
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 017E zv
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 017F s long
// 0x0180:
CH_PROP_LOWER, // 0180 (Ll) LATIN SMALL LETTER B WITH STROKE
CH_PROP_UPPER, // 0181 (Lu) LATIN CAPITAL LETTER B WITH HOOK
CH_PROP_UPPER, // 0182 (Lu) LATIN CAPITAL LETTER B WITH TOPBAR
CH_PROP_LOWER, // 0183 (Ll) LATIN SMALL LETTER B WITH TOPBAR
CH_PROP_UPPER, // 0184 (Lu) LATIN CAPITAL LETTER TONE SIX
CH_PROP_LOWER, // 0185 (Ll) LATIN SMALL LETTER TONE SIX
CH_PROP_UPPER, // 0186 (Lu) LATIN CAPITAL LETTER OPEN O
CH_PROP_UPPER, // 0187 (Lu) LATIN CAPITAL LETTER C WITH HOOK
CH_PROP_LOWER, // 0188 (Ll) LATIN SMALL LETTER C WITH HOOK
CH_PROP_UPPER, // 0189 (Lu) LATIN CAPITAL LETTER AFRICAN D
CH_PROP_UPPER, // 018A (Lu) LATIN CAPITAL LETTER D WITH HOOK
CH_PROP_UPPER, // 018B (Lu) LATIN CAPITAL LETTER D WITH TOPBAR
CH_PROP_LOWER, // 018C (Ll) LATIN SMALL LETTER D WITH TOPBAR
CH_PROP_LOWER, // 018D (Ll) LATIN SMALL LETTER TURNED DELTA
CH_PROP_UPPER, // 018E (Lu) LATIN CAPITAL LETTER REVERSED E
CH_PROP_UPPER, // 018F (Lu) LATIN CAPITAL LETTER SCHWA
CH_PROP_UPPER, // 0190 (Lu) LATIN CAPITAL LETTER OPEN E
CH_PROP_UPPER, // 0191 (Lu) LATIN CAPITAL LETTER F WITH HOOK
CH_PROP_LOWER, // 0192 (Ll) LATIN SMALL LETTER F WITH HOOK
CH_PROP_UPPER, // 0193 (Lu) LATIN CAPITAL LETTER G WITH HOOK
CH_PROP_UPPER, // 0194 (Lu) LATIN CAPITAL LETTER GAMMA
CH_PROP_LOWER, // 0195 (Ll) LATIN SMALL LETTER HV
CH_PROP_UPPER, // 0196 (Lu) LATIN CAPITAL LETTER IOTA
CH_PROP_UPPER, // 0197 (Lu) LATIN CAPITAL LETTER I WITH STROKE
CH_PROP_UPPER, // 0198 (Lu) LATIN CAPITAL LETTER K WITH HOOK
CH_PROP_LOWER, // 0199 (Ll) LATIN SMALL LETTER K WITH HOOK
CH_PROP_LOWER, // 019A (Ll) LATIN SMALL LETTER L WITH BAR
CH_PROP_LOWER, // 019B (Ll) LATIN SMALL LETTER LAMBDA WITH STROKE
CH_PROP_UPPER, // 019C (Lu) LATIN CAPITAL LETTER TURNED M
CH_PROP_UPPER, // 019D (Lu) LATIN CAPITAL LETTER N WITH LEFT HOOK
CH_PROP_LOWER, // 019E (Ll) LATIN SMALL LETTER N WITH LONG RIGHT LEG
CH_PROP_UPPER, // 019F (Lu) LATIN CAPITAL LETTER O WITH MIDDLE TILDE
CH_PROP_UPPER, // 01A0 (Lu) LATIN CAPITAL LETTER O WITH HORN
CH_PROP_LOWER, // 01A1 (Ll) LATIN SMALL LETTER O WITH HORN
CH_PROP_UPPER, // 01A2 (Lu) LATIN CAPITAL LETTER OI
CH_PROP_LOWER, // 01A3 (Ll) LATIN SMALL LETTER OI
CH_PROP_UPPER, // 01A4 (Lu) LATIN CAPITAL LETTER P WITH HOOK
CH_PROP_LOWER, // 01A5 (Ll) LATIN SMALL LETTER P WITH HOOK
CH_PROP_UPPER, // 01A6 (Lu) LATIN LETTER YR
CH_PROP_UPPER, // 01A7 (Lu) LATIN CAPITAL LETTER TONE TWO
CH_PROP_LOWER, // 01A8 (Ll) LATIN SMALL LETTER TONE TWO
CH_PROP_UPPER, // 01A9 (Lu) LATIN CAPITAL LETTER ESH
CH_PROP_LOWER, // 01AA (Ll) LATIN LETTER REVERSED ESH LOOP
CH_PROP_LOWER, // 01AB (Ll) LATIN SMALL LETTER T WITH PALATAL HOOK
CH_PROP_UPPER, // 01AC (Lu) LATIN CAPITAL LETTER T WITH HOOK
CH_PROP_LOWER, // 01AD (Ll) LATIN SMALL LETTER T WITH HOOK
CH_PROP_UPPER, // 01AE (Lu) LATIN CAPITAL LETTER T WITH RETROFLEX HOOK
CH_PROP_UPPER, // 01AF (Lu) LATIN CAPITAL LETTER U WITH HORN
CH_PROP_LOWER, // 01B0 (Ll) LATIN SMALL LETTER U WITH HORN
CH_PROP_UPPER, // 01B1 (Lu) LATIN CAPITAL LETTER UPSILON
CH_PROP_UPPER, // 01B2 (Lu) LATIN CAPITAL LETTER V WITH HOOK
CH_PROP_UPPER, // 01B3 (Lu) LATIN CAPITAL LETTER Y WITH HOOK
CH_PROP_LOWER, // 01B4 (Ll) LATIN SMALL LETTER Y WITH HOOK
CH_PROP_UPPER, // 01B5 (Lu) LATIN CAPITAL LETTER Z WITH STROKE
CH_PROP_LOWER, // 01B6 (Ll) LATIN SMALL LETTER Z WITH STROKE
CH_PROP_UPPER, // 01B7 (Lu) LATIN CAPITAL LETTER EZH
CH_PROP_UPPER, // 01B8 (Lu) LATIN CAPITAL LETTER EZH REVERSED
CH_PROP_LOWER, // 01B9 (Ll) LATIN SMALL LETTER EZH REVERSED
CH_PROP_LOWER, // 01BA (Ll) LATIN SMALL LETTER EZH WITH TAIL
CH_PROP_LOWER, // 01BB (Lo) LATIN LETTER TWO WITH STROKE
CH_PROP_UPPER, // 01BC (Lu) LATIN CAPITAL LETTER TONE FIVE
CH_PROP_LOWER, // 01BD (Ll) LATIN SMALL LETTER TONE FIVE
CH_PROP_LOWER, // 01BE (Ll) LATIN LETTER INVERTED GLOTTAL STOP WITH STROKE
CH_PROP_LOWER, // 01BF (Ll) LATIN LETTER WYNN
CH_PROP_LOWER, // 01C0 (Lo) LATIN LETTER DENTAL CLICK
CH_PROP_LOWER, // 01C1 (Lo) LATIN LETTER LATERAL CLICK
CH_PROP_LOWER, // 01C2 (Lo) LATIN LETTER ALVEOLAR CLICK
CH_PROP_LOWER, // 01C3 (Lo) LATIN LETTER RETROFLEX CLICK
CH_PROP_UPPER, // 01C4 (Lu) LATIN CAPITAL LETTER DZ WITH CARON
CH_PROP_UPPER, // 01C5 (Lt) LATIN CAPITAL LETTER D WITH SMALL LETTER Z WITH CARON
CH_PROP_LOWER, // 01C6 (Ll) LATIN SMALL LETTER DZ WITH CARON
CH_PROP_UPPER, // 01C7 (Lu) LATIN CAPITAL LETTER LJ
CH_PROP_UPPER, // 01C8 (Lt) LATIN CAPITAL LETTER L WITH SMALL LETTER J
CH_PROP_LOWER, // 01C9 (Ll) LATIN SMALL LETTER LJ
CH_PROP_UPPER, // 01CA (Lu) LATIN CAPITAL LETTER NJ
CH_PROP_UPPER, // 01CB (Lt) LATIN CAPITAL LETTER N WITH SMALL LETTER J
CH_PROP_LOWER, // 01CC (Ll) LATIN SMALL LETTER NJ
CH_PROP_UPPER, // 01CD (Lu) LATIN CAPITAL LETTER A WITH CARON
CH_PROP_LOWER, // 01CE (Ll) LATIN SMALL LETTER A WITH CARON
CH_PROP_UPPER, // 01CF (Lu) LATIN CAPITAL LETTER I WITH CARON
CH_PROP_LOWER, // 01D0 (Ll) LATIN SMALL LETTER I WITH CARON
CH_PROP_UPPER, // 01D1 (Lu) LATIN CAPITAL LETTER O WITH CARON
CH_PROP_LOWER, // 01D2 (Ll) LATIN SMALL LETTER O WITH CARON
CH_PROP_UPPER, // 01D3 (Lu) LATIN CAPITAL LETTER U WITH CARON
CH_PROP_LOWER, // 01D4 (Ll) LATIN SMALL LETTER U WITH CARON
CH_PROP_UPPER, // 01D5 (Lu) LATIN CAPITAL LETTER U WITH DIAERESIS AND MACRON
CH_PROP_LOWER, // 01D6 (Ll) LATIN SMALL LETTER U WITH DIAERESIS AND MACRON
CH_PROP_UPPER, // 01D7 (Lu) LATIN CAPITAL LETTER U WITH DIAERESIS AND ACUTE
CH_PROP_LOWER, // 01D8 (Ll) LATIN SMALL LETTER U WITH DIAERESIS AND ACUTE
CH_PROP_UPPER, // 01D9 (Lu) LATIN CAPITAL LETTER U WITH DIAERESIS AND CARON
CH_PROP_LOWER, // 01DA (Ll) LATIN SMALL LETTER U WITH DIAERESIS AND CARON
CH_PROP_UPPER, // 01DB (Lu) LATIN CAPITAL LETTER U WITH DIAERESIS AND GRAVE
CH_PROP_LOWER, // 01DC (Ll) LATIN SMALL LETTER U WITH DIAERESIS AND GRAVE
CH_PROP_LOWER, // 01DD (Ll) LATIN SMALL LETTER TURNED E
CH_PROP_UPPER, // 01DE (Lu) LATIN CAPITAL LETTER A WITH DIAERESIS AND MACRON
CH_PROP_LOWER, // 01DF (Ll) LATIN SMALL LETTER A WITH DIAERESIS AND MACRON
CH_PROP_UPPER, // 01E0 (Lu) LATIN CAPITAL LETTER A WITH DOT ABOVE AND MACRON
CH_PROP_LOWER, // 01E1 (Ll) LATIN SMALL LETTER A WITH DOT ABOVE AND MACRON
CH_PROP_UPPER, // 01E2 (Lu) LATIN CAPITAL LETTER AE WITH MACRON
CH_PROP_LOWER, // 01E3 (Ll) LATIN SMALL LETTER AE WITH MACRON
CH_PROP_UPPER, // 01E4 (Lu) LATIN CAPITAL LETTER G WITH STROKE
CH_PROP_LOWER, // 01E5 (Ll) LATIN SMALL LETTER G WITH STROKE
CH_PROP_UPPER, // 01E6 (Lu) LATIN CAPITAL LETTER G WITH CARON
CH_PROP_LOWER, // 01E7 (Ll) LATIN SMALL LETTER G WITH CARON
CH_PROP_UPPER, // 01E8 (Lu) LATIN CAPITAL LETTER K WITH CARON
CH_PROP_LOWER, // 01E9 (Ll) LATIN SMALL LETTER K WITH CARON
CH_PROP_UPPER, // 01EA (Lu) LATIN CAPITAL LETTER O WITH OGONEK
CH_PROP_LOWER, // 01EB (Ll) LATIN SMALL LETTER O WITH OGONEK
CH_PROP_UPPER, // 01EC (Lu) LATIN CAPITAL LETTER O WITH OGONEK AND MACRON
CH_PROP_LOWER, // 01ED (Ll) LATIN SMALL LETTER O WITH OGONEK AND MACRON
CH_PROP_UPPER, // 01EE (Lu) LATIN CAPITAL LETTER EZH WITH CARON
CH_PROP_LOWER, // 01EF (Ll) LATIN SMALL LETTER EZH WITH CARON
CH_PROP_LOWER, // 01F0 (Ll) LATIN SMALL LETTER J WITH CARON
CH_PROP_UPPER, // 01F1 (Lu) LATIN CAPITAL LETTER DZ
CH_PROP_UPPER, // 01F2 (Lt) LATIN CAPITAL LETTER D WITH SMALL LETTER Z
CH_PROP_LOWER, // 01F3 (Ll) LATIN SMALL LETTER DZ
CH_PROP_UPPER, // 01F4 (Lu) LATIN CAPITAL LETTER G WITH ACUTE
CH_PROP_LOWER, // 01F5 (Ll) LATIN SMALL LETTER G WITH ACUTE
CH_PROP_UPPER, // 01F6 (Lu) LATIN CAPITAL LETTER HWAIR
CH_PROP_UPPER, // 01F7 (Lu) LATIN CAPITAL LETTER WYNN
CH_PROP_UPPER, // 01F8 (Lu) LATIN CAPITAL LETTER N WITH GRAVE
CH_PROP_LOWER, // 01F9 (Ll) LATIN SMALL LETTER N WITH GRAVE
CH_PROP_UPPER, // 01FA (Lu) LATIN CAPITAL LETTER A WITH RING ABOVE AND ACUTE
CH_PROP_LOWER, // 01FB (Ll) LATIN SMALL LETTER A WITH RING ABOVE AND ACUTE
CH_PROP_UPPER, // 01FC (Lu) LATIN CAPITAL LETTER AE WITH ACUTE
CH_PROP_LOWER, // 01FD (Ll) LATIN SMALL LETTER AE WITH ACUTE
CH_PROP_UPPER, // 01FE (Lu) LATIN CAPITAL LETTER O WITH STROKE AND ACUTE
CH_PROP_LOWER, // 01FF (Ll) LATIN SMALL LETTER O WITH STROKE AND ACUTE
// 0x0200:
CH_PROP_UPPER, // 0200 (Lu) LATIN CAPITAL LETTER A WITH DOUBLE GRAVE
CH_PROP_LOWER, // 0201 (Ll) LATIN SMALL LETTER A WITH DOUBLE GRAVE
CH_PROP_UPPER, // 0202 (Lu) LATIN CAPITAL LETTER A WITH INVERTED BREVE
CH_PROP_LOWER, // 0203 (Ll) LATIN SMALL LETTER A WITH INVERTED BREVE
CH_PROP_UPPER, // 0204 (Lu) LATIN CAPITAL LETTER E WITH DOUBLE GRAVE
CH_PROP_LOWER, // 0205 (Ll) LATIN SMALL LETTER E WITH DOUBLE GRAVE
CH_PROP_UPPER, // 0206 (Lu) LATIN CAPITAL LETTER E WITH INVERTED BREVE
CH_PROP_LOWER, // 0207 (Ll) LATIN SMALL LETTER E WITH INVERTED BREVE
CH_PROP_UPPER, // 0208 (Lu) LATIN CAPITAL LETTER I WITH DOUBLE GRAVE
CH_PROP_LOWER, // 0209 (Ll) LATIN SMALL LETTER I WITH DOUBLE GRAVE
CH_PROP_UPPER, // 020A (Lu) LATIN CAPITAL LETTER I WITH INVERTED BREVE
CH_PROP_LOWER, // 020B (Ll) LATIN SMALL LETTER I WITH INVERTED BREVE
CH_PROP_UPPER, // 020C (Lu) LATIN CAPITAL LETTER O WITH DOUBLE GRAVE
CH_PROP_LOWER, // 020D (Ll) LATIN SMALL LETTER O WITH DOUBLE GRAVE
CH_PROP_UPPER, // 020E (Lu) LATIN CAPITAL LETTER O WITH INVERTED BREVE
CH_PROP_LOWER, // 020F (Ll) LATIN SMALL LETTER O WITH INVERTED BREVE
CH_PROP_UPPER, // 0210 (Lu) LATIN CAPITAL LETTER R WITH DOUBLE GRAVE
CH_PROP_LOWER, // 0211 (Ll) LATIN SMALL LETTER R WITH DOUBLE GRAVE
CH_PROP_UPPER, // 0212 (Lu) LATIN CAPITAL LETTER R WITH INVERTED BREVE
CH_PROP_LOWER, // 0213 (Ll) LATIN SMALL LETTER R WITH INVERTED BREVE
CH_PROP_UPPER, // 0214 (Lu) LATIN CAPITAL LETTER U WITH DOUBLE GRAVE
CH_PROP_LOWER, // 0215 (Ll) LATIN SMALL LETTER U WITH DOUBLE GRAVE
CH_PROP_UPPER, // 0216 (Lu) LATIN CAPITAL LETTER U WITH INVERTED BREVE
CH_PROP_LOWER, // 0217 (Ll) LATIN SMALL LETTER U WITH INVERTED BREVE
CH_PROP_UPPER, // 0218 (Lu) LATIN CAPITAL LETTER S WITH COMMA BELOW
CH_PROP_LOWER, // 0219 (Ll) LATIN SMALL LETTER S WITH COMMA BELOW
CH_PROP_UPPER, // 021A (Lu) LATIN CAPITAL LETTER T WITH COMMA BELOW
CH_PROP_LOWER, // 021B (Ll) LATIN SMALL LETTER T WITH COMMA BELOW
CH_PROP_UPPER, // 021C (Lu) LATIN CAPITAL LETTER YOGH
CH_PROP_LOWER, // 021D (Ll) LATIN SMALL LETTER YOGH
CH_PROP_UPPER, // 021E (Lu) LATIN CAPITAL LETTER H WITH CARON
CH_PROP_LOWER, // 021F (Ll) LATIN SMALL LETTER H WITH CARON
CH_PROP_UPPER, // 0220 (Lu) LATIN CAPITAL LETTER N WITH LONG RIGHT LEG
CH_PROP_LOWER, // 0221 (Ll) LATIN SMALL LETTER D WITH CURL
CH_PROP_UPPER, // 0222 (Lu) LATIN CAPITAL LETTER OU
CH_PROP_LOWER, // 0223 (Ll) LATIN SMALL LETTER OU
CH_PROP_UPPER, // 0224 (Lu) LATIN CAPITAL LETTER Z WITH HOOK
CH_PROP_LOWER, // 0225 (Ll) LATIN SMALL LETTER Z WITH HOOK
CH_PROP_UPPER, // 0226 (Lu) LATIN CAPITAL LETTER A WITH DOT ABOVE
CH_PROP_LOWER, // 0227 (Ll) LATIN SMALL LETTER A WITH DOT ABOVE
CH_PROP_UPPER, // 0228 (Lu) LATIN CAPITAL LETTER E WITH CEDILLA
CH_PROP_LOWER, // 0229 (Ll) LATIN SMALL LETTER E WITH CEDILLA
CH_PROP_UPPER, // 022A (Lu) LATIN CAPITAL LETTER O WITH DIAERESIS AND MACRON
CH_PROP_LOWER, // 022B (Ll) LATIN SMALL LETTER O WITH DIAERESIS AND MACRON
CH_PROP_UPPER, // 022C (Lu) LATIN CAPITAL LETTER O WITH TILDE AND MACRON
CH_PROP_LOWER, // 022D (Ll) LATIN SMALL LETTER O WITH TILDE AND MACRON
CH_PROP_UPPER, // 022E (Lu) LATIN CAPITAL LETTER O WITH DOT ABOVE
CH_PROP_LOWER, // 022F (Ll) LATIN SMALL LETTER O WITH DOT ABOVE
CH_PROP_UPPER, // 0230 (Lu) LATIN CAPITAL LETTER O WITH DOT ABOVE AND MACRON
CH_PROP_LOWER, // 0231 (Ll) LATIN SMALL LETTER O WITH DOT ABOVE AND MACRON
CH_PROP_UPPER, // 0232 (Lu) LATIN CAPITAL LETTER Y WITH MACRON
CH_PROP_LOWER, // 0233 (Ll) LATIN SMALL LETTER Y WITH MACRON
CH_PROP_LOWER, // 0234 (Ll) LATIN SMALL LETTER L WITH CURL
CH_PROP_LOWER, // 0235 (Ll) LATIN SMALL LETTER N WITH CURL
CH_PROP_LOWER, // 0236 (Ll) LATIN SMALL LETTER T WITH CURL
CH_PROP_LOWER, // 0237 (Ll) LATIN SMALL LETTER DOTLESS J
CH_PROP_LOWER, // 0238 (Ll) LATIN SMALL LETTER DB DIGRAPH
CH_PROP_LOWER, // 0239 (Ll) LATIN SMALL LETTER QP DIGRAPH
CH_PROP_UPPER, // 023A (Lu) LATIN CAPITAL LETTER A WITH STROKE
CH_PROP_UPPER, // 023B (Lu) LATIN CAPITAL LETTER C WITH STROKE
CH_PROP_LOWER, // 023C (Ll) LATIN SMALL LETTER C WITH STROKE
CH_PROP_UPPER, // 023D (Lu) LATIN CAPITAL LETTER L WITH BAR
CH_PROP_UPPER, // 023E (Lu) LATIN CAPITAL LETTER T WITH DIAGONAL STROKE
CH_PROP_LOWER, // 023F (Ll) LATIN SMALL LETTER S WITH SWASH TAIL
CH_PROP_LOWER, // 0240 (Ll) LATIN SMALL LETTER Z WITH SWASH TAIL
CH_PROP_UPPER, // 0241 (Lu) LATIN CAPITAL LETTER GLOTTAL STOP
CH_PROP_LOWER, // 0242 (Ll) LATIN SMALL LETTER GLOTTAL STOP
CH_PROP_UPPER, // 0243 (Lu) LATIN CAPITAL LETTER B WITH STROKE
CH_PROP_UPPER, // 0244 (Lu) LATIN CAPITAL LETTER U BAR
CH_PROP_UPPER, // 0245 (Lu) LATIN CAPITAL LETTER TURNED V
CH_PROP_UPPER, // 0246 (Lu) LATIN CAPITAL LETTER E WITH STROKE
CH_PROP_LOWER, // 0247 (Ll) LATIN SMALL LETTER E WITH STROKE
CH_PROP_UPPER, // 0248 (Lu) LATIN CAPITAL LETTER J WITH STROKE
CH_PROP_LOWER, // 0249 (Ll) LATIN SMALL LETTER J WITH STROKE
CH_PROP_UPPER, // 024A (Lu) LATIN CAPITAL LETTER SMALL Q WITH HOOK TAIL
CH_PROP_LOWER, // 024B (Ll) LATIN SMALL LETTER Q WITH HOOK TAIL
CH_PROP_UPPER, // 024C (Lu) LATIN CAPITAL LETTER R WITH STROKE
CH_PROP_LOWER, // 024D (Ll) LATIN SMALL LETTER R WITH STROKE
CH_PROP_UPPER, // 024E (Lu) LATIN CAPITAL LETTER Y WITH STROKE
CH_PROP_LOWER, // 024F (Ll) LATIN SMALL LETTER Y WITH STROKE
CH_PROP_LOWER, // 0250 (Ll) LATIN SMALL LETTER TURNED A
CH_PROP_LOWER, // 0251 (Ll) LATIN SMALL LETTER ALPHA
CH_PROP_LOWER, // 0252 (Ll) LATIN SMALL LETTER TURNED ALPHA
CH_PROP_LOWER, // 0253 (Ll) LATIN SMALL LETTER B WITH HOOK
CH_PROP_LOWER, // 0254 (Ll) LATIN SMALL LETTER OPEN O
CH_PROP_LOWER, // 0255 (Ll) LATIN SMALL LETTER C WITH CURL
CH_PROP_LOWER, // 0256 (Ll) LATIN SMALL LETTER D WITH TAIL
CH_PROP_LOWER, // 0257 (Ll) LATIN SMALL LETTER D WITH HOOK
CH_PROP_LOWER, // 0258 (Ll) LATIN SMALL LETTER REVERSED E
CH_PROP_LOWER, // 0259 (Ll) LATIN SMALL LETTER SCHWA
CH_PROP_LOWER, // 025A (Ll) LATIN SMALL LETTER SCHWA WITH HOOK
CH_PROP_LOWER, // 025B (Ll) LATIN SMALL LETTER OPEN E
CH_PROP_LOWER, // 025C (Ll) LATIN SMALL LETTER REVERSED OPEN E
CH_PROP_LOWER, // 025D (Ll) LATIN SMALL LETTER REVERSED OPEN E WITH HOOK
CH_PROP_LOWER, // 025E (Ll) LATIN SMALL LETTER CLOSED REVERSED OPEN E
CH_PROP_LOWER, // 025F (Ll) LATIN SMALL LETTER DOTLESS J WITH STROKE
CH_PROP_LOWER, // 0260 (Ll) LATIN SMALL LETTER G WITH HOOK
CH_PROP_LOWER, // 0261 (Ll) LATIN SMALL LETTER SCRIPT G
CH_PROP_LOWER, // 0262 (Ll) LATIN LETTER SMALL CAPITAL G
CH_PROP_LOWER, // 0263 (Ll) LATIN SMALL LETTER GAMMA
CH_PROP_LOWER, // 0264 (Ll) LATIN SMALL LETTER RAMS HORN
CH_PROP_LOWER, // 0265 (Ll) LATIN SMALL LETTER TURNED H
CH_PROP_LOWER, // 0266 (Ll) LATIN SMALL LETTER H WITH HOOK
CH_PROP_LOWER, // 0267 (Ll) LATIN SMALL LETTER HENG WITH HOOK
CH_PROP_LOWER, // 0268 (Ll) LATIN SMALL LETTER I WITH STROKE
CH_PROP_LOWER, // 0269 (Ll) LATIN SMALL LETTER IOTA
CH_PROP_LOWER, // 026A (Ll) LATIN LETTER SMALL CAPITAL I
CH_PROP_LOWER, // 026B (Ll) LATIN SMALL LETTER L WITH MIDDLE TILDE
CH_PROP_LOWER, // 026C (Ll) LATIN SMALL LETTER L WITH BELT
CH_PROP_LOWER, // 026D (Ll) LATIN SMALL LETTER L WITH RETROFLEX HOOK
CH_PROP_LOWER, // 026E (Ll) LATIN SMALL LETTER LEZH
CH_PROP_LOWER, // 026F (Ll) LATIN SMALL LETTER TURNED M
CH_PROP_LOWER, // 0270 (Ll) LATIN SMALL LETTER TURNED M WITH LONG LEG
CH_PROP_LOWER, // 0271 (Ll) LATIN SMALL LETTER M WITH HOOK
CH_PROP_LOWER, // 0272 (Ll) LATIN SMALL LETTER N WITH LEFT HOOK
CH_PROP_LOWER, // 0273 (Ll) LATIN SMALL LETTER N WITH RETROFLEX HOOK
CH_PROP_LOWER, // 0274 (Ll) LATIN LETTER SMALL CAPITAL N
CH_PROP_LOWER, // 0275 (Ll) LATIN SMALL LETTER BARRED O
CH_PROP_LOWER, // 0276 (Ll) LATIN LETTER SMALL CAPITAL OE
CH_PROP_LOWER, // 0277 (Ll) LATIN SMALL LETTER CLOSED OMEGA
CH_PROP_LOWER, // 0278 (Ll) LATIN SMALL LETTER PHI
CH_PROP_LOWER, // 0279 (Ll) LATIN SMALL LETTER TURNED R
CH_PROP_LOWER, // 027A (Ll) LATIN SMALL LETTER TURNED R WITH LONG LEG
CH_PROP_LOWER, // 027B (Ll) LATIN SMALL LETTER TURNED R WITH HOOK
CH_PROP_LOWER, // 027C (Ll) LATIN SMALL LETTER R WITH LONG LEG
CH_PROP_LOWER, // 027D (Ll) LATIN SMALL LETTER R WITH TAIL
CH_PROP_LOWER, // 027E (Ll) LATIN SMALL LETTER R WITH FISHHOOK
CH_PROP_LOWER, // 027F (Ll) LATIN SMALL LETTER REVERSED R WITH FISHHOOK
CH_PROP_LOWER, // 0280 (Ll) LATIN LETTER SMALL CAPITAL R
CH_PROP_LOWER, // 0281 (Ll) LATIN LETTER SMALL CAPITAL INVERTED R
CH_PROP_LOWER, // 0282 (Ll) LATIN SMALL LETTER S WITH HOOK
CH_PROP_LOWER, // 0283 (Ll) LATIN SMALL LETTER ESH
CH_PROP_LOWER, // 0284 (Ll) LATIN SMALL LETTER DOTLESS J WITH STROKE AND HOOK
CH_PROP_LOWER, // 0285 (Ll) LATIN SMALL LETTER SQUAT REVERSED ESH
CH_PROP_LOWER, // 0286 (Ll) LATIN SMALL LETTER ESH WITH CURL
CH_PROP_LOWER, // 0287 (Ll) LATIN SMALL LETTER TURNED T
CH_PROP_LOWER, // 0288 (Ll) LATIN SMALL LETTER T WITH RETROFLEX HOOK
CH_PROP_LOWER, // 0289 (Ll) LATIN SMALL LETTER U BAR
CH_PROP_LOWER, // 028A (Ll) LATIN SMALL LETTER UPSILON
CH_PROP_LOWER, // 028B (Ll) LATIN SMALL LETTER V WITH HOOK
CH_PROP_LOWER, // 028C (Ll) LATIN SMALL LETTER TURNED V
CH_PROP_LOWER, // 028D (Ll) LATIN SMALL LETTER TURNED W
CH_PROP_LOWER, // 028E (Ll) LATIN SMALL LETTER TURNED Y
CH_PROP_LOWER, // 028F (Ll) LATIN LETTER SMALL CAPITAL Y
CH_PROP_LOWER, // 0290 (Ll) LATIN SMALL LETTER Z WITH RETROFLEX HOOK
CH_PROP_LOWER, // 0291 (Ll) LATIN SMALL LETTER Z WITH CURL
CH_PROP_LOWER, // 0292 (Ll) LATIN SMALL LETTER EZH
CH_PROP_LOWER, // 0293 (Ll) LATIN SMALL LETTER EZH WITH CURL
CH_PROP_LOWER, // 0294 (Lo) LATIN LETTER GLOTTAL STOP
CH_PROP_LOWER, // 0295 (Ll) LATIN LETTER PHARYNGEAL VOICED FRICATIVE
CH_PROP_LOWER, // 0296 (Ll) LATIN LETTER INVERTED GLOTTAL STOP
CH_PROP_LOWER, // 0297 (Ll) LATIN LETTER STRETCHED C
CH_PROP_LOWER, // 0298 (Ll) LATIN LETTER BILABIAL CLICK
CH_PROP_LOWER, // 0299 (Ll) LATIN LETTER SMALL CAPITAL B
CH_PROP_LOWER, // 029A (Ll) LATIN SMALL LETTER CLOSED OPEN E
CH_PROP_LOWER, // 029B (Ll) LATIN LETTER SMALL CAPITAL G WITH HOOK
CH_PROP_LOWER, // 029C (Ll) LATIN LETTER SMALL CAPITAL H
CH_PROP_LOWER, // 029D (Ll) LATIN SMALL LETTER J WITH CROSSED-TAIL
CH_PROP_LOWER, // 029E (Ll) LATIN SMALL LETTER TURNED K
CH_PROP_LOWER, // 029F (Ll) LATIN LETTER SMALL CAPITAL L
CH_PROP_LOWER, // 02A0 (Ll) LATIN SMALL LETTER Q WITH HOOK
CH_PROP_LOWER, // 02A1 (Ll) LATIN LETTER GLOTTAL STOP WITH STROKE
CH_PROP_LOWER, // 02A2 (Ll) LATIN LETTER REVERSED GLOTTAL STOP WITH STROKE
CH_PROP_LOWER, // 02A3 (Ll) LATIN SMALL LETTER DZ DIGRAPH
CH_PROP_LOWER, // 02A4 (Ll) LATIN SMALL LETTER DEZH DIGRAPH
CH_PROP_LOWER, // 02A5 (Ll) LATIN SMALL LETTER DZ DIGRAPH WITH CURL
CH_PROP_LOWER, // 02A6 (Ll) LATIN SMALL LETTER TS DIGRAPH
CH_PROP_LOWER, // 02A7 (Ll) LATIN SMALL LETTER TESH DIGRAPH
CH_PROP_LOWER, // 02A8 (Ll) LATIN SMALL LETTER TC DIGRAPH WITH CURL
CH_PROP_LOWER, // 02A9 (Ll) LATIN SMALL LETTER FENG DIGRAPH
CH_PROP_LOWER, // 02AA (Ll) LATIN SMALL LETTER LS DIGRAPH
CH_PROP_LOWER, // 02AB (Ll) LATIN SMALL LETTER LZ DIGRAPH
CH_PROP_LOWER, // 02AC (Ll) LATIN LETTER BILABIAL PERCUSSIVE
CH_PROP_LOWER, // 02AD (Ll) LATIN LETTER BIDENTAL PERCUSSIVE
CH_PROP_LOWER, // 02AE (Ll) LATIN SMALL LETTER TURNED H WITH FISHHOOK
CH_PROP_LOWER, // 02AF (Ll) LATIN SMALL LETTER TURNED H WITH FISHHOOK AND TAIL
CH_PROP_LOWER, // 02B0 (Lm) MODIFIER LETTER SMALL H
CH_PROP_LOWER, // 02B1 (Lm) MODIFIER LETTER SMALL H WITH HOOK
CH_PROP_LOWER, // 02B2 (Lm) MODIFIER LETTER SMALL J
CH_PROP_LOWER, // 02B3 (Lm) MODIFIER LETTER SMALL R
CH_PROP_LOWER, // 02B4 (Lm) MODIFIER LETTER SMALL TURNED R
CH_PROP_LOWER, // 02B5 (Lm) MODIFIER LETTER SMALL TURNED R WITH HOOK
CH_PROP_LOWER, // 02B6 (Lm) MODIFIER LETTER SMALL CAPITAL INVERTED R
CH_PROP_LOWER, // 02B7 (Lm) MODIFIER LETTER SMALL W
CH_PROP_LOWER, // 02B8 (Lm) MODIFIER LETTER SMALL Y
CH_PROP_LOWER, // 02B9 (Lm) MODIFIER LETTER PRIME
CH_PROP_LOWER, // 02BA (Lm) MODIFIER LETTER DOUBLE PRIME
CH_PROP_LOWER, // 02BB (Lm) MODIFIER LETTER TURNED COMMA
CH_PROP_LOWER, // 02BC (Lm) MODIFIER LETTER APOSTROPHE
CH_PROP_LOWER, // 02BD (Lm) MODIFIER LETTER REVERSED COMMA
CH_PROP_LOWER, // 02BE (Lm) MODIFIER LETTER RIGHT HALF RING
CH_PROP_LOWER, // 02BF (Lm) MODIFIER LETTER LEFT HALF RING
CH_PROP_LOWER, // 02C0 (Lm) MODIFIER LETTER GLOTTAL STOP
CH_PROP_LOWER, // 02C1 (Lm) MODIFIER LETTER REVERSED GLOTTAL STOP
CH_PROP_MODIFIER, // 02C2 (Sk) MODIFIER LETTER LEFT ARROWHEAD
CH_PROP_MODIFIER, // 02C3 (Sk) MODIFIER LETTER RIGHT ARROWHEAD
CH_PROP_MODIFIER, // 02C4 (Sk) MODIFIER LETTER UP ARROWHEAD
CH_PROP_MODIFIER, // 02C5 (Sk) MODIFIER LETTER DOWN ARROWHEAD
CH_PROP_LOWER, // 02C6 (Lm) MODIFIER LETTER CIRCUMFLEX ACCENT
CH_PROP_LOWER, // 02C7 (Lm) CARON
CH_PROP_LOWER, // 02C8 (Lm) MODIFIER LETTER VERTICAL LINE
CH_PROP_LOWER, // 02C9 (Lm) MODIFIER LETTER MACRON
CH_PROP_LOWER, // 02CA (Lm) MODIFIER LETTER ACUTE ACCENT
CH_PROP_LOWER, // 02CB (Lm) MODIFIER LETTER GRAVE ACCENT
CH_PROP_LOWER, // 02CC (Lm) MODIFIER LETTER LOW VERTICAL LINE
CH_PROP_LOWER, // 02CD (Lm) MODIFIER LETTER LOW MACRON
CH_PROP_LOWER, // 02CE (Lm) MODIFIER LETTER LOW GRAVE ACCENT
CH_PROP_LOWER, // 02CF (Lm) MODIFIER LETTER LOW ACUTE ACCENT
CH_PROP_LOWER, // 02D0 (Lm) MODIFIER LETTER TRIANGULAR COLON
CH_PROP_LOWER, // 02D1 (Lm) MODIFIER LETTER HALF TRIANGULAR COLON
CH_PROP_MODIFIER, // 02D2 (Sk) MODIFIER LETTER CENTRED RIGHT HALF RING
CH_PROP_MODIFIER, // 02D3 (Sk) MODIFIER LETTER CENTRED LEFT HALF RING
CH_PROP_MODIFIER, // 02D4 (Sk) MODIFIER LETTER UP TACK
CH_PROP_MODIFIER, // 02D5 (Sk) MODIFIER LETTER DOWN TACK
CH_PROP_MODIFIER, // 02D6 (Sk) MODIFIER LETTER PLUS SIGN
CH_PROP_MODIFIER, // 02D7 (Sk) MODIFIER LETTER MINUS SIGN
CH_PROP_MODIFIER, // 02D8 (Sk) BREVE
CH_PROP_MODIFIER, // 02D9 (Sk) DOT ABOVE
CH_PROP_MODIFIER, // 02DA (Sk) RING ABOVE
CH_PROP_MODIFIER, // 02DB (Sk) OGONEK
CH_PROP_MODIFIER, // 02DC (Sk) SMALL TILDE
CH_PROP_MODIFIER, // 02DD (Sk) DOUBLE ACUTE ACCENT
CH_PROP_MODIFIER, // 02DE (Sk) MODIFIER LETTER RHOTIC HOOK
CH_PROP_MODIFIER, // 02DF (Sk) MODIFIER LETTER CROSS ACCENT
CH_PROP_LOWER, // 02E0 (Lm) MODIFIER LETTER SMALL GAMMA
CH_PROP_LOWER, // 02E1 (Lm) MODIFIER LETTER SMALL L
CH_PROP_LOWER, // 02E2 (Lm) MODIFIER LETTER SMALL S
CH_PROP_LOWER, // 02E3 (Lm) MODIFIER LETTER SMALL X
CH_PROP_LOWER, // 02E4 (Lm) MODIFIER LETTER SMALL REVERSED GLOTTAL STOP
CH_PROP_MODIFIER, // 02E5 (Sk) MODIFIER LETTER EXTRA-HIGH TONE BAR
CH_PROP_MODIFIER, // 02E6 (Sk) MODIFIER LETTER HIGH TONE BAR
CH_PROP_MODIFIER, // 02E7 (Sk) MODIFIER LETTER MID TONE BAR
CH_PROP_MODIFIER, // 02E8 (Sk) MODIFIER LETTER LOW TONE BAR
CH_PROP_MODIFIER, // 02E9 (Sk) MODIFIER LETTER EXTRA-LOW TONE BAR
CH_PROP_MODIFIER, // 02EA (Sk) MODIFIER LETTER YIN DEPARTING TONE MARK
CH_PROP_MODIFIER, // 02EB (Sk) MODIFIER LETTER YANG DEPARTING TONE MARK
CH_PROP_LOWER, // 02EC (Lm) MODIFIER LETTER VOICING
CH_PROP_MODIFIER, // 02ED (Sk) MODIFIER LETTER UNASPIRATED
CH_PROP_LOWER, // 02EE (Lm) MODIFIER LETTER DOUBLE APOSTROPHE
CH_PROP_MODIFIER, // 02EF (Sk) MODIFIER LETTER LOW DOWN ARROWHEAD
CH_PROP_MODIFIER, // 02F0 (Sk) MODIFIER LETTER LOW UP ARROWHEAD
CH_PROP_MODIFIER, // 02F1 (Sk) MODIFIER LETTER LOW LEFT ARROWHEAD
CH_PROP_MODIFIER, // 02F2 (Sk) MODIFIER LETTER LOW RIGHT ARROWHEAD
CH_PROP_MODIFIER, // 02F3 (Sk) MODIFIER LETTER LOW RING
CH_PROP_MODIFIER, // 02F4 (Sk) MODIFIER LETTER MIDDLE GRAVE ACCENT
CH_PROP_MODIFIER, // 02F5 (Sk) MODIFIER LETTER MIDDLE DOUBLE GRAVE ACCENT
CH_PROP_MODIFIER, // 02F6 (Sk) MODIFIER LETTER MIDDLE DOUBLE ACUTE ACCENT
CH_PROP_MODIFIER, // 02F7 (Sk) MODIFIER LETTER LOW TILDE
CH_PROP_MODIFIER, // 02F8 (Sk) MODIFIER LETTER RAISED COLON
CH_PROP_MODIFIER, // 02F9 (Sk) MODIFIER LETTER BEGIN HIGH TONE
CH_PROP_MODIFIER, // 02FA (Sk) MODIFIER LETTER END HIGH TONE
CH_PROP_MODIFIER, // 02FB (Sk) MODIFIER LETTER BEGIN LOW TONE
CH_PROP_MODIFIER, // 02FC (Sk) MODIFIER LETTER END LOW TONE
CH_PROP_MODIFIER, // 02FD (Sk) MODIFIER LETTER SHELF
CH_PROP_MODIFIER, // 02FE (Sk) MODIFIER LETTER OPEN SHELF
CH_PROP_MODIFIER, // 02FF (Sk) MODIFIER LETTER LOW LEFT ARROW
// 0x0300:
CH_PROP_MODIFIER, // 0300 (Mn) COMBINING GRAVE ACCENT
CH_PROP_MODIFIER, // 0301 (Mn) COMBINING ACUTE ACCENT
CH_PROP_MODIFIER, // 0302 (Mn) COMBINING CIRCUMFLEX ACCENT
CH_PROP_MODIFIER, // 0303 (Mn) COMBINING TILDE
CH_PROP_MODIFIER, // 0304 (Mn) COMBINING MACRON
CH_PROP_MODIFIER, // 0305 (Mn) COMBINING OVERLINE
CH_PROP_MODIFIER, // 0306 (Mn) COMBINING BREVE
CH_PROP_MODIFIER, // 0307 (Mn) COMBINING DOT ABOVE
CH_PROP_MODIFIER, // 0308 (Mn) COMBINING DIAERESIS
CH_PROP_MODIFIER, // 0309 (Mn) COMBINING HOOK ABOVE
CH_PROP_MODIFIER, // 030A (Mn) COMBINING RING ABOVE
CH_PROP_MODIFIER, // 030B (Mn) COMBINING DOUBLE ACUTE ACCENT
CH_PROP_MODIFIER, // 030C (Mn) COMBINING CARON
CH_PROP_MODIFIER, // 030D (Mn) COMBINING VERTICAL LINE ABOVE
CH_PROP_MODIFIER, // 030E (Mn) COMBINING DOUBLE VERTICAL LINE ABOVE
CH_PROP_MODIFIER, // 030F (Mn) COMBINING DOUBLE GRAVE ACCENT
CH_PROP_MODIFIER, // 0310 (Mn) COMBINING CANDRABINDU
CH_PROP_MODIFIER, // 0311 (Mn) COMBINING INVERTED BREVE
CH_PROP_MODIFIER, // 0312 (Mn) COMBINING TURNED COMMA ABOVE
CH_PROP_MODIFIER, // 0313 (Mn) COMBINING COMMA ABOVE
CH_PROP_MODIFIER, // 0314 (Mn) COMBINING REVERSED COMMA ABOVE
CH_PROP_MODIFIER, // 0315 (Mn) COMBINING COMMA ABOVE RIGHT
CH_PROP_MODIFIER, // 0316 (Mn) COMBINING GRAVE ACCENT BELOW
CH_PROP_MODIFIER, // 0317 (Mn) COMBINING ACUTE ACCENT BELOW
CH_PROP_MODIFIER, // 0318 (Mn) COMBINING LEFT TACK BELOW
CH_PROP_MODIFIER, // 0319 (Mn) COMBINING RIGHT TACK BELOW
CH_PROP_MODIFIER, // 031A (Mn) COMBINING LEFT ANGLE ABOVE
CH_PROP_MODIFIER, // 031B (Mn) COMBINING HORN
CH_PROP_MODIFIER, // 031C (Mn) COMBINING LEFT HALF RING BELOW
CH_PROP_MODIFIER, // 031D (Mn) COMBINING UP TACK BELOW
CH_PROP_MODIFIER, // 031E (Mn) COMBINING DOWN TACK BELOW
CH_PROP_MODIFIER, // 031F (Mn) COMBINING PLUS SIGN BELOW
CH_PROP_MODIFIER, // 0320 (Mn) COMBINING MINUS SIGN BELOW
CH_PROP_MODIFIER, // 0321 (Mn) COMBINING PALATALIZED HOOK BELOW
CH_PROP_MODIFIER, // 0322 (Mn) COMBINING RETROFLEX HOOK BELOW
CH_PROP_MODIFIER, // 0323 (Mn) COMBINING DOT BELOW
CH_PROP_MODIFIER, // 0324 (Mn) COMBINING DIAERESIS BELOW
CH_PROP_MODIFIER, // 0325 (Mn) COMBINING RING BELOW
CH_PROP_MODIFIER, // 0326 (Mn) COMBINING COMMA BELOW
CH_PROP_MODIFIER, // 0327 (Mn) COMBINING CEDILLA
CH_PROP_MODIFIER, // 0328 (Mn) COMBINING OGONEK
CH_PROP_MODIFIER, // 0329 (Mn) COMBINING VERTICAL LINE BELOW
CH_PROP_MODIFIER, // 032A (Mn) COMBINING BRIDGE BELOW
CH_PROP_MODIFIER, // 032B (Mn) COMBINING INVERTED DOUBLE ARCH BELOW
CH_PROP_MODIFIER, // 032C (Mn) COMBINING CARON BELOW
CH_PROP_MODIFIER, // 032D (Mn) COMBINING CIRCUMFLEX ACCENT BELOW
CH_PROP_MODIFIER, // 032E (Mn) COMBINING BREVE BELOW
CH_PROP_MODIFIER, // 032F (Mn) COMBINING INVERTED BREVE BELOW
CH_PROP_MODIFIER, // 0330 (Mn) COMBINING TILDE BELOW
CH_PROP_MODIFIER, // 0331 (Mn) COMBINING MACRON BELOW
CH_PROP_MODIFIER, // 0332 (Mn) COMBINING LOW LINE
CH_PROP_MODIFIER, // 0333 (Mn) COMBINING DOUBLE LOW LINE
CH_PROP_MODIFIER, // 0334 (Mn) COMBINING TILDE OVERLAY
CH_PROP_MODIFIER, // 0335 (Mn) COMBINING SHORT STROKE OVERLAY
CH_PROP_MODIFIER, // 0336 (Mn) COMBINING LONG STROKE OVERLAY
CH_PROP_MODIFIER, // 0337 (Mn) COMBINING SHORT SOLIDUS OVERLAY
CH_PROP_MODIFIER, // 0338 (Mn) COMBINING LONG SOLIDUS OVERLAY
CH_PROP_MODIFIER, // 0339 (Mn) COMBINING RIGHT HALF RING BELOW
CH_PROP_MODIFIER, // 033A (Mn) COMBINING INVERTED BRIDGE BELOW
CH_PROP_MODIFIER, // 033B (Mn) COMBINING SQUARE BELOW
CH_PROP_MODIFIER, // 033C (Mn) COMBINING SEAGULL BELOW
CH_PROP_MODIFIER, // 033D (Mn) COMBINING X ABOVE
CH_PROP_MODIFIER, // 033E (Mn) COMBINING VERTICAL TILDE
CH_PROP_MODIFIER, // 033F (Mn) COMBINING DOUBLE OVERLINE
CH_PROP_MODIFIER, // 0340 (Mn) COMBINING GRAVE TONE MARK
CH_PROP_MODIFIER, // 0341 (Mn) COMBINING ACUTE TONE MARK
CH_PROP_MODIFIER, // 0342 (Mn) COMBINING GREEK PERISPOMENI
CH_PROP_MODIFIER, // 0343 (Mn) COMBINING GREEK KORONIS
CH_PROP_MODIFIER, // 0344 (Mn) COMBINING GREEK DIALYTIKA TONOS
CH_PROP_MODIFIER, // 0345 (Mn) COMBINING GREEK YPOGEGRAMMENI
CH_PROP_MODIFIER, // 0346 (Mn) COMBINING BRIDGE ABOVE
CH_PROP_MODIFIER, // 0347 (Mn) COMBINING EQUALS SIGN BELOW
CH_PROP_MODIFIER, // 0348 (Mn) COMBINING DOUBLE VERTICAL LINE BELOW
CH_PROP_MODIFIER, // 0349 (Mn) COMBINING LEFT ANGLE BELOW
CH_PROP_MODIFIER, // 034A (Mn) COMBINING NOT TILDE ABOVE
CH_PROP_MODIFIER, // 034B (Mn) COMBINING HOMOTHETIC ABOVE
CH_PROP_MODIFIER, // 034C (Mn) COMBINING ALMOST EQUAL TO ABOVE
CH_PROP_MODIFIER, // 034D (Mn) COMBINING LEFT RIGHT ARROW BELOW
CH_PROP_MODIFIER, // 034E (Mn) COMBINING UPWARDS ARROW BELOW
CH_PROP_MODIFIER, // 034F (Mn) COMBINING GRAPHEME JOINER
CH_PROP_MODIFIER, // 0350 (Mn) COMBINING RIGHT ARROWHEAD ABOVE
CH_PROP_MODIFIER, // 0351 (Mn) COMBINING LEFT HALF RING ABOVE
CH_PROP_MODIFIER, // 0352 (Mn) COMBINING FERMATA
CH_PROP_MODIFIER, // 0353 (Mn) COMBINING X BELOW
CH_PROP_MODIFIER, // 0354 (Mn) COMBINING LEFT ARROWHEAD BELOW
CH_PROP_MODIFIER, // 0355 (Mn) COMBINING RIGHT ARROWHEAD BELOW
CH_PROP_MODIFIER, // 0356 (Mn) COMBINING RIGHT ARROWHEAD AND UP ARROWHEAD BELOW
CH_PROP_MODIFIER, // 0357 (Mn) COMBINING RIGHT HALF RING ABOVE
CH_PROP_MODIFIER, // 0358 (Mn) COMBINING DOT ABOVE RIGHT
CH_PROP_MODIFIER, // 0359 (Mn) COMBINING ASTERISK BELOW
CH_PROP_MODIFIER, // 035A (Mn) COMBINING DOUBLE RING BELOW
CH_PROP_MODIFIER, // 035B (Mn) COMBINING ZIGZAG ABOVE
CH_PROP_MODIFIER, // 035C (Mn) COMBINING DOUBLE BREVE BELOW
CH_PROP_MODIFIER, // 035D (Mn) COMBINING DOUBLE BREVE
CH_PROP_MODIFIER, // 035E (Mn) COMBINING DOUBLE MACRON
CH_PROP_MODIFIER, // 035F (Mn) COMBINING DOUBLE MACRON BELOW
CH_PROP_MODIFIER, // 0360 (Mn) COMBINING DOUBLE TILDE
CH_PROP_MODIFIER, // 0361 (Mn) COMBINING DOUBLE INVERTED BREVE
CH_PROP_MODIFIER, // 0362 (Mn) COMBINING DOUBLE RIGHTWARDS ARROW BELOW
CH_PROP_MODIFIER, // 0363 (Mn) COMBINING LATIN SMALL LETTER A
CH_PROP_MODIFIER, // 0364 (Mn) COMBINING LATIN SMALL LETTER E
CH_PROP_MODIFIER, // 0365 (Mn) COMBINING LATIN SMALL LETTER I
CH_PROP_MODIFIER, // 0366 (Mn) COMBINING LATIN SMALL LETTER O
CH_PROP_MODIFIER, // 0367 (Mn) COMBINING LATIN SMALL LETTER U
CH_PROP_MODIFIER, // 0368 (Mn) COMBINING LATIN SMALL LETTER C
CH_PROP_MODIFIER, // 0369 (Mn) COMBINING LATIN SMALL LETTER D
CH_PROP_MODIFIER, // 036A (Mn) COMBINING LATIN SMALL LETTER H
CH_PROP_MODIFIER, // 036B (Mn) COMBINING LATIN SMALL LETTER M
CH_PROP_MODIFIER, // 036C (Mn) COMBINING LATIN SMALL LETTER R
CH_PROP_MODIFIER, // 036D (Mn) COMBINING LATIN SMALL LETTER T
CH_PROP_MODIFIER, // 036E (Mn) COMBINING LATIN SMALL LETTER V
CH_PROP_MODIFIER, // 036F (Mn) COMBINING LATIN SMALL LETTER X
CH_PROP_UPPER, // 0370 (Lu) GREEK CAPITAL LETTER HETA
CH_PROP_LOWER, // 0371 (Ll) GREEK SMALL LETTER HETA
CH_PROP_UPPER, // 0372 (Lu) GREEK CAPITAL LETTER ARCHAIC SAMPI
CH_PROP_LOWER, // 0373 (Ll) GREEK SMALL LETTER ARCHAIC SAMPI
CH_PROP_LOWER, // 0374 (Lm) GREEK NUMERAL SIGN
CH_PROP_MODIFIER, // 0375 (Sk) GREEK LOWER NUMERAL SIGN
CH_PROP_UPPER, // 0376 (Lu) GREEK CAPITAL LETTER PAMPHYLIAN DIGAMMA
CH_PROP_LOWER, // 0377 (Ll) GREEK SMALL LETTER PAMPHYLIAN DIGAMMA
0            , // 0378 (Cn) n/a
0            , // 0379 (Cn) n/a
CH_PROP_LOWER, // 037A (Lm) GREEK YPOGEGRAMMENI
CH_PROP_LOWER, // 037B (Ll) GREEK SMALL REVERSED LUNATE SIGMA SYMBOL
CH_PROP_LOWER, // 037C (Ll) GREEK SMALL DOTTED LUNATE SIGMA SYMBOL
CH_PROP_LOWER, // 037D (Ll) GREEK SMALL REVERSED DOTTED LUNATE SIGMA SYMBOL
CH_PROP_PUNCT, // 037E (Po) GREEK QUESTION MARK
CH_PROP_UPPER, // 037F (Lu) GREEK CAPITAL LETTER YOT
// 0x0380:
0,0,0,0,
CH_PROP_MODIFIER, // 0384 (Sk) GREEK TONOS
CH_PROP_MODIFIER, // 0385 (Sk) GREEK DIALYTIKA TONOS
CH_PROP_UPPER | CH_PROP_VOWEL, //    GREEK CAPITAL LETTER ALPHA WITH TONOS 	0386
CH_PROP_PUNCT, //    GREEK ANO TELEIA 	0387
CH_PROP_UPPER | CH_PROP_VOWEL, //    GREEK CAPITAL LETTER EPSILON WITH TONOS 	0388
CH_PROP_UPPER | CH_PROP_VOWEL, //    GREEK CAPITAL LETTER ETA WITH TONOS 	0389
CH_PROP_UPPER | CH_PROP_VOWEL, //    GREEK CAPITAL LETTER IOTA WITH TONOS 	038A
0,//038b
CH_PROP_UPPER | CH_PROP_VOWEL, //    GREEK CAPITAL LETTER OMICRON WITH TONOS 	038C
0,//038d
CH_PROP_UPPER | CH_PROP_VOWEL, //    GREEK CAPITAL LETTER UPSILON WITH TONOS 	038E
CH_PROP_UPPER | CH_PROP_VOWEL, //    GREEK CAPITAL LETTER OMEGA WITH TONOS 	038F
// 0x0390:
CH_PROP_LOWER | CH_PROP_VOWEL, //    GREEK SMALL LETTER IOTA WITH DIALYTIKA AND TONOS 	0390
CH_PROP_UPPER | CH_PROP_VOWEL, //    GREEK CAPITAL LETTER ALPHA	Α	0391 	&Alpha;
CH_PROP_UPPER | CH_PROP_CONSONANT, //    GREEK CAPITAL LETTER BETA	0392 	&Beta;
CH_PROP_UPPER | CH_PROP_CONSONANT, //    GREEK CAPITAL LETTER GAMMA	0393 	&Gamma;
CH_PROP_UPPER | CH_PROP_CONSONANT, //    GREEK CAPITAL LETTER DELTA	0394 	&Delta;
CH_PROP_UPPER | CH_PROP_VOWEL, //    GREEK CAPITAL LETTER EPSILON	0395 	&Epsilon;
CH_PROP_UPPER | CH_PROP_CONSONANT, //    GREEK CAPITAL LETTER ZETA	0396 	&Zeta;
CH_PROP_UPPER | CH_PROP_VOWEL, //    GREEK CAPITAL LETTER ETA	0397 	&Eta;
CH_PROP_UPPER | CH_PROP_CONSONANT, //    GREEK CAPITAL LETTER THETA	0398 	&Theta;
CH_PROP_UPPER | CH_PROP_VOWEL, //    GREEK CAPITAL LETTER IOTA	0399 	&Iota;
CH_PROP_UPPER | CH_PROP_CONSONANT, //    GREEK CAPITAL LETTER KAPPA	039A 	&Kappa;
CH_PROP_UPPER | CH_PROP_CONSONANT, //    GREEK CAPITAL LETTER LAM(B)DA	039B 	&Lambda;
CH_PROP_UPPER | CH_PROP_CONSONANT, //    GREEK CAPITAL LETTER MU	039C 	&Mu;
CH_PROP_UPPER | CH_PROP_CONSONANT, //    GREEK CAPITAL LETTER NU	039D 	&Nu;
CH_PROP_UPPER | CH_PROP_CONSONANT, //    GREEK CAPITAL LETTER XI	039E 	&Xi;
CH_PROP_UPPER | CH_PROP_VOWEL, //    GREEK CAPITAL LETTER OMICRON	039F 	&Omicron;
CH_PROP_UPPER | CH_PROP_CONSONANT, //    GREEK CAPITAL LETTER PI	03A0 	&Pi;
CH_PROP_UPPER | CH_PROP_CONSONANT, //    GREEK CAPITAL LETTER RHO	03A1 	&Rho;
0, // 03a2
CH_PROP_UPPER | CH_PROP_CONSONANT, //    GREEK CAPITAL LETTER SIGMA	03A3 	&Sigma;
CH_PROP_UPPER | CH_PROP_CONSONANT, //    GREEK CAPITAL LETTER TAU	03A4 	&Tau;
CH_PROP_UPPER | CH_PROP_VOWEL, //    GREEK CAPITAL LETTER UPSILON	03A5 	&Upsilon;
CH_PROP_UPPER | CH_PROP_CONSONANT, //    GREEK CAPITAL LETTER PHI	03A6 	&Phi;
CH_PROP_UPPER | CH_PROP_CONSONANT, //    GREEK CAPITAL LETTER CHI	03A7 	&Chi;
CH_PROP_UPPER | CH_PROP_CONSONANT, //    GREEK CAPITAL LETTER PSI	03A8 	&Psi;
CH_PROP_UPPER | CH_PROP_VOWEL, //    GREEK CAPITAL LETTER OMEGA	03A9 	&Omega;
CH_PROP_UPPER | CH_PROP_VOWEL, //    GREEK CAPITAL LETTER IOTA WITH DIALYTIKA 	03AA
CH_PROP_UPPER | CH_PROP_VOWEL, //    GREEK CAPITAL LETTER UPSILON WITH DIALYTIKA 	03AB
CH_PROP_LOWER | CH_PROP_VOWEL, //    GREEK SMALL LETTER ALPHA WITH TONOS 	03AC
CH_PROP_LOWER | CH_PROP_VOWEL, //    GREEK SMALL LETTER EPSILON WITH TONOS 	03AD
CH_PROP_LOWER | CH_PROP_VOWEL, //    GREEK SMALL LETTER ETA WITH TONOS 	03AE
CH_PROP_LOWER | CH_PROP_VOWEL, //    GREEK SMALL LETTER IOTA WITH TONOS 	03AF
CH_PROP_LOWER | CH_PROP_VOWEL, //    GREEK SMALL LETTER UPSILON WITH DIALYTIKA AND TONOS 	03B0
CH_PROP_LOWER | CH_PROP_VOWEL, //    GREEK SMALL LETTER ALPHA   03B1 	&alpha;
CH_PROP_LOWER | CH_PROP_CONSONANT, //    GREEK SMALL LETTER BETA	03B2 	&beta;
CH_PROP_LOWER | CH_PROP_CONSONANT, //    GREEK SMALL LETTER GAMMA	03B3 	&gamma;
CH_PROP_LOWER | CH_PROP_CONSONANT, //    GREEK SMALL LETTER DELTA	03B4 	&delta;
CH_PROP_LOWER | CH_PROP_VOWEL, //    GREEK SMALL LETTER EPSILON	03B5 	&epsilon;
CH_PROP_LOWER | CH_PROP_CONSONANT, //    GREEK SMALL LETTER ZETA	03B6 	&zeta;
CH_PROP_LOWER | CH_PROP_VOWEL, //    GREEK SMALL LETTER ETA     03B7 	&eta;
CH_PROP_LOWER | CH_PROP_CONSONANT, //    GREEK SMALL LETTER THETA	03B8 	&theta;
CH_PROP_LOWER | CH_PROP_VOWEL, //    GREEK SMALL LETTER IOTA	03B9 	&iota;
CH_PROP_LOWER | CH_PROP_CONSONANT, //    GREEK SMALL LETTER KAPPA	03BA 	&kappa;
CH_PROP_LOWER | CH_PROP_CONSONANT, //    GREEK SMALL LETTER LAM(B)DA	03BB 	&lambda;
CH_PROP_LOWER | CH_PROP_CONSONANT, //    GREEK SMALL LETTER MU      03BC 	&mu;
CH_PROP_LOWER | CH_PROP_CONSONANT, //    GREEK SMALL LETTER NU      03BD 	&nu;
CH_PROP_LOWER | CH_PROP_CONSONANT, //    GREEK SMALL LETTER XI      03BE 	&xi;
CH_PROP_LOWER | CH_PROP_VOWEL, //    GREEK SMALL LETTER OMICRON	03BF 	&omicron;
CH_PROP_LOWER | CH_PROP_CONSONANT, //    GREEK SMALL LETTER PI      03C0 	&pi;
CH_PROP_LOWER | CH_PROP_CONSONANT, //    GREEK SMALL LETTER RHO     03C1 	&rho;
CH_PROP_LOWER | CH_PROP_CONSONANT, //    GREEK SMALL LETTER FINAL SIGMA	03C2
CH_PROP_LOWER | CH_PROP_CONSONANT, //    GREEK SMALL LETTER SIGMA	03C3 	&sigma;
CH_PROP_LOWER | CH_PROP_CONSONANT, //    GREEK SMALL LETTER TAU     03C4 	&tau;
CH_PROP_LOWER | CH_PROP_VOWEL, //    GREEK SMALL LETTER UPSILON	03C5 	&upsilon;
CH_PROP_LOWER | CH_PROP_CONSONANT, //    GREEK SMALL LETTER PHI     03C6 	&phi;
CH_PROP_LOWER | CH_PROP_CONSONANT, //    GREEK SMALL LETTER CHI     03C7 	&chi;
CH_PROP_LOWER | CH_PROP_CONSONANT, //    GREEK SMALL LETTER PSI     03C8 	&psi;
CH_PROP_LOWER | CH_PROP_VOWEL, //    GREEK SMALL LETTER OMEGA   03C9 	&omega;
CH_PROP_LOWER | CH_PROP_VOWEL, //    GREEK SMALL LETTER IOTA WITH DIALYTIKA 	03CA
CH_PROP_LOWER | CH_PROP_VOWEL, //    GREEK SMALL LETTER UPSILON WITH DIALYTIKA 	03CB
CH_PROP_LOWER | CH_PROP_VOWEL, //    GREEK SMALL LETTER OMICRON WITH TONOS 	03CC
CH_PROP_LOWER | CH_PROP_VOWEL, //    GREEK SMALL LETTER UPSILON WITH TONOS 	03CD
CH_PROP_LOWER | CH_PROP_VOWEL, //    GREEK SMALL LETTER OMEGA WITH TONOS 	03CE
CH_PROP_UPPER, // 03CF (Lu) GREEK CAPITAL KAI SYMBOL
CH_PROP_LOWER | CH_PROP_CONSONANT, // 03D0 (Ll) GREEK BETA SYMBOL
CH_PROP_LOWER | CH_PROP_CONSONANT, // 03D1 (Ll) GREEK THETA SYMBOL
CH_PROP_UPPER | CH_PROP_VOWEL, // 03D2 (Lu) GREEK UPSILON WITH HOOK SYMBOL
CH_PROP_UPPER | CH_PROP_VOWEL, // 03D3 (Lu) GREEK UPSILON WITH ACUTE AND HOOK SYMBOL
CH_PROP_UPPER | CH_PROP_VOWEL, // 03D4 (Lu) GREEK UPSILON WITH DIAERESIS AND HOOK SYMBOL
CH_PROP_LOWER | CH_PROP_CONSONANT, // 03D5 (Ll) GREEK PHI SYMBOL
CH_PROP_LOWER | CH_PROP_CONSONANT, // 03D6 (Ll) GREEK PI SYMBOL
CH_PROP_LOWER | CH_PROP_CONSONANT, // 03D7 (Ll) GREEK KAI SYMBOL
CH_PROP_UPPER, // 03D8 (Lu) GREEK LETTER ARCHAIC KOPPA
CH_PROP_LOWER, // 03D9 (Ll) GREEK SMALL LETTER ARCHAIC KOPPA
CH_PROP_UPPER | CH_PROP_CONSONANT, // 03DA (Lu) GREEK LETTER STIGMA
CH_PROP_LOWER | CH_PROP_CONSONANT, // 03DB (Ll) GREEK SMALL LETTER STIGMA
CH_PROP_UPPER | CH_PROP_CONSONANT, // 03DC (Lu) GREEK LETTER DIGAMMA
CH_PROP_LOWER | CH_PROP_CONSONANT, // 03DD (Ll) GREEK SMALL LETTER DIGAMMA
CH_PROP_UPPER | CH_PROP_CONSONANT, // 03DE (Lu) GREEK LETTER KOPPA
CH_PROP_LOWER | CH_PROP_CONSONANT, // 03DF (Ll) GREEK SMALL LETTER KOPPA
CH_PROP_UPPER | CH_PROP_CONSONANT, // 03E0 (Lu) GREEK LETTER SAMPI
CH_PROP_LOWER | CH_PROP_CONSONANT, // 03E1 (Ll) GREEK SMALL LETTER SAMPI
CH_PROP_UPPER, // 03E2 (Lu) COPTIC CAPITAL LETTER SHEI
CH_PROP_LOWER, // 03E3 (Ll) COPTIC SMALL LETTER SHEI
CH_PROP_UPPER, // 03E4 (Lu) COPTIC CAPITAL LETTER FEI
CH_PROP_LOWER, // 03E5 (Ll) COPTIC SMALL LETTER FEI
CH_PROP_UPPER, // 03E6 (Lu) COPTIC CAPITAL LETTER KHEI
CH_PROP_LOWER, // 03E7 (Ll) COPTIC SMALL LETTER KHEI
CH_PROP_UPPER, // 03E8 (Lu) COPTIC CAPITAL LETTER HORI
CH_PROP_LOWER, // 03E9 (Ll) COPTIC SMALL LETTER HORI
CH_PROP_UPPER, // 03EA (Lu) COPTIC CAPITAL LETTER GANGIA
CH_PROP_LOWER, // 03EB (Ll) COPTIC SMALL LETTER GANGIA
CH_PROP_UPPER, // 03EC (Lu) COPTIC CAPITAL LETTER SHIMA
CH_PROP_LOWER, // 03ED (Ll) COPTIC SMALL LETTER SHIMA
CH_PROP_UPPER, // 03EE (Lu) COPTIC CAPITAL LETTER DEI
CH_PROP_LOWER, // 03EF (Ll) COPTIC SMALL LETTER DEI
CH_PROP_LOWER, // 03F0 (Ll) GREEK KAPPA SYMBOL
CH_PROP_LOWER, // 03F1 (Ll) GREEK RHO SYMBOL
CH_PROP_LOWER, // 03F2 (Ll) GREEK LUNATE SIGMA SYMBOL
CH_PROP_LOWER, // 03F3 (Ll) GREEK LETTER YOT
CH_PROP_UPPER, // 03F4 (Lu) GREEK CAPITAL THETA SYMBOL
CH_PROP_LOWER, // 03F5 (Ll) GREEK LUNATE EPSILON SYMBOL
CH_PROP_SIGN,  // 03F6 (Sm) GREEK REVERSED LUNATE EPSILON SYMBOL
CH_PROP_UPPER, // 03F7 (Lu) GREEK CAPITAL LETTER SHO
CH_PROP_LOWER, // 03F8 (Ll) GREEK SMALL LETTER SHO
CH_PROP_UPPER, // 03F9 (Lu) GREEK CAPITAL LUNATE SIGMA SYMBOL
CH_PROP_UPPER, // 03FA (Lu) GREEK CAPITAL LETTER SAN
CH_PROP_LOWER, // 03FB (Ll) GREEK SMALL LETTER SAN
CH_PROP_LOWER, // 03FC (Ll) GREEK RHO WITH STROKE SYMBOL
CH_PROP_UPPER, // 03FD (Lu) GREEK CAPITAL REVERSED LUNATE SIGMA SYMBOL
CH_PROP_UPPER, // 03FE (Lu) GREEK CAPITAL DOTTED LUNATE SIGMA SYMBOL
CH_PROP_UPPER, // 03FF (Lu) GREEK CAPITAL REVERSED DOTTED LUNATE SIGMA SYMBOL

// 0x0400:
CH_PROP_UPPER, // 0400 (Lu) CYRILLIC CAPITAL LETTER IE WITH GRAVE
CH_PROP_UPPER | CH_PROP_VOWEL,      // 0401 cyrillic E:
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0402 cyrillic Dje
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0403 cyrillic Gje
CH_PROP_UPPER | CH_PROP_VOWEL,      // 0404 cyrillic ukr Ie
CH_PROP_UPPER | CH_PROP_CONSONANT,  // 0405 cyrillic Dze
CH_PROP_UPPER | CH_PROP_VOWEL,      // 0406 cyrillic ukr I
CH_PROP_UPPER | CH_PROP_VOWEL,      // 0407 cyrillic ukr I:
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0408 cyrillic J
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0409 cyrillic L'
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 040A cyrillic N'
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 040B cyrillic Th
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 040C cyrillic K'
CH_PROP_UPPER, // 040D (Lu) CYRILLIC CAPITAL LETTER I WITH GRAVE
CH_PROP_UPPER | CH_PROP_VOWEL,      // 040E cyrillic Yu
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 040F cyrillic Dzhe
// 0x0410:
CH_PROP_UPPER | CH_PROP_VOWEL,      // 0410 cyrillic A
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0411 cyrillic B
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0412 cyrillic V
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0413 cyrillic G
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0414 cyrillic D
CH_PROP_UPPER | CH_PROP_VOWEL,      // 0415 cyrillic E
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0416 cyrillic Zh
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0417 cyrillic Z
CH_PROP_UPPER | CH_PROP_VOWEL,      // 0418 cyrillic I
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0419 cyrillic YI
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 041A cyrillic K
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 041B cyrillic L
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 041C cyrillic M
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 041D cyrillic N
CH_PROP_UPPER | CH_PROP_VOWEL,      // 041E cyrillic O
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 041F cyrillic P
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0420 cyrillic R
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0421 cyrillic S
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0422 cyrillic T
CH_PROP_UPPER | CH_PROP_VOWEL,      // 0423 cyrillic U
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0424 cyrillic F
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0425 cyrillic H
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0426 cyrillic C
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0427 cyrillic Ch
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0428 cyrillic Sh
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0429 cyrillic Sch
CH_PROP_UPPER | CH_PROP_ALPHA_SIGN,      // 042A cyrillic Hard sign
CH_PROP_UPPER | CH_PROP_VOWEL,      // 042B cyrillic Y
CH_PROP_UPPER | CH_PROP_ALPHA_SIGN,      // 042C cyrillic Soft sign
CH_PROP_UPPER | CH_PROP_VOWEL,      // 042D cyrillic EE
CH_PROP_UPPER | CH_PROP_VOWEL,      // 042E cyrillic Yu
CH_PROP_UPPER | CH_PROP_VOWEL,      // 042F cyrillic Ya
// 0x0430:
CH_PROP_LOWER | CH_PROP_VOWEL,      // 0430 cyrillic A
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0431 cyrillic B
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0432 cyrillic V
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0433 cyrillic G
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0434 cyrillic D
CH_PROP_LOWER | CH_PROP_VOWEL,      // 0435 cyrillic E
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0436 cyrillic Zh
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0437 cyrillic Z
CH_PROP_LOWER | CH_PROP_VOWEL,      // 0438 cyrillic I
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0439 cyrillic YI
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 043A cyrillic K
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 043B cyrillic L
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 043C cyrillic M
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 043D cyrillic N
CH_PROP_LOWER | CH_PROP_VOWEL,      // 043E cyrillic O
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 043F cyrillic P
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0440 cyrillic R
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0441 cyrillic S
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0442 cyrillic T
CH_PROP_LOWER | CH_PROP_VOWEL,      // 0443 cyrillic U
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0444 cyrillic F
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0445 cyrillic H
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0446 cyrillic C
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0447 cyrillic Ch
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0448 cyrillic Sh
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0449 cyrillic Sch
CH_PROP_LOWER | CH_PROP_ALPHA_SIGN,     // 044A cyrillic Hard sign
CH_PROP_LOWER | CH_PROP_VOWEL,      // 044B cyrillic Y
CH_PROP_LOWER | CH_PROP_ALPHA_SIGN,     // 044C cyrillic Soft sign
CH_PROP_LOWER | CH_PROP_VOWEL,      // 044D cyrillic EE
CH_PROP_LOWER | CH_PROP_VOWEL,      // 044E cyrillic Yu
CH_PROP_LOWER | CH_PROP_VOWEL,      // 044F cyrillic Ya
CH_PROP_LOWER, // 0450 (Ll) CYRILLIC SMALL LETTER IE WITH GRAVE
CH_PROP_LOWER | CH_PROP_VOWEL,      // 0451 cyrillic e:
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0452 cyrillic Dje
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0453 cyrillic Gje
CH_PROP_LOWER | CH_PROP_VOWEL,      // 0454 cyrillic ukr Ie
CH_PROP_LOWER | CH_PROP_CONSONANT,  // 0455 cyrillic Dze
CH_PROP_LOWER | CH_PROP_VOWEL,      // 0456 cyrillic ukr I
CH_PROP_LOWER | CH_PROP_VOWEL,      // 0457 cyrillic ukr I:
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0458 cyrillic J
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0459 cyrillic L'
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 045A cyrillic N'
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 045B cyrillic Th
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 045C cyrillic K'
CH_PROP_LOWER, // 045D (Ll) CYRILLIC SMALL LETTER I WITH GRAVE
CH_PROP_LOWER | CH_PROP_VOWEL,      // 045E cyrillic Yu
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 045F cyrillic Dzhe
// 0x0460:
CH_PROP_UPPER, // 0460 (Lu) CYRILLIC CAPITAL LETTER OMEGA
CH_PROP_LOWER, // 0461 (Ll) CYRILLIC SMALL LETTER OMEGA
CH_PROP_UPPER, // 0462 (Lu) CYRILLIC CAPITAL LETTER YAT
CH_PROP_LOWER, // 0463 (Ll) CYRILLIC SMALL LETTER YAT
CH_PROP_UPPER, // 0464 (Lu) CYRILLIC CAPITAL LETTER IOTIFIED E
CH_PROP_LOWER, // 0465 (Ll) CYRILLIC SMALL LETTER IOTIFIED E
CH_PROP_UPPER, // 0466 (Lu) CYRILLIC CAPITAL LETTER LITTLE YUS
CH_PROP_LOWER, // 0467 (Ll) CYRILLIC SMALL LETTER LITTLE YUS
CH_PROP_UPPER, // 0468 (Lu) CYRILLIC CAPITAL LETTER IOTIFIED LITTLE YUS
CH_PROP_LOWER, // 0469 (Ll) CYRILLIC SMALL LETTER IOTIFIED LITTLE YUS
CH_PROP_UPPER, // 046A (Lu) CYRILLIC CAPITAL LETTER BIG YUS
CH_PROP_LOWER, // 046B (Ll) CYRILLIC SMALL LETTER BIG YUS
CH_PROP_UPPER, // 046C (Lu) CYRILLIC CAPITAL LETTER IOTIFIED BIG YUS
CH_PROP_LOWER, // 046D (Ll) CYRILLIC SMALL LETTER IOTIFIED BIG YUS
CH_PROP_UPPER, // 046E (Lu) CYRILLIC CAPITAL LETTER KSI
CH_PROP_LOWER, // 046F (Ll) CYRILLIC SMALL LETTER KSI
CH_PROP_UPPER, // 0470 (Lu) CYRILLIC CAPITAL LETTER PSI
CH_PROP_LOWER, // 0471 (Ll) CYRILLIC SMALL LETTER PSI
CH_PROP_UPPER, // 0472 (Lu) CYRILLIC CAPITAL LETTER FITA
CH_PROP_LOWER, // 0473 (Ll) CYRILLIC SMALL LETTER FITA
CH_PROP_UPPER, // 0474 (Lu) CYRILLIC CAPITAL LETTER IZHITSA
CH_PROP_LOWER, // 0475 (Ll) CYRILLIC SMALL LETTER IZHITSA
CH_PROP_UPPER, // 0476 (Lu) CYRILLIC CAPITAL LETTER IZHITSA WITH DOUBLE GRAVE ACCENT
CH_PROP_LOWER, // 0477 (Ll) CYRILLIC SMALL LETTER IZHITSA WITH DOUBLE GRAVE ACCENT
CH_PROP_UPPER, // 0478 (Lu) CYRILLIC CAPITAL LETTER UK
CH_PROP_LOWER, // 0479 (Ll) CYRILLIC SMALL LETTER UK
CH_PROP_UPPER, // 047A (Lu) CYRILLIC CAPITAL LETTER ROUND OMEGA
CH_PROP_LOWER, // 047B (Ll) CYRILLIC SMALL LETTER ROUND OMEGA
CH_PROP_UPPER, // 047C (Lu) CYRILLIC CAPITAL LETTER OMEGA WITH TITLO
CH_PROP_LOWER, // 047D (Ll) CYRILLIC SMALL LETTER OMEGA WITH TITLO
CH_PROP_UPPER, // 047E (Lu) CYRILLIC CAPITAL LETTER OT
CH_PROP_LOWER, // 047F (Ll) CYRILLIC SMALL LETTER OT
CH_PROP_UPPER, // 0480 (Lu) CYRILLIC CAPITAL LETTER KOPPA
CH_PROP_LOWER, // 0481 (Ll) CYRILLIC SMALL LETTER KOPPA
CH_PROP_SIGN,  // 0482 (So) CYRILLIC THOUSANDS SIGN
CH_PROP_MODIFIER, // 0483 (Mn) COMBINING CYRILLIC TITLO
CH_PROP_MODIFIER, // 0484 (Mn) COMBINING CYRILLIC PALATALIZATION
CH_PROP_MODIFIER, // 0485 (Mn) COMBINING CYRILLIC DASIA PNEUMATA
CH_PROP_MODIFIER, // 0486 (Mn) COMBINING CYRILLIC PSILI PNEUMATA
CH_PROP_MODIFIER, // 0487 (Mn) COMBINING CYRILLIC POKRYTIE
CH_PROP_MODIFIER, // 0488 (Me) COMBINING CYRILLIC HUNDRED THOUSANDS SIGN
CH_PROP_MODIFIER, // 0489 (Me) COMBINING CYRILLIC MILLIONS SIGN
CH_PROP_UPPER, // 048A (Lu) CYRILLIC CAPITAL LETTER SHORT I WITH TAIL
CH_PROP_LOWER, // 048B (Ll) CYRILLIC SMALL LETTER SHORT I WITH TAIL
CH_PROP_UPPER, // 048C (Lu) CYRILLIC CAPITAL LETTER SEMISOFT SIGN
CH_PROP_LOWER, // 048D (Ll) CYRILLIC SMALL LETTER SEMISOFT SIGN
CH_PROP_UPPER, // 048E (Lu) CYRILLIC CAPITAL LETTER ER WITH TICK
CH_PROP_LOWER, // 048F (Ll) CYRILLIC SMALL LETTER ER WITH TICK
// 0x0490:
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0490 cyrillic G'
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0491 cyrillic g'
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0492 cyrillic G-
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0493 cyrillic g-
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0494 (Lu) CYRILLIC CAPITAL LETTER GHE WITH MIDDLE HOOK
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0495 (Ll) CYRILLIC SMALL LETTER GHE WITH MIDDLE HOOK
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0496 cyrillic Zh,
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0497 cyrillic zh,
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 0498 (Lu) CYRILLIC CAPITAL LETTER ZE WITH DESCENDER
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 0499 (Ll) CYRILLIC SMALL LETTER ZE WITH DESCENDER
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 049A cyrillic K,
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 049B cyrillic k,
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 049C cyrillic K|
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 049D cyrillic k|
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 049E (Lu) CYRILLIC CAPITAL LETTER KA WITH STROKE
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 049F (Ll) CYRILLIC SMALL LETTER KA WITH STROKE
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 04A0 (Lu) CYRILLIC CAPITAL LETTER BASHKIR KA
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 04A1 (Ll) CYRILLIC SMALL LETTER BASHKIR KA
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 04A2 cyrillic H,
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 04A3 cyrillic n,
CH_PROP_UPPER, // 04A4 (Lu) CYRILLIC CAPITAL LIGATURE EN GHE
CH_PROP_LOWER, // 04A5 (Ll) CYRILLIC SMALL LIGATURE EN GHE
CH_PROP_UPPER, // 04A6 (Lu) CYRILLIC CAPITAL LETTER PE WITH MIDDLE HOOK
CH_PROP_LOWER, // 04A7 (Ll) CYRILLIC SMALL LETTER PE WITH MIDDLE HOOK
CH_PROP_UPPER, // 04A8 (Lu) CYRILLIC CAPITAL LETTER ABKHASIAN HA
CH_PROP_LOWER, // 04A9 (Ll) CYRILLIC SMALL LETTER ABKHASIAN HA
CH_PROP_UPPER, // 04AA (Lu) CYRILLIC CAPITAL LETTER ES WITH DESCENDER
CH_PROP_LOWER, // 04AB (Ll) CYRILLIC SMALL LETTER ES WITH DESCENDER
CH_PROP_UPPER, // 04AC (Lu) CYRILLIC CAPITAL LETTER TE WITH DESCENDER
CH_PROP_LOWER, // 04AD (Ll) CYRILLIC SMALL LETTER TE WITH DESCENDER
CH_PROP_UPPER | CH_PROP_VOWEL,      // 04AE cyrillic Y
CH_PROP_LOWER | CH_PROP_VOWEL,      // 04AF cyrillic y
CH_PROP_UPPER | CH_PROP_VOWEL,      // 04B0 cyrillic Y-
CH_PROP_LOWER | CH_PROP_VOWEL,      // 04B1 cyrillic y-
CH_PROP_UPPER | CH_PROP_CONSONANT,      // 04B2 cyrillic X,
CH_PROP_LOWER | CH_PROP_CONSONANT,      // 04B3 cyrillic x,
};


static lUInt16 char_props_1f00[] = {
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH PSILI 1F00
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH DASIA 1F01
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH PSILI AND VARIA 1F02
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH DASIA AND VARIA 1F03
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH PSILI AND OXIA 1F04
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH DASIA AND OXIA 1F05
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH PSILI AND PERISPOMENI 1F06
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH DASIA AND PERISPOMENI 1F07
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH PSILI 1F08
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH DASIA 1F09
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH PSILI AND VARIA 1F0A
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH DASIA AND VARIA 1F0B
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH PSILI AND OXIA 1F0C
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH DASIA AND OXIA 1F0D
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH PSILI AND PERISPOMENI 1F0E
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH DASIA AND PERISPOMENI 1F0F
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER EPSILON WITH PSILI 1F10
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER EPSILON WITH DASIA 1F11
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER EPSILON WITH PSILI AND VARIA 1F12
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER EPSILON WITH DASIA AND VARIA 1F13
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER EPSILON WITH PSILI AND OXIA 1F14
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER EPSILON WITH DASIA AND OXIA 1F15
0, 0,
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER EPSILON WITH PSILI 1F18
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER EPSILON WITH DASIA 1F19
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER EPSILON WITH PSILI AND VARIA 1F1A
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER EPSILON WITH DASIA AND VARIA 1F1B
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER EPSILON WITH PSILI AND OXIA 1F1C
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER EPSILON WITH DASIA AND OXIA 1F1D
0, 0,
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH PSILI 1F20
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH DASIA 1F21
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH PSILI AND VARIA 1F22
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH DASIA AND VARIA 1F23
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH PSILI AND OXIA 1F24
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH DASIA AND OXIA 1F25
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH PSILI AND PERISPOMENI 1F26
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH DASIA AND PERISPOMENI 1F27
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ETA WITH PSILI 1F28
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ETA WITH DASIA 1F29
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ETA WITH PSILI AND VARIA 1F2A
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ETA WITH DASIA AND VARIA 1F2B
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ETA WITH PSILI AND OXIA 1F2C
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ETA WITH DASIA AND OXIA 1F2D
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ETA WITH PSILI AND PERISPOMENI 1F2E
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ETA WITH DASIA AND PERISPOMENI 1F2F
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER IOTA WITH PSILI 1F30
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER IOTA WITH DASIA 1F31
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER IOTA WITH PSILI AND VARIA 1F32
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER IOTA WITH DASIA AND VARIA 1F33
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER IOTA WITH PSILI AND OXIA 1F34
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER IOTA WITH DASIA AND OXIA 1F35
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER IOTA WITH PSILI AND PERISPOMENI 1F36
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER IOTA WITH DASIA AND PERISPOMENI 1F37
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER IOTA WITH PSILI 1F38
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER IOTA WITH DASIA 1F39
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER IOTA WITH PSILI AND VARIA 1F3A
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER IOTA WITH DASIA AND VARIA 1F3B
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER IOTA WITH PSILI AND OXIA 1F3C
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER IOTA WITH DASIA AND OXIA 1F3D
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER IOTA WITH PSILI AND PERISPOMENI 1F3E
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER IOTA WITH DASIA AND PERISPOMENI 1F3F
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMICRON WITH PSILI 1F40
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMICRON WITH DASIA 1F41
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMICRON WITH PSILI AND VARIA 1F42
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMICRON WITH DASIA AND VARIA 1F43
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMICRON WITH PSILI AND OXIA 1F44
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMICRON WITH DASIA AND OXIA 1F45
0, 0,
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMICRON WITH PSILI 1F48
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMICRON WITH DASIA 1F49
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMICRON WITH PSILI AND VARIA 1F4A
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMICRON WITH DASIA AND VARIA 1F4B
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMICRON WITH PSILI AND OXIA 1F4C
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMICRON WITH DASIA AND OXIA 1F4D
0, 0,
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER UPSILON WITH PSILI 1F50
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER UPSILON WITH DASIA 1F51
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER UPSILON WITH PSILI AND VARIA 1F52
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER UPSILON WITH DASIA AND VARIA 1F53
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER UPSILON WITH PSILI AND OXIA 1F54
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER UPSILON WITH DASIA AND OXIA 1F55
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER UPSILON WITH PSILI AND PERISPOMENI 1F56
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER UPSILON WITH DASIA AND PERISPOMENI 1F57
0,
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER UPSILON WITH DASIA 1F59
0,
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER UPSILON WITH DASIA AND VARIA 1F5B
0,
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER UPSILON WITH DASIA AND OXIA 1F5D
0,
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER UPSILON WITH DASIA AND PERISPOMENI 1F5F
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH PSILI 1F60
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH DASIA 1F61
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH PSILI AND VARIA 1F62
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH DASIA AND VARIA 1F63
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH PSILI AND OXIA 1F64
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH DASIA AND OXIA 1F65
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH PSILI AND PERISPOMENI 1F66
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH DASIA AND PERISPOMENI 1F67
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMEGA WITH PSILI 1F68
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMEGA WITH PSILI 1F69
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMEGA WITH PSILI 1F6A
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMEGA WITH DASIA AND VARIA 1F6B
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMEGA WITH PSILI AND OXIA 1F6C
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMEGA WITH DASIA AND OXIA 1F6D
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMEGA WITH PSILI AND PERISPOMENI 1F6E
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMEGA WITH DASIA AND PERISPOMENI 1F6F
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH VARIA 1F70
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH OXIA 1F71
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER EPSILON WITH VARIA 1F72
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER EPSILON WITH OXIA 1F73
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH VARIA 1F74
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH OXIA 1F75
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER IOTA WITH VARIA 1F76
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER IOTA WITH OXIA 1F77
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMICRON WITH VARIA 1F78
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMICRON WITH OXIA 1F79
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER UPSILON WITH VARIA 1F7A
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER UPSILON WITH OXIA 1F7B
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH VARIA 1F7C
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH OXIA 1F7D
0, 0,
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH PSILI AND YPOGEGRAMMENI 1F80
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH DASIA AND YPOGEGRAMMENI 1F81
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH PSILI AND VARIA AND YPOGEGRAMMENI 1F82
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH DASIA AND VARIA AND YPOGEGRAMMENI 1F83
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH PSILI AND OXIA AND YPOGEGRAMMENI 1F84
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH DASIA AND OXIA AND YPOGEGRAMMENI 1F85
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH PSILI AND PERISPOMENI AND YPOGEGRAMMENI 1F86
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH DASIA AND PERISPOMENI AND YPOGEGRAMMENI 1F87
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH PSILI AND PROSGEGRAMMENI 1F88
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH DASIA AND PROSGEGRAMMENI 1F89
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH PSILI AND VARIA AND PROSGEGRAMMENI 1F8A
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH DASIA AND VARIA AND PROSGEGRAMMENI 1F8B
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH PSILI AND OXIA AND PROSGEGRAMMENI 1F8C
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH DASIA AND OXIA AND PROSGEGRAMMENI 1F8D
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH PSILI AND PERISPOMENI AND PROSGEGRAMMENI 1F8E
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH DASIA AND PERISPOMENI AND PROSGEGRAMMENI 1F8F
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH PSILI AND YPOGEGRAMMENI 1F90
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH DASIA AND YPOGEGRAMMENI 1F91
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH PSILI AND VARIA AND YPOGEGRAMMENI 1F92
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH DASIA AND VARIA AND YPOGEGRAMMENI 1F93
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH PSILI AND OXIA AND YPOGEGRAMMENI 1F94
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH DASIA AND OXIA AND YPOGEGRAMMENI 1F95
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH PSILI AND PERISPOMENI AND YPOGEGRAMMENI 1F96
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH DASIA AND PERISPOMENI AND YPOGEGRAMMENI 1F97
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ETA WITH PSILI AND PROSGEGRAMMENI 1F98
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ETA WITH DASIA AND PROSGEGRAMMENI 1F99
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ETA WITH PSILI AND VARIA AND PROSGEGRAMMENI 1F9A
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ETA WITH DASIA AND VARIA AND PROSGEGRAMMENI 1F9B
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ETA WITH PSILI AND OXIA AND PROSGEGRAMMENI 1F9C
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ETA WITH DASIA AND OXIA AND PROSGEGRAMMENI 1F9D
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ETA WITH PSILI AND PERISPOMENI AND PROSGEGRAMMENI 1F9E
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ETA WITH DASIA AND PERISPOMENI AND PROSGEGRAMMENI 1F9F
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH PSILI AND YPOGEGRAMMENI 1FA0
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH DASIA AND YPOGEGRAMMENI 1FA1
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH PSILI AND VARIA AND YPOGEGRAMMENI 1FA2
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH DASIA AND VARIA AND YPOGEGRAMMENI 1FA3
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH PSILI AND OXIA AND YPOGEGRAMMENI 1FA4
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH DASIA AND OXIA AND YPOGEGRAMMENI 1FA5
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH PSILI AND PERISPOMENI AND YPOGEGRAMMENI 1FA6
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH DASIA AND PERISPOMENI AND YPOGEGRAMMENI 1FA7
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMEGA WITH PSILI AND PROSGEGRAMMENI 1FA8
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMEGA WITH DASIA AND PROSGEGRAMMENI 1FA9
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMEGA WITH PSILI AND VARIA AND PROSGEGRAMMENI 1FAA
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMEGA WITH DASIA AND VARIA AND PROSGEGRAMMENI 1FAB
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMEGA WITH PSILI AND OXIA AND PROSGEGRAMMENI 1FAC
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMEGA WITH DASIA AND OXIA AND PROSGEGRAMMENI 1FAD
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMEGA WITH PSILI AND PERISPOMENI AND PROSGEGRAMMENI 1FAE
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMEGA WITH DASIA AND PERISPOMENI AND PROSGEGRAMMENI 1FAF
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH VRACHY 1FB0
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH MACRON 1FB1
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH VARIA AND YPOGEGRAMMENI 1FB2
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH YPOGEGRAMMENI 1FB3
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH OXIA AND YPOGEGRAMMENI 1FB4
0,
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH PERISPOMENI 1FB6
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ALPHA WITH PERISPOMENI AND YPOGEGRAMMENI 1FB7
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH VRACHY 1FB8
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH MACRON 1FB9
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH VARIA 1FBA
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH OXIA 1FBB
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ALPHA WITH PROSGEGRAMMENI 1FBC
CH_PROP_MODIFIER, // 1FBD (Sk) GREEK KORONIS
CH_PROP_LOWER,    // 1FBE (Ll) GREEK PROSGEGRAMMENI
CH_PROP_MODIFIER, // 1FBF (Sk) GREEK PSILI
CH_PROP_MODIFIER, // 1FC0 (Sk) GREEK PERISPOMENI
CH_PROP_MODIFIER, // 1FC1 (Sk) GREEK DIALYTIKA AND PERISPOMENI
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH VARIA AND YPOGEGRAMMENI 1FC2
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH YPOGEGRAMMENI 1FC3
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH OXIA AND YPOGEGRAMMENI 1FC4
0,
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH PERISPOMENI 1FC6
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER ETA WITH PERISPOMENI AND YPOGEGRAMMENI 1FC7
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER EPSILON WITH VARIA 1FC8
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER EPSILON WITH OXIA 1FC9
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ETA WITH VARIA 1FCA
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ETA WITH OXIA 1FCB
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER ETA WITH PROSGEGRAMMENI 1FCC
CH_PROP_MODIFIER, // 1FCD (Sk) GREEK PSILI AND VARIA
CH_PROP_MODIFIER, // 1FCE (Sk) GREEK PSILI AND OXIA
CH_PROP_MODIFIER, // 1FCF (Sk) GREEK PSILI AND PERISPOMENI
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER IOTA WITH VRACHY 1FD0
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER IOTA WITH MACRON 1FD1
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER IOTA WITH DIALYTIKA AND VARIA 1FD2
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER IOTA WITH DIALYTIKA AND OXIA 1FD3
0, 0,
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER IOTA WITH PERISPOMENI 1FD6
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER IOTA WITH DIALYTIKA AND PERISPOMENI 1FD7
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER IOTA WITH VRACHY 1FD8
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER IOTA WITH MACRON 1FD9
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER IOTA WITH VARIA 1FDA
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER IOTA WITH OXIA 1FDB
0,
CH_PROP_MODIFIER, // 1FDD (Sk) GREEK DASIA AND VARIA
CH_PROP_MODIFIER, // 1FDE (Sk) GREEK DASIA AND OXIA
CH_PROP_MODIFIER, // 1FDF (Sk) GREEK DASIA AND PERISPOMENI
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER UPSILON WITH VRACHY 1FE0
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER UPSILON WITH MACRON 1FE1
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER UPSILON WITH DIALYTIKA AND VARIA 1FE2
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER UPSILON WITH DIALYTIKA AND OXIA 1FE3
CH_PROP_LOWER | CH_PROP_CONSONANT, // GREEK SMALL LETTER RHO WITH PSILI 1FE4
CH_PROP_LOWER | CH_PROP_CONSONANT, // GREEK SMALL LETTER RHO WITH DASIA 1FE5
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER UPSILON WITH PERISPOMENI 1FE6
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER UPSILON WITH DIALYTIKA AND PERISPOMENI 1FE7
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER UPSILON WITH VRACHY 1FE8
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER UPSILON WITH MACRON 1FE9
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER UPSILON WITH VARIA 1FEA
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER UPSILON WITH OXIA 1FEB
CH_PROP_UPPER | CH_PROP_CONSONANT, // GREEK CAPITAL LETTER RHO WITH DASIA 1FEC
CH_PROP_MODIFIER, // 1FED (Sk) GREEK DIALYTIKA AND VARIA
CH_PROP_MODIFIER, // 1FEE (Sk) GREEK DIALYTIKA AND OXIA
CH_PROP_MODIFIER, // 1FEF (Sk) GREEK VARIA
0, 0,
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH VARIA AND YPOGEGRAMMENI 1FF2
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH YPOGEGRAMMENI 1FF3
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH OXIA AND YPOGEGRAMMENI 1FF4
0,
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH PERISPOMENI 1FF6
CH_PROP_LOWER | CH_PROP_VOWEL, // GREEK SMALL LETTER OMEGA WITH PERISPOMENI AND YPOGEGRAMMENI 1FF7
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMICRON WITH VARIA 1FF8
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMICRON WITH OXIA 1FF9
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMEGA WITH VARIA 1FFA
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMEGA WITH OXIA 1FFB
CH_PROP_UPPER | CH_PROP_VOWEL, // GREEK CAPITAL LETTER OMEGA WITH PROSGEGRAMMENI 1FFC
CH_PROP_MODIFIER, // 1FFD (Sk) GREEK OXIA
CH_PROP_MODIFIER, // 1FFE (Sk) GREEK DASIA
0
};

inline lUInt16 getCharProp(lChar32 ch) {
    // For the Ascii/Latin/Greek/Cyrillic unicode early ranges, use our hardcoded
    // handcrafted (but mostly consistent with Unicode) char props arrays above
    static const lChar32 maxchar = sizeof(char_props) / sizeof( lUInt16 );
    if (ch<maxchar)
        return char_props[ch];
    else if ((ch>>8) == 0x1F)
        return char_props_1f00[ch & 255];

#if (USE_LIBUNIBREAK!=1)
    else if (ch==0x201C) // left double quotation mark (Unicode Pi)
        return CH_PROP_PUNCT_OPEN | CH_PROP_AVOID_WRAP_AFTER;
    else if (ch==0x201D) // right double quotation mark (Unicode Pf)
        return CH_PROP_PUNCT_CLOSE | CH_PROP_AVOID_WRAP_BEFORE;
#endif

    // Try to guess a few other things about other chars we don't handle above
    lUInt16 prop;
#if (USE_UTF8PROC==1)
    // For other less known ranges, fallback to detecting letters with utf8proc,
    // which is enough to be able to ensure hyphenation for Armenian and Georgian.
    utf8proc_category_t cat = utf8proc_category(ch);
    switch (cat) {
        case UTF8PROC_CATEGORY_LU: // Uppercase Letter
        case UTF8PROC_CATEGORY_LT: // Titlecase Letter (ligatures containing uppercase followed by lowercase letters)
            prop = CH_PROP_UPPER;
            break;
        case UTF8PROC_CATEGORY_LL: // Lowercase Letter
        case UTF8PROC_CATEGORY_LM: // Modifier Letter (diacritics, consider them all as letters, assuming they follow a letter)
        case UTF8PROC_CATEGORY_LO: // Other Letter (Hebrew, Arabic, Devanagari...)
            prop = CH_PROP_LOWER;
            break;
        case UTF8PROC_CATEGORY_ND: // Decimal number (includes Arabic, Bengali... digits)
        case UTF8PROC_CATEGORY_NL: // Letter number (includes roman numeral...)
        case UTF8PROC_CATEGORY_NO: // Other number (includes superscript 2,3, fractions 1/2...)
            prop = CH_PROP_DIGIT;
            break;
        case UTF8PROC_CATEGORY_MN: // Nonspacing mark (many combining diacritic like U+0300 "Combining grave accent" and friends)
        case UTF8PROC_CATEGORY_ME: // Enclosing mark (few symbols like enclosing combining circle)
        case UTF8PROC_CATEGORY_MC: // Spacing mark (things like "Devanagari Vowel Sign Ooe", which sounds like a letter)
        case UTF8PROC_CATEGORY_SK: // Modifier symbol (feels like non-combining, but includes some accents and breathings)
                                   // that might be considered part of a word)
            prop = CH_PROP_MODIFIER;
            break;
        case UTF8PROC_CATEGORY_SC: // Symbol, currency (dollar, pound...)
        case UTF8PROC_CATEGORY_SO: // Symbol, other (bar, degree, (c), (r)...)
        case UTF8PROC_CATEGORY_SM: // Symbol, math (+<>=|, some arrows)
            prop = CH_PROP_SIGN;
            break;
        case UTF8PROC_CATEGORY_PC: // Punctuation connectors (underscore...)
        case UTF8PROC_CATEGORY_PD: // Punctuation dashes
        case UTF8PROC_CATEGORY_PO: // Punctuation others (!"#.,;:/*@...)
            prop = CH_PROP_PUNCT;
            break;
        case UTF8PROC_CATEGORY_PS: // Punctuation starting (left parenthesis, brackets...)
        case UTF8PROC_CATEGORY_PI: // Punctuation initial quote (left quotation marks...)
            prop = CH_PROP_PUNCT_OPEN;
            break;
        case UTF8PROC_CATEGORY_PE: // Punctuation ending (right parenthesis, brackets...)
        case UTF8PROC_CATEGORY_PF: // Punctuation final quote (right quotation marks...)
            prop = CH_PROP_PUNCT_CLOSE;
            break;
        case UTF8PROC_CATEGORY_ZS: // Separator, space (different size spaces)
        case UTF8PROC_CATEGORY_ZL: // Separator, line (only U+2028)
        case UTF8PROC_CATEGORY_ZP: // Separator, paragraph (only U+2029)
            prop = CH_PROP_SPACE;
            break;
        case UTF8PROC_CATEGORY_CC: // Control code
        case UTF8PROC_CATEGORY_CF: // Formatting (bidi, ZWNJ...)
        case UTF8PROC_CATEGORY_CO: // Private use
        case UTF8PROC_CATEGORY_CS: // Surrogate (should have been converted)
        case UTF8PROC_CATEGORY_CN: // Not assigned
        default:
            prop = 0;
            break;
    }
#else
    // Flag known ranges of punctuations (some chars in there might not be)
    if ( ch >= 0x2000 && ch <= 0x200B ) { // General Punctuation: spaces
        prop = CH_PROP_SPACE;
    }
    else if ( ch >= 0x2010 && ch <= 0x2027 ) { // General Punctuation: punctuations
        prop = CH_PROP_PUNCT;
    }
    else if ( ch >= 0x2030 && ch <= 0x206F ) { // General Punctuation: punctuations
        prop = CH_PROP_PUNCT;
    }
    else if ( ch >= 0x3000 && ch <= 0x303F ) { // CJK Symbols and Punctuation
        prop = CH_PROP_PUNCT;
    }
    else if ( ch >= 0xFF01 && ch <= 0xFFEE ) { // Halfwidth and Fullwidth Forms
        prop = CH_PROP_PUNCT; // This is obviously wrong, but keeping this legacy choice
    }
    // Other punctuation
    else if (ch == 0x0387 ) { // GREEK ANO TELEIA
        prop = CH_PROP_PUNCT;
    }
    // Some others spaces (from https://www.cs.tut.fi/~jkorpela/chars/spaces.html)
    else if (ch == 0x1680 ) { // OGHAM SPACE MARK
        prop = CH_PROP_SPACE;
    }
    else if (ch == 0x180E ) { // MONGOLIAN VOWEL SEPARATOR
        prop = CH_PROP_SPACE;
    }
    else if (ch == 0xFEFF ) { // ZERO WIDTH NO-BREAK SPACE
        prop = CH_PROP_SPACE;
    }
    else {
        // Consider all others as letters (so, not word separators)
        prop = CH_PROP_LOWER;
    }
#endif
    return prop;
}

void lStr_getCharProps( const lChar32 * str, int sz, lUInt16 * props )
{
    for ( int i=0; i<sz; i++ ) {
        lChar32 ch = str[i];
        props[i] = getCharProp(ch);
    }
}

bool lStr_isWordSeparator( lChar32 ch )
{
    // The meaning of "word separator" is ambiguous.
    // For this, spaces, punctuations and signs/symbols are considered
    // word separators (that is, they cut a sequence of alphanum into
    // two words). This is fine for use by lStr_capitalize().
    lUInt16 props = getCharProp(ch);
    return !(props & (CH_PROP_ALPHA|CH_PROP_MODIFIER|CH_PROP_HYPHEN|CH_PROP_DIGIT));
}

/// find alpha sequence bounds
void lStr_findWordBounds( const lChar32 * str, int sz, int pos, int & start, int & end, bool & has_rtl )
{
    // 20180615: don't split anymore on UNICODE_SOFT_HYPHEN_CODE, consider
    // it like an alpha char of zero width not drawn.
    // Only hyphenation code will care about it
    // We don't use lStr_isWordSeparator() here, but we exclusively look
    // for ALPHA chars or soft-hyphens, as this function is and should
    // only be used before calling hyphenate() to find a real word to
    // give to the hyphenation algorithms.

    has_rtl = false;

    // Check if alpha at pos, or find one on its left
    // (Note: previously, this was looking from pos-1 down to 1, for no
    // obvious reason. Now, we look from pos down to 0.)
    int cur = pos;
    for (; cur>=0; cur--) {
        lUInt16 props = getCharProp(str[cur]);
        if ( props & (CH_PROP_ALPHA|CH_PROP_HYPHEN) && !lStr_isCJK(str[cur]) )
            break;
    }
    if ( cur<0 ) { // no alpha found at pos or on its left
        start = end = pos;
        return;
    }
    pos = cur; // We'll start looking on the right from there

    // Find the furthest alpha on the left
    int first = cur;
    cur--;
    for (; cur>=0; cur--) {
        lChar32 ch = str[cur];
        lUInt16 props = getCharProp(ch);
        if ( props & (CH_PROP_ALPHA|CH_PROP_HYPHEN) && !lStr_isCJK(ch) ) {
            first = cur;
            if ( !has_rtl && lStr_isRTL(ch) ) {
                has_rtl = true;
            }
        }
        else if ( props & CH_PROP_MODIFIER ) {
            // depends on preceeding non-modifier char: don't break
        }
        else { // not an alpha/modifier
            break;
        }
    }
    start = first; // returned 'start' in inclusive

    // Find the furthest alpha (or modifiers following an alpha) on the right
    for (cur=pos+1; cur<sz; cur++) {
        lUInt16 props = getCharProp(str[cur]);
        if ( !(props & (CH_PROP_ALPHA|CH_PROP_HYPHEN|CH_PROP_MODIFIER)) || lStr_isCJK(str[cur]) )
            break;
    }
    end = cur; // returned 'end' is exclusive
    //CRLog::debug("Word bounds: '%s'", LCSTR(lString32(str+start, end-start)));
}

void  lString32::limit( size_type sz )
{
    if ( length() > sz ) {
        modify();
        pchunk->len = sz;
        pchunk->buf32[sz] = 0;
    }
}

lUInt16 lGetCharProps( lChar32 ch )
{
    return getCharProp(ch);
}



CRLog * CRLog::CRLOG = NULL;
void CRLog::setLogger( CRLog * logger )
{
    if ( CRLOG!=NULL ) {
        delete CRLOG;
    }
    CRLOG = logger;
}

void CRLog::setLogLevel( CRLog::log_level level )
{
    if ( !CRLOG )
        return;
    warn( "Changing log level from %d to %d", (int)CRLOG->curr_level, (int)level );
    CRLOG->curr_level = level;
}

CRLog::log_level CRLog::getLogLevel()
{
    if ( !CRLOG )
        return LL_INFO;
    return CRLOG->curr_level;
}

bool CRLog::isLogLevelEnabled( CRLog::log_level level )
{
    if ( !CRLOG )
        return false;
    return (CRLOG->curr_level >= level);
}

void CRLog::fatal( const char * msg, ... )
{
    if ( !CRLOG )
        return;
    va_list args;
    va_start( args, msg );
    CRLOG->log( "FATAL", msg, args );
    va_end(args);
}

void CRLog::error( const char * msg, ... )
{
    if ( !CRLOG || CRLOG->curr_level<LL_ERROR )
        return;
    va_list args;
    va_start( args, msg );
    CRLOG->log( "ERROR", msg, args );
    va_end(args);
}

void CRLog::warn( const char * msg, ... )
{
    if ( !CRLOG || CRLOG->curr_level<LL_WARN )
        return;
    va_list args;
    va_start( args, msg );
    CRLOG->log( "WARN", msg, args );
    va_end(args);
}

void CRLog::info( const char * msg, ... )
{
    if ( !CRLOG || CRLOG->curr_level<LL_INFO )
        return;
    va_list args;
    va_start( args, msg );
    CRLOG->log( "INFO", msg, args );
    va_end(args);
}

void CRLog::debug( const char * msg, ... )
{
    if ( !CRLOG || CRLOG->curr_level<LL_DEBUG )
        return;
    va_list args;
    va_start( args, msg );
    CRLOG->log( "DEBUG", msg, args );
    va_end(args);
}

void CRLog::trace( const char * msg, ... )
{
    if ( !CRLOG || CRLOG->curr_level<LL_TRACE )
        return;
    va_list args;
    va_start( args, msg );
    CRLOG->log( "TRACE", msg, args );
    va_end(args);
}

CRLog::CRLog()
    : curr_level(LL_INFO)
{
}

CRLog::~CRLog()
{
}

#ifndef LOG_HEAP_USAGE
#define LOG_HEAP_USAGE 0
#endif

class CRFileLogger : public CRLog
{
protected:
    FILE * f;
    bool autoClose;
    bool autoFlush;
    virtual void log( const char * level, const char * msg, va_list args )
    {
        if ( !f )
            return;
#ifdef LINUX
        struct timeval tval;
        gettimeofday( &tval, NULL );
        int ms = tval.tv_usec;
        time_t t = tval.tv_sec;
#if LOG_HEAP_USAGE
        struct mallinfo mi = mallinfo();
        int memusage = mi.arena;
#endif
#else
        lUInt64 ts = GetCurrentTimeMillis();
        //time_t t = (time_t)time(0);
        time_t t = ts / 1000;
        int ms = (ts % 1000) * 1000;
#if LOG_HEAP_USAGE
        int memusage = 0;
#endif
#endif
        tm * bt = localtime(&t);
#if LOG_HEAP_USAGE
        fprintf(f, "%04d/%02d/%02d %02d:%02d:%02d.%04d [%d] %s ", bt->tm_year+1900, bt->tm_mon+1, bt->tm_mday, bt->tm_hour, bt->tm_min, bt->tm_sec, ms/100, memusage, level);
#else
        fprintf(f, "%04d/%02d/%02d %02d:%02d:%02d.%04d %s ", bt->tm_year+1900, bt->tm_mon+1, bt->tm_mday, bt->tm_hour, bt->tm_min, bt->tm_sec, ms/100, level);
#endif
        vfprintf( f, msg, args );
        fprintf(f, "\n" );
        if ( autoFlush )
            fflush( f );
    }
public:
    CRFileLogger( FILE * file, bool _autoClose, bool _autoFlush )
    : f(file), autoClose(_autoClose), autoFlush( _autoFlush )
    {
        info( "Started logging" );
    }

    CRFileLogger( const char * fname, bool _autoFlush )
    : f(fopen( fname, "wt" STDIO_CLOEXEC )), autoClose(true), autoFlush( _autoFlush )
    {
        static unsigned char utf8sign[] = {0xEF, 0xBB, 0xBF};
        static const char * log_level_names[] = {
        "FATAL",
        "ERROR",
        "WARN",
        "INFO",
        "DEBUG",
        "TRACE",
        };
        fwrite( utf8sign, 3, 1, f);
        info( "Started logging. Level=%s", log_level_names[getLogLevel()] );
    }

    virtual ~CRFileLogger() {
        if ( f && autoClose ) {
            info( "Stopped logging" );
            fclose( f );
        }
        f = NULL;
    }
};

void CRLog::setFileLogger( const char * fname, bool autoFlush )
{
    setLogger( new CRFileLogger( fname, autoFlush ) );
}

void CRLog::setStdoutLogger()
{
    setLogger( new CRFileLogger( (FILE*)stdout, false, true ) );
}

void CRLog::setStderrLogger()
{
    setLogger( new CRFileLogger( (FILE*)stderr, false, true ) );
}

/// returns true if string starts with specified substring, case insensitive
bool lString32::startsWithNoCase ( const lString32 & substring ) const
{
    lString32 a = *this;
    lString32 b = substring;
    a.uppercase();
    b.uppercase();
    return a.startsWith( b );
}

/// returns true if string starts with specified substring
bool lString8::startsWith( const char * substring ) const
{
    if (!substring || !substring[0])
        return true;
    int len = (int)strlen(substring);
    if (length() < len)
        return false;
    const lChar8 * s1 = c_str();
    const lChar8 * s2 = substring;
    for (int i=0; i<len; i++ )
        if ( s1[i] != s2[i] )
            return false;
    return true;
}

/// returns true if string starts with specified substring
bool lString8::startsWith( const lString8 & substring ) const
{
    if ( substring.empty() )
        return true;
    int len = substring.length();
    if (length() < len)
        return false;
    const lChar8 * s1 = c_str();
    const lChar8 * s2 = substring.c_str();
    for (int i=0; i<len; i++ )
        if ( s1[i] != s2[i] )
            return false;
    return true;
}

/// returns true if string ends with specified substring
bool lString8::endsWith( const lChar8 * substring ) const
{
	if ( !substring || !*substring )
		return true;
    int len = (int)strlen(substring);
    if ( length() < len )
        return false;
    const lChar8 * s1 = c_str() + (length()-len);
    const lChar8 * s2 = substring;
	return lStr_cmp( s1, s2 )==0;
}

/// returns true if string ends with specified substring
bool lString32::endsWith( const lChar32 * substring ) const
{
	if ( !substring || !*substring )
		return true;
    int len = lStr_len(substring);
    if ( length() < len )
        return false;
    const lChar32 * s1 = c_str() + (length()-len);
    const lChar32 * s2 = substring;
	return lStr_cmp( s1, s2 )==0;
}

/// returns true if string ends with specified substring
bool lString32::endsWith( const lChar8 * substring ) const
{
    if ( !substring || !*substring )
        return true;
    int len = lStr_len(substring);
    if ( length() < len )
        return false;
    const lChar32 * s1 = c_str() + (length()-len);
    const lChar8 * s2 = substring;
    return lStr_cmp( s1, s2 )==0;
}

/// returns true if string ends with specified substring
bool lString32::endsWith ( const lString32 & substring ) const
{
    if ( substring.empty() )
        return true;
    int len = substring.length();
    if ( length() < len )
        return false;
    const lChar32 * s1 = c_str() + (length()-len);
    const lChar32 * s2 = substring.c_str();
	return lStr_cmp( s1, s2 )==0;
}

/// returns true if string starts with specified substring
bool lString32::startsWith( const lString32 & substring ) const
{
    if ( substring.empty() )
        return true;
    int len = substring.length();
    if ( length() < len )
        return false;
    const lChar32 * s1 = c_str();
    const lChar32 * s2 = substring.c_str();
    for ( int i=0; i<len; i++ )
        if ( s1[i]!=s2[i] )
            return false;
    return true;
}

/// returns true if string starts with specified substring
bool lString32::startsWith(const lChar32 * substring) const
{
    if (!substring || !substring[0])
        return true;
    int len = _lStr_len(substring);
    if ( length() < len )
        return false;
    const lChar32 * s1 = c_str();
    const lChar32 * s2 = substring;
    for ( int i=0; i<len; i++ )
        if ( s1[i] != s2[i] )
            return false;
    return true;
}

/// returns true if string starts with specified substring
bool lString32::startsWith(const lChar8 * substring) const
{
    if (!substring || !substring[0])
        return true;
    int len = _lStr_len(substring);
    if ( length() < len )
        return false;
    const lChar32 * s1 = c_str();
    const lChar8 * s2 = substring;
    for ( int i=0; i<len; i++ )
        if (s1[i] != (lChar32)s2[i])
            return false;
    return true;
}



/// serialization/deserialization buffer

/// constructor of serialization buffer
SerialBuf::SerialBuf( int sz, bool autoresize )
	: _buf( (lUInt8*)malloc(sz) ), _ownbuf(true), _error(false), _autoresize(autoresize), _size(sz), _pos(0)
{
    memset( _buf, 0, _size );
}
/// constructor of deserialization buffer
SerialBuf::SerialBuf( const lUInt8 * p, int sz )
	: _buf( const_cast<lUInt8 *>(p) ), _ownbuf(false), _error(false), _autoresize(false), _size(sz), _pos(0)
{
}

SerialBuf::~SerialBuf()
{
	if ( _ownbuf )
		free( _buf );
}

bool SerialBuf::copyTo( lUInt8 * buf, int maxSize )
{
    if ( _pos==0 )
        return true;
    if ( _pos > maxSize )
        return false;
    memcpy( buf, _buf, _pos );
    return true;
}

/// checks whether specified number of bytes is available, returns true in case of error
bool SerialBuf::check( int reserved )
{
	if ( _error )
		return true;
	if ( space()<reserved ) {
        if ( _autoresize ) {
            _size = (_size>16384 ? _size*2 : 16384) + reserved;
            _buf = cr_realloc(_buf, _size );
            memset( _buf+_pos, 0, _size-_pos );
            return false;
        } else {
		    _error = true;
		    return true;
        }
	}
	return false;
}

// write methods
/// put magic signature
void SerialBuf::putMagic( const char * s )
{
	if ( check(1) )
		return;
	while ( *s ) {
		_buf[ _pos++ ] = *s++;
		if ( check(1) )
			return;
	}
}

#define SWAPVARS(t,a) \
{ \
  t tmp; \
  tmp = a; a = v.a; v.a = tmp; \
}
void SerialBuf::swap( SerialBuf & v )
{
    SWAPVARS(lUInt8 *, _buf)
    SWAPVARS(bool, _ownbuf)
    SWAPVARS(bool, _error)
    SWAPVARS(bool, _autoresize)
    SWAPVARS(int, _size)
    SWAPVARS(int, _pos)
}


/// add contents of another buffer
SerialBuf & SerialBuf::operator << ( const SerialBuf & v )
{
    if ( check(v.pos()) || v.pos()==0 )
		return *this;
    memcpy( _buf + _pos, v._buf, v._pos );
    _pos += v._pos;
	return *this;
}

SerialBuf & SerialBuf::operator << ( lUInt8 n )
{
	if ( check(1) )
		return *this;
	_buf[_pos++] = n;
	return *this;
}
SerialBuf & SerialBuf::operator << ( char n )
{
	if ( check(1) )
		return *this;
	_buf[_pos++] = (lUInt8)n;
	return *this;
}
SerialBuf & SerialBuf::operator << ( bool n )
{
	if ( check(1) )
		return *this;
	_buf[_pos++] = (lUInt8)(n ? 1 : 0);
	return *this;
}
SerialBuf & SerialBuf::operator << ( lUInt16 n )
{
	if ( check(2) )
		return *this;
	_buf[_pos++] = (lUInt8)(n & 255);
	_buf[_pos++] = (lUInt8)((n>>8) & 255);
	return *this;
}
SerialBuf & SerialBuf::operator << ( lInt16 n )
{
	if ( check(2) )
		return *this;
	_buf[_pos++] = (lUInt8)(n & 255);
	_buf[_pos++] = (lUInt8)((n>>8) & 255);
	return *this;
}
SerialBuf & SerialBuf::operator << ( lUInt32 n )
{
	if ( check(4) )
		return *this;
	_buf[_pos++] = (lUInt8)(n & 255);
	_buf[_pos++] = (lUInt8)((n>>8) & 255);
	_buf[_pos++] = (lUInt8)((n>>16) & 255);
	_buf[_pos++] = (lUInt8)((n>>24) & 255);
	return *this;
}
SerialBuf & SerialBuf::operator << ( lInt32 n )
{
	if ( check(4) )
		return *this;
	_buf[_pos++] = (lUInt8)(n & 255);
	_buf[_pos++] = (lUInt8)((n>>8) & 255);
	_buf[_pos++] = (lUInt8)((n>>16) & 255);
	_buf[_pos++] = (lUInt8)((n>>24) & 255);
	return *this;
}
SerialBuf & SerialBuf::operator << ( const lString32 & s )
{
	if ( check(2) )
		return *this;
	lString8 s8 = UnicodeToUtf8(s);
	lUInt32 len = (lUInt32)s8.length();
	(*this) << len;
	for ( unsigned i=0; i<len; i++ ) {
		if ( check(1) )
			return *this;
		(*this) << (lUInt8)(s8[i]);
	}
	return *this;
}
SerialBuf & SerialBuf::operator << ( const lString8 & s8 )
{
	if ( check(2) )
		return *this;
	lUInt32 len = (lUInt32)s8.length();
	(*this) << len;
	for ( unsigned i=0; i<len; i++ ) {
		if ( check(1) )
			return *this;
		(*this) << (lUInt8)(s8[i]);
	}
	return *this;
}

SerialBuf & SerialBuf::operator >> ( lUInt8 & n )
{
	if ( check(1) )
		return *this;
	n = _buf[_pos++];
	return *this;
}

SerialBuf & SerialBuf::operator >> ( char & n )
{
	if ( check(1) )
		return *this;
	n = (char)_buf[_pos++];
	return *this;
}

SerialBuf & SerialBuf::operator >> ( bool & n )
{
	if ( check(1) )
		return *this;
    n = _buf[_pos++] ? true : false;
	return *this;
}

SerialBuf & SerialBuf::operator >> ( lUInt16 & n )
{
	if ( check(2) )
		return *this;
	n = _buf[_pos++];
    n |= (((lUInt16)_buf[_pos++]) << 8);
	return *this;
}

SerialBuf & SerialBuf::operator >> ( lInt16 & n )
{
	if ( check(2) )
		return *this;
	n = (lInt16)(_buf[_pos++]);
    n |= (lInt16)(((lUInt16)_buf[_pos++]) << 8);
	return *this;
}

SerialBuf & SerialBuf::operator >> ( lUInt32 & n )
{
	if ( check(4) )
		return *this;
	n = _buf[_pos++];
    n |= (((lUInt32)_buf[_pos++]) << 8);
    n |= (((lUInt32)_buf[_pos++]) << 16);
    n |= (((lUInt32)_buf[_pos++]) << 24);
	return *this;
}

SerialBuf & SerialBuf::operator >> ( lInt32 & n )
{
	if ( check(4) )
		return *this;
	n = (lInt32)(_buf[_pos++]);
    n |= (((lUInt32)_buf[_pos++]) << 8);
    n |= (((lUInt32)_buf[_pos++]) << 16);
    n |= (((lUInt32)_buf[_pos++]) << 24);
	return *this;
}

SerialBuf & SerialBuf::operator >> ( lString8 & s8 )
{
	if ( check(2) )
		return *this;
	lUInt32 len = 0;
	(*this) >> len;
	s8.clear();
	s8.reserve(len);
	for ( unsigned i=0; i<len; i++ ) {
		if ( check(1) )
			return *this;
        lUInt8 c = 0;
		(*this) >> c;
		s8.append(1, c);
	}
	return *this;
}

SerialBuf & SerialBuf::operator >> ( lString32 & s )
{
	lString8 s8;
	(*this) >> s8;
	s = Utf8ToUnicode(s8);
	return *this;
}

// read methods
bool SerialBuf::checkMagic( const char * s )
{
    if ( _error )
        return false;
	while ( *s ) {
		if ( check(1) )
			return false;
        if ( _buf[ _pos++ ] != *s++ ) {
            seterror();
			return false;
        }
	}
	return true;
}

bool lString32::split2( const lString32 & delim, lString32 & value1, lString32 & value2 )
{
    if ( empty() )
        return false;
    int p = pos(delim);
    if ( p<=0 || p>=length()-delim.length() )
        return false;
    value1 = substr(0, p);
    value2 = substr(p+delim.length());
    return true;
}

bool lString32::split2( const lChar32 * delim, lString32 & value1, lString32 & value2 )
{
    if (empty())
        return false;
    int p = pos(delim);
    int l = lStr_len(delim);
    if (p<=0 || p >= length() - l)
        return false;
    value1 = substr(0, p);
    value2 = substr(p + l);
    return true;
}

bool lString32::split2( const lChar8 * delim, lString32 & value1, lString32 & value2 )
{
    if (empty())
        return false;
    int p = pos(delim);
    int l = lStr_len(delim);
    if (p<=0 || p >= length() - l)
        return false;
    value1 = substr(0, p);
    value2 = substr(p + l);
    return true;
}

bool splitIntegerList( lString32 s, lString32 delim, int &value1, int &value2 )
{
    if ( s.empty() )
        return false;
    lString32 s1, s2;
    if ( !s.split2( delim, s1, s2 ) )
        return false;
    int n1, n2;
    if ( !s1.atoi(n1) )
        return false;
    if ( !s2.atoi(n2) )
        return false;
    value1 = n1;
    value2 = n2;
    return true;
}

lString8 & lString8::replace(size_type p0, size_type n0, const lString8 & str) {
    lString8 s1 = substr( 0, p0 );
    lString8 s2 = length() - p0 - n0 > 0 ? substr( p0+n0, length()-p0-n0 ) : lString8::empty_str;
    *this = s1 + str + s2;
    return *this;
}

lString8 & lString8::replace(value_type before, value_type after) {
    value_type* ptr = modify();
    while (*ptr) {
        if (*ptr == before)
            *ptr = after;
        ++ptr;
    }
    return *this;
}

lString32 & lString32::replace(size_type p0, size_type n0, const lString32 & str)
{
    lString32 s1 = substr( 0, p0 );
    lString32 s2 = length() - p0 - n0 > 0 ? substr( p0+n0, length()-p0-n0 ) : lString32::empty_str;
    *this = s1 + str + s2;
    return *this;
}

/// replaces part of string, if pattern is found
bool lString32::replace(const lString32 & findStr, const lString32 & replaceStr)
{
    int p = pos(findStr);
    if ( p<0 )
        return false;
    *this = replace( p, findStr.length(), replaceStr );
    return true;
}

bool lString32::replaceParam(int index, const lString32 & replaceStr)
{
    return replace( cs32("$") + fmt::decimal(index), replaceStr );
}

/// replaces first found occurence of "$N" pattern with itoa of integer, where N=index
bool lString32::replaceIntParam(int index, int replaceNumber)
{
    return replaceParam( index, lString32::itoa(replaceNumber));
}

static int decodeHex( lChar32 ch )
{
    if ( ch>='0' && ch<='9' )
        return ch-'0';
    else if ( ch>='a' && ch<='f' )
        return ch-'a'+10;
    else if ( ch>='A' && ch<='F' )
        return ch-'A'+10;
    return -1;
}

static lChar8 decodeHTMLChar( const lChar32 * s )
{
    if (s[0] == '%') {
        int d1 = decodeHex( s[1] );
        if (d1 >= 0) {
            int d2 = decodeHex( s[2] );
            if (d2 >= 0) {
                return (lChar8)(d1*16 + d2);
            }
        }
    }
    return 0;
}

/// decodes path like "file%20name%C3%A7" to "file nameç"
lString32 DecodeHTMLUrlString( lString32 s )
{
    const lChar32 * str = s.c_str();
    for ( int i=0; str[i]; i++ ) {
        if ( str[i]=='%'  ) {
            lChar8 ch = decodeHTMLChar( str + i );
            if ( ch==0 ) {
                continue;
            }
            // HTML encoded char found
            lString8 res;
            res.reserve(s.length());
            res.append(UnicodeToUtf8(str, i));
            res.append(1, ch);
            i+=3;

            // continue conversion
            for ( ; str[i]; i++ ) {
                if ( str[i]=='%'  ) {
                    ch = decodeHTMLChar( str + i );
                    if ( ch==0 ) {
                        res.append(1, (lChar8)str[i]);
                        continue;
                    }
                    res.append(1, ch);
                    i+=2;
                } else {
                    res.append(1, (lChar8)str[i]);
                }
            }
            return Utf8ToUnicode(res);
        }
    }
    return s;
}

void limitStringSize(lString32 & str, int maxSize) {
    if (str.length() < maxSize)
		return;
	int lastSpace = -1;
	for (int i = str.length() - 1; i > 0; i--)
		if (str[i] == ' ') {
			while (i > 0 && str[i - 1] == ' ')
				i--;
			lastSpace = i;
			break;
		}
	int split = lastSpace > 0 ? lastSpace : maxSize;
	str = str.substr(0, split);
    str += "...";
}

/// remove soft-hyphens from string
lString32 removeSoftHyphens( lString32 s )
{
    lChar32 hyphen = lChar32(UNICODE_SOFT_HYPHEN_CODE);
    int start = 0;
    while (true) {
        int p = -1;
        int len = s.length();
        for (int i = start; i < len; i++) {
            if (s[i] == hyphen) {
                p = i;
                break;
            }
        }
        if (p == -1)
            break;
        start = p;
        lString32 s1 = s.substr( 0, p );
        lString32 s2 = p < len-1 ? s.substr( p+1, len-p-1 ) : lString32::empty_str;
        s = s1 + s2;
    }
    return s;
}


#ifdef _WIN32
static bool __timerInitialized = false;
static double __timeTicksPerMillis;
static lUInt64 __timeStart;
static lUInt64 __timeAbsolute;
static lUInt64 __startTimeMillis;
#endif

void CRReinitTimer() {
#ifdef _WIN32
    LARGE_INTEGER tps;
    QueryPerformanceFrequency(&tps);
    __timeTicksPerMillis = (double)(tps.QuadPart / 1000L);
    LARGE_INTEGER queryTime;
    QueryPerformanceCounter(&queryTime);
    __timeStart = (lUInt64)(queryTime.QuadPart / __timeTicksPerMillis);
    __timerInitialized = true;
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    __startTimeMillis = (ft.dwLowDateTime | (((lUInt64)ft.dwHighDateTime) << 32)) / 10000;
#else
    // do nothing. it's for win32 only
#endif
}


lUInt64 GetCurrentTimeMillis() {
#if defined(LINUX) || defined(ANDROID) || defined(_LINUX)
    timeval ts;
    gettimeofday(&ts, NULL);
    return ts.tv_sec * (lUInt64)1000 + ts.tv_usec / 1000;
#else
 #ifdef _WIN32
    if (!__timerInitialized) {
        CRReinitTimer();
        return __startTimeMillis;
    } else {
        LARGE_INTEGER queryTime;
        QueryPerformanceCounter(&queryTime);
        __timeAbsolute = (lUInt64)(queryTime.QuadPart / __timeTicksPerMillis);
        return __startTimeMillis + (lUInt64)(__timeAbsolute - __timeStart);
    }
 #else
 #error * You should define GetCurrentTimeMillis() *
 #endif
#endif
}

