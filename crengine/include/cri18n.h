/** \file cri18n.h
    \brief internationalization support, gettext wrapper

    CoolReader Engine

    (c) Vadim Lopatin, 2000-2006
    This source code is distributed under the terms of
    GNU General Public License.
    See LICENSE file for details.

*/

#ifndef __CRI18N_H_INCLUDED__
#define __CRI18N_H_INCLUDED__

#if CR_EMULATE_GETTEXT!=1 && !defined(_WIN32)
#include <libintl.h>
#endif

#include "lvstring.h"
#include "lvptrvec.h"
#include "lvhashtable.h"

/// i18n interface
class CRI18NTranslator
{
protected:
	static CRI18NTranslator * _translator;
	static CRI18NTranslator * _defTranslator;
	virtual const char * getText( const char * src ) = 0;
public:
	virtual ~CRI18NTranslator() { }
	static void setTranslator( CRI18NTranslator * translator );
	static void setDefTranslator( CRI18NTranslator * translator );
    static const char * translate( const char * src );
    static const lString8 translate8( const char * src );
    static const lString32 translate32( const char * src );
};

class CRMoFileTranslator : public CRI18NTranslator
{
public:
	class Item {
	public:
		lString8 src;
		lString8 dst;
		Item( lString8 srcText, lString8 dstText )
			: src(srcText), dst(dstText)
		{
		}
	protected:
		// no copy
                Item( const Item & ) { }
                Item & operator = ( const Item & ) { return *this; }
	};
protected:
	LVPtrVector<Item> _list;
	// call in src sort order only!
	virtual void add( lString8 src, lString8 dst );
	virtual const char * getText( const char * src );
	virtual void sort();
public:
	CRMoFileTranslator();
	bool openMoFile( lString32 fileName );
	virtual ~CRMoFileTranslator();
};

class CRIniFileTranslator : public CRI18NTranslator
{
public:
	LVHashTable<lString8, lString8> _map;
protected:
	virtual const char * getText( const char * src );
public:
	bool open(const char * fileName);
	CRIniFileTranslator() : _map(3000) {}
	virtual ~CRIniFileTranslator() {}
	static CRIniFileTranslator * create(const char * fileName);
};


#ifdef _
#undef _
#endif
#ifdef _8
#undef _8
#endif
#ifdef _32
#undef _32
#endif
#define _(String) CRI18NTranslator::translate(String)
#define _8(String) CRI18NTranslator::translate8(String)
#define _32(String) CRI18NTranslator::translate32(String)


#endif
