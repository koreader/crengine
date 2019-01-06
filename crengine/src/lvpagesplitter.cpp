/** \file lvpagesplitter.cpp
    \brief page splitter implementation

    CoolReader Engine

    (c) Vadim Lopatin, 2000-2006
    This source code is distributed under the terms of
    GNU General Public License.

    See LICENSE file for details.
*/

#include "../include/lvpagesplitter.h"
#include "../include/lvtinydom.h"
#include <time.h>

// Uncomment for debugging page splitting algorithm:
// #define DEBUG_PAGESPLIT
// (Also change '#if 0' around 'For debugging lvpagesplitter.cpp'
// to '#if 1' in src/lvdocview.cpp to get a summary of pages.

int LVRendPageList::FindNearestPage( int y, int direction )
{
    if (!length())
        return 0;
    for (int i=0; i<length(); i++)
    {
        const LVRendPageInfo * pi = ((*this)[i]);
        if (y<pi->start) {
            if (i==0 || direction>=0)
                return i;
            else
                return i-1;
        } else if (y<pi->start+pi->height) {
            if (i<length()-1 && direction>0)
                return i+1;
            else if (i==0 || direction>=0)
                return i;
            else
                return i-1;
        }
    }
    return length()-1;
}

LVRendPageContext::LVRendPageContext(LVRendPageList * pageList, int pageHeight)
    : callback(NULL), totalFinalBlocks(0)
    , renderedFinalBlocks(0), lastPercent(-1), page_list(pageList), page_h(pageHeight), footNotes(64), curr_note(NULL)
{
    if ( callback ) {
        callback->OnFormatStart();
    }
}

bool LVRendPageContext::updateRenderProgress( int numFinalBlocksRendered )
{
    renderedFinalBlocks += numFinalBlocksRendered;
    int percent = totalFinalBlocks>0 ? renderedFinalBlocks * 100 / totalFinalBlocks : 0;
    if ( percent<0 )
        percent = 0;
    if ( percent>100 )
        percent = 100;
    if ( callback && percent>lastPercent+RENDER_PROGRESS_INTERVAL_PERCENT ) {
        if ( progressTimeout.expired() ) {
            callback->OnFormatProgress(percent);
            progressTimeout.restart(RENDER_PROGRESS_INTERVAL_MILLIS);
            lastPercent = percent;
            return true;
        }
    }
    return false;
}

/// append footnote link to last added line
void LVRendPageContext::addLink( lString16 id )
{
    if ( !page_list )
        return;
    if ( lines.empty() )
        return;
    LVFootNote * note = getOrCreateFootNote( id );
    lines.last()->addLink(note);
}

/// mark start of foot note
void LVRendPageContext::enterFootNote( lString16 id )
{
    if ( !page_list )
        return;
    //CRLog::trace("enterFootNote( %s )", LCSTR(id) );
    if ( curr_note != NULL ) {
        CRLog::error("Nested entering note" );
        return;
    }
    curr_note = getOrCreateFootNote( id );
}

/// mark end of foot note
void LVRendPageContext::leaveFootNote()
{
    if ( !page_list )
        return;
    //CRLog::trace("leaveFootNote()" );
    if ( !curr_note ) {
        CRLog::error("leaveFootNote() w/o current note set");
    }
    curr_note = NULL;
}


void LVRendPageContext::AddLine( int starty, int endy, int flags )
{
    #ifdef DEBUG_PAGESPLIT
        printf("PS: AddLine (%x, #%d): %d > %d (%d)\n", this, lines.length(), starty, endy, flags);
    #endif
    if ( curr_note!=NULL )
        flags |= RN_SPLIT_FOOT_NOTE;
    LVRendLineInfo * line = new LVRendLineInfo(starty, endy, flags);
    lines.add( line );
    if ( curr_note != NULL ) {
        //CRLog::trace("adding line to note (%d)", line->start);
        curr_note->addLine( line );
    }
}

// We use 1.0rem (1x root font size) as the footnote margin (vertical margin
// between text and first foornote)
#define FOOTNOTE_MARGIN_REM 1
extern int gRootFontSize;

