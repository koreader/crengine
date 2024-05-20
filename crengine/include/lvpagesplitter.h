/** \file lvpagesplitter.h
    \brief page splitter interface

    CoolReader Engine

    (c) Vadim Lopatin, 2000-2006
    This source code is distributed under the terms of
    GNU General Public License.

    See LICENSE file for details.
*/

#ifndef __LV_PAGESPLITTER_H_INCLUDED__
#define __LV_PAGESPLITTER_H_INCLUDED__

#include <stdlib.h>
#include <time.h>
#include "lvtypes.h"
#include "lvarray.h"
#include "lvptrvec.h"
#include "lvref.h"
#include "lvstring.h"
#include "lvhashtable.h"

#ifndef RENDER_PROGRESS_INTERVAL_MILLIS
#define RENDER_PROGRESS_INTERVAL_MILLIS 300
#endif
#ifndef RENDER_PROGRESS_INTERVAL_PERCENT
#define RENDER_PROGRESS_INTERVAL_PERCENT 2
#endif

/// &7 values
#define RN_SPLIT_AUTO   0
#define RN_SPLIT_AVOID  1
#define RN_SPLIT_ALWAYS 2
/// right-shift
#define RN_SPLIT_BEFORE 0
#define RN_SPLIT_AFTER  3

#define RN_SPLIT_BEFORE_AUTO   (RN_SPLIT_AUTO<<RN_SPLIT_BEFORE)
#define RN_SPLIT_BEFORE_AVOID  (RN_SPLIT_AVOID<<RN_SPLIT_BEFORE)
#define RN_SPLIT_BEFORE_ALWAYS (RN_SPLIT_ALWAYS<<RN_SPLIT_BEFORE)
#define RN_SPLIT_AFTER_AUTO    (RN_SPLIT_AUTO<<RN_SPLIT_AFTER)
#define RN_SPLIT_AFTER_AVOID   (RN_SPLIT_AVOID<<RN_SPLIT_AFTER)
#define RN_SPLIT_AFTER_ALWAYS  (RN_SPLIT_ALWAYS<<RN_SPLIT_AFTER)

#define RN_SPLIT_BOTH_AUTO      RN_SPLIT_BEFORE_AUTO | RN_SPLIT_AFTER_AUTO
#define RN_SPLIT_BOTH_AVOID    RN_SPLIT_BEFORE_AVOID | RN_SPLIT_AFTER_AVOID

#define RN_SPLIT_FOOT_NOTE 0x100
#define RN_SPLIT_FOOT_LINK 0x200

#define RN_SPLIT_DISCARD_AT_START 0x400

#define RN_LINE_IS_RTL 0x1000

#define RN_GET_SPLIT_BEFORE(flags) ((flags >> RN_SPLIT_BEFORE) & 0x7)
#define RN_GET_SPLIT_AFTER(flags)  ((flags >> RN_SPLIT_AFTER)  & 0x7)

#define RN_PAGE_TYPE_NORMAL           0x01
#define RN_PAGE_TYPE_COVER            0x02
#define RN_PAGE_MOSTLY_RTL            0x10
#define RN_PAGE_FOOTNOTES_MOSTLY_RTL  0x20

/// footnote fragment inside page
class LVPageFootNoteInfo {
public:
    int start;
    int height;
    LVPageFootNoteInfo()
    : start(0), height(0)
    { }
    LVPageFootNoteInfo( int s, int h )
    : start(s), height(h) 
    { }
};

