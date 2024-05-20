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

// Uncomment to use old page splitter code
// #define USE_LEGACY_PAGESPLITTER

// Uncomment for debugging legacy page splitting algorithm:
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

static LVRendPageContext * main_context = NULL;

LVRendPageContext::LVRendPageContext(LVRendPageList * pageList, int pageHeight, int docFontSize, bool gatherLines)
    : callback(NULL), totalFinalBlocks(0)
    , renderedFinalBlocks(0), lastPercent(-1), page_list(pageList), page_h(pageHeight)
    , doc_font_size(docFontSize), gather_lines(gatherLines), current_flow(0), max_flow(0), current_flow_empty(false)
    , footNotes(64), curr_note(NULL)
{
    if ( callback ) {
        callback->OnFormatStart();
    }
}

bool LVRendPageContext::updateRenderProgress( int numFinalBlocksRendered )
{
    if ( !callback ) {
        if ( main_context ) {
            main_context->updateRenderProgress( numFinalBlocksRendered );
        }
        return false;
    }
    if ( !main_context && callback ) {
        // Save the main context (with the progress callback), so other
        // flows' contexts can forward their progress to it
        main_context = this;
    }
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

/// Get the number of links in the current line links list, or
// in link_ids when !gather_lines
int LVRendPageContext::getCurrentLinksCount()
{
    if ( !gather_lines ) {
        return link_ids.length();
    }
    if ( lines.empty() )
        return 0;
    return lines.last()->getLinksCount();
}

/// append or insert footnote link to last added line
void LVRendPageContext::addLink( lString32 id, int pos )
{
    if ( !gather_lines ) {
        if ( pos >= 0 ) // insert at pos
            link_ids.insert( pos, id );
        else // append
            link_ids.add( id );
        return;
    }
    if ( lines.empty() )
        return;
    LVFootNoteRef note = getOrCreateFootNote( id, false ); // not yet actual
    lines.last()->addLink(note.get(), pos);
}

/// mark start of foot note
void LVRendPageContext::enterFootNote( lString32 id )
{
    if ( !page_list )
        return;
    //CRLog::trace("enterFootNote( %s )", LCSTR(id) );
    if ( curr_note != NULL ) {
        CRLog::error("Nested entering note" );
        return;
    }
    curr_note = getOrCreateFootNote( id ).get();
}

void LVRendPageContext::enterFootNote( lString32Collection & ids )
{
    if ( !page_list )
        return;
    if ( curr_note != NULL ) {
        CRLog::error("Nested entering note" );
        return;
    }
    curr_note = getOrCreateFootNote( ids ).get();
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

void LVRendPageContext::newFlow( bool nonlinear )
{
    /// A new non-linear flow gets the next number
    /// A new linear flow simply gets appended to flow 0
    if ( current_flow > 0 && current_flow_empty ) {
        // We created a flow but didn't fill it: have it like it never happened
        max_flow--;
        if ( page_list )
            page_list->setHasNonLinearFlows(max_flow > 0);
        current_flow_empty = false;
    }
    if (nonlinear) {
        max_flow++;
        current_flow = max_flow;
        current_flow_empty = true;
        if ( page_list )
            page_list->setHasNonLinearFlows(max_flow > 0);
    } else {
        current_flow = 0;
    }
}


void LVRendPageContext::AddLine( int starty, int endy, int flags )
{
    #ifdef DEBUG_PAGESPLIT
        printf("PS: AddLine (%x, #%d): %d > %d (%d)\n", this, lines.length(), starty, endy, flags);
    #endif
    if ( curr_note!=NULL )
        flags |= RN_SPLIT_FOOT_NOTE;
    LVRendLineInfo * line = new LVRendLineInfo(starty, endy, flags, current_flow);
    lines.add( line );
    current_flow_empty = false;
    if ( curr_note != NULL ) {
        //CRLog::trace("adding line to note (%d)", line->start);
        curr_note->addLine( line );
    }
}

// We use 1.0rem (1x root font size) as the footnote margin (vertical margin
// between text and first footnote)
#define FOOTNOTE_MARGIN_REM 1

#ifdef USE_LEGACY_PAGESPLITTER
// helper class
struct PageSplitState {
public:
    int page_h;
    int doc_font_size;
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
    LVPtrVector<const LVRendLineInfo> own_lines; // to store our own made lines, so we can clear them when done
    int lastpageend;
    int nb_lines;
    int nb_lines_rtl;
    int nb_footnotes_lines;
    int nb_footnotes_lines_rtl;
    int current_flow;

    PageSplitState(LVRendPageList * pl, int pageHeight, int docFontSize)
        : page_h(pageHeight)
        , doc_font_size(docFontSize)
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
        , nb_lines(0)
        , nb_lines_rtl(0)
        , nb_footnotes_lines(0)
        , nb_footnotes_lines_rtl(0)
        , current_flow(0)
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

    void ResetLineAccount(bool reset_footnotes=false)
    {
        nb_lines = 0;
        nb_lines_rtl = 0;
        if (reset_footnotes) {
            nb_footnotes_lines = 0;
            nb_footnotes_lines_rtl = 0;
        }
    }
    void AccountLine( const LVRendLineInfo * line )
    {
        nb_lines++;
        if ( line->flags & RN_LINE_IS_RTL )
            nb_lines_rtl++;
    }
    void AccountFootnoteLine( const LVRendLineInfo * line )
    {
        nb_footnotes_lines++;
        if ( line->flags & RN_LINE_IS_RTL )
            nb_footnotes_lines_rtl++;
    }
    int getLineTypeFlags()
    {
        int flags = 0;
        if ( nb_lines_rtl > nb_lines / 2 )
            flags |= RN_PAGE_MOSTLY_RTL;
        if ( nb_footnotes_lines_rtl > nb_footnotes_lines / 2 )
            flags |= RN_PAGE_FOOTNOTES_MOSTLY_RTL;
        return flags;
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
                pagestart ? pagestart->getStart() : -111111111,
                last ? last->getEnd() : -111111111,
                pagestart && last ? last->getEnd() - pagestart->getStart() : -111111111);
        #endif
        ResetLineAccount();
        if (line)
            AccountLine(line);
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
            printf("PS: ========= ADDING PAGE %d: %d > %d h=%d",
                page_list->length(), start, start+h, h);
            if (footheight || hasFootnotes)
                printf(" (+ %d footnotes, fh=%d => h=%d)", footnotes.length(),
                    footheight, h+footheight+FOOTNOTE_MARGIN_REM*doc_font_size);
            printf(" [rtl l:%d/%d fl:%d/%d]\n", nb_lines_rtl, nb_lines, nb_footnotes_lines_rtl, nb_footnotes_lines);
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
        page->flags |= getLineTypeFlags();
        if (pagestart)
            current_flow = pagestart->flow;
        page->flow = current_flow;
        ResetLineAccount(true);
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
        int h;
        if ( line && pagestart )
            h = line->getEnd() - pagestart->getStart();
        else if ( line )
            h = line->getHeight();
        else
            h = 0;
        int footh = 0 /*currentFootnoteHeight()*/ + footheight;
        if ( footh )
            h += FOOTNOTE_MARGIN_REM*doc_font_size + footh;
        return h;
    }
    void SplitLineIfOverflowPage( LVRendLineInfo * line )
    {
        // If page_h <= 0 this function will fall into an infinite loop
        // in which it will allocate memory until it runs out.
        if (page_h <= 0)
            return;
        // A 'line' is usually a line of text from a paragraph, but
        // can be an image, or a fully rendered table row (<tr>), and
        // can have a huge height, possibly overflowing page height.
        // This line (rendered block) is at start of page. If its
        // height is greater than page height, we need to split it
        // and put slices on new pages. We have no knowledge of what
        // this 'line' contains (text, image...), so we can't really
        // find a better 'y' to cut at than the page height. We may
        // then cut in the middle of a text line, and have halves
        // displayed on different pages (although it seems crengine
        // deals with displaying the cut line fully at start of
        // next page).
        // we don't take the current footnotes height into account
        // here to get the maximum vertical space for this line (to
        // avoid more splits if we were to display the accumulated
        // footnotes with the first slice). They will be displayed
        // on the page with the last slice of the overflowed 'line'.

        // If we have a previous pagestart (which will be 'line' if
        // we are called following a StartPage(line), take it as
        // part of the first slice
        int slice_start = pagestart ? pagestart->getStart() : line->getStart();
        #ifdef DEBUG_PAGESPLIT
            if (line->getEnd() - slice_start > page_h) {
                printf("PS:     line overflows page: %d > %d\n",
                    line->getEnd() - slice_start, page_h);
            }
        #endif
        bool did_slice = false;
        while (line->getEnd() - slice_start > page_h) {
            if (did_slice)
                AccountLine(line);
            // Greater than page height: we need to cut
            LVRendPageInfo * page = new LVRendPageInfo(slice_start, page_h, page_list->length());
            #ifdef DEBUG_PAGESPLIT
                printf("PS: ==== SPLITTED AS PAGE %d: %d > %d h=%d\n",
                    page_list->length(), slice_start, slice_start+page_h, page_h);
            #endif
            page->flags |= getLineTypeFlags();
            page->flow = line->flow;
            ResetLineAccount();
            page_list->add(page);
            slice_start += page_h;
            lastpageend = slice_start;
            last = new LVRendLineInfo(slice_start, line->getEnd(), line->flags, line->flow);
            own_lines.add( last ); // so we can have it 'delete'd in Finalize()
            pageend = last;
            did_slice = true;
        }
        if (did_slice) {
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
        if (pagestart==NULL ) { // first line added,
                                // or new page created by addition of footnotes
            int footnotes_h = currentHeight(NULL);
            if (footnotes_h > 0) { // we have some footnote on this new page
                if (footnotes_h + line->getHeight() > page_h) { // adding this line would overflow
                    #ifdef DEBUG_PAGESPLIT
                        printf("   overflow over previous footnotes, letting footnotes on their own page\n");
                    #endif
                    AddToList(); //create a page with only the footnotes
                }
            }
            if ( line->flags & RN_SPLIT_DISCARD_AT_START ) {
                // Don't put this line at start of new page (this flag
                // is set on a margin line when page split auto, as it should
                // be seen when inside page, but should not be seen on top of
                // page - and can be kept if it fits at the end of previous page)
                #ifdef DEBUG_PAGESPLIT
                    printf("PS:   discarded discardable line at start of page %d\n", page_list->length());
                #endif
                return;
            }
            #ifdef DEBUG_PAGESPLIT
                printf("   starting page with it\n");
            #endif
            last = line; // 'last' should never be NULL from now on
                         // (but it can still happen when footnotes are enabled,
                         // with long footnotes spanning multiple pages)
            StartPage( line );
            SplitLineIfOverflowPage(line); // may update 'last'
        }
        else {
            #ifdef DEBUG_PAGESPLIT
                printf("   to current page %d>%d h=%d\n",
                    pagestart->getStart(), last->getEnd(),
                    last->getEnd() - pagestart->getStart());
            #endif
            // Check if line has some backward part
            if (line->getEnd() < last->getEnd()) {
                #ifdef DEBUG_PAGESPLIT
                    printf("PS:   FULLY BACKWARD, IGNORED\n");
                #endif
                return; // for table cells
            }
            else if (line->getStart() < last->getEnd()) {
                #ifdef DEBUG_PAGESPLIT
                    printf("PS:   SOME PART BACKWARD, CROPPED TO FORWARD PART\n");
                #endif
                // Make a new line with the forward part only, and avoid a
                // split with previous line (which might not be enough if
                // the backward spans over multiple past lines which had
                // SPLIT_AUTO - but we can't change the past...)
                lUInt16 flags = line->flags & ~RN_SPLIT_BEFORE_ALWAYS & RN_SPLIT_BEFORE_AVOID;
                line = new LVRendLineInfo(last->getEnd(), line->getEnd(), flags);
                own_lines.add( line ); // so we can have it 'delete'd in Finalize()
            }
            unsigned flgSplit = CalcSplitFlag( last->getSplitAfter(), line->getSplitBefore() );
            //bool flgFit = currentHeight( next ? next : line ) <= page_h;
            bool flgFit = currentHeight( line ) <= page_h;

            if (!flgFit && flgSplit==RN_SPLIT_AVOID && pageend && next
                        && line->getHeight() <= page_h) {
                        // (But not if the line itself is taller than page height
                        // and would be split again: keeping it on current page
                        // (with its preceeding line) may prevent some additional
                        // splits.)
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

            if (!flgFit && line->getHeight() > page_h) {
                // If it doesn't fit and if the line itself is taller than
                // page height and would be split again, keep it on current
                // page to possibly avoid additional splits.
                SplitLineIfOverflowPage(line); // may update 'last'
            }
            else if (!flgFit) {
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
                if ( flgSplit==RN_SPLIT_AUTO && (line->flags & RN_SPLIT_DISCARD_AT_START) ) {
                    // Don't put this line at start of new page (this flag
                    // is set on a margin line when page split auto, as it should
                    // be seen when inside page, but should not be seen on top of
                    // page - and can be kept if it fits at the end of previous page)
                    StartPage(NULL);
                    last = NULL; // and don't even put it on last page if it ends the book
                    #ifdef DEBUG_PAGESPLIT
                        printf("PS:   discarded discardable line at start of page %d\n", page_list->length());
                    #endif
                }
                else {
                    last = line;
                    StartPage(line);
                    SplitLineIfOverflowPage(line); // may update 'last'
                }
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
                AccountLine(line); // might be wrong if we split backward, but well...
            }
            else if (flgSplit==RN_SPLIT_AVOID) {
                #ifdef DEBUG_PAGESPLIT
                    printf("PS:   fit, split to avoid\n");
                #endif
                // Don't update previous 'next' and 'pageend' (they
                // were updated on last AUTO or ALWAYS), so we know
                // we can still split on these if needed.
                last = line;
                AccountLine(line); // might be wrong if we split backward, but well...
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
        own_lines.clear();
    }
    void StartFootNote( LVFootNote * note )
    {
        #ifdef DEBUG_FOOTNOTES
            CRLog::trace( "StartFootNote(%d)", note->length() );
        #endif
        if ( !note || note->length()==0 )
            return;
        footnote = note;
        //footstart = footnote->getLine(0);
        //footlast = footnote->getLine(0);
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
            + (footheight==0 ? FOOTNOTE_MARGIN_REM*doc_font_size : 0);
        int h = currentHeight(NULL); //next
        #ifdef DEBUG_FOOTNOTES
            CRLog::trace("Add footnote line %d  footheight=%d  h=%d  dh=%d  page_h=%d",
                line->start, footheight, h, dh, page_h);
        #endif
        #ifdef DEBUG_PAGESPLIT
            printf("PS: Adding footnote line h=%d => current footnotes height=%d (available: %d)\n",
                line->getEnd() - line->getStart(), dh, page_h - h);
        #endif
        if ( h + dh > page_h ) {
            #ifdef DEBUG_FOOTNOTES
                CRLog::trace("No current page space for this line, %s",
                    (footstart?"footstart is not null":"footstart is null"));
            #endif
            #ifdef DEBUG_PAGESPLIT
                if (footstart)
                    printf("PS:   does not fit, splitting current footnote\n");
                else
                    printf("PS:   does not fit, starting footnote on next page\n");
                // printf("PS:       pageend=%d, next=%d, last=%d\n",
                //    pageend?pageend->getEnd():-1, next?next->getStart():-1, last->getStart());
            #endif
            if (footstart) { // Add what fitted to current page
                AddFootnoteFragmentToList();
            }

            // Forget about SPLIT_AVOID when footnotes are in the way,
            // keeping things simpler and natural.
            // (But doing that will just cut an ongoing float...
            // not obvious how to do that well: the alternative
            // below seems sometimes better, sometimes worse...)
            pageend = last;
            AddToList(); // create a page with current text and footnotes
            StartPage(NULL);

            /* Alternative, splitting on previous allowed position (pageend)
               if we're in SPLIT_AVOID:
            AddToList(); // create a page with current text (up to 'pagenend'
                         // or 'last') and footnotes
            // StartPage( last );
            // We shouldn't use 'last', as we could lose some lines if
            // we're into some SPLIT_AVOID, and footnotes fill current
            // height before we get the chance to deal with it with
            // normal lines in AddLine().
            if (pageend && next)
                // Safer to use 'next' if there is one to not lose any
                // normal lines (footnotes for what will be pushed to next
                // page might then be displayed on the previous page... but
                // this somehow avoid pushing stuff too much on new pages)
                StartPage(next);
            else
                // Otherwise, use NULL instead of 'last': 'last' is already
                // on the previous page, and if coming foonotes span multiple
                // page, 'last' might be displayed on each of these pages!
                StartPage(NULL);
            */

            footstart = footlast = line;
            footend = NULL;
            AccountFootnoteLine(line);
            return;
        }
        if ( footstart==NULL ) {
            #ifdef DEBUG_PAGESPLIT
                printf("PS:   fit, footnote started, added to current page\n");
            #endif
            footstart = footlast = line;
            footend = line;
        } else {
            #ifdef DEBUG_PAGESPLIT
                printf("PS:   fit, footnote continued, added to current page\n");
            #endif
            footend = line;
            footlast = line;
        }
        AccountFootnoteLine(line);
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
#endif // USE_LEGACY_PAGESPLITTER

#ifndef USE_LEGACY_PAGESPLITTER
struct PageSplitState2 {
public:
    LVRendPageList * page_list;
    LVPtrVector<LVRendLineInfo> & lines;
    int page_height;
    int doc_font_size;
    int footnote_margin;
    int nb_lines;

    int cur_page_top;
    int cur_page_bottom;
    int cur_page_footnotes_h;
    int cur_page_nb_lines;
    int cur_page_nb_lines_rtl;
    int cur_page_nb_footnotes_lines;
    int cur_page_nb_footnotes_lines_rtl;
    int cur_page_flow;
    int prev_page_flow;

    LVArray<LVPageFootNoteInfo> cur_page_footnotes;
    LVArray<LVFootNote *> cur_page_seen_footnotes; // footnotes already on this page, to avoid duplicates
    LVArray<LVFootNote *> delayed_footnotes; // footnotes to be displayed on a next page

    PageSplitState2(LVRendPageList * pl, LVPtrVector<LVRendLineInfo> & ls, int pageHeight, int docFontSize)
        : page_list(pl) // output
        , lines(ls)     // input
        , page_height(pageHeight) // parameters
        , doc_font_size(docFontSize)
    {
        footnote_margin = FOOTNOTE_MARGIN_REM * doc_font_size;
        nb_lines = lines.length();
        prev_page_flow = 0;
        // With FB2 books with a cover, a first page has already been added
        // by ldomDocument::render(), and the first content line's y is not 0
        resetCurPageData(nb_lines ? lines[0]->getStart() : 0);
    }

    void resetCurPageData(int page_top) {
        cur_page_top = page_top;
        cur_page_bottom = page_top;
        cur_page_footnotes_h = 0;
        cur_page_nb_lines = 0;
        cur_page_nb_lines_rtl = 0;
        cur_page_nb_footnotes_lines = 0;
        cur_page_nb_footnotes_lines_rtl = 0;
        cur_page_flow = -1;
        cur_page_seen_footnotes.reset();
    }

    inline void accountLine(bool is_rtl=false) {
        cur_page_nb_lines++;
        if ( is_rtl)
            cur_page_nb_lines_rtl++;
    }

    inline void accountFootnoteLine(bool is_rtl=false) {
        cur_page_nb_footnotes_lines++;
        if ( is_rtl)
            cur_page_nb_footnotes_lines_rtl++;
    }

    inline int getCurPageMaxBottom() {
        return cur_page_top + (page_height - cur_page_footnotes_h);
    }

    inline int getAvailableHeightForFootnotes() {
        // If no footnote height yet, account for the margin that would be used with the first line
        return page_height - (cur_page_bottom - cur_page_top)
                      - (cur_page_footnotes_h > 0 ? cur_page_footnotes_h : footnote_margin);
    }

    inline void pushDelayedFootnotes() {
        if ( !delayed_footnotes.empty() ) {
            for ( int i=0; i<delayed_footnotes.length(); i++ )
                addFootnoteToPage( delayed_footnotes[i] );
            delayed_footnotes.reset();
        }
    }

    void flushCurrentPage(bool push_delayed=true) {
        if ( cur_page_nb_lines > 0 || cur_page_nb_footnotes_lines > 0 ) {
            #ifdef DEBUG_PAGESPLIT
                printf("PS: ========= ADDING PAGE %d: %d > %d h=%d",
                    page_list->length(), cur_page_top, cur_page_bottom, cur_page_bottom - cur_page_top);
                if ( cur_page_footnotes.length() > 0 )
                    printf(" (+ %d footnotes, fh=%d)", cur_page_footnotes.length(), cur_page_footnotes_h);
                printf(" [rtl l:%d/%d fl:%d/%d]\n", cur_page_nb_lines_rtl, cur_page_nb_lines,
                                            cur_page_nb_footnotes_lines_rtl, cur_page_nb_footnotes_lines);
            #endif
            // Some content was added: create and add the new page
            LVRendPageInfo * page = new LVRendPageInfo(cur_page_top, cur_page_bottom - cur_page_top, page_list->length());
            page_list->add(page);

            // Flag the page if it looks like it has more RTL than LTR content
            if ( cur_page_nb_lines_rtl > cur_page_nb_lines / 2 )
                page->flags |= RN_PAGE_MOSTLY_RTL;
            if ( cur_page_nb_footnotes_lines_rtl > cur_page_nb_footnotes_lines / 2 )
                page->flags |= RN_PAGE_FOOTNOTES_MOSTLY_RTL;

            // If no cur_page_flow set (page with only footnotes), use the one from previous page
            page->flow = cur_page_flow < 0 ? prev_page_flow : cur_page_flow;
            prev_page_flow = page->flow;

            // Add footnotes
	    if ( cur_page_footnotes.length() > 0 ) {
		page->footnotes.add( cur_page_footnotes );
		cur_page_footnotes.reset();
	    }

            // Make the new page start when the previous page ended
            resetCurPageData(cur_page_bottom);
        }
        if ( push_delayed ) { // Add any delayed footnotes to the new page
            pushDelayedFootnotes();
        }
    }

    void addLinesToPage(int start, int end) {
        // We're usually called with a single line (start==end), but we
        // can get multiple lines.
        // We may have backward lines (overlapping with previous lines),
        // so find out the bottomest of all.
        // (We don't care about parts that would overlap with what has
        // already been added to the page.)
        int lines_max_bottom = cur_page_bottom;
        bool has_footnotes = false;
        bool push_delayed_footnotes = false; // done only in one specific case below
        int start_if_new_page = -1;
        int orig_start = start; // to not ignore footnotes on lines discarded at start
        for ( int i=start; i <= end; i++ ) {
            LVRendLineInfo * line = lines[i];
            if ( start_if_new_page < 0 && !(line->flags & RN_SPLIT_DISCARD_AT_START) ) {
                // Margin lines with SPLIT_AUTO are flagged with RN_SPLIT_DISCARD_AT_START:
                // They should be shown inside a page, but should be discarded if they
                // would be at the top of a new page.
                start_if_new_page = i;
            }
            if ( lines_max_bottom < lines[i]->getEnd() ) {
                lines_max_bottom = lines[i]->getEnd();
            }
            if ( !has_footnotes && lines[i]->getLinks() ) {
                has_footnotes = true;
            }
        }
        // Handle a rare edge case
        if ( cur_page_nb_lines == 0 && start_if_new_page >= 0 && (cur_page_nb_footnotes_lines>0 || !delayed_footnotes.empty()) ) {
            // Empty page, but with footnotes or delayed footnotes not yet added.
            // These footnotes are associated to the previous page's flow.
            // If the lines we are about to add are from a different flow, we don't want
            // these footnotes to be with them (or they could get hidden and skipped).
            // If that's the case, add any delayed footnotes to this empty page, before
            // creating a new page for these lines from another flow.
            LVRendLineInfo * line = lines[start_if_new_page];
            if ( line->flow != prev_page_flow ) {
                pushDelayedFootnotes();
                flushCurrentPage(false);
                // and call us again
                addLinesToPage(start, end);
                return;
            }
        }
        #ifdef DEBUG_PAGESPLIT
            printf("PS: adding (%d+%d) to current page %d>%d\n", lines[start]->getStart() - cur_page_bottom,
                            lines_max_bottom - lines[start]->getStart(), cur_page_top, cur_page_bottom);
        #endif
        // printf("addLinesToPage %d (%d %d) [%d->%d %d]\n", end-start+1, start, end,
        // lines[start]->getStart(), lines[end]->getEnd(), lines[end]->getEnd() - lines[start]->getStart());
        if ( lines_max_bottom > getCurPageMaxBottom() ) {
            // Does not fit on this page
            if ( lines_max_bottom - cur_page_bottom <= page_height ) {
                // If we end current page at the current cur_page_bottom,
                // these lines will fit in a new empty page
                flushCurrentPage(false);
                // We don't push delayed footnotes (as this would reduce
                // the available height and these lines may then not fit),
                // so make sure that if there is any, we'll push them below
                // before adding our lines' own footnotes
                push_delayed_footnotes = true;
            }
        }
        if ( cur_page_nb_lines == 0 && start_if_new_page != start ) {
            // Empty page, either after our flush or already empty:
            // ignore first line(s) marked with RN_SPLIT_DISCARD_AT_START
            if ( start_if_new_page < 0 ) {
                // Only discardable lines: ignore them, make the new page start after them
                cur_page_top = lines[end]->getEnd();
                cur_page_bottom = cur_page_top;
                if ( push_delayed_footnotes ) {
                    // We made a new page just above, but didn't add any line:
                    // add any delayted footnotes (as we would do below)
                    pushDelayedFootnotes();
                }
                return;
            }
            start = start_if_new_page;
            cur_page_top = lines[start]->getStart();
            cur_page_bottom = cur_page_top;
        }
        // Either we have enough room to fit them all on the page,
        // or we made a new page (but we may still not have enough room)
        for ( int i=start; i <= end; i++ ) {
            LVRendLineInfo * line = lines[i];
            if ( cur_page_flow < 0 ) {
                // We set the flow from the first line (non-footnote) that will
                // go on this page (we can't just use the last, as the last line
                // in a non-linear flow may be some bottom vertical margin which
                // will collapse with some of the next non non-linear one, and
                // could get flow=0. (This was witnessed, not sure it's not a
                // bug in lvrend.cpp non-linear-flow/sequence handling.)
                cur_page_flow = line->flow;
            }
            int line_bottom = line->getEnd();
            if ( line_bottom <= getCurPageMaxBottom() ) {
                // This line fits on the current page: just add it
                if ( cur_page_bottom < line_bottom ) {
                    cur_page_bottom = line_bottom;
                }
                accountLine( line->flags & RN_LINE_IS_RTL);
            }
            else {
                // This line does not fit on the current page.
                // We could do what we did for the first line above: make
                // a new page if this line would fit on an empty page, but
                // this could add lots of blank space and kill the notion
                // of break:avoid associated with all the lines we get here.
                // It feels better (although possibly uglier) to keep them
                // stacked onto each other and filling the pages, by slicing
                // individual lines
                // But if the line fits on a page, and the blank space we'd
                // get at bottom by flushing the page is sufficiently small,
                // we may do that. (If the line does not fit on a page, it
                // will be sliced anyway, so we may as well slice it now.)
                // Let's use 2em as "sufficiently small", which should ensure
                // that lines of main content text (and small headings), that
                // can't fit in what's left on a page, are pushed uncut to
                // the next page, avoiding cutted lines of text.
                if ( (line->getHeight() <= page_height) &&
                     (getCurPageMaxBottom()-cur_page_bottom < doc_font_size*2) ) {
                    flushCurrentPage(false);
                    push_delayed_footnotes = true; // as done above
                    cur_page_flow = line->flow;
                    if ( line->flags & RN_SPLIT_DISCARD_AT_START ) {
                        // If this line we're slicing should be discarded
                        // at start, forget it now
                        cur_page_top = line_bottom;
                        cur_page_bottom = cur_page_top;
                    }
                    else {
                        cur_page_bottom = line_bottom;
                        accountLine( line->flags & RN_LINE_IS_RTL);
                    }
                }
                else {
                    // Slice this line.
                    // We have no knowledge of what this 'line' contains (text,
                    // image...), so we can't really find a better 'y' to cut
                    // at than the page height. We may then cut in the middle
                    // of a text line, and have halves displayed on different
                    // pages (although it seems crengine deals with displaying
                    // the cut line fully at start of next page).
                    // So, slice it and make pages as long as it keeps not fitting
                    bool discarded = false;
                    while ( line_bottom > getCurPageMaxBottom() ) {
                        cur_page_bottom = getCurPageMaxBottom();
                        accountLine( line->flags & RN_LINE_IS_RTL);
                        // Don't push any delayed footnote, so get the most
                        // of space for these lines
                        flushCurrentPage(false);
                        push_delayed_footnotes = true; // as done above
                        if ( line->flags & RN_SPLIT_DISCARD_AT_START ) {
                            // If this line we're slicing should be discarded
                            // at start, forget it now
                            cur_page_top = line_bottom;
                            cur_page_bottom = cur_page_top;
                            discarded = true;
                            break;
                        }
                        cur_page_flow = line->flow;
                    }
                    if ( !discarded ) {
                        cur_page_bottom = line_bottom;
                        accountLine( line->flags & RN_LINE_IS_RTL);
                    }
                }
            }
        }
        // Handle footnotes (if any) after we have handled all the non-splittable main lines
        if ( push_delayed_footnotes && !delayed_footnotes.empty() ) {
            // Above, we flushed one or more new pages without pushing
            // delayed footnotes, so push them now.
            // But only if the first line of them fits. Otherwise, keep
            // them delayed (our own footnotes will then be delayed too).
            if ( delayed_footnotes[0]->getLine(0)->getHeight() <= getAvailableHeightForFootnotes() ) {
                pushDelayedFootnotes();
            }
        }
        if ( has_footnotes ) {
            addLinesFootnotesToPage(orig_start, end);
        }
    }

    void addFootnoteToPage(LVFootNote * note) {
        if ( note->empty() )
            return;
        // Avoid duplicated footnotes in the same page
        if ( cur_page_seen_footnotes.indexOf(note) >= 0 )
            return;
        cur_page_seen_footnotes.add(note);
        // Also check the actual footnote if this one is just a proxy
        LVFootNote * actual_footnote = note->getActualFootnote();
        if ( actual_footnote ) {
            if ( cur_page_seen_footnotes.indexOf(actual_footnote) >= 0 )
                return;
            cur_page_seen_footnotes.add(actual_footnote);
        }

        int note_nb_lines = note->length();
        int note_top = -1;
        int note_bottom = -1;
        for ( int i=0; i < note_nb_lines; i++ ) {
            // Note: we don't ensure SPLIT_AVOID/ALWAYS inside footnotes
            LVRendLineInfo * line = note->getLine(i);
            if ( note_top < 0 )
                note_top = line->getStart();
            int new_note_bottom = line->getEnd();
            if ( new_note_bottom - note_top > getAvailableHeightForFootnotes() ) {
                // This new line makes the footnote not fit
                if ( note_bottom >= 0 ) {
                    // Some previous lines fitted: add them to current page
                    int note_height = note_bottom - note_top;
                    cur_page_footnotes.add( LVPageFootNoteInfo( note_top, note_height ) );
                    if (cur_page_footnotes_h == 0)
                        cur_page_footnotes_h = footnote_margin;
                    cur_page_footnotes_h += note_height;
                    // We'll add a new footnote with remaining lines to the new page
                    note_top = line->getStart();
                }
                flushCurrentPage(false); // avoid re-adding delayed footnotes, which we might be doing
                // We're not yet done adding this footnote (so it will continue
                // on the new page): consider it already present in this new page
                // (even if one won't see the starting text and footnote number,
                // it's better than seeing again the same duplicated footnote text)
                cur_page_seen_footnotes.add(note);
                if ( actual_footnote )
                    cur_page_seen_footnotes.add(actual_footnote);
            }
            // This footnote line fits
            note_bottom = new_note_bottom;
            accountFootnoteLine(line->flags & RN_LINE_IS_RTL);
        }
        // Add this footnote (or the remaining part of it) to current page
        int note_height = note_bottom - note_top;
        if ( note_height > 0 ) {
            cur_page_footnotes.add( LVPageFootNoteInfo( note_top, note_height ) );
            if (cur_page_footnotes_h == 0)
                cur_page_footnotes_h = footnote_margin;
            cur_page_footnotes_h += note_height;
        }
        // Note: we don't handle individual footnote lines larger than page height (which
        // must be rare), but it looks like we'll just make with this line a page larger
        // than screen height, that will then be truncated when rendered. Next page will
        // continue with the remaining footnotes or content lines - which looks fine.
    }

    void addLinesFootnotesToPage(int start, int end) {
        for ( int i=start; i <= end; i++ ) {
            LVRendLineInfo * line = lines[i];
            if ( !line->getLinks() )
                continue;
            for ( int j=0; j<line->getLinks()->length(); j++ ) {
                LVFootNote* note = line->getLinks()->get(j);
                if ( note->empty() )
                    continue;
                if ( cur_page_seen_footnotes.indexOf(note) >= 0 )
                    continue; // Already shown on this page
                LVFootNote * actual_footnote = note->getActualFootnote();
                if ( actual_footnote && cur_page_seen_footnotes.indexOf(actual_footnote) >= 0 )
                    continue;
                if ( !delayed_footnotes.empty() ) {
                    // Already some delayed footnotes
                    if ( delayed_footnotes.indexOf(note) < 0 )
                        delayed_footnotes.add( note );
                    continue;
                }
                if ( cur_page_nb_footnotes_lines == 0 ) {
                    // See if a first footnote line + its top margin fit on the page.
                    // If they don't, delay all footnotes but don't flush the page,
                    // as some main content line could still fit on this page.
                    if ( note->getLine(0)->getHeight() > getAvailableHeightForFootnotes() ) {
                        if ( delayed_footnotes.indexOf(note) < 0 )
                            delayed_footnotes.add( note );
                        continue;
                    }
                    // Note: this could be made even more generic, delaying 2++ lines
                    // of a footnote and 2++ footnotes in case of tall footnotes lines
                    // (images? tables? which must be rare)
                }
                // We can proceed adding this footnote to the page (and flushing pages,
                // continuing a footnote or adding others on new pages)
                addFootnoteToPage( note );
            }
        }
    }

    void splitToPages() {
        // A "line" is a slice of the document, it's a unit of what can be stacked
        // into pages. It has a y coordinate as start and an other at end,
        // that make its height.
        // It's usually a line of text, or an image, but we also have one
        // for each non-zero vertical margin, border and padding above and
        // below block elements.
        // A single "line" can also include multiple lines of text that have to
        // be considered as a slice-unit for technical reasons: this happens with
        // table rows (TR), so a table row will usually not be splitted among
        // multiple pages, but pushed to the next page (except when a single row
        // can't fit on a page: we'll then split inside that unit of slice).
        if (nb_lines == 0)
            return;
        // Some of these lines can be flagged to indicate they should stick
        // together: if not enough room at a bottom of a page, they can force
        // a new page so they have more room at the top of the next page. This
        // includes: top/bottom margins/borders/padding around blocks, lines of
        // text with explicite page-break-inside: avoid, lines of text when CSS
        // widows/orphans, lines of text surrounded by floats spanning them...
        //
        // Check for and handle lines per group of non-breakable lines
        int start = 0;
        for ( int i=0; i < nb_lines; i++ ) {
            #ifdef DEBUG_PAGESPLIT
                LVRendLineInfo * line = lines[i];
                printf("PS:   line %d>%d h=%d, flags=<%d|%d>\n",
                    line->getStart(), line->getEnd(), line->getHeight(),
                    line->getSplitBefore(), line->getSplitAfter());
            #endif
            bool force_new_page = false;
            bool is_last_line = i == nb_lines-1;
            int nxt_line_start = 0;
            if ( !is_last_line ) {
                LVRendLineInfo * cur_line = lines[i];
                LVRendLineInfo * nxt_line = lines[i+1];
                int cur_after = cur_line->getSplitAfter();
                int nxt_before = nxt_line->getSplitBefore();
                if ( cur_after==RN_SPLIT_AVOID || nxt_before==RN_SPLIT_AVOID) {
                    // Should stick together
                    continue;
                }
                nxt_line_start = nxt_line->getStart();
                if ( nxt_line_start < cur_line->getEnd() ) {
                    // Next line has some backward part that overlaps with current line:
                    // avoid a split between them
                    continue;
                    // Note: we may have later lines that could start backward and
                    // overlap with what we have already pushed... Nothing much we can
                    // do except looking way ahead here, which feels excessive.
                    // This should be rare though.
                }
                force_new_page = cur_after==RN_SPLIT_ALWAYS || nxt_before==RN_SPLIT_ALWAYS;
                // (any AVOID has precedence over ALWAYS)
            }
            // We're allowed to break after cur_line (most often, we
            // have start==i and we're just adding a single line).
            addLinesToPage(start, i);
            start = i+1;
            if ( force_new_page && nxt_line_start < cur_page_bottom ) {
                // Next line is backward and overlaps with what has been put
                // on current page: cancel the forced page break
                force_new_page = false;
                // Note: it might feel ok to still make a new page, and (below
                // after flushCurrentPage()) adjust the new page top to start
                // at next_line_start - but in some heavy negative margins
                // conditions, we might see some amount of the same text
                // content on 2 pages, which feels really odd.
            }
            if ( force_new_page || is_last_line ) {
                flushCurrentPage();
            }
        }
    }
};
#endif // !USE_LEGACY_PAGESPLITTER

void LVRendPageContext::split()
{
    if ( !page_list )
        return;
    #ifdef DEBUG_PAGESPLIT
        printf("PS: splitting lines into pages, page height=%d\n", page_h);
    #endif

#ifndef USE_LEGACY_PAGESPLITTER
    PageSplitState2 s(page_list, lines, page_h, doc_font_size);
    s.splitToPages();

#else
    PageSplitState s(page_list, page_h, doc_font_size);

    int lineCount = lines.length();

    LVRendLineInfo * line = NULL;
    for ( int lindex=0; lindex<lineCount; lindex++ ) {
        line = lines[lindex];
        s.AddLine( line );
        // add footnotes for line, if any...
        if ( line->getLinks() ) {
            // These are not needed: we shouldn't mess here with last/next that
            // are correctly managed by s.AddLine() just above.
            // s.last = line;
            // s.next = lindex<lineCount-1?lines[lindex+1]:line;
            bool foundFootNote = false;
            //if ( CRLog::isTraceEnabled() && line->getLinks()->length()>0 ) {
            //    CRLog::trace("LVRendPageContext::split() line %d: found %d links", lindex, line->getLinks()->length() );
           // }
            for ( int j=0; j<line->getLinks()->length(); j++ ) {
                LVFootNote* note = line->getLinks()->get(j);
                if ( note->length() ) {
                    // Avoid duplicated footnotes in the same page
                    if (s.IsFootNoteInCurrentPage(note))
                        continue;
                    foundFootNote = true;
                    s.StartFootNote( note );
                    for ( int k=0; k<note->length(); k++ ) {
                        s.AddFootnoteLine( note->getLine(k) );
                    }
                    s.EndFootNote();
                }
            }
            if ( !foundFootNote )
                line->flags = line->flags & ~RN_SPLIT_FOOT_LINK;
        }
    }
    s.Finalize();
#endif // USE_LEGACY_PAGESPLITTER
}

void LVRendPageContext::Finalize()
{
    split();
    lines.clear();
    footNotes.clear();
    if ( main_context == this ) {
        main_context = NULL;
    }
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
        if (item->flow > 0)
            has_nonlinear_flows = true;
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
    buf << (lUInt32)start;  /// start of page
    buf << (lUInt16)height; /// height of page, does not include footnotes
    buf << (lUInt8) flags;  /// RN_PAGE_*
    buf << (lUInt16)flow;   /// flow the page belongs to
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
    lUInt16 n4;

    buf >> n1 >> n2 >> n3 >> n4; /// start of page

    start = n1;
    height = n2;
    flags = n3;
    flow = n4;

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

void LVRendPageList::replacePages(int old_y, int old_h, LVRendPageList * pages, int next_pages_shift_y)
{
    int remove_idx = -1;
    int remove_count = 0;
    int insert_idx = -1;
    int added_count = pages->length();
    for  ( int i=0; i < length(); i++ ) {
        LVRendPageInfo * pi = ((*this)[i]);
        if ( pi->start + pi->height <= old_y ) {
            // printf("%d (%d +%d) left as is\n", pi->index, pi->start, pi->height);
            continue; // page fully before
        }
        if ( pi->start < old_y ) {
            // part of this page before: truncate it
            pi->height = old_y - pi->start;
            insert_idx = i + 1;
            // printf("%d (%d +%d) truncated\n", pi->index, pi->start, pi->height);
            continue;
        }
        if ( pi->start >= old_y + old_h ) {
            // page fully after
            pi->start += next_pages_shift_y;
            pi->index += added_count - remove_count;
            // printf("%d (%d +%d) after, fixed\n", pi->index, pi->start, pi->height);
        }
        else if ( pi->start + pi->height > old_y + old_h ) {
            // part of this page after: truncate it
            pi->start = old_y + old_h;
            pi->height = pi->start + pi->height - pi->start;
            pi->index += added_count - remove_count;
            // printf("%d (%d +%d) truncated\n", pi->index, pi->start, pi->height);
        }
        else {
            // Page fully in old_y > old_h
            if ( remove_idx < 0 )
                remove_idx = i;
            remove_count++;
            // printf("%d (%d +%d) will be removed\n", pi->index, pi->start, pi->height);
        }
        if ( insert_idx < 0 )
            insert_idx = i;
    }
    // printf("LVRendPageList::replacePages at %d (h=%d +%d): %d/%d -%d+%d\n", old_y, old_h, next_pages_shift_y, remove_idx, insert_idx, remove_count, pages->length());
    if ( remove_idx >= 0 )
        erase( remove_idx, remove_count );
    if ( insert_idx < 0 )
        insert_idx = length();
    while ( pages->length() ) {
        LVRendPageInfo * pi = pages->popHead();
        pi->index = insert_idx;;
        insert( insert_idx, pi );
        // printf("%d (%d +%d) inserted\n", pi->index, pi->start, pi->height);
        insert_idx++;
    }
}