// helper class
struct PageSplitState {
public:
    int page_h;
    LVRendPageList * page_list;
    const LVRendLineInfo * pagestart;
    const LVRendLineInfo * pageend;
    const LVRendLineInfo * next;
    const LVRendLineInfo * last;
    int   footheight;
    LVFootNote * footnote;
    const LVRendLineInfo * footstart;
    const LVRendLineInfo * footend;
    const LVRendLineInfo * footlast;
    LVArray<LVPageFootNoteInfo> footnotes;
    LVArray<LVFootNote *> page_footnotes; // foonotes already on this page, to avoid duplicates
    int lastpageend;

    PageSplitState(LVRendPageList * pl, int pageHeight)
        : page_h(pageHeight)
        , page_list(pl)
        , pagestart(NULL)
        , pageend(NULL)
        , next(NULL)
        , last(NULL)
        , footheight(0)
        , footnote(NULL)
        , footstart(NULL)
        , footend(NULL)
        , footlast(NULL)
        , lastpageend(0)
    {
    }

    // The final list of pages can be listed by changing some '#if 0' to '#if 1'
    // in lvdocview.cpp LVDocView::Render()

    unsigned CalcSplitFlag( int flg1, int flg2 )
    {
        if (flg1==RN_SPLIT_AVOID || flg2==RN_SPLIT_AVOID)
            return RN_SPLIT_AVOID;
        if (flg1==RN_SPLIT_ALWAYS || flg2==RN_SPLIT_ALWAYS)
            return RN_SPLIT_ALWAYS;
        return RN_SPLIT_AUTO;
    }