template <typename T, int RESIZE_MULT, int RESIZE_ADD> class CompactArray
{
    struct Array {
        T * _list;
        int _size;
        int _length;
        Array()
        : _list(NULL), _size(0), _length(0)
        {
        }
        ~Array()
        {
            clear();
        }
        void add( T item )
        {
            if ( _size<=_length ) {
                _size = _size*RESIZE_MULT + RESIZE_ADD;
                _list = cr_realloc( _list, _size );
            }
            _list[_length++] = item;
        }
        void add( T * items, int count )
        {
            if ( count<=0 )
                return;
            if ( _size<_length+count ) {
                _size = _length+count;
                _list = cr_realloc( _list, _size );
            }
            for ( int i=0; i<count; i++ )
                _list[_length+i] = items[i];
            _length += count;
        }
        void reserve( int count )
        {
            if ( count<=0 )
                return;
            if ( _size<_length+count ) {
                _size = _length+count;
                _list = cr_realloc( _list, _size );
            }
        }
        void clear()
        {
            if ( _list ) {
                free( _list );
                _list = NULL;
                _size = 0;
                _length = 0;
            }
        }
        int length() const
        {
            return _length;
        }
        T get( int index ) const
        {
            return _list[index];
        }
        const T & operator [] (int index) const
        {
            return _list[index];
        }
        T & operator [] (int index)
        {
            return _list[index];
        }
    };

    Array * _data;
public:
    CompactArray()
    : _data(NULL)
    {
    }
    ~CompactArray()
    {
        if ( _data )
            delete _data; // NOLINT(clang-analyzer-cplusplus.NewDelete)
    }
    void add( T item )
    {
        if ( !_data )
            _data = new Array();
        _data->add(item);
    }
    void add( T * items, int count )
    {
        if ( !_data )
            _data = new Array();
        _data->add(items, count);
    }
    void add( LVArray<T> & items )
    {
        if ( items.length()<=0 )
            return;
        if ( !_data )
            _data = new Array();
        _data->add( &(items[0]), items.length() );
    }
    void reserve( int count )
    {
        if ( count<=0 )
            return;
        if ( !_data )
            _data = new Array();
        _data->reserve( count );
    }
    void clear()
    {
        if ( _data ) {
            delete _data;
            _data = NULL;
        }
    }
    int length() const
    {
        return _data ? _data->length() : 0;
    }
    T get( int index ) const
    {
        return _data->get(index);
    }
    const T & operator [] (int index) const
    {
        return _data->operator [](index);
    }
    T & operator [] (int index)
    {
        return _data->operator [](index);
    }
    bool empty() { return !_data || _data->length()==0; }

};

/// rendered page splitting info
class LVRendPageInfo {
public:
    int start; /// start of page
    int index;  /// index of page
    lInt16 height; /// height of page, does not include footnotes
    lInt8 flags;   /// RN_PAGE_*
    CompactArray<LVPageFootNoteInfo, 1, 4> footnotes; /// footnote fragment list for page
    lUInt16 flow;
    LVRendPageInfo(int pageStart, lUInt16 pageHeight, int pageIndex)
    : start(pageStart), index(pageIndex), height(pageHeight), flags(RN_PAGE_TYPE_NORMAL), flow(0) {}
    LVRendPageInfo(lUInt16 coverHeight)
    : start(0), index(0), height(coverHeight), flags(RN_PAGE_TYPE_COVER), flow(0) {}
    LVRendPageInfo() 
    : start(0), index(0), height(0), flags(RN_PAGE_TYPE_NORMAL), flow(0) {}
    bool serialize( SerialBuf & buf );
    bool deserialize( SerialBuf & buf );
};

class LVRendPageList : public LVPtrVector<LVRendPageInfo>
{
    bool has_nonlinear_flows;
public:
    LVRendPageList() : has_nonlinear_flows(false) {}
    int FindNearestPage( int y, int direction );
    void setHasNonLinearFlows( bool hasnonlinearflows ) { has_nonlinear_flows = hasnonlinearflows; }
    bool hasNonLinearFlows() { return has_nonlinear_flows; }
    bool serialize( SerialBuf & buf );
    bool deserialize( SerialBuf & buf );
    void replacePages( int old_y, int old_h, LVRendPageList * pages, int next_pages_shift_y );
};

class LVFootNote;

class LVFootNoteList;

class LVFootNoteList : public LVArray<LVFootNote*> {
public: 
    LVFootNoteList() {}
};


class LVRendLineInfo {
    friend struct PageSplitState;
    LVFootNoteList * links; // 4 bytes
    int start;              // 4 bytes
    int height;             // 4 bytes (we may get extra tall lines with tables TR)
public:
    lUInt16 flags;          // 2 bytes
    lUInt16 flow;           // 2 bytes (should be enough)
    int getSplitBefore() const { return (flags>>RN_SPLIT_BEFORE)&7; }
    int getSplitAfter() const { return (flags>>RN_SPLIT_AFTER)&7; }
/*
    LVRendLineInfo & operator = ( const LVRendLineInfoBase & v )
    {
        start = v.start;
        end = v.end;
        flags = v.flags;
        return *this;
    }
*/
    bool empty() const { 
        return start==-1; 
    }

    void clear() { 
        start = -1; height = 0; flags = 0;
        if ( links!=NULL ) {
            delete links; 
            links=NULL;
        } 
    }

    inline int getEnd() const { return start + height; }
    inline int getStart() const { return start; }
    inline int getHeight() const { return height; }
    inline lUInt16 getFlags() const { return flags; }

