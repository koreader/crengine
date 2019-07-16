/** \file lvhashtable.h
    \brief hash table template

    CoolReader Engine

    (c) Vadim Lopatin, 2000-2006
    This source code is distributed under the terms of
    GNU General Public License.
    See LICENSE file for details.

*/

#ifndef __LVHASHTABLE_H_INCLUDED__
#define __LVHASHTABLE_H_INCLUDED__

#include "lvtypes.h"
#include <stdlib.h>
#include <string.h>

inline lUInt32 getHash( lUInt16 n )
{
    return (lUInt32)n * 1975317 + 164521;
}

inline lUInt32 getHash( lUInt32 n )
{
    return n * 1975317 + 164521;
}

inline lUInt32 getHash( lUInt64 n )
{
    return (lUInt32)(n * 1975317 + (n >> 32) * 31 + 164521);
}

class LVFont;
inline lUInt32 getHash(LVFont * n )
{
    return getHash((lUInt64)n);
}

inline lUInt32 getHash(void * n )
{
    return getHash((lUInt64)n);
}

/// Hash table
/**
    Implements hash table map
*/
template <typename keyT, typename valueT> class LVHashTable
{
	friend class iterator;
public:
    class pair {
		friend class LVHashTable;
    public:
        pair *  next; // extend
        keyT    key;
        valueT  value;
        pair( const keyT & nkey, valueT nvalue, pair * pnext ) : next(pnext), key(nkey), value(nvalue) { }
    };

	class iterator {
        	friend class LVHashTable;
		const LVHashTable & _tbl;
		int index;
		pair * ptr;
		iterator & operator = (iterator &) {
			// no assignment
			return *this;
		}
	public:
		iterator( const LVHashTable & table )
			: _tbl( table ), index(0), ptr(NULL)
		{
		}
		iterator( const iterator & v )
			: _tbl( v._tbl ), index(v.index), ptr(v.ptr)
		{
		}
		pair * next()
		{
			if ( index>=_tbl._size )
				return NULL;
			if ( ptr )
				ptr = ptr->next;
			if ( !ptr ) {
				for ( ; index < _tbl._size; ) {
					ptr = _tbl._table[ index++ ];
					if ( ptr )
						return ptr;
				}
			}
			return ptr;
		}
	};

	iterator forwardIterator() const
	{
		return iterator(*this);
	}

    LVHashTable( int size )
    {
        if (size < 16 )
            size = 16;
        _table = new pair* [ size ]();
        _size = size;
        _count = 0;
    }
    ~LVHashTable()
    {
        if ( _table ) {
            clear();
            delete[] _table;
        }
    }
    void clear()
    {
        for ( int i=0; i<_size; i++ ) {
            pair * p = _table[i];
            while ( p ) {
                pair * tmp = p;
                p = p->next;
                delete tmp;
            }
        }
        memset( _table, 0, sizeof(pair*) * _size );
        _count = 0;
    }
    int length() { return _count; }
    int size() { return _size; }
    void resize( int nsize )
    {
        pair ** new_table = new pair * [ nsize ]();
		if (_table) {
			for ( int i=0; i<_size; i++ ) {
				pair * p = _table[i];
				while ( p  )
				{
					lUInt32 index = getHash( p->key ) % ( nsize );
					new_table[index] = new pair( p->key, p->value, new_table[index] );
					pair * tmp = p;
					p = p->next;
					delete tmp;
				}
			}
            delete[] _table;
		}
        _table = new_table;
        _size = nsize;

    }
    void set( const keyT & key, valueT value )
    {
        lUInt32 index = getHash( key ) % ( _size );
        pair ** p = &_table[index];
        for ( ;*p ;p = &(*p)->next )
        {
            if ( (*p)->key == key )
            {
                (*p)->value = value;
                return;
            }
        }
        if ( _count >= _size ) {
            resize( _size * 2 );
            index = getHash( key ) % ( _size );
            p = &_table[index];
            for ( ;*p ;p = &(*p)->next )
            {
            }
        }
        *p = new pair( key, value, NULL );
        _count++;
    }
    void remove( const keyT & key )
    {
        lUInt32 index = getHash( key ) % ( _size );
        pair ** p = &_table[index];
        for ( ;*p ;p = &(*p)->next )
        {
            if ( (*p)->key == key )
            {
                pair * tmp = *p;
                *p = (*p)->next;
                delete tmp;
                _count--;
                return;
            }
        }
    }
    valueT get( const keyT & key )
    {
        lUInt32 index = getHash( key ) % ( _size );
        pair * p = _table[index];
        for ( ;p ;p = p->next )
        {
            if ( p->key == key )
            {
                return p->value;
            }
        }
        return valueT();
    }
    bool get( const keyT & key, valueT & res )
    {
        lUInt32 index = getHash( key ) % ( _size );
        pair * p = _table[index];
        for ( ;p ;p = p->next )
        {
            if ( p->key == key )
            {
                res = p->value;
                return true;
            }
        }
        return false;
    }
private:
    int _size;
    int _count;
    pair ** _table;
};


#endif