    void StartPage( const LVRendLineInfo * line )
    {
        #ifdef DEBUG_FOOTNOTES
            if ( !line ) {
                CRLog::trace("StartPage(NULL)");
            }
            if ( CRLog::isTraceEnabled() )
                CRLog::trace("StartPage(%d)", line ? line->start : -111111111);
        #endif
        // A "line" is a slice of the document, it's a unit of what can be stacked
        // into pages. It has a y coordinate as start and an other at end,
        // that make its height.
        // It's usually a line of text, or an image, but we also have one
        // for each non-zero vertical margin, border and padding above and
        // below block elements.
        // A single "line" can also include multiple lines of text that have
        // to be considered as a slice-unit for technical reasons: this happens
        // with table rows (TR), so a table row will usually not be splitted
        // among multiple pages, but pushed to the next page (except when a single
        // row can't fit on a page: we'll then split inside that unit of slice).
        pagestart = line; // First line of the new future page
        pageend = NULL; // No end of page yet (pagestart will be used if no pageend set)
        next = NULL; // Last known line that we can split on
        // We should keep current 'last' (we'll use its ->getEnd()) and will
        // compare its flags to next coming line). We don't want to reset
        // it in the one case we add a past line: when using 'StartPage(next)',
        // when there is AVOID between 'last' and the current line. We don't
        // want to reset 'last' to be this past line!
        #ifdef DEBUG_PAGESPLIT
            printf("PS:           new current page %d>%d h=%d\n",
                pagestart->getStart(), last->getEnd(), last->getEnd() - pagestart->getStart());
        #endif
    }
    void AddToList()
    {
        bool hasFootnotes = footnotes.length() > 0;
        if ( !pageend )
            pageend = pagestart;
        if ( !pagestart && !hasFootnotes )
            return;
        int start = (pagestart && pageend) ? pagestart->getStart() : lastpageend;
        int h = (pagestart && pageend) ? pageend->getEnd()-pagestart->getStart() : 0;
        #ifdef DEBUG_FOOTNOTES
            if ( CRLog::isTraceEnabled() ) {
                if ( pagestart && pageend )
                    CRLog::trace("AddToList(%d, %d) footnotes: %d  pageHeight=%d",
                        pagestart->start, pageend->start+pageend->height, footnotes.length(), h);
                else
                    CRLog::trace("AddToList(Only footnote: %d) footnotes: %d  pageHeight=%d",
                        lastpageend, footnotes.length(), h);
            }
        #endif
        #ifdef DEBUG_PAGESPLIT
            printf("PS: ========= ADDING PAGE %d: %d > %d h=%d\n",
                page_list->length(), start, start+h, h);
        #endif
        LVRendPageInfo * page = new LVRendPageInfo(start, h, page_list->length());
        lastpageend = start + h;
        if ( footnotes.length()>0 ) {
            page->footnotes.add( footnotes );
            footnotes.clear();
            footheight = 0;
        }
        if ( page_footnotes.length()>0 ) {
            page_footnotes.clear();
        }
        if (footnote) {
            // If we're not yet done adding this footnote (so it will continue
            // on the new page), consider it already present in this new page
            // (even if one won't see the starting text and footnote number,
            // it's better than seeing again the same duplicated footnote text)
            page_footnotes.add(footnote);
        }
        page_list->add(page);
    }
    int currentFootnoteHeight()
    {
        if ( !footstart )
            return 0;
        int h = 0;
        h = (footlast?footlast:footstart)->getEnd() - footstart->getStart();
        return h;
    }
    int currentHeight( const LVRendLineInfo * line = NULL )
    {
        if ( line == NULL )
            line = last;
        int h = 0;
        if ( line && pagestart )
            h += line->getEnd() - pagestart->getStart();
        int footh = 0 /*currentFootnoteHeight()*/ + footheight;
        if ( footh )
            h += FOOTNOTE_MARGIN_REM*gRootFontSize + footh;
        return h;
    }
    void SplitLineIfOverflowPage( LVRendLineInfo * line )
    {
        // A 'line' is usually a line of text from a paragraph, but
        // can be an image, or a fully rendered table row (<tr>), and
        // can have a huge height, possibly overflowing page height.
        // This line (rendered block) is at start of page. If its
        // height is greater than page height, we need to split it
        // and put slices on new pages. We have no knowledge of what
        // this 'line' contains (text, image...), so we can't really
        // find a better 'y' to cut at than the page height. We may
        // then cut in the middle of a text line, and have halves
        // displayed on different pages (althought it seems crengine
        // deals with displaying the cut line fully at start of
        // next page).
        // Note: this was not tested with footnotes. We don't take
        // footnotes into account here, so hopefully they would be
        // displayed with the last slice of an overflowed 'line'.

        int slice_start = line->getStart();
        #ifdef DEBUG_PAGESPLIT
            if (line->getEnd() - slice_start > page_h) {
                printf("PS:     line overflows page: %d > %d\n",
                    line->getEnd() - slice_start, page_h);
            }
        #endif
        while (line->getEnd() - slice_start > page_h) {
            // Greater than page height: we need to cut
            LVRendPageInfo * page = new LVRendPageInfo(slice_start, page_h, page_list->length());
            #ifdef DEBUG_PAGESPLIT
                printf("PS: ==== SPLITTED AS PAGE %d: %d > %d h=%d\n",
                    page_list->length(), slice_start, slice_start+page_h, page_h);
            #endif
            page_list->add(page);
            slice_start += page_h;
            lastpageend = slice_start;
            last = new LVRendLineInfo(slice_start, line->getEnd(), line->flags);
            pageend = last;
        }
        if (slice_start != line->getStart()) {
            // We did cut slices: we made a virtual 'line' with the last slice,
            // and as it fits on a page, use it at start of next page, to
            // possibly have other lines added after it on this page.
            StartPage(last);
            // This 'last' we made is the good 'last' to use from now on.
        }
        // Else, keep current 'last' (which must have been set to 'line'
        // before we were called)
    }
    void AddLine( LVRendLineInfo * line )
    {
        #ifdef DEBUG_PAGESPLIT
            printf("PS: Adding line %d>%d h=%d, flags=<%d|%d>",
                line->getStart(), line->getEnd(), line->getHeight(),
                line->getSplitBefore(), line->getSplitAfter());
        #endif
        if (pagestart==NULL ) { // first line added
            #ifdef DEBUG_PAGESPLIT
                printf("   starting page with it\n");
            #endif
            last = line; // 'last' should never be NULL from now on
            StartPage( line );
            SplitLineIfOverflowPage(line); // may update 'last'
        }
        else {
            #ifdef DEBUG_PAGESPLIT
                printf("   to current page %d>%d h=%d\n",
                    pagestart->getStart(), last->getEnd(),
                    last->getEnd() - pagestart->getStart());
            #endif
            if (line->getStart() < last->getEnd()) {
                #ifdef DEBUG_PAGESPLIT
                    printf("PS:   BACKWARD, IGNORED\n");
                #endif
                return; // for table cells
                // (Note: this would prevent using negative vertical margins)
            }
            unsigned flgSplit = CalcSplitFlag( last->getSplitAfter(), line->getSplitBefore() );
            //bool flgFit = currentHeight( next ? next : line ) <= page_h;
            bool flgFit = currentHeight( line ) <= page_h;

            if (!flgFit && flgSplit==RN_SPLIT_AVOID && pageend && next) {
                #ifdef DEBUG_PAGESPLIT
                    printf("PS:   does not fit but split avoid\n");
                #endif
                // This new line doesn't fit, but split should be avoided between
                // 'last' and this line - and we have a previous line where a split
                // is allowed (pageend and next were reset on StartPage(),
                // and were only updated below when flgSplit==RN_SPLIT_AUTO).
                // Let AddToList() use current pagestart and pageend,
                // and StartPage on this old 'next'.
                AddToList();
                StartPage(next);
                // 'next' fitted previously on a page, so it must still fit now that it's
                // the first line in a new page: no need for SplitLineIfOverflowPage(next)
                // We keep the current 'last' (which can be 'next', or can be a line
                // met after 'next').
                // Recompute flgFit (if it still doesn't fit, it will be splitted below)
                flgFit = currentHeight( line ) <= page_h;
                // What happens after now:
                // 'last' fitted before, so it still fits now.
                // We still have RN_SPLIT_AVOID between 'last' and 'line'.
                // - If !flgFit ('last'+'line' do not fit), we'll go below in the if (!flgFit)
                //   and we will split between 'last' and 'line' (in spite of RN_SPLIT_AVOID): OK
                // - If flgFit ('last'+'line' now does fit), we'll go below in the 'else'
                //   where 'next' and 'pageend' are not set, so they'll stay NULL.
                //   - If an upcoming line is not AVOID: good, we'll have a 'next' and
                //     'pageend', and we can start again doing what we just did when
                //     we later meet a SPLIT_AVOID that does not fit
                //   - If the upcoming lines are all SPLIT_AVOID:
                //     - as long as pagestart+..+upcoming fit: OK, but still no 'next' and 'pageend'.
                //     - when comes the point where pagestart+..+upcoming do not fit: OK
                //       no 'next' and 'pageend', so we split between 'last' and upcoming in
                //       the 'if (!flgFit)' in spite of RN_SPLIT_AVOID.
            }

            if (!flgFit) {
                #ifdef DEBUG_PAGESPLIT
                    printf("PS:   does not fit but split ");
                    if (flgSplit==RN_SPLIT_AUTO) { printf("is allowed\n"); }
                    else if (flgSplit==RN_SPLIT_ALWAYS) { printf("is mandatory\n"); }
                    else if (flgSplit==RN_SPLIT_AVOID) { printf("can't be avoided\n"); }
                    else printf("????\n");
                #endif
                // Doesn't fit, but split is allowed (or mandatory) between
                // last and this line - or we don't have a previous line
                // where split is allowed: split between last and this line
                next = line; // Not useful anyway, as 'next' is not used
                             // by AddToList() and reset by StartPage()
                pageend = last;
                AddToList();
                last = line;
                StartPage(line);
                SplitLineIfOverflowPage(line); // may update 'last'
            }
            else if (flgSplit==RN_SPLIT_ALWAYS) {
                // Fits, but split is mandatory
                #ifdef DEBUG_PAGESPLIT
                    printf("PS:   fit but split mandatory\n");
                #endif
                if (next==NULL) { // Not useful anyway, as 'next' is not
                    next = line;  // used by AddToList() and reset by StartPage()
                }
                pageend = last;
                AddToList();
                last = line;
                StartPage(line);
                SplitLineIfOverflowPage(line); // may update 'last'
            }
            else if (flgSplit==RN_SPLIT_AUTO) {
                #ifdef DEBUG_PAGESPLIT
                    printf("PS:   fit, split allowed\n");
                #endif
                // Fits, split is allowed, but we don't split yet.
                // Update split candidate for when it's needed.
                pageend = last;
                next = line;
                last = line;
            }
            else if (flgSplit==RN_SPLIT_AVOID) {
                #ifdef DEBUG_PAGESPLIT
                    printf("PS:   fit, split to avoid\n");
                #endif
                // Don't update previous 'next' and 'pageend' (they
                // were updated on last AUTO or ALWAYS), so we know
                // we can still split on these if needed.
                last = line;
            }
            else { // should not happen
                #ifdef DEBUG_PAGESPLIT
                    printf("PS:   fit, unknown case");
                #endif
                last = line;
            }
        }
    }
    void Finalize()
    {
        if (last==NULL)
            return;
        // Add remaining line to current page
        pageend = last;
        AddToList();
    }
    void StartFootNote( LVFootNote * note )
    {
        #ifdef DEBUG_FOOTNOTES
            CRLog::trace( "StartFootNote(%d)", note->getLines().length() );
        #endif
        if ( !note || note->getLines().length()==0 )
            return;
        footnote = note;
        //footstart = footnote->getLines()[0];
        //footlast = footnote->getLines()[0];
        footend = NULL;
    }
    void AddFootnoteFragmentToList()
    {
        if ( footstart==NULL )
            return; // no data
        if ( footend==NULL )
            footend = footstart;
        //CRLog::trace("AddFootnoteFragmentToList(%d, %d)", footstart->start, footend->end );
        int h = footend->getEnd() - footstart->getStart(); // currentFootnoteHeight();
        if ( h>0 && h<page_h ) {
            footheight += h;
            #ifdef DEBUG_FOOTNOTES
                CRLog::trace("AddFootnoteFragmentToList(%d, %d)", footstart->getStart(), h);
            #endif
            footnotes.add( LVPageFootNoteInfo( footstart->getStart(), h ) );
        }
        footstart = footend = NULL;
    }
    /// footnote is finished
    void EndFootNote()
    {
        #ifdef DEBUG_FOOTNOTES
            CRLog::trace("EndFootNote()");
        #endif
        footend = footlast;
        AddFootnoteFragmentToList();
        footnote = NULL;
        footstart = footend = footlast = NULL;
    }
    void AddFootnoteLine( LVRendLineInfo * line )
    {
        int dh = line->getEnd()
            - (footstart ? footstart->getStart() : line->getStart())
            + (footheight==0 ? FOOTNOTE_MARGIN_REM*gRootFontSize : 0);
        int h = currentHeight(NULL); //next
        #ifdef DEBUG_FOOTNOTES
            CRLog::trace("Add footnote line %d  footheight=%d  h=%d  dh=%d  page_h=%d",
                line->start, footheight, h, dh, page_h);
        #endif
        if ( h + dh > page_h ) {
            #ifdef DEBUG_FOOTNOTES
                CRLog::trace("No current page space for this line, %s",
                    (footstart?"footstart is not null":"footstart is null"));
            #endif
            if ( footstart==NULL ) {
                //CRLog::trace("Starting new footnote fragment");
                // no footnote lines fit
                //pageend = last;
                AddToList();
                //StartPage( last );
                StartPage( last );
            } else {
                AddFootnoteFragmentToList();
                //const LVRendLineInfo * save = ?:last;
                // = NULL;
                // LVE-TODO-TEST
                //if ( next != NULL ) {
                    pageend = last;
                    AddToList();
                    StartPage( NULL );
                    //StartPage( next );
                //}
            }
            footstart = footlast = line;
            footend = NULL;
            return;
        }
        if ( footstart==NULL ) {
            footstart = footlast = line;
            footend = line;
        } else {
            footend = line;
            footlast = line;
        }
    }
    bool IsFootNoteInCurrentPage( LVFootNote* note )
    {
        if (page_footnotes.length() > 0)
            for (int n = 0; n < page_footnotes.length(); n++)
                if (note == page_footnotes[n])
                    return true;
        // Assume calling code will add it to this page, so remember it
        page_footnotes.add(note);
        return false;
    }
};