    LVRendLineInfo() : links(NULL), start(-1), height(0), flags(0), flow(0) { }
    LVRendLineInfo( int line_start, int line_end, lUInt16 line_flags )
    : links(NULL), start(line_start), height(line_end-line_start), flags(line_flags), flow(0)
    {
    }
    LVRendLineInfo( int line_start, int line_end, lUInt16 line_flags, int flow )
    : links(NULL), start(line_start), height(line_end-line_start), flags(line_flags), flow(flow)
    {
    }
    LVFootNoteList * getLinks() { return links; }
    ~LVRendLineInfo()
    {
        clear();
    }
    int getLinksCount()
    {
        if ( links==NULL )
            return 0;
        return links->length();
    }
    void addLink( LVFootNote * note, int pos=-1 )
    {
        if ( links==NULL )
            links = new LVFootNoteList();
        if ( pos >= 0 ) // insert at pos
            links->insert( pos, note );
        else // append
            links->add( note );
        flags |= RN_SPLIT_FOOT_LINK;
    }
};


typedef LVFastRef<LVFootNote> LVFootNoteRef;

// A LVFootNote is created when we meet a <a href=> with some internal link, and may
// later be fed with "lines" of footnote text, and then it becomes actual.
// When not yet actual, it may be diverted to another already created LVFootnote (when
// an actual footnote contains, and is then associated to, multiple id=).
class LVFootNote : public LVRefCounter {
    lString32 id;
    bool is_actual; // set when a LVFootNote holds text (it should then never become a proxy to another one)
    LVFootNote * actual_footnote; // when set, this LVFootNote is a proxy to this actual_footnote
    CompactArray<LVRendLineInfo*, 2, 4> lines;
public:
    LVFootNote( lString32 noteId )
        : id(noteId), is_actual(false), actual_footnote(NULL)
    {
    }
    bool isActual() {
        return is_actual;
    }
    void setIsActual( bool actual ) {
        is_actual = actual;
        if ( actual ) // clean any previously set proxy
            actual_footnote = NULL;
    }
    LVFootNote * getActualFootnote() {
        return actual_footnote;
    }
    void setActualFootnote( LVFootNote * actualfootnote ) {
        actual_footnote = actualfootnote;
    }
    void addLine( LVRendLineInfo * line )
    {
        lines.add( line );
    }
    // CompactArray<LVRendLineInfo*, 2, 4> & getLines() { printf("getLines %x\n", lines); return lines; }
    LVRendLineInfo * getLine(int index) {
        if ( actual_footnote )
            return actual_footnote->lines[index];
        return lines[index];
    }
    int  length() {
        if ( actual_footnote )
            return actual_footnote->lines.length();
        return lines.length();
    }
    bool empty() {
        if ( actual_footnote )
            return actual_footnote->lines.empty();
        return lines.empty();
    }
    void clear() { lines.clear(); }
    lString32 getId() { return id; }
};

class LVDocViewCallback;
class LVRendPageContext
{


    LVPtrVector<LVRendLineInfo> lines;

    LVDocViewCallback * callback;
    int totalFinalBlocks;
    int renderedFinalBlocks;
    int lastPercent;
    CRTimerUtil progressTimeout;


    // page start line
    //LVRendLineInfoBase pagestart;
    // page end candidate line
    //LVRendLineInfoBase pageend;
    // next line after page end candidate
    //LVRendLineInfoBase next;
    // last fit line
    //LVRendLineInfoBase last;
    // page list to fill
    LVRendPageList * page_list;
    // page height
    int page_h;
    // document default font size (= root node font size)
    int doc_font_size;
    // Whether to gather lines or not (only footnote links will be gathered if not)
    bool gather_lines;
    // Links gathered when !gather_lines
    lString32Collection link_ids;
    // current flow being processed
    int current_flow;
    // maximum flow encountered so far
    int max_flow;
    // to know if current flow got some lines
    bool current_flow_empty;

    LVHashTable<lString32, LVFootNoteRef> footNotes;

    LVFootNote * curr_note;