void LVRendPageContext::split()
{
    if ( !page_list )
        return;
    PageSplitState s(page_list, page_h);
    #ifdef DEBUG_PAGESPLIT
        printf("PS: splitting lines into pages, page height=%d\n", page_h);
    #endif

    int lineCount = lines.length();


    LVRendLineInfo * line = NULL;
    for ( int lindex=0; lindex<lineCount; lindex++ ) {
        line = lines[lindex];
        s.AddLine( line );
        // add footnotes for line, if any...
        if ( line->getLinks() ) {
            s.last = line;
            s.next = lindex<lineCount-1?lines[lindex+1]:line;
            bool foundFootNote = false;
            //if ( CRLog::isTraceEnabled() && line->getLinks()->length()>0 ) {
            //    CRLog::trace("LVRendPageContext::split() line %d: found %d links", lindex, line->getLinks()->length() );
           // }
            for ( int j=0; j<line->getLinks()->length(); j++ ) {
                LVFootNote* note = line->getLinks()->get(j);
                if ( note->getLines().length() ) {
                    // Avoid duplicated footnotes in the same page
                    if (s.IsFootNoteInCurrentPage(note))
                        continue;
                    foundFootNote = true;
                    s.StartFootNote( note );
                    for ( int k=0; k<note->getLines().length(); k++ ) {
                        s.AddFootnoteLine( note->getLines()[k] );
                    }
                    s.EndFootNote();
                }
            }
            if ( !foundFootNote )
                line->flags = line->flags & ~RN_SPLIT_FOOT_LINK;
        }
    }
    s.Finalize();
}

void LVRendPageContext::Finalize()
{
    split();
    lines.clear();
    footNotes.clear();
}

static const char * pagelist_magic = "PageList";

bool LVRendPageList::serialize( SerialBuf & buf )
{
    if ( buf.error() )
        return false;
    buf.putMagic( pagelist_magic );
    int pos = buf.pos();
    buf << (lUInt32)length();
    for ( int i=0; i<length(); i++ ) {
        get(i)->serialize( buf );
    }
    buf.putMagic( pagelist_magic );
    buf.putCRC( buf.pos() - pos );
    return !buf.error();
}

bool LVRendPageList::deserialize( SerialBuf & buf )
{
    if ( buf.error() )
        return false;
    if ( !buf.checkMagic( pagelist_magic ) )
        return false;
    clear();
    int pos = buf.pos();
    lUInt32 len;
    buf >> len;
    clear();
    reserve(len);
    for (lUInt32 i = 0; i < len; i++) {
        LVRendPageInfo * item = new LVRendPageInfo();
        item->deserialize( buf );
        item->index = i;
        add( item );
    }
    if ( !buf.checkMagic( pagelist_magic ) )
        return false;
    buf.checkCRC( buf.pos() - pos );
    return !buf.error();
}

bool LVRendPageInfo::serialize( SerialBuf & buf )
{
    if ( buf.error() )
        return false;
    buf << (lUInt32)start; /// start of page
    buf << (lUInt16)height; /// height of page, does not include footnotes
    buf << (lUInt8) type;   /// type: PAGE_TYPE_NORMAL, PAGE_TYPE_COVER
    lUInt16 len = footnotes.length();
    buf << len;
    for ( int i=0; i<len; i++ ) {
        buf << (lUInt32)footnotes[i].start;
        buf << (lUInt32)footnotes[i].height;
    }
    return !buf.error();
}

bool LVRendPageInfo::deserialize( SerialBuf & buf )
{
    if ( buf.error() )
        return false;
    lUInt32 n1;
	lUInt16 n2;
    lUInt8 n3;

    buf >> n1 >> n2 >> n3; /// start of page

    start = n1;
    height = n2;
    type = n3;

    lUInt16 len;
    buf >> len;
    footnotes.clear();
    if ( len ) {
        footnotes.reserve(len);
        for ( int i=0; i<len; i++ ) {
            lUInt32 n1;
            lUInt32 n2;
            buf >> n1;
            buf >> n2;
            footnotes.add( LVPageFootNoteInfo( n1, n2 ) );
        }
    }
    return !buf.error();
}