    // Note: a footnote indexed in 'footNotes' may have pointers to it stored in other
    // footnotes' actual_footnote. 'footNotes' is usually the only thing holding a
    // reference to a LVFootNote. If we replace with footNotes.set() an existing
    // footnote, it may be deleted, and using these other footnotes' actual_footnote
    // could cause segfault. So, below, we should avoid doing that, and prefer just
    // erasing a previous footnote's 'lines'.
    // In the following, we want to handle as well as possible the edge case
    // of buggy books having duplicated id= among footnotes (which may happen
    // in Wikipedia EPUBs), which makes things a tad more complex...
    LVFootNoteRef getOrCreateFootNote( lString32 id, bool actual=true )
    {
        LVFootNoteRef ref = footNotes.get(id);
        if ( ref.isNull() ) {
            // Not found: create one and index it
            ref = LVFootNoteRef( new LVFootNote( id ) );
            footNotes.set( id, ref );
            if ( actual ) {
                ref.get()->setIsActual(true);
            }
        }
        else {
            // Found an existing one
            if ( actual ) {
                // We are going to add lines
                if ( ref.get()->isActual() ) {
                    // If the one we found is already actual, something is wrong: this may
                    // happen with buggy books with duplicated id= (ie. Wikipedia EPUBs...).
                    // LVPageSplitter expects a footnote to be a single chunk/slice of the
                    // document, so we can't accumulate lines from different places: override
                    // its content. (This is consistent with the way crengine handle id= when
                    // building the DOM: later ones override ealier ones).
                    ref.get()->clear();
                }
                // Make a non-actual (which may be a proxy or not) actual
                ref.get()->setIsActual(true);
            }
            // if actual=false, we're all fine, any kind will do.
        }
        return ref;
    }
    LVFootNoteRef getOrCreateFootNote( lString32Collection & ids )
    {
        // This, with multiple ids, is always called to create an actual footnote,
        // so no need to handle any actual=false case.
        if (ids.length() == 1) { // use single id method.
            return getOrCreateFootNote(ids.at(0));
        }
        // Multiple ids provided: zero, one, or more of them may exist
        LVFootNoteRef ref;
        int found = -1;
        for ( int n=0; n<ids.length(); n++ ) {
            ref = footNotes.get(ids.at(n));
            if ( !ref.isNull() ) {
                found = n;
                // As above, see comments there.
                if ( ref.get()->isActual() ) {
                    ref.get()->clear();
                }
                ref.get()->setIsActual(true);
                break;
            }
        }
        if ( found < 0 ) {
            // None found: create one with the first of the ids provided
            ref = LVFootNoteRef( new LVFootNote( ids.at(0) ) );
            ref.get()->setIsActual(true);
        }
        // Need to do something for each of the ids provided
        for ( int n=0; n<ids.length(); n++ ) {
            if (found < 0 ) {
                // No existing footnote found: index with this id the same LVFootnote we created
                footNotes.set( ids.at(n), ref );
            }
            else if ( n == found ) {
                // The one we found (so, already indexed), and that we will return and
                // that will get lines: nothing else to do.
            }
            else {
                // Other ids may alredy exist or not
                LVFootNoteRef nref = footNotes.get(ids.at(n));
                if ( nref.isNull() ) {
                    // No existing footnote with this id: index with this id the same LVFootnote we created
                    footNotes.set( ids.at(n), ref );
                }
                else {
                    // Existing other footnote associated to one of our multiple ids:
                    if ( nref.get()->isActual() ) {
                        // That other footnote is already actual, holding lines: it feels it's best
                        // to not do anything and let it be, and don't associate to this footnote.
                    }
                    else {
                        // Not actual: make it a proxy to the footnote we will return
                        // (and that will get lines)
                        nref.get()->setActualFootnote( ref.get() );
                    }
                }
            }
        }
        return ref;
    }

    void split();
public:


    void setCallback(LVDocViewCallback * cb, int _totalFinalBlocks) {
        callback = cb; totalFinalBlocks=_totalFinalBlocks;
        progressTimeout.restart(RENDER_PROGRESS_INTERVAL_MILLIS);
    }
    bool updateRenderProgress( int numFinalBlocksRendered );

    bool wantsLines() { return gather_lines; }

    void newFlow( bool nonlinear );

    /// Get the number of links in the current line links list, or
    // in link_ids when !gather_lines
    int getCurrentLinksCount();

    /// append or insert footnote link to last added line
    void addLink( lString32 id, int pos=-1 );

    /// get gathered links when !gather_lines
    // (returns a reference to avoid lString32Collection destructor from
    // being called twice and a double free crash)
    lString32Collection * getLinkIds() { return &link_ids; }

    /// mark start of foot note
    void enterFootNote( lString32 id );
    void enterFootNote( lString32Collection & ids );

    /// mark end of foot note
    void leaveFootNote();

    /// returns page height
    int getPageHeight() { return page_h; }

    /// returns document font size
    int getDocFontSize() { return doc_font_size; }

    /// returns page list pointer
    LVRendPageList * getPageList() { return page_list; }

    /// constructor (docFontSize is only needed for with main context actually used to split pages)
    LVRendPageContext(LVRendPageList * pageList, int pageHeight, int docFontSize=0, bool gatherLines=true);

    /// add source line
    void AddLine( int starty, int endy, int flags );

    LVPtrVector<LVRendLineInfo> * getLines() {
        return &lines;
    };
    void Finalize();
};

#endif

