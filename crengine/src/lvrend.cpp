/*******************************************************

   CoolReader Engine

   lvrend.cpp:  XML DOM tree rendering tools

   (c) Vadim Lopatin, 2000-2006
   This source code is distributed under the terms of
   GNU General Public License
   See LICENSE file for details

*******************************************************/

#include <stdlib.h>
#include <string.h>
#include <lvtextfm.h>
#include "../include/lvtinydom.h"
#include "../include/fb2def.h"
#include "../include/lvrend.h"

// Note about box model/sizing in crengine:
// https://quirksmode.org/css/user-interface/boxsizing.html says:
//   - In the W3C box model, the width of an element gives the width of
//     the content of the box, excluding padding and border.
//   - In the traditional box model, the width of an element gives the
//     width between the borders of the box, including padding and border.
//   By default, all browsers use the W3C box model, with the exception
//   of IE in "Quirks Mode" (IE5.5 Mode), which uses the traditional one.
//
// These models are toggable with CSS in current browsers:
//   - the first one is used when "box-sizing: content-box" (default in browsers)
//   - the second one is used when "box-sizing: border-box".
//
// crengine uses the traditional one (box-sizing: border-box).
//
// See: https://www.456bereastreet.com/archive/201112/the_difference_between_widthauto_and_width100/
// for some example differences in rendering.
//
// As a side note, TABLE {width: auto} has a different behaviour that
// what is described there: with width:auto, a table adjusts its width to
// the table content, and does not take its full container width when
// the content does not need it.
// width: auto is the default for TABLEs in current browsers.
// crengine default used to be "width: 100%", but now that we
// can shrink to fit, it is "width: auto".

int gRenderDPI = DEF_RENDER_DPI; // if 0: old crengine behaviour: 1px/pt=1px, 1in/cm/pc...=0px
bool gRenderScaleFontWithDPI = DEF_RENDER_SCALE_FONT_WITH_DPI;
int gRootFontSize = 24; // will be reset as soon as font size is set

int scaleForRenderDPI( int value ) {
    // if gRenderDPI == 0 or 96, use value as is (1px = 1px)
    if (gRenderDPI && gRenderDPI != BASE_CSS_DPI) {
        value = value * gRenderDPI / BASE_CSS_DPI;
    }
    return value;
}

//#define DEBUG_TREE_DRAW 3
// define to non-zero (1..5) to see block bounds
#define DEBUG_TREE_DRAW 0

//#ifdef _DEBUG
//#define DEBUG_DUMP_ENABLED
//#endif

#ifdef DEBUG_DUMP_ENABLED

class simpleLogFile
{
public:
    FILE * f;
    simpleLogFile(const char * fname) { f = fopen( fname, "wt" ); }
    ~simpleLogFile() { if (f) fclose(f); }
    simpleLogFile & operator << ( const char * str ) { fprintf( f, "%s", str ); fflush( f ); return *this; }
    //simpleLogFile & operator << ( int d ) { fprintf( f, "%d(0x%X) ", d, d ); fflush( f ); return *this; }
    simpleLogFile & operator << ( int d ) { fprintf( f, "%d ", d ); fflush( f ); return *this; }
    simpleLogFile & operator << ( const wchar_t * str )
    {
        if (str)
        {
            for (; *str; str++ )
            {
                fputc( *str >= 32 && *str<127 ? *str : '?', f );
            }
        }
        fflush( f );
        return *this;
    }
    simpleLogFile & operator << ( const lString16 &str ) { return operator << (str.c_str()); }
};

simpleLogFile logfile("/tmp/logfile.log");

#else

// stubs
class simpleLogFile
{
public:
    simpleLogFile & operator << ( const char * ) { return *this; }
    simpleLogFile & operator << ( int ) { return *this; }
    simpleLogFile & operator << ( const wchar_t * ) { return *this; }
    simpleLogFile & operator << ( const lString16 & ) { return *this; }
};

simpleLogFile logfile;

#endif

// prototypes
void copystyle( css_style_ref_t sourcestyle, css_style_ref_t deststyle );
css_page_break_t getPageBreakBefore( ldomNode * el );
int CssPageBreak2Flags( css_page_break_t prop );
///////////////////////////////////////////////////////////////////////////////
//
// TABLE RENDERING CLASSES
//
///////////////////////////////////////////////////////////////////////////////

// Uncomment for debugging table rendering:
// #define DEBUG_TABLE_RENDERING

#define TABLE_BORDER_WIDTH 1

class CCRTableCol;
class CCRTableRow;

class CCRTableCell {
public:
    CCRTableCol * col;
    CCRTableRow * row;
    int width;
    int height;
    int percent;
    int max_content_width;
    int min_content_width;
    short colspan;
    short rowspan;
    char halign;
    char valign;
    ldomNode * elem;
    CCRTableCell() : col(NULL), row(NULL)
    , width(0)
    , height(0)
    , percent(0)
    , max_content_width(0)
    , min_content_width(0)
    , colspan(1)
    , rowspan(1)
    , halign(0)
    , valign(0)
    , elem(NULL)
    { }
};

class CCRTableRowGroup;

class CCRTableRow {
public:
    int index;
    int height;
    int y;
    int numcols; // sum of colspan
    int linkindex;
    ldomNode * elem;
    LVPtrVector<CCRTableCell> cells;
    CCRTableRowGroup * rowgroup;
    CCRTableRow() : index(0)
    , height(0)
    , y(0)
    , numcols(0) // sum of colspan
    , linkindex(-1)
    , elem(NULL)
    , rowgroup(NULL)
    { }
};

class CCRTableRowGroup {
public:
    int index;
    int height;
    int y;
    ldomNode * elem;
    LVPtrVector<CCRTableRow, false> rows;
    CCRTableRowGroup() : index(0)
    , height(0)
    , y(0)
    , elem(NULL)
    { }
};

class CCRTableCol {
public:
    int index;
    int width;
    int percent;
    int max_width;
    int sum_max_content_width;
    int nb_sum_max_content_width;
    int min_width;
    int nrows;
    int x;      // sum of previous col widths
    bool width_auto; // true when no width or percent is specified
    LVPtrVector<CCRTableCell, false> cells;
    ldomNode * elem;
    CCRTableCol() :
    index(0)
    , width(0)
    , percent(0)
    , max_width(0)
    , sum_max_content_width(0)
    , nb_sum_max_content_width(0)
    , min_width(0)
    , nrows(0)
    , x(0) // sum of previous col widths
    , width_auto(true)
    , elem( NULL )
    { }
    ~CCRTableCol() { }
};

/*
    in: string      25   35%
    out:            25   -35
*/
int StrToIntPercent( const wchar_t * s, int digitwidth=0 );
int StrToIntPercent( const wchar_t * s, int digitwidth )
{
    int n=0;
    if (!s || !s[0]) return 0;
    for (int i=0; s[i]; i++) {
        if (s[i]>='0' && s[i]<='9') {
            //=================
            n=n*10+(s[i]-'0');
        } else if (s[i]=='d') {
            //=================
            n=n*digitwidth;
            break;
        } else if (s[i]=='%') {
            //=================
            n=-n;
            break;
        }
    }
    return n;
}

// Utility function used in CCRTable::PlaceCells() when border_collapse.
// border_id is the border index: 0=top, 1=right, 2=bottom, 3=left.
// Update provided target_style and current_target_size.
// We don't implement fully the rules describe in:
//   https://www.w3.org/TR/CSS21/tables.html#border-conflict-resolution
//
// With use_neighbour_if_equal=true, neighbour border wins if its width
// is equal to target width.
// We would have intuitively set it to true, for a TABLE or TR border
// color to be used even if border width is the same as the TD one. But
// the above url rules state: "If border styles differ only in color,
// then a style set on a cell wins over one on a row, which wins over
// a row group, column, column group and, lastly, table."
void collapse_border(css_style_ref_t & target_style, int & current_target_size,
    int border_id, ldomNode * neighbour_node, bool use_neighbour_if_equal=false) {
    if (neighbour_node) {
        int neighbour_size = measureBorder(neighbour_node, border_id);
        if ( neighbour_size > current_target_size ||
                (use_neighbour_if_equal && neighbour_size == current_target_size) ) {
            css_style_ref_t neighbour_style = neighbour_node->getStyle();
            switch (border_id) {
                case 0: target_style->border_style_top = neighbour_style->border_style_top; break;
                case 1: target_style->border_style_right = neighbour_style->border_style_right; break;
                case 2: target_style->border_style_bottom = neighbour_style->border_style_bottom; break;
                case 3: target_style->border_style_left = neighbour_style->border_style_left; break;
            }
            target_style->border_width[border_id] = neighbour_style->border_width[border_id];
            target_style->border_color[border_id] = neighbour_style->border_color[border_id];
            current_target_size = neighbour_size;
        }
    }
}

class CCRTable {
public:
    int table_width;
    int digitwidth;
    bool shrink_to_fit;
    ldomNode * elem;
    ldomNode * caption;
    int caption_h;
    LVPtrVector<CCRTableRow> rows;
    LVPtrVector<CCRTableCol> cols;
    LVPtrVector<CCRTableRowGroup> rowgroups;
    // LVMatrix<CCRTableCell*> cells; // not used (it was filled, but never read)
    CCRTableRowGroup * currentRowGroup;

    void ExtendCols( int ncols ) {
        while (cols.length()<ncols) {
            CCRTableCol * col = new CCRTableCol;
            col->index = cols.length();
            cols.add(col);
        }
    }

    int LookupElem( ldomNode * el, int state ) {
        if (!el->getChildCount())
            return 0;
        int colindex = 0;
        int tdindex = 0;
        for (int i=0; i<el->getChildCount(); i++) {
            ldomNode * item = el->getChildElementNode(i);
            if ( item ) {
                // for each child element
                lvdom_element_render_method rendMethod = item->getRendMethod();
                //CRLog::trace("LookupElem[%d] (%s, %d) %d", i, LCSTR(item->getNodeName()), state, (int)item->getRendMethod() );
                switch ( rendMethod ) {
                case erm_invisible:  // invisible: don't render
                case erm_killed:     // no room to render element
                    // do nothing: invisible
                    break;
                case erm_table:      // table element: render as table
                    // do nothing: impossible
                    break;
                case erm_table_row_group: // table row group
                case erm_table_header_group: // table header group
                case erm_table_footer_group: // table footer group
                    if ( state==0 && currentRowGroup==NULL ) {
                        currentRowGroup = new CCRTableRowGroup();
                        currentRowGroup->elem = item;
                        currentRowGroup->index = rowgroups.length();
                        rowgroups.add( currentRowGroup );
                        LookupElem( item, 0 );
                        currentRowGroup = NULL;
                    } else {
                    }
                    break;
                case erm_table_column_group: // table column group
                    // just fall into groups
                    LookupElem( item, 0 );
                    break;
                case erm_table_row: // table row
                    {
                        // rows of table
                        CCRTableRow * row = new CCRTableRow;
                        row->elem = item;
                        if ( currentRowGroup ) {
                            // add row to group
                            row->rowgroup = currentRowGroup;
                            currentRowGroup->rows.add( row );
                        }
                        rows.add( row );
                        // What could <tr link="number"> have been in the past ?
                        // It's not mentionned in any HTML or FB2 spec,
                        // and row->linkindex is never used.
                        if (row->elem->hasAttribute(LXML_NS_ANY, attr_link)) {
                            lString16 lnk=row->elem->getAttributeValue(attr_link);
                            row->linkindex = lnk.atoi();
                        }
                        // recursion: search for inner elements
                        //int res =
                        LookupElem( item, 1 ); // lookup row
                    }
                    break;
                case erm_table_column: // table column
                    {
                        // cols width definitions
                        ExtendCols(colindex+1);
                        CCRTableCol * col = cols[colindex];
                        col->elem = item;
                        /*
                        lString16 w = item->getAttributeValue(attr_width);
                        if (!w.empty()) {
                            // TODO: px, em, and other length types support
                            int wn = StrToIntPercent(w.c_str(), digitwidth);
                            if (wn<0)
                                col->percent = -wn;
                            else if (wn>0)
                                col->width = wn;
                        }
                        */
                        css_length_t w = item->getStyle()->width;
                        if ( w.type == css_val_percent ) { // %
                            col->percent = w.value / 256;
                        }
                        else if ( w.type != css_val_unspecified ) { // px, em...
                            int em = item->getFont()->getSize();
                            col->width = lengthToPx( w, 0, em );
                            // (0 as the base width for %, as % was dealt with just above)
                        }
                        // otherwise cell->percent and cell->width stay at 0
                        colindex++;
                    }
                    break;
                case erm_list_item:     // obsolete rendering method (used only when gDOMVersionRequested < 20180524)
                case erm_block:         // render as block element (render as containing other elements)
                case erm_final:         // final element: render the whole it's content as single render block
                case erm_mixed:         // block and inline elements are mixed: autobox inline portions of nodes; TODO
                case erm_table_cell:    // table cell
                    {
                        // <th> or <td> inside <tr>

                        if ( rows.length()==0 ) {
                            CCRTableRow * row = new CCRTableRow;
                            row->elem = item;
                            if ( currentRowGroup ) {
                                // add row to group
                                row->rowgroup = currentRowGroup;
                                currentRowGroup->rows.add( row );
                            }
                            rows.add( row );
                        }


                        CCRTableCell * cell = new CCRTableCell;
                        cell->elem = item;
                        int cs=StrToIntPercent(item->getAttributeValue(attr_colspan).c_str());
                        if (cs>0 && cs<100) { // colspan=0 (span all remaining columns) not supported
                            cell->colspan=cs;
                        } else {
                            cs=1;
                        }
                        int rs=StrToIntPercent(item->getAttributeValue(attr_rowspan).c_str());
                        if (rs>0 && rs<100) {
                            cell->rowspan=rs;
                        } else {
                            rs=1;
                        }
                        /*
                        // "width"
                        lString16 w = item->getAttributeValue(attr_width);
                        if (!w.empty()) {
                            int wn = StrToIntPercent(w.c_str(), digitwidth);
                            if (wn<0)
                                cell->percent = -wn;
                            else if (wn>0)
                                cell->width = wn;
                        }
                        // "align"
                        lString16 halign = item->getAttributeValue(attr_align);
                        if (halign == "center")
                            cell->halign = 1; // center
                        else if (halign == "right")
                            cell->halign = 2; // right
                        // "valign"
                        lString16 valign = item->getAttributeValue(attr_valign);
                        if (valign == "center")
                            cell->valign = 1; // center
                        else if (valign == "bottom")
                            cell->valign = 2; // bottom
                        */
                        // These commented above attributes have been translated to
                        // CSS properties by ldomDocumentWriterFilter::OnAttribute():
                        //   width= has been translated to elem style->width
                        //   align= has been translated to elem style->text_align
                        //   valign= has been translated to elem style->vertical_align
                        // (This allows overriding them with Style tweaks to remove
                        // publisher alignments and specified widths)
                        css_length_t w = item->getStyle()->width;
                        if ( w.type == css_val_percent ) { // %
                            cell->percent = w.value / 256;
                        }
                        else if ( w.type != css_val_unspecified ) { // px, em...
                            int em = item->getFont()->getSize();
                            cell->width = lengthToPx( w, 0, em );
                        }
                        // else: cell->percent and cell->width stay at 0

                        // This is not used here, but getStyle()->text_align will
                        // be naturally handled when cells are rendered
                        css_length_t ta = item->getStyle()->text_align;
                        if ( ta == css_ta_center )
                            cell->halign = 1; // center
                        else if ( ta == css_ta_right )
                            cell->halign = 2; // right

                        css_length_t va = item->getStyle()->vertical_align;
                        if ( va.type == css_val_unspecified ) {
                            if ( va.value == css_va_middle )
                                cell->valign = 1; // middle
                            else if ( va.value == css_va_bottom )
                                cell->valign = 2; // bottom
                        }

                        cell->row = rows[rows.length()-1];
                        cell->row->cells.add( cell );
                        cell->row->numcols += cell->colspan;
                        ExtendCols( cell->row->numcols ); // update col count
                        tdindex++;
                    }
                    break;
                case erm_table_caption: // table caption
                    {
                        caption = item;
                    }
                    break;
                case erm_inline:
                case erm_runin:
                    // do nothing
                    break;
                }
            }
        }
        return 0;
    }

    // More or less complex algorithms to calculate column widths are described at:
    //   https://www.w3.org/TR/css-tables-3/#computing-cell-measures
    //   https://www.w3.org/TR/REC-html40/appendix/notes.html#h-B.5.2
    //   https://www.w3.org/TR/CSS2/tables.html#auto-table-layout
    //   https://drafts.csswg.org/css3-tables-algorithms/Overview.src.htm
    //   https://developer.mozilla.org/en-US/docs/Archive/Mozilla/Table_Layout_Strategy
    //   http://www.tads.org/t3doc/doc/htmltads/tables.htm
    //   https://github.com/Kozea/WeasyPrint (html to pdf in python)

    // Beware of risk of division by zero!
    // (In vim, find divisions with: /\v(\/)@<!(\*)@<!\/(\/)@!(\*)@!.*   )

    void PlaceCells() {
        int i, j;
        // search for max column number
        int maxcols = 0;
        for (i=0; i<rows.length(); i++) {
            if (maxcols<rows[i]->numcols)
                maxcols=rows[i]->numcols;
        }
        // add column objects
        ExtendCols(maxcols);
        // place row cells horizontally
        for (i=0; i<rows.length(); i++) {
            int x=0;
            int miny=-1;
            CCRTableRow * row = rows[i];
            row->index = i;
            for (j=0; j<rows[i]->cells.length(); j++) {
                CCRTableCell * cell = rows[i]->cells[j];
                int cs = cell->colspan;
                //int rs = cell->rowspan;
                while (x<cols.length() && cols[x]->nrows>i) { // find free cell position
                    x++;
                    ExtendCols(x); // update col count
                }
                ExtendCols( x + cs ); // update col count
                cell->col = cols[x];
                for (int xx=0; xx<cs; xx++) {
                    // place cell
                    ExtendCols(x+xx+1); // update col count
                    if ( cols[x+xx]->nrows < i+cell->rowspan )
                        cols[x+xx]->nrows = i+cell->rowspan;
                    if (cell->rowspan>1) {
                        //int flg =1;
                    }
                }
                // update col width (for regular cells with colspan=1 only)
                if (cell->colspan==1) {
                    if (cell->width>0 && cell->col->width<cell->width && cell->col->percent==0) {
                        cell->col->width = cell->width;
                    } else if (cell->percent>0 && cell->col->width==0 && cell->col->percent<cell->percent) {
                        cell->col->percent = cell->percent;
                    }
                }
                x += cs;
            }
            // update min row count
            for (j=0; j<x; j++) {
                if (miny==-1 || miny>cols[j]->nrows)
                    miny=cols[j]->nrows;
            }
            // skip fully filled rows!
            while (miny>i+1) {
                i++;
                // add new row (already filled)
                CCRTableRow * nrow = new CCRTableRow;
                nrow->index = i;
                rows.insert(i, nrow);
            }
        }
        int maxy = 0; // check highest column
        for (j=0; j<cols.length(); j++)
            if (maxy<cols[j]->nrows)
                maxy=cols[j]->nrows;
        // padding table with empty lines up to max col height
        while (maxy>i) {
            i++;
            // add new row (already filled)
            CCRTableRow * nrow = new CCRTableRow;
            nrow->index = i;
            rows.insert(i, nrow);
        }
        // The above code has possibly added with ExtendCols() virtual columns
        // (that do not contain any cell) to 'cols' to account for colspan
        // values (which may be bogus in some documents).
        // Remove any extraneous cols and rows, and adjust the colspan and
        // rowspan values of involved cells.
        int max_used_x = -1;
        int max_used_y = -1;
        for (i=0; i<rows.length(); i++) {
            for (j=0; j<rows[i]->cells.length(); j++) {
                CCRTableCell * cell = (rows[i]->cells[j]);
                int x0 = cell->col->index;
                if (x0 > max_used_x)
                    max_used_x = x0;
                int y0 = cell->row->index;
                if (y0 > max_used_y)
                    max_used_y = y0;
            }
        }
        for (i=0; i<rows.length(); i++) {
            for (j=0; j<rows[i]->cells.length(); j++) {
                CCRTableCell * cell = (rows[i]->cells[j]);
                if (cell->col->index + cell->colspan - 1 > max_used_x)
                    cell->colspan = max_used_x - cell->col->index + 1;
                if (cell->row->index + cell->rowspan - 1 > max_used_y)
                    cell->rowspan = max_used_y - cell->row->index + 1;
            }
        }
        #ifdef DEBUG_TABLE_RENDERING
            printf("TABLE: grid %dx%d reduced to %dx%d\n",
                cols.length(), rows.length(), max_used_x+1, max_used_y+1);
        #endif
        for (i=rows.length()-1; i>max_used_y; i--) {
            if (rows[i]->rowgroup) {
                rows[i]->rowgroup->rows.remove(rows[i]);
            }
            delete rows.remove(i);
        }
        for (i=cols.length()-1; i>max_used_x; i--) {
            delete cols.remove(i);
            // No need to adjust cols[x]->nrows, we don't use it from now
        }

        int table_em = elem->getFont()->getSize();
        // border-spacing does not accept values in % unit
        int borderspacing_h = lengthToPx(elem->getStyle()->border_spacing[0], 0, table_em);
        bool border_collapse = (elem->getStyle()->border_collapse == css_border_collapse);

        if (border_collapse) {
            borderspacing_h = 0; // no border spacing when table collapse
            // Each cell is responsible for drawing its borders.
            for (i=0; i<rows.length(); i++) {
                for (j=0; j<rows[i]->cells.length(); j++) {
                    CCRTableCell * cell = (rows[i]->cells[j]);
                    css_style_ref_t style = cell->elem->getStyle();
                    // (Note: we should not modify styles directly, as the change
                    // in style cache will affect other nodes with the same style,
                    // and corrupt style cache Hash, invalidating cache reuse.)
                    css_style_ref_t newstyle(new css_style_rec_t);
                    copystyle(style, newstyle);

                    // We don't do adjacent-cells "border size comparisons and
                    // take the larger one", as it's not obvious to get here
                    // this cell's adjactent ones (possibly multiple on a side
                    // when rowspan/colspan).
                    // But we should at least, for cells with no border, get the
                    // top and bottom, and the left and right for cells at
                    // table edges, from the TR, THEAD/TBODY or TABLE.
                    bool is_at_top = cell->row->index == 0;
                    bool is_at_bottom = (cell->row->index + cell->rowspan) == rows.length();
                    bool is_at_left = cell->col->index == 0;
                    bool is_at_right = (cell->col->index + cell->colspan) == cols.length();
                    // We'll avoid calling measureBorder() many times for this same cell,
                    // by passing these by reference to collapse_border():
                    int cell_border_top = measureBorder(cell->elem, 0);
                    int cell_border_right = measureBorder(cell->elem, 1);
                    int cell_border_bottom = measureBorder(cell->elem, 2);
                    int cell_border_left = measureBorder(cell->elem, 3);
                    //
                    // With border-collapse, a cell may get its top and bottom
                    // borders from its TR.
                    ldomNode * rtop = rows[cell->row->index]->elem;
                        // (may be NULL, but collapse_border() checks for that)
                    // For a cell with rowspan>1, not sure if its bottom border should come
                    // from its own starting row, or the other row it happens to end on.
                    // Should look cleaner if we use the later.
                    ldomNode * rbottom = rows[cell->row->index + cell->rowspan - 1]->elem;
                    collapse_border(newstyle, cell_border_top, 0, rtop);
                    collapse_border(newstyle, cell_border_bottom, 2, rbottom);
                    // We also get the left and right borders, for the first or
                    // the last cell in a row, from the row (top row if multi rows span)
                    if (is_at_left)
                        collapse_border(newstyle, cell_border_left, 3, rtop);
                    if (is_at_right)
                        collapse_border(newstyle, cell_border_right, 1, rtop);
                        // If a row is missing some cells, there is none that stick
                        // to the right of the table (is_at_right is false): the outer
                        // table border will have a hole for this row...
                    // We may also get them from the rowgroup this TR is part of, if any, for
                    // the cells in the first or the last row of this rowgroup
                    if (rows[cell->row->index]->rowgroup) {
                        CCRTableRowGroup * grp = rows[cell->row->index]->rowgroup;
                        if (rows[cell->row->index] == grp->rows.first())
                            collapse_border(newstyle, cell_border_top, 0, grp->elem);
                        if (rows[cell->row->index] == grp->rows.last())
                            collapse_border(newstyle, cell_border_bottom, 2, grp->elem);
                        if (is_at_left)
                            collapse_border(newstyle, cell_border_left, 3, grp->elem);
                        if (is_at_right)
                            collapse_border(newstyle, cell_border_right, 1, grp->elem);
                    }
                    // And we may finally get borders from the table itself ("elem")
                    if (is_at_top)
                        collapse_border(newstyle, cell_border_top, 0, elem);
                    if (is_at_bottom)
                        collapse_border(newstyle, cell_border_bottom, 2, elem);
                    if (is_at_left)
                        collapse_border(newstyle, cell_border_left, 3, elem);
                    if (is_at_right)
                        collapse_border(newstyle, cell_border_right, 1, elem);

                    // Now, we should disable some borders for this cell,
                    // at inter-cell boundaries.
                    // We could either keep the right and bottom borders
                    // (which would be better to catch coordinates bugs).
                    // Or keep the top and left borders, which is better:
                    // if a cell carries its top border, when splitting
                    // rows to pages, a row at top of page will have its
                    // top border (unlike previous alternative), which is
                    // clearer for the reader.
                    // So, we disable the bottom and right borders, except
                    // for cells that are on the right or at the bottom of
                    // the table, as these will draw the outer table border
                    // on these sides.
                    if ( !is_at_right )
                        newstyle->border_style_right = css_border_none;
                    if ( !is_at_bottom )
                        newstyle->border_style_bottom = css_border_none;

                    cell->elem->setStyle(newstyle);
                    // (Note: we should no more modify a style after it has been
                    // applied to a node with setStyle().)
                }
                // (Some optimisation could be made in these loops, as
                // collapse_border() currently calls measureBorder() many
                // times for the same elements: TR, TBODY, TABLE...)
            }
            // The TR and TFOOT borders will explicitely NOT be drawn by DrawDocument()
            // (Firefox never draws them, even when no border-collapse).
            // But the TABLE ones will be (needed when no border-collapse).
            // So, as we possibly collapsed the table border to the cells, we just
            // set them to none on the TABLE.
            css_style_ref_t style = elem->getStyle();
            css_style_ref_t newstyle(new css_style_rec_t);
            copystyle(style, newstyle);
            newstyle->border_style_top = css_border_none;
            newstyle->border_style_right = css_border_none;
            newstyle->border_style_bottom = css_border_none;
            newstyle->border_style_left = css_border_none;
            elem->setStyle(newstyle);
        }
        // We should no longer modify cells' padding and border beyond this point,
        // as these are used below to compute max_content_width and min_content_width,
        // which account for them.

        // Compute for each cell the max (prefered if possible) and min (width of the
        // longest word, so no word is cut) rendered widths.
        for (i=0; i<rows.length(); i++) {
            for (j=0; j<rows[i]->cells.length(); j++) {
                // rows[i]->cells contains only real cells made from node elements
                CCRTableCell * cell = (rows[i]->cells[j]);
                getRenderedWidths(cell->elem, cell->max_content_width, cell->min_content_width);
                #ifdef DEBUG_TABLE_RENDERING
                    printf("TABLE: cell[%d,%d] getRenderedWidths: %d (min %d)\n",
                        j, i, cell->max_content_width, cell->min_content_width);
                #endif
                int x0 = cell->col->index;
                if (cell->colspan == 1) {
                    cols[x0]->sum_max_content_width += cell->max_content_width;
                    cols[x0]->nb_sum_max_content_width += 1;
                    // Update cols max/min widths only for colspan=1 cells
                    if ( cell->max_content_width > cols[x0]->max_width )
                        cols[x0]->max_width = cell->max_content_width;
                    if ( cell->min_content_width > cols[x0]->min_width )
                        cols[x0]->min_width = cell->min_content_width;
                }
            }
        }
        // Second pass for cells with colspan > 1
        for (i=0; i<rows.length(); i++) {
            for (j=0; j<rows[i]->cells.length(); j++) {
                CCRTableCell * cell = (rows[i]->cells[j]);
                if (cell->colspan > 1) {
                    // Check if we need to update the max_width and min_width
                    // of each cols we span
                    int nbspans = cell->colspan;
                    int x0 = cell->col->index;
                    // Get the existing aggregated min/max width of the cols we span
                    int cols_max_width = 0;
                    int cols_min_width = 0;
                    for (int x=0; x<nbspans; x++) {
                        cols_min_width += cols[x0+x]->min_width;
                        cols_max_width += cols[x0+x]->max_width;
                    }
                    cols_min_width += borderspacing_h * (nbspans-1);
                    cols_max_width += borderspacing_h * (nbspans-1);
                    #ifdef DEBUG_TABLE_RENDERING
                        printf("TABLE: COLS SPANNED[%d>%d] min_width=%d max_width=%d\n",
                            x0, x0+nbspans-1, cols_min_width, cols_max_width);
                    #endif
                    if ( cell->min_content_width > cols_min_width ) {
                        // Our min width is larger than the spanned cols min width
                        int to_distribute = cell->min_content_width - cols_min_width;
                        int distributed = 0;
                        for (int x=0; x<nbspans; x++) {
                            // Distribute more to bigger min_width to keep original
                            // cell proportions
                            int this_dist;
                            if (cols_min_width > 0)
                                this_dist = to_distribute * cols[x0+x]->min_width / cols_min_width;
                            else
                                this_dist = to_distribute / nbspans;
                            cols[x0+x]->min_width += this_dist;
                            distributed += this_dist;
                            #ifdef DEBUG_TABLE_RENDERING
                                printf("TABLE:   COL[%d] (todist:%d) min_wdith += %d => %d\n",
                                    x0+x, to_distribute, this_dist, cols[x0+x]->min_width);
                            #endif
                        }
                        // Distribute left over to last col
                        cols[x0+nbspans-1]->min_width += to_distribute - distributed;
                        #ifdef DEBUG_TABLE_RENDERING
                            printf("TABLE: COL[%d] (leftover:%d-%d) min_wdith += %d => %d\n",
                                x0+nbspans-1, to_distribute, distributed, to_distribute - distributed,
                                cols[x0+nbspans-1]->min_width);
                        #endif
                    }
                    // Let's do the same for max_width, although it may lead to
                    // messier layouts in complex colspan setups...
                    // (And we should probably not let all cols->max_width at 0 if they are 0!)
                    if ( cell->max_content_width > cols_max_width ) {
                        // Our max width is larger than the spanned cols max width
                        int to_distribute = cell->max_content_width - cols_max_width;
                        int distributed = 0;
                        for (int x=0; x<nbspans; x++) {
                            // Distribute more to bigger max_width to keep original
                            // cell proportions
                            int this_dist;
                            if (cols_max_width > 0)
                                this_dist = to_distribute * cols[x0+x]->max_width / cols_max_width;
                            else
                                this_dist = to_distribute / nbspans;
                            cols[x0+x]->max_width += this_dist;
                            distributed += this_dist;
                            #ifdef DEBUG_TABLE_RENDERING
                                printf("TABLE:   COL[%d] (todist:%d) max_wdith += %d => %d\n",
                                    x0+x, to_distribute, this_dist, cols[x0+x]->max_width);
                            #endif
                        }
                        // Distribute left over to last col
                        cols[x0+nbspans-1]->max_width += to_distribute - distributed;
                        #ifdef DEBUG_TABLE_RENDERING
                            printf("TABLE: COL[%d] (leftover:%d-%d) max_wdith += %d => %d\n",
                                x0+nbspans-1, to_distribute, distributed, to_distribute - distributed,
                                cols[x0+nbspans-1]->max_width);
                        #endif
                    }
                    // Also distribute to sum_max_content_width
                    for (int x=0; x<nbspans; x++) {
                        int this_dist;
                        if (cols_max_width > 0)
                            this_dist = cell->max_content_width * cols[x0+x]->max_width / cols_max_width;
                        else
                            this_dist = cell->max_content_width / nbspans;
                        cols[x0+x]->sum_max_content_width += this_dist;
                        cols[x0+x]->nb_sum_max_content_width += 1;
                    }
                }
            }
        }

        /////////////////////////// From here until further noticed, we just use and update the cols objects
        // Find width available for cells content (including their borders and paddings)
        // Start with table full width
        int assignable_width = table_width;
        // Remove table outer borders
        assignable_width -= measureBorder(elem,1) + measureBorder(elem,3); // (border indexes are TRBL)
        if ( border_collapse ) {
            // Table own outer paddings and any border-spacing are
            // ignored with border-collapse
        }
        else { // no collapse
            // Remove table outer paddings (margin and padding indexes are LRTB)
            assignable_width -= lengthToPx(elem->getStyle()->padding[0], table_width, table_em);
            assignable_width -= lengthToPx(elem->getStyle()->padding[1], table_width, table_em);
            // Remove (nb cols + 1) border-spacing
            assignable_width -= (cols.length() + 1) * borderspacing_h;
        }
        #ifdef DEBUG_TABLE_RENDERING
            printf("TABLE: table_width=%d assignable_width=%d\n", table_width, assignable_width);
        #endif
        if (assignable_width <= 0) { // safety check
            // In case we get a zero or negative value (too much padding or
            // borderspacing and many columns), just re-set it to table_width
            // for our calculation purpose, which expect a positive value to
            // work with: the rendering will be bad, but some stuff will show.
            assignable_width = table_width;
            borderspacing_h = 0;
        }

        // Find best width for each column
        int npercent=0;
        int sumpercent=0;
        int nwidth = 0;
        int sumwidth = 0;
        for (int x=0; x<cols.length(); x++) {
            #ifdef DEBUG_TABLE_RENDERING
                printf("TABLE WIDTHS step1: cols[%d]: %d%% %dpx\n",
                    x, cols[x]->percent, cols[x]->width);
            #endif
            if (cols[x]->percent>0) {
                cols[x]->width_auto = false;
                sumpercent += cols[x]->percent;
                cols[x]->width = 0;
                npercent++;
            } else if (cols[x]->width>0) {
                cols[x]->width_auto = false;
                sumwidth += cols[x]->width;
                nwidth++;
            }
        }
        int nrest = cols.length() - nwidth - npercent; // nb of cols with auto width
        int sumwidthpercent = 0; // percent of sum-width
        // Percents are to be used as a ratio of assignable_width (if we would
        // use the original full table width, 100% would overflow with borders,
        // paddings and border spacings)
        if (sumwidth) {
            sumwidthpercent = 100*sumwidth/assignable_width;
            if (sumpercent+sumwidthpercent+5*nrest>100) { // 5% (?) for each unsized column
                // too wide: convert widths to percents
                for (int i=0; i<cols.length(); i++) {
                    if (cols[i]->width>0) {
                        cols[i]->percent = cols[i]->width*100/assignable_width;
                        cols[i]->width = 0;
                        sumpercent += cols[i]->percent;
                        npercent++;
                    }
                }
                nwidth = 0;
                sumwidth = 0;
            }
        }
        // scale percents
        int maxpercent = 100-3*nrest; // 3% (?) for each unsized column
        if (sumpercent>maxpercent && sumpercent>0) {
            // scale percents
            int newsumpercent = 0;
            for (int i=0; i<cols.length(); i++) {
                if (cols[i]->percent>0) {
                    cols[i]->percent = cols[i]->percent*maxpercent/sumpercent;
                    newsumpercent += cols[i]->percent;
                    cols[i]->width = 0;
                }
            }
            sumpercent = newsumpercent;
        }
        // calc width by percents
        sumwidth = 0;
        int sum_auto_max_width = 0;
        int sum_auto_mean_max_content_width = 0;
        nwidth = 0;
        for (i=0; i<cols.length(); i++) {
            if (cols[i]->percent>0) {
                cols[i]->width = assignable_width * cols[i]->percent / 100;
                cols[i]->percent = 0;
            }
            if (cols[i]->width>0) {
                // calc width stats
                sumwidth += cols[i]->width;
                nwidth++;
            } else if (cols[i]->max_width>0) {
                sum_auto_max_width += cols[i]->max_width;
                sum_auto_mean_max_content_width += cols[i]->sum_max_content_width / cols[i]->nb_sum_max_content_width;
            }
        }
        #ifdef DEBUG_TABLE_RENDERING
            for (int x=0; x<cols.length(); x++)
                printf("TABLE WIDTHS step2: cols[%d]: %d%% %dpx\n",
                    x, cols[x]->percent, cols[x]->width);
        #endif
        // At this point, all columns with specified width or percent has been
        // set accordingly, or reduced to fit table width
        // We need to compute a width for columns with unspecified width.
        nrest = cols.length() - nwidth;
        int restwidth = assignable_width - sumwidth;
        int sumMinWidths = 0;
        // new pass: convert text len percent into width
        for (i=0; i<cols.length(); i++) {
            if (cols[i]->width==0) { // unspecified (or width scaled down and rounded to 0)
                // We have multiple options:
                // Either distribute remaining width according to max_width ratio
                // (so larger content gets more width to use less height)
                //     if (sum_auto_max_width > 0)
                //         cols[i]->width = cols[i]->max_width * restwidth / sum_auto_max_width;
                // Or better: distribute remaining width according to the mean
                // of cells' max_content_width, so a single large cell among
                // many smaller cells doesn't request all the width for this
                // column (which would give less to others, which could make
                // them use more height). This should help getting more
                // reasonable heights across all rows.
                if (sum_auto_mean_max_content_width > 0)
                    cols[i]->width = cols[i]->sum_max_content_width / cols[i]->nb_sum_max_content_width * restwidth / sum_auto_mean_max_content_width;
                // else stays at 0
                sumwidth += cols[i]->width;
                nwidth++;
            }
            if (cols[i]->width < cols[i]->min_width) {
                // extend too small cols to their min_width
                int delta = cols[i]->min_width - cols[i]->width;
                cols[i]->width += delta;
                sumwidth += delta;
            }
            sumMinWidths += cols[i]->min_width; // will be handy later
        }
        if (sumwidth>assignable_width && sumwidth>0) {
            // too wide! rescale down
            int newsumwidth = 0;
            for (i=0; i<cols.length(); i++) {
                cols[i]->width = cols[i]->width * assignable_width / sumwidth;
                newsumwidth += cols[i]->width;
            }
            sumwidth = newsumwidth;
        }
        #ifdef DEBUG_TABLE_RENDERING
            for (int x=0; x<cols.length(); x++)
                printf("TABLE WIDTHS step3: cols[%d]: %d%% %dpx (min:%d / max:%d)\n",
                    x, cols[x]->percent, cols[x]->width, cols[x]->min_width, cols[x]->max_width);
        #endif
        bool canFitMinWidths = (sumMinWidths > 0 && sumMinWidths < assignable_width);
        // new pass: resize columns with originally unspecified widths
        int rw=0;
        int dist_nb_cols = 0;
        for (int x=0; x<cols.length(); x++) {
            // Adjust it further down to the measured max_width
            int prefered_width = cols[x]->min_width;
            if (cols[x]->max_width > prefered_width) {
                prefered_width = cols[x]->max_width;
            }
            if (cols[x]->width_auto && cols[x]->width > prefered_width) {
                // Column can nicely fit in a smaller width
                rw += (cols[x]->width - prefered_width);
                cols[x]->width = prefered_width;
                // min_width is no longer needed: use it as a flag so we don't
                // redistribute width to this column
                cols[x]->min_width = -1;
            }
            else if (canFitMinWidths && cols[x]->width < cols[x]->min_width) {
                // Even for columns with originally specified width:
                // If all min_width can fit into available width, ensure
                // cell's min_width by taking back from to-be redistributed width
                rw -= (cols[x]->min_width - cols[x]->width);
                cols[x]->width = cols[x]->min_width;
                // min_width is no longer needed: use it as a flag so we don't
                // redistribute width to this column
                cols[x]->min_width = -1;
            }
            else if (cols[x]->width_auto && cols[x]->min_width > 0) {
                dist_nb_cols += 1;      // candidate to get more width
            }
        }
        #ifdef DEBUG_TABLE_RENDERING
            for (int x=0; x<cols.length(); x++)
                printf("TABLE WIDTHS step4: cols[%d]: %d%% %dpx (min %dpx)\n",
                    x, cols[x]->percent, cols[x]->width, cols[x]->min_width);
        #endif
        int restw = assignable_width - sumwidth + rw; // may be negative if we needed to
                                                      // increase to fulfill min_width
        if (shrink_to_fit && restw > 0) {
            // If we're asked to shrink width to fit cells content, don't
            // distribute restw to columns, but shrink table width
            // Table padding may be in %, and need to be corrected
            int correction = 0;
            correction += lengthToPx(elem->getStyle()->padding[0], table_width, table_em);
            correction += lengthToPx(elem->getStyle()->padding[0], table_width, table_em);
            table_width -= restw;
            correction -= lengthToPx(elem->getStyle()->padding[0], table_width, table_em);
            correction -= lengthToPx(elem->getStyle()->padding[0], table_width, table_em);
            table_width -= correction;
            assignable_width -= restw + correction; // (for debug printf() below)
            #ifdef DEBUG_TABLE_RENDERING
                printf("TABLE WIDTHS step5 (fit): reducing table_width %d -%d -%d > %d\n",
                    table_width+restw+correction, restw, correction, table_width);
            #endif
        }
        else {
            #ifdef DEBUG_TABLE_RENDERING
                printf("TABLE WIDTHS step5 (dist): %d to distribute to %d cols\n",
                    restw, dist_nb_cols);
            #endif
            // distribute rest of width between all cols that can benefit from more
            bool dist_all_non_empty_cols = false;
            if (dist_nb_cols == 0) {
                dist_all_non_empty_cols = true;
                // distribute to all non empty cols
                for (int x=0; x<cols.length(); x++) {
                    if (cols[x]->min_width != 0) {
                        dist_nb_cols += 1;
                    }
                }
                #ifdef DEBUG_TABLE_RENDERING
                    printf("TABLE WIDTHS step5: %d to distribute to all %d non empty cols\n",
                        restw, dist_nb_cols);
                #endif
            }
            if (restw != 0 && dist_nb_cols>0) {
                int a = restw / dist_nb_cols;
                int b = restw % dist_nb_cols;
                for (i=0; i<cols.length(); i++) {
                    if ( (!dist_all_non_empty_cols && cols[i]->min_width > 0)
                      || (dist_all_non_empty_cols && cols[i]->min_width != 0) ) {
                        cols[i]->width += a;
                        if (b>0) {
                            cols[i]->width ++;
                            b--;
                        }
                        else if (b<0) {
                            cols[i]->width --;
                            b++;
                        }
                    }
                }
                // (it would be better to distribute restw according
                // to each column max_width / min_width, so larger ones
                // get more of it)
            }
            #ifdef DEBUG_TABLE_RENDERING
                for (int x=0; x<cols.length(); x++)
                    printf("TABLE WIDTHS step5: cols[%d]: %d%% %dpx\n",
                        x, cols[x]->percent, cols[x]->width);
            #endif
        }
        #ifdef DEBUG_TABLE_RENDERING
            printf("TABLE WIDTHS SUM:");
            int colswidthsum = 0;
            for (int x=0; x<cols.length(); x++) {
                printf(" +%d", cols[x]->width);
                colswidthsum += cols[x]->width;
            }
            printf(" = %d", colswidthsum);
            if (assignable_width == colswidthsum)
                printf(" == assignable_width, GOOD\n");
            else
                printf(" != assignable_width %d, BAAAAAAADDD\n", assignable_width);
        #endif

        // update col x
        for (i=0; i<cols.length(); i++) {
            if (i == 0)
                cols[i]->x = borderspacing_h;
            else
                cols[i]->x = cols[i-1]->x + cols[i-1]->width + borderspacing_h;
            #ifdef DEBUG_TABLE_RENDERING
                printf("TABLE WIDTHS step6: cols[%d]->x = %d\n", i, cols[i]->x);
            #endif
        }
        /////////////////////////// Done with just using and updating the cols objects

        // Columns widths calculated ok!
        // Update width of each cell
        for (i=0; i<rows.length(); i++) {
            for (j=0; j<rows[i]->cells.length(); j++) {
                CCRTableCell * cell = (rows[i]->cells[j]);
                cell->width = 0;
                int x0 = cell->col->index;
                for (int x=0; x<cell->colspan; x++) {
                    cell->width += cols[x0+x]->width;
                }
                if ( cell->colspan > 1 ) {
                    // include skipped borderspacings in cell width
                    cell->width += borderspacing_h * (cell->colspan-1);
                }
                #ifdef DEBUG_TABLE_RENDERING
                    printf("TABLE: placeCell[%d,%d] width: %d\n", j, i, cell->width);
                #endif
            }
        }
    }

    int renderCells( LVRendPageContext & context )
    {
        // We should set, for each of the table children and sub-children,
        // its RenderRectAccessor fmt(node) x/y/w/h.
        // x/y of a cell are relative to its own parent node top left corner
        int em = elem->getFont()->getSize();
        css_style_ref_t table_style = elem->getStyle();
        int table_border_top = measureBorder(elem, 0);
        int table_border_right = measureBorder(elem, 1);
        int table_border_bottom = measureBorder(elem, 2);
        int table_border_left = measureBorder(elem, 3);
        int table_padding_left = lengthToPx(table_style->padding[0], table_width, em);
        int table_padding_right = lengthToPx(table_style->padding[1], table_width, em);
        int table_padding_top = lengthToPx(table_style->padding[2], table_width, em);
        int table_padding_bottom = lengthToPx(table_style->padding[3], table_width, em);
        int borderspacing_h = lengthToPx(table_style->border_spacing[0], 0, em); // does not accept %
        int borderspacing_v = lengthToPx(table_style->border_spacing[1], 0, em);
        bool border_collapse = (table_style->border_collapse==css_border_collapse);
        if (border_collapse) {
            table_padding_top = 0;
            table_padding_bottom = 0;
            table_padding_left = 0;
            table_padding_right = 0;
            borderspacing_v = 0;
            borderspacing_h = 0;
        }
        // We want to distribute border spacing on top and bottom of each row,
        // mainly for page splitting to carry half of it on each page.
        int borderspacing_v_top = borderspacing_v / 2;
        int borderspacing_v_bottom = borderspacing_v - borderspacing_v_top;
        // (Both will be 0 if border_collapse)

        int nb_rows = rows.length();

        // We will context.AddLine() for page splitting the elements
        // (caption, rows) as soon as we meet them and their y-positionnings
        // inside the tables are known and won't change.
        // (This would need that rowgroups be dealt with in this flow (and
        // not at the end) if we change the fact that we ignore their
        // border/padding/margin - see below why we do.)
        lvRect rect;
        elem->getAbsRect(rect);
        const int table_y0 = rect.top; // absolute y in document for top of table
        int last_y = table_y0; // used as y0 to AddLine(y0, table_y0+table_h)
        int line_flags = 0;
        bool splitPages = context.getPageList() != NULL;

        // Final table height will be added to as we meet table content
        int table_h = 0;
        table_h += table_border_top;

        // render caption
        if ( caption ) {
            int em = caption->getFont()->getSize();
            int w = table_width - table_border_left - table_border_right;
            // (When border-collapse, these table_border_* will be 0)
            // Note: table padding does not apply to caption, and table padding-top
            // should be applied between the caption and the first row
            // Also, Firefox does not include the caption inside the table outer border.
            // We'll display as Firefox when border-collapse, as the table borders were
            // reset to 0 after we collapsed them to the cells.
            // But not when not border-collapse: we can't do that in crengine because
            // of parent>children boxes containment, so the caption will be at top
            // inside the table border.
            // A caption can have borders, that we must include in its padding:
            // we may then get a double border with the table one... (We could hack
            // caption->style to remove its border if the table has some, if needed.)
            LFormattedTextRef txform;
            RenderRectAccessor fmt( caption );
            fmt.setX( table_border_left );
            fmt.setY( table_h );
            fmt.setWidth( w ); // fmt.width must be set before 'caption->renderFinalBlock'
                               // to have text-indent in % not mess up at render time
            int padding_left = lengthToPx( caption->getStyle()->padding[0], w, em ) + measureBorder(caption, 3);
            int padding_right = lengthToPx( caption->getStyle()->padding[1], w, em ) + measureBorder(caption,1);
            int padding_top = lengthToPx( caption->getStyle()->padding[2], w, em ) + measureBorder(caption,0);
            int padding_bottom = lengthToPx( caption->getStyle()->padding[3], w, em ) + measureBorder(caption,2);
            caption_h = caption->renderFinalBlock( txform, &fmt, w - padding_left - padding_right );
            caption_h += padding_top + padding_bottom;
            fmt.setHeight( caption_h );
            fmt.push();
            table_h += caption_h;
        }
        table_h += table_padding_top; // padding top applies after caption
        if (nb_rows > 0) {
            // There must be the full borderspacing_v above first row.
            // Includes half of it here, and the other half when adding the row
            table_h += borderspacing_v_bottom;
        }
        if (splitPages) {
            // Includes table border top + full caption if any + table padding
            // top + half of borderspacing_v.
            // We ask for a split between these and the first row to be avoided,
            // but if it can't, padding-top will be on previous page, leaving
            // more room for the big first row on next page.
            // Any table->style->page-break-before AVOID or ALWAYS has been
            // taken care of by renderBlockElement(), so we can use AVOID here.
            line_flags = RN_SPLIT_BEFORE_AVOID | RN_SPLIT_AFTER_AVOID;
            context.AddLine(last_y, table_y0 + table_h, line_flags);
            last_y = table_y0 + table_h;
        }

        int i, j;
        // Calc individual cells dimensions
        for (i=0; i<rows.length(); i++) {
            CCRTableRow * row = rows[i];
            for (j=0; j<rows[i]->cells.length(); j++) {
                CCRTableCell * cell = rows[i]->cells[j];
                //int x = cell->col->index;
                int y = cell->row->index;
                int n = rows[i]->cells.length();
                if ( i==y ) { // upper left corner of cell
                    RenderRectAccessor fmt( cell->elem );
                    // TRs padding and border don't apply (see below), so they
                    // don't add any x/y shift to the cells' positions in the TR
                    fmt.setX(cell->col->x); // relative to its TR (border_spacing_h is
                                            // already accounted in col->x)
                    fmt.setY(0); // relative to its TR
                    fmt.setWidth( cell->width ); // needed before calling elem->renderFinalBlock
                    // We need to render the cell to get its height
                    if ( cell->elem->getRendMethod() == erm_final ) {
                        LFormattedTextRef txform;
                        int em = cell->elem->getFont()->getSize();
                        int padding_left = lengthToPx( cell->elem->getStyle()->padding[0], cell->width, em ) + measureBorder(cell->elem,3);
                        int padding_right = lengthToPx( cell->elem->getStyle()->padding[1], cell->width, em ) + measureBorder(cell->elem,1);
                        int padding_top = lengthToPx( cell->elem->getStyle()->padding[2], cell->width, em ) + measureBorder(cell->elem,0);
                        int padding_bottom = lengthToPx( cell->elem->getStyle()->padding[3], cell->width, em ) + measureBorder(cell->elem,2);
                        int h = cell->elem->renderFinalBlock( txform, &fmt, cell->width - padding_left - padding_right);
                        cell->height = h + padding_top + padding_bottom;
                    } else if ( cell->elem->getRendMethod()!=erm_invisible ) {
                        // We must use a different context (used by rendering
                        // functions to record, with context.AddLine(), each
                        // rendered block's height, to be used for splitting
                        // blocks among pages, for page-mode display), so that
                        // sub-renderings (of cells' content) do not add to our
                        // main context. Their heights will already be accounted
                        // in their row's height (added to main context below).
                        LVRendPageContext emptycontext( NULL, context.getPageHeight() );
                        int h = renderBlockElement( emptycontext, cell->elem, 0, 0, cell->width);
                        cell->height = h;
                    }
                    fmt.setHeight( cell->height );
                    // Some fmt.set* will be updated below
                    #ifdef DEBUG_TABLE_RENDERING
                        printf("TABLE: renderCell[%d,%d] w/h: %d/%d\n", j, i, cell->width, cell->height);
                    #endif
                    if ( cell->rowspan == 1 ) {
                        // Only set row height from this cell height if it is rowspan=1
                        // We'll update rows height from cells with rowspan > 1 just below
                        if ( row->height < cell->height )
                            row->height = cell->height;
                    }
                }
            }
        }

        // Update rows heights from multi-row (rowspan > 1) cells height
        for (i=0; i<rows.length(); i++) {
            //CCRTableRow * row = rows[i];
            for (j=0; j<rows[i]->cells.length(); j++) {
                CCRTableCell * cell = rows[i]->cells[j];
                //int x = cell->col->index;
                int y = cell->row->index;
                if ( i==y && cell->rowspan>1 ) {
                    int k;
                    int total_h = 0;
                    for ( k=i; k<=i+cell->rowspan-1; k++ ) {
                        CCRTableRow * row2 = rows[k];
                        total_h += row2->height;
                    }
                    int extra_h = cell->height - total_h;
                    if ( extra_h>0 ) {
                        int delta = extra_h / cell->rowspan;
                        int delta_h = extra_h - delta * cell->rowspan;
                        for ( k=i; k<=i+cell->rowspan-1; k++ ) {
                            CCRTableRow * row2 = rows[k];
                            row2->height += delta;
                            if ( delta_h > 0 ) {
                                row2->height++;
                                delta_h--;
                            }
                        }
                    }
                }
            }
        }

        // update rows y and total height
        //
        // Notes:
        // TR are not supposed to have margin or padding according
        // to CSS 2.1 https://www.w3.org/TR/CSS21/box.html (they could
        // in CSS 2, and may be in CSS 3, not clear), and Firefox ignores
        // them too (no effet whatever value, with border-collapse or not).
        // (If we have to support them, we could account for their top
        // and bottom values here, but the left and right values would
        // need to be accounted above while computing assignable_width for
        // columns content. Given that each row can be styled differently
        // with classNames, we may have different values for each row,
        // which would make computing the assignable_width very tedious.)
        //
        // TR can have borders, but, tested on Firefox with various
        // table styles:
        //   - they are never displayed when NOT border-collapse
        //   - with border-collapse, they are only displayed when
        //     the border is greater than the TD ones, so when
        //     collapsing is applied.
        // So, we don't need to account for TR borders here either:
        // we collapsed them to the cell if border-collapse,
        // and we can just ignore them here, and not draw them in
        // DrawDocument() (Former crengine code did draw the border,
        // but it drew it OVER the cell content, for lack of accounting
        // it in cells placement and content width.)
        for (i=0; i<nb_rows; i++) {
            table_h += borderspacing_v_top;
            CCRTableRow * row = rows[i];
            row->y = table_h;
            // It can happen there is a row that does not map to
            // a node (some are added at start of PlaceCells()),
            // so check for row->elem to avoid a segfault
            if ( row->elem ) {
                RenderRectAccessor fmt( row->elem );
                // TR position relative to the TABLE. If it is contained in a table group
                // (thead, tbody...), these will be adjusted below to be relative to it.
                // (Here were previously added row->elem borders)
                fmt.setX(table_border_left + table_padding_left);
                fmt.setY(row->y);
                fmt.setWidth( table_width - table_border_left - table_padding_left - table_padding_right - table_border_right );
                fmt.setHeight( row->height );
            }
            table_h += row->height;
            table_h += borderspacing_v_bottom;
            if (splitPages) {
                // Includes the row and half of its border_spacing above and half below.
                if (i == 0) { // first row (or single row)
                    // Avoid a split between table top border/padding/caption and first row.
                    // Also, the first row could be column headers: avoid a split between it
                    // and the 2nd row. (Any other reason to do that?)
                    line_flags = RN_SPLIT_BEFORE_AVOID | RN_SPLIT_AFTER_AVOID;
                    // Former code had:
                    // line_flags |= CssPageBreak2Flags(getPageBreakBefore(elem))<<RN_SPLIT_BEFORE;
                }
                else if ( i==nb_rows-1 ) { // last row
                    // Avoid a split between last row and previous to last (really?)
                    // Avoid a split between last row and table bottom padding/border
                    line_flags = RN_SPLIT_BEFORE_AVOID | RN_SPLIT_AFTER_AVOID;
                }
                else {
                    // Otherwise, allow any split between rows
                    line_flags = RN_SPLIT_BEFORE_AUTO | RN_SPLIT_AFTER_AUTO;
                }
                context.AddLine(last_y, table_y0 + table_h, line_flags);
                last_y = table_y0 + table_h;
            }
        }
        if (nb_rows > 0) {
            // There must be the full borderspacing_v below last row.
            // Includes the last half of it here, as the other half was added
            // above with the row.
            table_h += borderspacing_v_top;
        }
        table_h += table_padding_bottom + table_border_bottom;
        if (splitPages) {
            // Any table->style->page-break-after AVOID or ALWAYS will be taken
            // care of by renderBlockElement(), so we can use AVOID here.
            line_flags = RN_SPLIT_BEFORE_AVOID | RN_SPLIT_AFTER_AVOID;
            context.AddLine(last_y, table_y0 + table_h, line_flags);
            last_y = table_y0 + table_h;
        }

        // Update each cell height to be its row height, so it can draw its
        // bottom border where it should be: as the row border.
        // We also apply table cells' vertical-align property.
        for (i=0; i<rows.length(); i++) {
            for (j=0; j<rows[i]->cells.length(); j++) {
                CCRTableCell * cell = rows[i]->cells[j];
                //int x = cell->col->index;
                int y = cell->row->index;
                if ( i==y ) {
                    RenderRectAccessor fmt( cell->elem );
                    CCRTableRow * lastrow = rows[ cell->row->index + cell->rowspan - 1 ];
                    int row_h = lastrow->y + lastrow->height - cell->row->y;
                    // Implement CSS property vertical-align for table cells
                    // We have to format the cell with the row height for the borders
                    // to be drawn at the correct positions: we can't just use
                    // fmt.setY(fmt.getY() + pad) below to implement vertical-align.
                    // We have to shift down the cell content itself
                    int cell_h = fmt.getHeight(); // original height that fit cell content
                    fmt.setHeight( row_h );
                    if ( cell->valign && cell_h < row_h ) {
                        int pad = 0;
                        if (cell->valign == 1) // center
                            pad = (row_h - cell_h)/2;
                        else if (cell->valign == 2) // bottom
                            pad = (row_h - cell_h);
                        if ( cell->elem->getRendMethod() == erm_final ) {
                            // We need to update the cell element padding-top to include this pad
                            css_style_ref_t style = cell->elem->getStyle();
                            css_style_ref_t newstyle(new css_style_rec_t);
                            copystyle(style, newstyle);
                            // If padding-top is a percentage, it is relative to
                            // the *width* of the containing block
                            int em = cell->elem->getFont()->getSize();
                            int orig_padding_top = lengthToPx( style->padding[2], cell->width, em );
                            newstyle->padding[2].type = css_val_screen_px;
                            newstyle->padding[2].value = orig_padding_top + pad;
                            cell->elem->setStyle(newstyle);
                        } else if ( cell->elem->getRendMethod() != erm_invisible ) { // erm_block
                            // We need to update each child fmt.y to include this pad
                            for (int i=0; i<cell->elem->getChildCount(); i++) {
                                ldomNode * item = cell->elem->getChildElementNode(i);
                                if ( item ) {
                                    RenderRectAccessor f( item );
                                    f.setY( f.getY() + pad );
                                }
                            }
                        }
                    }
                }
            }
        }

        // Update row groups (thead, tbody...) placement (we need to do that as
        // these rowgroup elements are just block containers of the row elements,
        // and they will be navigated by DrawDocument() to draw each child
        // relative to its container: RenderRectAccessor X & Y are relative to
        // the parent container top left corner)
        //
        // As mentionned above, rowgroups' margins and paddings should be
        // ignored, and their borders are only used with border-collapse,
        // and we collapsed them to the cells when we had to.
        // So, we ignore them here, and DrawDocument() will NOT draw their
        // border.
        for ( int i=0; i<rowgroups.length(); i++ ) {
            CCRTableRowGroup * grp = rowgroups[i];
            if ( grp->rows.length() > 0 ) {
                int y0 = grp->rows.first()->y;
                int y1 = grp->rows.last()->y + grp->rows.first()->height;
                RenderRectAccessor fmt( grp->elem );
                fmt.setY( y0 );
                fmt.setHeight( y1 - y0 );
                fmt.setX( 0 );
                fmt.setWidth( table_width );
                for ( int j=0; j<grp->rows.length(); j++ ) {
                    // make row Y position relative to group
                    RenderRectAccessor rowfmt( grp->rows[j]->elem );
                    rowfmt.setY( rowfmt.getY() - y0 );
                }
            }
        }

        return table_h;
    }

    CCRTable(ldomNode * tbl_elem, int tbl_width, bool tbl_shrink_to_fit, int dwidth) : digitwidth(dwidth) {
        currentRowGroup = NULL;
        caption = NULL;
        caption_h = 0;
        elem = tbl_elem;
        table_width = tbl_width;
        shrink_to_fit = tbl_shrink_to_fit;
        #ifdef DEBUG_TABLE_RENDERING
            printf("TABLE: ============ parsing new table %s\n",
                UnicodeToLocal(ldomXPointer(elem, 0).toString()).c_str());
        #endif
        LookupElem( tbl_elem, 0 );
        PlaceCells();
    }
};

int renderTable( LVRendPageContext & context, ldomNode * node, int x, int y, int width, bool shrink_to_fit, int & fitted_width )
{
    CR_UNUSED2(x, y);
    CCRTable table( node, width, shrink_to_fit, 10 );
    int h = table.renderCells( context );
    if (shrink_to_fit)
        fitted_width = table.table_width;
    return h;
}

void freeFormatData( ldomNode * node )
{
    node->clearRenderData();
}

bool isSameFontStyle( css_style_rec_t * style1, css_style_rec_t * style2 )
{
    return (style1->font_family == style2->font_family)
        && (style1->font_size == style2->font_size)
        && (style1->font_style == style2->font_style)
        && (style1->font_name == style2->font_name)
        && (style1->font_weight == style2->font_weight);
}

//int rend_font_embolden = STYLE_FONT_EMBOLD_MODE_EMBOLD;
int rend_font_embolden = STYLE_FONT_EMBOLD_MODE_NORMAL;

void LVRendSetFontEmbolden( int addWidth )
{
    if ( addWidth < 0 )
        addWidth = 0;
    else if ( addWidth>STYLE_FONT_EMBOLD_MODE_EMBOLD )
        addWidth = STYLE_FONT_EMBOLD_MODE_EMBOLD;

    rend_font_embolden = addWidth;
}

int LVRendGetFontEmbolden()
{
    return rend_font_embolden;
}

LVFontRef getFont(css_style_rec_t * style, int documentId)
{
    int sz;
    if ( style->font_size.type == css_val_em || style->font_size.type == css_val_ex ||
            style->font_size.type == css_val_percent ) {
        // font_size.type can't be em/ex/%, it should have been converted to px
        // or screen_px while in setNodeStyle().
        printf("CRE WARNING: getFont: %d of unit %d\n", style->font_size.value>>8, style->font_size.type);
        sz = style->font_size.value >> 8; // set some value anyway
    }
    else {
        // We still need to convert other absolute units to px.
        // (we pass 0 as base_em and base_px, as these would not be used).
        sz = lengthToPx(style->font_size, 0, 0);
    }
    if ( sz < 8 )
        sz = 8;
    if ( sz > 340 )
        sz = 340;
    int fw;
    if (style->font_weight>=css_fw_100 && style->font_weight<=css_fw_900)
        fw = ((style->font_weight - css_fw_100)+1) * 100;
    else
        fw = 400;
    fw += rend_font_embolden;
    if ( fw>900 )
        fw = 900;
    // printf("cssd_font_family: %d %s", style->font_family, style->font_name.c_str());
    LVFontRef fnt = fontMan->GetFont(
        sz,
        fw,
        style->font_style==css_fs_italic,
        style->font_family,
        lString8(style->font_name.c_str()),
        documentId, true); // useBias=true, so that our preferred font gets used
    //fnt = LVCreateFontTransform( fnt, LVFONT_TRANSFORM_EMBOLDEN );
    return fnt;
}

int styleToTextFmtFlags( const css_style_ref_t & style, int oldflags )
{
    int flg = oldflags;
    if ( style->display == css_d_run_in ) {
        flg |= LTEXT_RUNIN_FLAG;
    } //else
    if (style->display != css_d_inline) {
        // text alignment flags
        flg = oldflags & ~LTEXT_FLAG_NEWLINE;
        if ( !(oldflags & LTEXT_RUNIN_FLAG) ) {
            switch (style->text_align)
            {
            case css_ta_left:
                flg |= LTEXT_ALIGN_LEFT;
                break;
            case css_ta_right:
                flg |= LTEXT_ALIGN_RIGHT;
                break;
            case css_ta_center:
                flg |= LTEXT_ALIGN_CENTER;
                break;
            case css_ta_justify:
                flg |= LTEXT_ALIGN_WIDTH;
                break;
            case css_ta_inherit:
                break;
            }
            switch (style->text_align_last)
            {
            case css_ta_left:
                flg |= LTEXT_LAST_LINE_ALIGN_LEFT;
                break;
            case css_ta_right:
                flg |= LTEXT_LAST_LINE_ALIGN_RIGHT;
                break;
            case css_ta_center:
                flg |= LTEXT_LAST_LINE_ALIGN_CENTER;
                break;
            case css_ta_justify:
                flg |= LTEXT_LAST_LINE_ALIGN_LEFT;
                break;
            case css_ta_inherit:
                break;
            }
        }
    }
    if ( style->white_space == css_ws_pre )
        flg |= LTEXT_FLAG_PREFORMATTED;
    //flg |= oldflags & ~LTEXT_FLAG_NEWLINE;
    return flg;
}

// Convert CSS value (type + number value) to screen px
int lengthToPx( css_length_t val, int base_px, int base_em )
{
    if (val.type == css_val_screen_px) { // use value as is
        return val.value;
    }

    // base_px is usually the width of the container element
    // base_em is usually the font size of the parent element
    int px = 0; // returned screen px
    bool ensure_non_zero = true; // return at least 1px if val.value is non-zero
    // Previously, we didn't, so don't ensure non-zero if gRenderDPI=0
    if (!gRenderDPI) ensure_non_zero = false;

    // Scale style value according to gRenderDPI (no scale if 96 or 0)
    // we do that early to not lose precision
    int value = scaleForRenderDPI(val.value);

    // value for all units is stored *256 to not lose fractional part
    switch( val.type )
    {
    /* absolute value, most often seen */
    case css_val_px:
        // round it to closest int
        px = (value + 0x7F) >> 8;
        break;

    /* relative values */
    /* We should use val.value (not scaled by gRenderDPI) here */
    case css_val_em: // value = em*256 (font size of the current element)
        px = (base_em * val.value) >> 8;
        break;
    case css_val_percent:
        px = ( base_px * val.value / 100 ) >> 8;
        break;
    case css_val_ex: // value = ex*512 (approximated with base_em, 1ex =~ 0.5em in many fonts)
        px = (base_em * val.value) >> 9;
        break;

    case css_val_rem: // value = rem*256 (font size of the root element)
        px = (gRootFontSize * val.value) >> 8;
        break;

    /* absolute value, less often used - value = unit*256 */
    /* (previously treated by crengine as 0, which we still do if gRenderDPI=0) */
    case css_val_in: // 2.54 cm   1in = 96px
        if (gRenderDPI)
            px = (96 * value ) >> 8;
        break;
    case css_val_cm: //        2.54cm = 96px
        if (gRenderDPI)
            px = (int)(96 * value / 2.54) >> 8;
        break;
    case css_val_mm: //        25.4mm = 96px
        if (gRenderDPI)
            px = (int)(96 * value / 25.4) >> 8;
        break;
    case css_val_pt: // 1/72 in  72pt = 96px
        if (gRenderDPI)
            px = 96 * value / 72 >> 8;
        break;
    case css_val_pc: // 12 pt     6pc = 96px
        if (gRenderDPI)
            px = 96 * value / 6 >> 8;
        break;

    case css_val_unspecified: // may be used with named values like "auto", but should
    case css_val_inherited:            // return 0 when lengthToPx() is called on them
    default:
        px = 0;
        ensure_non_zero = false;
    }
    if (!px && val.value && ensure_non_zero)
        px = 1;
    return px;
}

void SplitLines( const lString16 & str, lString16Collection & lines )
{
    const lChar16 * s = str.c_str();
    const lChar16 * start = s;
    for ( ; *s; s++ ) {
        if ( *s=='\r' || *s=='\n' ) {
            //if ( s > start )
            //    lines.add( cs16("*") + lString16( start, s-start ) + cs16("<") );
            //else
            //    lines.add( cs16("#") );
            if ( (s[1] =='\r' || s[1]=='\n') && (s[1]!=s[0]) )
                s++;
            start = s+1;
        }
    }
    while ( *start=='\r' || *start=='\n' )
        start++;
    if ( s > start )
        lines.add( lString16( start, (lvsize_t)(s-start) ) );
}

// Returns the marker for a list item node. If txform is supplied render the marker, too.
// marker_width is updated and can be used to add indent or padding necessary to make
// room for the marker (what and how to do it depending of list-style_position (inside/outside)
// is left to the caller)
lString16 renderListItemMarker( ldomNode * enode, int & marker_width, LFormattedText * txform, int line_h, int flags ) {
    lString16 marker;
    marker_width = 0;
    ListNumberingPropsRef listProps =  enode->getDocument()->getNodeNumberingProps( enode->getParentNode()->getDataIndex() );
    if ( listProps.isNull() ) { // no previously cached info: compute and cache it
        int counterValue = 0;
        ldomNode * parent = enode->getParentNode();
        int maxWidth = 0;
        for ( int i=0; i<parent->getChildCount(); i++ ) {
            lString16 marker;
            int markerWidth = 0;
            ldomNode * child = parent->getChildElementNode(i);
            if ( child && child->getNodeListMarker( counterValue, marker, markerWidth ) ) {
                if ( markerWidth>maxWidth )
                    maxWidth = markerWidth;
            }
        }
        listProps = ListNumberingPropsRef( new ListNumberingProps(counterValue, maxWidth) );
        enode->getDocument()->setNodeNumberingProps( enode->getParentNode()->getDataIndex(), listProps );
    }
    int counterValue = 0;
    if ( enode->getNodeListMarker( counterValue, marker, marker_width ) ) {
        if ( !listProps.isNull() )
            marker_width = listProps->maxWidth;
        css_style_rec_t * style = enode->getStyle().get();
        LVFont * font = enode->getFont().get();
        lUInt32 cl = style->color.type!=css_val_color ? 0xFFFFFFFF : style->color.value;
        lUInt32 bgcl = style->background_color.type!=css_val_color ? 0xFFFFFFFF : style->background_color.value;
        marker += "\t";
        if ( txform ) {
            txform->AddSourceLine( marker.c_str(), marker.length(), cl, bgcl, font, flags|LTEXT_FLAG_OWNTEXT, line_h, 0, 0);
        }
    }
    return marker;
}


//=======================================================================
// Render final block
//=======================================================================
// This renderFinalBlock() is NOT the equivalent of renderBlockElement()
// for nodes with erm_final ! But ldomNode::renderFinalBlock() IS.
//
// Here, we just walk all the inline nodes to AddSourceLine() text nodes and
// AddSourceObject() image nodes to the provided LFormattedText * txform.
// It is the similarly named (called by renderBlockElement() when erm_final):
//   ldomNode::renderFinalBlock(LFormattedTextRef & frmtext, RenderRectAccessor * fmt, int width)
// that is provided with a width (with padding removed), and after calling
// this 'void renderFinalBlock()' here, calls:
//   int h = LFormattedTextRef->Format((lUInt16)width, (lUInt16)page_h)
// to do the actual width-constrained rendering of the AddSource*'ed objects.
// Note: here, RenderRectAccessor * fmt is only used to get the width of the container,
// which is only needed to compute: ident = lengthToPx(style->text_indent, width, em)
// (todo: replace 'fmt' with 'int basewidth' to avoid confusion)
void renderFinalBlock( ldomNode * enode, LFormattedText * txform, RenderRectAccessor * fmt, int & baseflags, int ident, int line_h, int valign_dy )
{
    int txform_src_count = txform->GetSrcCount(); // to track if we added lines to txform
    if ( enode->isElement() ) {
        lvdom_element_render_method rm = enode->getRendMethod();
        if ( rm == erm_invisible )
            return; // don't draw invisible
        bool is_object = false;
        const css_elem_def_props_t * ntype = enode->getElementTypePtr();
        if ( ntype && ntype->is_object )
            is_object = true;
        //RenderRectAccessor fmt2( enode );
        //fmt = &fmt2;

        // About styleToTextFmtFlags:
        // - with inline nodes, it only updates LTEXT_FLAG_PREFORMATTED flag when css_ws_pre
        // - with block nodes (so, only with the first "final" node, and not when
        // recursing its children which are inline), it will set horitontal alignment flags
        int flags = styleToTextFmtFlags( enode->getStyle(), baseflags );
        int width = fmt->getWidth();
        int em = enode->getFont()->getSize();
        css_style_rec_t * style = enode->getStyle().get();
        if ((flags & LTEXT_FLAG_NEWLINE) && rm != erm_inline) {
            // Non-inline node in a final block: this is the top and single 'final' node:
            // get text-indent (mispelled 'ident' here and elsewhere) and line-height
            // that will apply to the full final block
            ident = lengthToPx(style->text_indent, width, em);

            // line-height may be a bit tricky, so let's fallback
            // to the original behaviour when gRenderDPI = 0
            if (gRenderDPI) {
                // line_h is named 'interval' in lvtextfm.cpp, and described as:
                //   *16 (16=normal, 32=double)
                // so both % and em should be related to the value '16'
                // line_height can be a number without unit, and it behaves as "em"
                css_length_t line_height = css_length_t(
                    style->line_height.type == css_val_unspecified ? css_val_em : style->line_height.type,
                    style->line_height.value);
                line_h = lengthToPx(line_height, 16, 16);
                // line_height should never be css_val_inherited as spreadParent
                // had updated it with its parent value, which could be the root
                // element value, which is a value in % (90, 100 or 120), so we
                // always got a valid style->line_height, and there is no need
                // to keep the provided line_h like the original computation does
            }
            else { // original crengine computation
                css_length_t len = style->line_height;
                switch( len.type )
                {
                case css_val_percent:
                    line_h = len.value * 16 / 100 >> 8;
                    break;
                case css_val_px:
                    line_h = len.value * 16 / enode->getFont()->getHeight() >> 8;
                    break;
                case css_val_em:
                    line_h = len.value * 16 / 256;
                    break;
                default:
                    break;
                }
            }
            // We set the LFormattedText strut_height and strut_baseline
            // with the values from this "final" node. All lines made out from
            // children will have a minimal height and baseline set to these.
            // See https://www.w3.org/TR/CSS2/visudet.html#line-height
            //   The minimum height consists of a minimum height above
            //   the baseline and a minimum depth below it, exactly as if
            //   each line box starts with a zero-width inline box with the
            //   element's font and line height properties. We call that
            //   imaginary box a "strut."
            // and https://iamvdo.me/en/blog/css-font-metrics-line-height-and-vertical-align
            int fh = enode->getFont()->getHeight();
            int fb = enode->getFont()->getBaseline();
            int f_line_h = (fh * line_h) >> 4; // font height + interline space
            int f_half_leading = (f_line_h - fh) /2;
            txform->setStrut(f_line_h, fb + f_half_leading);
        }
        // save flags: will be re-inited to that when we meet a <BR/>
        int final_block_flags = flags;

        // Now, process styles that may differ between inline nodes, and
        // are needed to display any children text node.

        // Vertical alignment flags & y-drift from main baseline.
        // valign_dy is all that is needed for text nodes, but we need
        // a LTEXT_VALIGN_* flag for objects (images), as their height
        // is not known here, and only computed in lvtextfm.cpp.
        //
        // Texts in quotes from https://www.w3.org/TR/CSS2/visudet.html#line-height
        //
        // We update valign_dy, so it is passed to all children and used
        // as a base for their own vertical align computations.
        // There are a few vertical-align named values that need a special
        // processing for images (their current font is the parent font, and
        // of no use for vertical-alignement).
        css_length_t vertical_align = style->vertical_align;
        if ( (vertical_align.type == css_val_unspecified && vertical_align.value == css_va_baseline) ||
              vertical_align.value == 0 ) {
            // "Align the baseline of the box with the baseline of the parent box.
            //  If the box does not have a baseline, align the bottom margin edge with
            //  the parent's baseline."
            // This is the default behaviour in lvtextfm.cpp: no valign_dy or flags
            // change needed: keep the existing ones (parent's y drift related to
            // the line box main baseline)
        }
        else {
            // We need current and parent nodes font metrics for most computations.
            // A few misc notes:
            //   - Freetype includes any "line gap" from the font metrics
            //   in the ascender (the part above the baseline).
            //   - getBaseline() gives the distance from the top to the baseline (so,
            //   ascender + line gap)
            //   - the descender font value is added to baseline to make height
            //   - getHeight() is usually larger than getSize(), and
            //   getSize() is often nearer to getBaseline().
            //   See: https://iamvdo.me/en/blog/css-font-metrics-line-height-and-vertical-align
            //   Some examples with various fonts at various sizes:
            //     size=9  height=11 baseline=8
            //     size=11 height=15 baseline=11
            //     size=13 height=16 baseline=13
            //     size=19 height=23 baseline=18
            //     size=23 height=31 baseline=24
            //     size=26 height=31 baseline=25
            //   - Freetype has no function to give us font subscript, superscript, x-height
            //   and related values, so we have to approximate them from height and baseline.
            int fh = enode->getFont()->getHeight();
            int fb = enode->getFont()->getBaseline();
            int f_line_h = (fh * line_h) >> 4; // font height + interline space
            int f_half_leading = (f_line_h - fh) /2;
            // Use the current font values if no parent (should not happen thus) to
            // avoid the need for if-checks below
            int pem = em;
            int pfh = fh;
            int pfb = fb;
            int pf_line_h = f_line_h;
            int pf_half_leading = f_half_leading;
            ldomNode *parent = enode->getParentNode();
            if (parent && !parent->isNull()) {
                pem = parent->getFont()->getSize();
                pfh = parent->getFont()->getHeight();
                pfb = parent->getFont()->getBaseline();
                pf_line_h = (pfh * line_h) >> 4; // font height + interline space
                pf_half_leading = (pf_line_h - pfh) /2;
            }
            if (vertical_align.type == css_val_unspecified) { // named values
                switch (style->vertical_align.value) {
                    case css_va_sub:
                        // "Lower the baseline of the box to the proper position for subscripts
                        //  of the parent's box."
                        // Use a fraction of the height below the baseline only
                        // 3/5 looks perfect with some fonts, 5/5 looks perfect with
                        // some others, so use 4/5 (which is not the finest with some
                        // fonts, but a sane middle ground)
                        valign_dy += (pfh - pfb)*4/5;
                        flags |= LTEXT_VALIGN_SUB;
                        break;
                    case css_va_super:
                        // "Raise the baseline of the box to the proper position for superscripts
                        //  of the parent's box."
                        // 1/4 of the font height looks alright with most fonts (we could also
                        // use a fraction of 'baseline' only, the height above the baseline)
                        valign_dy -= pfh / 4;
                        flags |= LTEXT_VALIGN_SUPER;
                        break;
                    case css_va_middle:
                        // "Align the vertical midpoint of the box with the baseline of the parent box
                        //  plus half the x-height of the parent."
                        // For CSS lengths, we approximate 'ex' with 1/2 'em'. Let's do the same here.
                        // (Firefox falls back to 0.56 x ascender for x-height:
                        //   valign_dy -= 0.56 * pfb / 2;  but this looks a little too low)
                        if (is_object)
                            valign_dy -= pem/4; // y for middle of image (lvtextfm.cpp will now from flags)
                        else {
                            valign_dy += fb - fh/2; // move down current middle point to baseline
                            valign_dy -= pem/4; // move up by half of parent ex
                            // This looks different from Firefox rendering, but actually a bit a
                            // better "middle" to me:
                            // valign_dy -= pem/2 - em/2;
                        }
                        flags |= LTEXT_VALIGN_MIDDLE;
                        break;
                    case css_va_text_bottom:
                        // "Align the bottom of the box with the bottom of the parent's content area"
                        // With valign_dy=0, they are centered on the baseline. We want
                        // them centered on their bottom line
                        if (is_object)
                            valign_dy += (pfh - pfb);
                        else
                            valign_dy += (pfh - pfb) - (fh - fb);
                        flags |= LTEXT_VALIGN_TEXT_BOTTOM;
                        break;
                    case css_va_text_top:
                        // "Align the top of the box with the top of the parent's content area"
                        // With valign_dy=0, they are centered on the baseline. We want
                        // them centered on their top line
                        if (is_object)
                            valign_dy -= pfb; // y for top of image (lvtextfm.cpp will now from flags)
                        else
                            valign_dy -= pfb - fb;
                        flags |= LTEXT_VALIGN_TEXT_TOP;
                        break;
                    case css_va_bottom:
                        // "Align the bottom of the aligned subtree with the bottom of the line box"
                        // This should most probably be re-computed once a full line has been laid
                        // out, which would need us to do this in lvtextfm.cpp, and we would need to
                        // go back words when the last word has been laid out...
                        // We go the easy way by just aligning to our parent bottom, so just
                        // as css_va_text_bottom + half_leading
                        if (is_object)
                            valign_dy += (pfh - pfb + pf_half_leading);
                        else
                            valign_dy += (pfh - pfb + pf_half_leading) - (fh - fb + f_half_leading);
                        flags |= LTEXT_VALIGN_BOTTOM;
                        break;
                    case css_va_top:
                        // "Align the top of the aligned subtree with the top of the line box."
                        // We go the easy way by just aligning to our parent top, so just
                        // as css_va_text_top + half_leading
                        if (is_object)
                            valign_dy -= (pfb + pf_half_leading); // y for top of image
                        else
                            valign_dy -= (pfb + pf_half_leading) - (fb + f_half_leading);
                        flags |= LTEXT_VALIGN_TOP;
                        break;
                    case css_va_baseline:
                    default:
                        break;
                }
            }
            else {
                // "<percentage> Raise (positive value) or lower (negative value) the box by this
                //  distance (a percentage of the 'line-height' value).
                //  <length> Raise (positive value) or lower (negative value) the box by this distance"
                // No mention if the base for 'em' should be the current font, or
                // the parent font, and if we should use ->getHeight() or ->getSize().
                // But using the current font size looks correct and similar when
                // comparing to Firefox rendering.
                int base_em = em; // use current font ->getSize()
                int base_pct = (fh * line_h) >> 4; // font height + interline space (as in lvtextfm.cpp)
                // positive values push text up, so reduce dy
                valign_dy -= lengthToPx(vertical_align, base_pct, base_em);
            }
        }
        switch ( style->text_decoration ) {
            case css_td_underline:
                flags |= LTEXT_TD_UNDERLINE;
                break;
            case css_td_overline:
                flags |= LTEXT_TD_OVERLINE;
                break;
            case css_td_line_through:
                flags |= LTEXT_TD_LINE_THROUGH;
                break;
            case css_td_blink:
                flags |= LTEXT_TD_BLINK;
                break;
            default:
                break;
        }
        switch ( style->hyphenate ) {
            case css_hyph_auto:
                flags |= LTEXT_HYPHENATE;
                break;
            default:
                break;
        }

        if ( rm==erm_list_item ) { // obsolete rendering method (used only when gDOMVersionRequested < 20180524)
            // put item number/marker to list
            lString16 marker;
            int marker_width = 0;

            ListNumberingPropsRef listProps =  enode->getDocument()->getNodeNumberingProps( enode->getParentNode()->getDataIndex() );
            if ( listProps.isNull() ) {
                int counterValue = 0;
                ldomNode * parent = enode->getParentNode();
                int maxWidth = 0;
                for ( int i=0; i<parent->getChildCount(); i++ ) {
                    lString16 marker;
                    int markerWidth = 0;
                    ldomNode * child = parent->getChildElementNode(i);
                    if ( child && child->getNodeListMarker( counterValue, marker, markerWidth ) ) {
                        if ( markerWidth>maxWidth )
                            maxWidth = markerWidth;
                    }
                }
                listProps = ListNumberingPropsRef( new ListNumberingProps(counterValue, maxWidth) );
                enode->getDocument()->setNodeNumberingProps( enode->getParentNode()->getDataIndex(), listProps );
            }
            int counterValue = 0;
            if ( enode->getNodeListMarker( counterValue, marker, marker_width ) ) {
                if ( !listProps.isNull() )
                    marker_width = listProps->maxWidth;
                css_list_style_position_t sp = style->list_style_position;
                LVFont * font = enode->getFont().get();
                lUInt32 cl = style->color.type!=css_val_color ? 0xFFFFFFFF : style->color.value;
                lUInt32 bgcl = style->background_color.type!=css_val_color ? 0xFFFFFFFF : style->background_color.value;
                int margin = 0;
                if ( sp==css_lsp_outside )
                    margin = -marker_width;
                marker += "\t";
                txform->AddSourceLine( marker.c_str(), marker.length(), cl, bgcl, font, flags|LTEXT_FLAG_OWNTEXT, line_h, valign_dy,
                                        margin, NULL );
                flags &= ~LTEXT_FLAG_NEWLINE;
            }
        }

        // List item marker rendering when css_d_list_item_block and list-style-position = inside:
        // render the marker if any, and continue rendering text on same line
        // Rendering hack: we do that too when list-style-position = outside AND text-align "right"
        // or "center", as this will draw the marker at the near left of the text (otherwise,
        // the marker would be drawn on the far left of the whole available width, which is ugly.
        if ( style->display == css_d_list_item_block && ( style->list_style_position == css_lsp_inside ||
                (style->list_style_position == css_lsp_outside &&
                    (style->text_align == css_ta_center || style->text_align == css_ta_right)) ) ) {
            // list_item_block rendered as final (containing only text and inline elements)
            int marker_width;
            lString16 marker = renderListItemMarker( enode, marker_width, txform, line_h, flags );
            if ( marker.length() ) {
                flags &= ~LTEXT_FLAG_NEWLINE;
            }
        }
        if ( rm == erm_final ) {
            // when list_item_block has been rendered as block (containing text and block elements)
            // and list-style-position = inside, we flagged (in renderBlockElement, by associating to
            // it a ListNumberingProps entry) the first child rendered as final so we know we have
            // to add the marker here
            ListNumberingPropsRef listProps =  enode->getDocument()->getNodeNumberingProps( enode->getDataIndex() );
            if ( !listProps.isNull() ) {
                // the associated ListNumberingProps stores the index of the list_item_block parent.
                // We get its marker and draw it here.
                ldomNode * list_item_block_parent = enode->getDocument()->getTinyNode(listProps->maxCounter);
                int marker_width;
                lString16 marker = renderListItemMarker( list_item_block_parent, marker_width, txform, line_h, flags );
                if ( marker.length() ) {
                    flags &= ~LTEXT_FLAG_NEWLINE;
                }
            }
        }

        if ( is_object ) { // object element, like <IMG>
#ifdef DEBUG_DUMP_ENABLED
            logfile << "+OBJECT ";
#endif
            bool isBlock = style->display == css_d_block;
            if ( isBlock ) {
                // If block image, forget any current flags and start from baseflags (?)
                int flags = styleToTextFmtFlags( enode->getStyle(), baseflags );
                //txform->AddSourceLine(L"title", 5, 0x000000, 0xffffff, font, baseflags, interval, margin, NULL, 0, 0);
                LVFont * font = enode->getFont().get();
                lUInt32 cl = style->color.type!=css_val_color ? 0xFFFFFFFF : style->color.value;
                lUInt32 bgcl = style->background_color.type!=css_val_color ? 0xFFFFFFFF : style->background_color.value;
                lString16 title;
                //txform->AddSourceLine( title.c_str(), title.length(), cl, bgcl, font, LTEXT_FLAG_OWNTEXT|LTEXT_FLAG_NEWLINE, line_h, 0, NULL );
                //baseflags
                title = enode->getAttributeValue(attr_suptitle);
                if ( !title.empty() ) {
                    lString16Collection lines;
                    lines.parse(title, cs16("\\n"), true);
                    for ( int i=0; i<lines.length(); i++ )
                        txform->AddSourceLine( lines[i].c_str(), lines[i].length(), cl, bgcl, font, flags|LTEXT_FLAG_OWNTEXT, line_h, valign_dy, 0, NULL );
                }
                txform->AddSourceObject(flags, line_h, valign_dy, ident, enode );
                title = enode->getAttributeValue(attr_subtitle);
                if ( !title.empty() ) {
                    lString16Collection lines;
                    lines.parse(title, cs16("\\n"), true);
                    for ( int i=0; i<lines.length(); i++ )
                        txform->AddSourceLine( lines[i].c_str(), lines[i].length(), cl, bgcl, font, flags|LTEXT_FLAG_OWNTEXT, line_h, valign_dy, 0, NULL );
                }
                title = enode->getAttributeValue(attr_title);
                if ( !title.empty() ) {
                    lString16Collection lines;
                    lines.parse(title, cs16("\\n"), true);
                    for ( int i=0; i<lines.length(); i++ )
                        txform->AddSourceLine( lines[i].c_str(), lines[i].length(), cl, bgcl, font, flags|LTEXT_FLAG_OWNTEXT, line_h, valign_dy, 0, NULL );
                }
            } else { // inline image
                // We use the flags computed previously (and not baseflags) as they
                // carry vertical alignment
                txform->AddSourceObject(flags, line_h, valign_dy, ident, enode );
                baseflags &= ~LTEXT_FLAG_NEWLINE; // clear newline flag
            }
        }
        else { // non-IMG element: render children (elements or text nodes)
            int cnt = enode->getChildCount();
#ifdef DEBUG_DUMP_ENABLED
            logfile << "+BLOCK [" << cnt << "]";
#endif
            // usual elements
            bool thisIsRunIn = enode->getStyle()->display==css_d_run_in;
            if ( thisIsRunIn )
                flags |= LTEXT_RUNIN_FLAG;
            for (int i=0; i<cnt; i++)
            {
                ldomNode * child = enode->getChildNode( i );
                renderFinalBlock( child, txform, fmt, flags, ident, line_h, valign_dy );
            }
            // Note: CSS "display: run-in" is no longer used with our epub.css (it is
            // used with older css files for "body[name="notes"] section title", either
            // for crengine internal footnotes displaying, or some FB2 features)
            if ( thisIsRunIn ) {
                // append space to run-in object
                LVFont * font = enode->getFont().get();
                css_style_ref_t style = enode->getStyle();
                lUInt32 cl = style->color.type!=css_val_color ? 0xFFFFFFFF : style->color.value;
                lUInt32 bgcl = style->background_color.type!=css_val_color ? 0xFFFFFFFF : style->background_color.value;
                lChar16 delimiter[] = {UNICODE_NO_BREAK_SPACE, UNICODE_NO_BREAK_SPACE}; //160
                txform->AddSourceLine( delimiter, sizeof(delimiter)/sizeof(lChar16), cl, bgcl, font, LTEXT_FLAG_OWNTEXT | LTEXT_RUNIN_FLAG, line_h, valign_dy, 0, NULL );
                flags &= ~LTEXT_RUNIN_FLAG;
            }
        }


#ifdef DEBUG_DUMP_ENABLED
      for (int i=0; i<enode->getNodeLevel(); i++)
        logfile << " . ";
#endif
#ifdef DEBUG_DUMP_ENABLED
        lvRect rect;
        enode->getAbsRect( rect );
        logfile << "<" << enode->getNodeName() << ">     flags( "
            << baseflags << "-> " << flags << ")  rect( "
            << rect.left << rect.top << rect.right << rect.bottom << ")\n";
#endif

        // restore flags
        //***********************************
        baseflags = final_block_flags; // to allow blocks in one level with inlines
        if ( enode->getNodeId()==el_br ) {
            if (baseflags & LTEXT_FLAG_NEWLINE) {
                // We meet a <BR/>, but no text node were met before (or it
                // would have cleared the newline flag).
                // Output a single space so that a blank line can be made,
                // as wanted by a <BR/>.
                // (This makes consecutive and stuck <br><br><br> work)
                LVFont * font = enode->getFont().get();
                txform->AddSourceLine( L" ", 1, 0, 0, font, baseflags | LTEXT_FLAG_OWNTEXT, line_h, valign_dy);
                // baseflags &= ~LTEXT_FLAG_NEWLINE; // clear newline flag
                // No need to clear the flag, as we set it just below
                // (any LTEXT_ALIGN_* set implies LTEXT_FLAG_NEWLINE)

            }
            // Re-set the newline and aligment flag for what's coming
            // after this <BR/>
            //baseflags |= LTEXT_ALIGN_LEFT;
            switch (style->text_align) {
            case css_ta_left:
                baseflags |= LTEXT_ALIGN_LEFT;
                break;
            case css_ta_right:
                baseflags |= LTEXT_ALIGN_RIGHT;
                break;
            case css_ta_center:
                baseflags |= LTEXT_ALIGN_CENTER;
                break;
            case css_ta_justify:
                baseflags |= LTEXT_ALIGN_WIDTH;
                ident = 0;
                break;
            case css_ta_inherit:
                break;
            }
//            baseflags &= ~LTEXT_FLAG_NEWLINE; // clear newline flag
//            LVFont * font = enode->getFont().get();
//            txform->AddSourceLine( L"\n", 1, 0, 0, font, baseflags | LTEXT_FLAG_OWNTEXT,
//                line_h, 0, enode, 0, 0 );
        } else {
            // 'baseflags' carries information about text alignment, and
            // clearing newline flag would remove it. This information is
            // only useful on the first source line added (with AddSourceLine)
            // so don't clear it if we hadn't added anything
            if (txform->GetSrcCount() > txform_src_count)
                baseflags &= ~LTEXT_FLAG_NEWLINE; // clear newline flag
        }
        //baseflags &= ~LTEXT_RUNIN_FLAG;
    }
    else if ( enode->isText() ) {
        // text nodes
        lString16 txt = enode->getText();
        if ( !txt.empty() )
        {

#ifdef DEBUG_DUMP_ENABLED
      for (int i=0; i<enode->getNodeLevel(); i++)
        logfile << " . ";
#endif
#ifdef DEBUG_DUMP_ENABLED
            logfile << "#text" << " flags( "
                << baseflags << ")\n";
#endif

            ldomNode * parent = enode->getParentNode();
            int tflags = LTEXT_FLAG_OWNTEXT;
            if ( parent->getNodeId() == el_a )
                tflags |= LTEXT_IS_LINK;
            LVFont * font = parent->getFont().get();
            css_style_ref_t style = parent->getStyle();
            lUInt32 cl = style->color.type!=css_val_color ? 0xFFFFFFFF : style->color.value;
            lUInt32 bgcl = style->background_color.type!=css_val_color ? 0xFFFFFFFF : style->background_color.value;
            if(!enode->getParentNode()->getParentNode()->isNull())
                if((enode->getParentNode()->getParentNode()->getStyle()->background_color.value)==
                        lInt32(bgcl))
                    bgcl=0xFFFFFFFF;

            switch (style->text_transform) {
            case css_tt_uppercase:
                txt.uppercase();
                break;
            case css_tt_lowercase:
                txt.lowercase();
                break;
            case css_tt_capitalize:
                txt.capitalize();
                break;
            case css_tt_full_width:
                // txt.fullWidthChars(); // disabled for now (may change CJK rendering)
                break;
            case css_tt_none:
            case css_tt_inherit:
                break;
            }

            lInt8 letter_spacing;
            // % is not supported for letter_spacing by Firefox, but crengine
            // did support it, by relating it to font size, so let's use em
            // in place of width
            int em = font->getSize();
            letter_spacing = lengthToPx(style->letter_spacing, em, em);
            /*
            if ( baseflags & LTEXT_FLAG_PREFORMATTED ) {
                int flags = baseflags | tflags;
                lString16Collection lines;
                SplitLines( txt, lines );
                for ( int k=0; k<lines.length(); k++ ) {
                    lString16 str = lines[k];
                    txform->AddSourceLine( str.c_str(), str.length(), cl, bgcl,
                        font, flags, line_h, 0, node, 0, letter_spacing );
                    flags &= ~LTEXT_FLAG_NEWLINE;
                    flags |= LTEXT_ALIGN_LEFT;
                }
            } else {
            }
            */
            /* removal of leading spaces is now managed directly by lvtextfm
            //int offs = 0;
            if ( txform->GetSrcCount()==0 && style->white_space!=css_ws_pre ) {
                // clear leading spaces for first text of paragraph
                int i=0;
                for ( ;txt.length()>i && (txt[i]==' ' || txt[i]=='\t'); i++ )
                    ;
                if ( i>0 ) {
                    txt.erase(0, i);
                    //offs = i;
                }
            }
            */
            if ( txt.length()>0 ) {
                txform->AddSourceLine( txt.c_str(), txt.length(), cl, bgcl, font, baseflags | tflags,
                    line_h, valign_dy, ident, enode, 0, letter_spacing );
                baseflags &= ~LTEXT_FLAG_NEWLINE; // clear newline flag
            }
        }
    }
    else {
        crFatalError();
    }
}

int CssPageBreak2Flags( css_page_break_t prop )
{
    switch (prop)
    {
    case css_pb_always:
    case css_pb_left:
    case css_pb_right:
        return RN_SPLIT_ALWAYS;
    case css_pb_avoid:
        return RN_SPLIT_AVOID;
    case css_pb_auto:
        return RN_SPLIT_AUTO;
    default:
        return RN_SPLIT_AUTO;
    }
}

bool isFirstBlockChild( ldomNode * parent, ldomNode * child ) {
    int count = parent->getChildCount();
    for ( int i=0; i<count; i++ ) {
        ldomNode * el = parent->getChildNode(i);
        if ( el==child )
            return true;
        if ( el->isElement() ) {
            lvdom_element_render_method rm = el->getRendMethod();
            if ( rm==erm_final || rm==erm_block ) {
                RenderRectAccessor acc(el);
                if ( acc.getHeight()>5 )
                    return false;
            }
        }
    }
    return true;
}

void copystyle( css_style_ref_t source, css_style_ref_t dest )
{
    dest->display = source->display ;
    dest->white_space = source->white_space ;
    dest->text_align = source->text_align ;
    dest->text_align_last = source->text_align_last ;
    dest->text_decoration = source->text_decoration ;
    dest->text_transform = source->text_transform ;
    dest->vertical_align = source->vertical_align ;
    dest->font_family = source->font_family;
    dest->font_name = source->font_name ;
    dest->font_size.type = source->font_size.type ;
    dest->font_size.value = source->font_size.value ;
    dest->font_style = source->font_style ;
    dest->font_weight = source->font_weight ;
    dest->text_indent = source->text_indent ;
    dest->line_height = source->line_height ;
    dest->width = source->width ;
    dest->height = source->height ;
    dest->margin[0] = source->margin[0] ;
    dest->margin[1] = source->margin[1] ;
    dest->margin[2] = source->margin[2] ;
    dest->margin[3] = source->margin[3] ;
    dest->padding[0] = source->padding[0] ;
    dest->padding[1] = source->padding[1] ;
    dest->padding[2] = source->padding[2] ;
    dest->padding[3] = source->padding[3] ;
    dest->color = source->color ;
    dest->background_color = source->background_color ;
    dest->letter_spacing = source->letter_spacing ;
    dest->page_break_before = source->page_break_before ;
    dest->page_break_after = source->page_break_after ;
    dest->page_break_inside = source->page_break_inside ;
    dest->hyphenate = source->hyphenate ;
    dest->list_style_type = source->list_style_type ;
    dest->list_style_position = source->list_style_position ;
    dest->border_style_top=source->border_style_top;
    dest->border_style_bottom=source->border_style_bottom;
    dest->border_style_right=source->border_style_right;
    dest->border_style_left=source->border_style_left;
    dest->border_width[0]=source->border_width[0];
    dest->border_width[1]=source->border_width[1];
    dest->border_width[2]=source->border_width[2];
    dest->border_width[3]=source->border_width[3];
    dest->border_color[0]=source->border_color[0];
    dest->border_color[1]=source->border_color[1];
    dest->border_color[2]=source->border_color[2];
    dest->border_color[3]=source->border_color[3];
    dest->background_image=source->background_image;
    dest->background_repeat=source->background_repeat;
    dest->background_attachment=source->background_attachment;
    dest->background_position=source->background_position;
    dest->border_collapse=source->border_collapse;
    dest->border_spacing[0]=source->border_spacing[0];
    dest->border_spacing[1]=source->border_spacing[1];
    dest->cr_hint = source->cr_hint;
}

css_page_break_t getPageBreakBefore( ldomNode * el ) {
    if ( el->isText() )
        el = el->getParentNode();
    css_page_break_t before = css_pb_auto;
    while (el) {
        css_style_ref_t style = el->getStyle();
        if ( style.isNull() )
            return before;
        before = style->page_break_before;
        if ( before!=css_pb_auto )
        {
            if(!style.isNull())
            {
                // we should not modify styles directly, as the change in style cache will affect other
                // node with same style, and corrupt style cache Hash, invalidating cache reuse
                css_style_ref_t newstyle( new css_style_rec_t );
                copystyle(style, newstyle);
                newstyle->page_break_before=css_pb_auto;
                newstyle->page_break_inside=style->page_break_inside;
                newstyle->page_break_after=style->page_break_after;
                // we should no more modify a style after it has been applied to a node with setStyle()
                el->setStyle(newstyle);
            }
            return before;
        }
        ldomNode * parent = el->getParentNode();
        if ( !parent )
            return before;
        if ( !isFirstBlockChild(parent, el) )
            return before;
        el = parent;
    }
    return before;
}

css_page_break_t getPageBreakAfter( ldomNode * el ) {
    if ( el->isText() )
        el = el->getParentNode();
    css_page_break_t after = css_pb_auto;
    bool lastChild = true;
    while (el) {
        css_style_ref_t style = el->getStyle();
        if ( style.isNull() )
            return after;
        if ( lastChild && after==css_pb_auto )
            after = style->page_break_after;
        if ( !lastChild || after!=css_pb_auto )
            return after;
        ldomNode * parent = el->getParentNode();
        if ( !parent )
            return after;
        lastChild = ( lastChild && parent->getLastChild()==el );
        el = parent;
    }
    return after;
}

css_page_break_t getPageBreakInside( ldomNode * el ) {
    if ( el->isText() )
        el = el->getParentNode();
    css_page_break_t inside = css_pb_auto;
    while (el) {
        css_style_ref_t style = el->getStyle();
        if ( style.isNull() )
            return inside;
        if ( inside==css_pb_auto )
            inside = style->page_break_inside;
        if ( inside!=css_pb_auto )
            return inside;
        ldomNode * parent = el->getParentNode();
        if ( !parent )
            return inside;
        el = parent;
    }
    return inside;
}

void getPageBreakStyle( ldomNode * el, css_page_break_t &before, css_page_break_t &inside, css_page_break_t &after ) {
    bool firstChild = true;
    bool lastChild = true;
    before = inside = after = css_pb_auto;
    while (el) {
        css_style_ref_t style = el->getStyle();
        if ( style.isNull() )
            return;
        if ( firstChild && before==css_pb_auto ) {
            before = style->page_break_before;
        }
        if ( lastChild && after==css_pb_auto ) {
            after = style->page_break_after;
        }
        if ( inside==css_pb_auto ) {
            inside = style->page_break_inside;
        }
        if ( (!firstChild || before!=css_pb_auto) && (!lastChild || after!=css_pb_auto)
            && inside!=css_pb_auto)
            return;
        ldomNode * parent = el->getParentNode();
        if ( !parent )
            return;
        firstChild = ( firstChild && parent->getFirstChild()==el );
        lastChild = ( lastChild && parent->getLastChild()==el );
        el = parent;
    }
}

// Default border width in screen px when border requested but no width specified
#define DEFAULT_BORDER_WIDTH 2

//measure border width, 0 for top,1 for right,2 for bottom,3 for left
int measureBorder(ldomNode *enode,int border) {
        int em = enode->getFont()->getSize();
        // No need for a width, as border does not support units in % according
        // to CSS specs.
        int width = 0;
        // (Note: another reason for disabling borders in % (that we did support)
        // is that, at the various places where measureBorder() is called,
        // fmt.setWidth() has not yet been called and fmt.getWidth() would
        // return 0. Later, at drawing time, fmt.getWidth() will return the real
        // width, which could cause rendering of borders over child elements,
        // as these were positionned with a border=0.)
        if (border==0){
                bool hastopBorder = (enode->getStyle()->border_style_top >= css_border_solid &&
                                     enode->getStyle()->border_style_top <= css_border_outset);
                if (!hastopBorder) return 0;
                css_length_t bw = enode->getStyle()->border_width[0];
                if (bw.value == 0 && bw.type > css_val_unspecified) return 0; // explicit value of 0: no border
                int topBorderwidth = lengthToPx(bw, width, em);
                topBorderwidth = topBorderwidth != 0 ? topBorderwidth : DEFAULT_BORDER_WIDTH;
                return topBorderwidth;}
            else if (border==1){
                bool hasrightBorder = (enode->getStyle()->border_style_right >= css_border_solid &&
                                       enode->getStyle()->border_style_right <= css_border_outset);
                if (!hasrightBorder) return 0;
                css_length_t bw = enode->getStyle()->border_width[1];
                if (bw.value == 0 && bw.type > css_val_unspecified) return 0;
                int rightBorderwidth = lengthToPx(bw, width, em);
                rightBorderwidth = rightBorderwidth != 0 ? rightBorderwidth : DEFAULT_BORDER_WIDTH;
                return rightBorderwidth;}
            else if (border ==2){
                bool hasbottomBorder = (enode->getStyle()->border_style_bottom >= css_border_solid &&
                                        enode->getStyle()->border_style_bottom <= css_border_outset);
                if (!hasbottomBorder) return 0;
                css_length_t bw = enode->getStyle()->border_width[2];
                if (bw.value == 0 && bw.type > css_val_unspecified) return 0;
                int bottomBorderwidth = lengthToPx(bw, width, em);
                bottomBorderwidth = bottomBorderwidth != 0 ? bottomBorderwidth : DEFAULT_BORDER_WIDTH;
                return bottomBorderwidth;}
            else if (border==3){
                bool hasleftBorder = (enode->getStyle()->border_style_left >= css_border_solid &&
                                      enode->getStyle()->border_style_left <= css_border_outset);
                if (!hasleftBorder) return 0;
                css_length_t bw = enode->getStyle()->border_width[3];
                if (bw.value == 0 && bw.type > css_val_unspecified) return 0;
                int leftBorderwidth = lengthToPx(bw, width, em);
                leftBorderwidth = leftBorderwidth != 0 ? leftBorderwidth : DEFAULT_BORDER_WIDTH;
                return leftBorderwidth;}
           else return 0;
        }

//calculate total margin+padding before node,if >0 don't do campulsory page split
int pagebreakhelper(ldomNode *enode,int width)
{
    int flag=css_pb_auto;
    int em = enode->getFont()->getSize();
    int margin_top = lengthToPx( enode->getStyle()->margin[2], width, em ) + DEBUG_TREE_DRAW;
    int padding_top = lengthToPx( enode->getStyle()->padding[2], width, em ) + DEBUG_TREE_DRAW+measureBorder(enode,0);
    flag=CssPageBreak2Flags(getPageBreakBefore(enode))<<RN_SPLIT_BEFORE;
    if (flag==RN_SPLIT_BEFORE_ALWAYS){
        ldomNode *node=enode;
        int top=0;
        while (!node->isNull()) {
            top+=lengthToPx( node->getStyle()->margin[2], width, em ) +
                 lengthToPx( node->getStyle()->padding[2], width, em ) +
                 measureBorder(node,0);
            ldomNode * parent = node->getParentNode();
            if ( !parent ) break;
            if ( !isFirstBlockChild(parent, node) ) break;
            node = parent;
        }
        top-=margin_top+padding_top;
        if (top>0) flag=RN_SPLIT_AUTO;
        if ((getPageBreakBefore(enode)==css_pb_always)) flag=RN_SPLIT_ALWAYS;
    }
    return flag;
}

//=======================================================================
// Render block element
//=======================================================================
int renderBlockElement( LVRendPageContext & context, ldomNode * enode, int x, int y, int width )
{
    if ( enode->isElement() )
    {
        bool isFootNoteBody = false;
        lString16 footnoteId;
        // Allow displaying footnote content at the bottom of all pages that contain a link
        // to it, when -cr-hint: footnote-inpage is set on the footnote block container.
        if ( enode->getStyle()->cr_hint == css_cr_hint_footnote_inpage &&
                    enode->getDocument()->getDocFlag(DOC_FLAG_ENABLE_FOOTNOTES)) {
            footnoteId = enode->getFirstInnerAttributeValue(attr_id);
            if ( !footnoteId.empty() )
                isFootNoteBody = true;
        }
        // For fb2 documents. Description of the <body> element from FictionBook2.2.xsd:
        //   Main content of the book, multiple bodies are used for additional
        //   information, like footnotes, that do not appear in the main book
        //   flow. The first body is presented to the reader by default, and
        //   content in the other bodies should be accessible by hyperlinks. Name
        //   attribute should describe the meaning of this body, this is optional
        //   for the main body.
        if ( enode->getNodeId()==el_section && enode->getDocument()->getDocFlag(DOC_FLAG_ENABLE_FOOTNOTES) ) {
            ldomNode * body = enode->getParentNode();
            while ( body != NULL && body->getNodeId()!=el_body )
                body = body->getParentNode();
            if ( body ) {
                if (body->getAttributeValue(attr_name) == "notes" || body->getAttributeValue(attr_name) == "comments")
                    footnoteId = enode->getAttributeValue(attr_id);
                    if ( !footnoteId.empty() )
                        isFootNoteBody = true;
            }
        }
//        if ( isFootNoteBody )
//            CRLog::trace("renderBlockElement() : Footnote body detected! %s", LCSTR(ldomXPointer(enode,0).toString()) );
        //if (!fmt)
        //    crFatalError();
//        if ( enode->getNodeId() == el_empty_line )
//            x = x;

        int em = enode->getFont()->getSize();
        int margin_left = lengthToPx( enode->getStyle()->margin[0], width, em ) + DEBUG_TREE_DRAW;
        int margin_right = lengthToPx( enode->getStyle()->margin[1], width, em ) + DEBUG_TREE_DRAW;
        int margin_top = lengthToPx( enode->getStyle()->margin[2], width, em ) + DEBUG_TREE_DRAW;
        int margin_bottom = lengthToPx( enode->getStyle()->margin[3], width, em ) + DEBUG_TREE_DRAW;
        int border_top = measureBorder(enode,0);
        int border_bottom = measureBorder(enode,2);
        int padding_left = lengthToPx( enode->getStyle()->padding[0], width, em ) + DEBUG_TREE_DRAW + measureBorder(enode,3);
        int padding_right = lengthToPx( enode->getStyle()->padding[1], width, em ) + DEBUG_TREE_DRAW + measureBorder(enode,1);
        int padding_top = lengthToPx( enode->getStyle()->padding[2], width, em ) + DEBUG_TREE_DRAW + border_top;
        int padding_bottom = lengthToPx( enode->getStyle()->padding[3], width, em ) + DEBUG_TREE_DRAW + border_bottom;
        // If there is a border at top/bottom, the AddLine(padding), which adds the room
        // for the border too, should avoid a page break between the node and its border
        int padding_top_split_flag = border_top ? RN_SPLIT_AFTER_AVOID : 0;
        int padding_bottom_split_flag = border_bottom ? RN_SPLIT_BEFORE_AVOID : 0;

        //margin_left += 50;
        //margin_right += 50;

        // todo: we should be able to allow horizontal negative margins:
        // (allow parsing negative values in lvstsheet.cpp, and
        // ensure margin_top > 0)
        if (margin_left>0)
            x += margin_left;
        y += margin_top;

        // Support style 'width:' attribute, for specific elements only: solely <HR> for now.
        // As crengine does not support many fancy display: styles, and each HTML block
        // elements is rendered as a crengine blockElement (an independant full width slice,
        // with possibly some margin/padding/indentation/border, of the document height),
        // we don't want to waste reading width with blank areas (as we are not sure
        // the content producer intended them because of crengine limitations).
        css_length_t style_width = enode->getStyle()->width;
        if (style_width.type != css_val_unspecified) {
            // printf("style_width.type: %d (%d)\n", style_width.value, style_width.type);

            bool apply_style_width = false; // Don't apply width by default
            bool style_width_pct_em_only = true; // only apply if width is in '%' or in 'em'
            int  style_width_alignment = 0; // 0: left aligned / 1: centered / 2: right aligned
            // Uncomment for testing alternate defaults:
            // apply_style_width = true;        // apply width to all elements (except table elements)
            // style_width_pct_em_only = false; // accept any kind of unit

            if (enode->getNodeId() == el_hr) { // <HR>
                apply_style_width = true;
                style_width_alignment = 1; // <hr> are auto-centered
                style_width_pct_em_only = false; // width for <hr> is safe, whether px or %
            }

            if (apply_style_width && enode->getStyle()->display >= css_d_table ) {
                // table elements are managed elsewhere: we'd rather not mess with the table
                // layout algorithm by applying styles width here (even if this algorithm
                // is not perfect, it looks like applying width here does not make it better).
                apply_style_width = false;
            }
            if (apply_style_width && style_width_pct_em_only) {
                if (style_width.type != css_val_percent && style_width.type != css_val_em) {
                    apply_style_width = false;
                }
            }
            if (apply_style_width) {
                int style_width_px = lengthToPx( style_width, width, em );
                if (style_width_px && style_width_px < width) { // ignore if greater than our given width
                    // printf("style_width: %dps at ~y=%d\n", style_width_px, y);
                    if (style_width_alignment == 1) { // centered
                        x += (width - style_width_px)/2;
                    }
                    else if (style_width_alignment == 2) { // right aligned
                        x += (width - style_width_px);
                    }
                    width = style_width_px;
                }
            }
        }
        // todo: With the help of getRenderedWidths(), we could implement
        // margin: auto with erm_block to have centered elements

        bool flgSplit = false;
        width -= margin_left + margin_right;
        int h = 0;
        LFormattedTextRef txform;
        {
            //CRLog::trace("renderBlockElement - creating render accessor");
            RenderRectAccessor fmt( enode );
            fmt.setX( x );
            fmt.setY( y );
            fmt.setWidth( width );
            fmt.setHeight( 0 );
            fmt.push();

            if (width <= 0) {
                // In case we get a negative width (no room to render and draw anything),
                // which may happen in hyper constrained layouts like heavy nested tables,
                // don't go further in the rendering code.
                // It seems erm_block and erm_final do "survive" such negative width,
                // by just keeping substracting margin and padding to this negative
                // number until, in ldomNode::renderFinalBlock(), when it's going to
                // be serious, it is (luckily?) casted to an unsigned int in:
                //   int h = f->Format((lUInt16)width, (lUInt16)page_h);
                // So, a width=-138 becomes width=65398 and the drawing is then done
                // without nearly any width constraint: some text may be drawn, some
                // parts clipped, but the user will see something is wrong.
                // So, we only do the following for tables, where the rendering code
                // is more easily messed up by negative widths. As we won't show
                // any table, and we want the user to notice something is missing,
                // we set this element rendering method to erm_killed, and
                // DrawDocument will then render a small figure...
                if (enode->getRendMethod() >= erm_table) {
                    printf("CRE WARNING: no width to draw %s\n", UnicodeToLocal(ldomXPointer(enode, 0).toString()).c_str());
                    enode->setRendMethod( erm_killed );
                    fmt.setHeight( 15 ); // not squared, so it does not look
                    fmt.setWidth( 10 );  // like a list square bullet
                    fmt.setX( fmt.getX() - 5 );
                        // We shift it half to the left, so a bit of it can be
                        // seen if some element on the right covers it with some
                        // background color.
                    return fmt.getHeight();
                }
            }

            int m = enode->getRendMethod();
            switch( m )
            {
            case erm_mixed:
                {
                    // TODO: autoboxing not supported yet
                    // (actually, erm_mixed is never used, and autoboxing
                    // IS supported and done when needed)
                }
                break;
            case erm_table:
                {
                    // ??? not sure
                    if ( isFootNoteBody )
                        context.enterFootNote( footnoteId );
                    lvRect r;
                    enode->getAbsRect(r); // this will get as r.top the absolute Y from
                                          // the relative fmt.setY( y ) we did above.
                    // Was: if (margin_top>0)
                    // but add it even if 0-margin to carry node's page-break-before ALWAYS
                    // or AVOID, so renderTable doesn't have to care about it and can
                    // use AVOID on its first AddLine()
                    context.AddLine(r.top - margin_top, r.top, pagebreakhelper(enode,width));
                    // (margin_top has already been added to make r.top, so we substracted it here)
                    // renderTable() deals itself with the table borders and paddings

                    // We allow a table to shrink width (cells to shrink to their content),
                    // unless they have a width specified.
                    // This can be tweaked with:
                    //   table {width: 100% !important} to have tables take the full available width
                    //   table {width: auto !important} to have tables shrink to their content
                    bool shrink_to_fit = false;
                    int fitted_width = -1;
                    int table_width = width;
                    int specified_width = lengthToPx( enode->getStyle()->width, width, em );
                    if (specified_width <= 0) {
                        // We get 0 when width unspecified (not set or when "width: auto"):
                        // use container width, but allow table to shrink
                        // XXX Should this only be done when explicit
                        //  (css_val_unspecified, css_generic_auto) ?
                        shrink_to_fit = true;
                    }
                    else {
                        if (specified_width > width)
                            specified_width = width;
                        table_width = specified_width;
                    }
                    int h = renderTable( context, enode, 0, y, table_width, shrink_to_fit, fitted_width );
                    // Should we really apply a specified height ?!
                    int st_h = lengthToPx( enode->getStyle()->height, em, em );
                    if ( h < st_h )
                        h = st_h;
                    fmt.setHeight( h );
                    // Update table width if it was fitted/shrunk
                    if (shrink_to_fit && fitted_width > 0)
                        table_width = fitted_width;
                    fmt.setWidth( table_width );
                    if (table_width < width) {
                        // See for margin: auto, to center or align right the table
                        int shift_x = 0;
                        css_length_t m_left = enode->getStyle()->margin[0];
                        css_length_t m_right = enode->getStyle()->margin[1];
                        bool left_auto = m_left.type == css_val_unspecified && m_left.value == css_generic_auto;
                        bool right_auto = m_right.type == css_val_unspecified && m_right.value == css_generic_auto;
                        if (left_auto) {
                            if (right_auto) { // center align
                                shift_x = (width - table_width)/2;
                            }
                            else { // right align
                                shift_x = (width - table_width);
                            }
                        }
                        if (shift_x) {
                            fmt.setX( fmt.getX() + shift_x );
                        }
                    }

                    fmt.push();
                    enode->getAbsRect(r); // this will get as r.bottom the absolute Y after fmt.setHeight( y )

                    // Was: if(margin_bottom>0)
                    //   context.AddLine(r.bottom, r.bottom + margin_bottom, RN_SPLIT_AFTER_AUTO);;
                    // but add it even if 0-margin to carry node's page-break-after ALWAYS
                    // or AVOID, so renderTable doesn't have to care about it and can
                    // use AVOID on its last AddLine()
                    int pb_flag = RN_SPLIT_BEFORE_AVOID | CssPageBreak2Flags(getPageBreakAfter(enode))<<RN_SPLIT_AFTER;
                    context.AddLine(r.bottom, r.bottom + margin_bottom, pb_flag);
                    if ( isFootNoteBody )
                        context.leaveFootNote();
                    return h + margin_top + margin_bottom; // return block height
                }
                break;
            case erm_block:
                {
                    if ( isFootNoteBody )
                        context.enterFootNote( footnoteId );


                    // recurse all sub-blocks for blocks
                    int y = padding_top;
                    int cnt = enode->getChildCount();
                    lvRect r;
                    enode->getAbsRect(r);
                    if (margin_top>0)
                        context.AddLine(r.top-margin_top, r.top, pagebreakhelper(enode,width));
                    if (padding_top>0)
                        context.AddLine(r.top, r.top+padding_top, pagebreakhelper(enode,width)|padding_top_split_flag);

                    // List item marker rendering when css_d_list_item_block
                    int list_marker_padding = 0; // set to non-zero when list-style-position = outside
                    int list_marker_height = 0;
                    if ( enode->getStyle()->display == css_d_list_item_block ) {
                        // list_item_block rendered as block (containing text and block elements)
                        // Get marker width and height
                        LFormattedTextRef txform( enode->getDocument()->createFormattedText() );
                        int list_marker_width;
                        lString16 marker = renderListItemMarker( enode, list_marker_width, txform.get(), 16, 0);
                        list_marker_height = txform->Format( (lUInt16)(width - list_marker_width), (lUInt16)enode->getDocument()->getPageHeight() );
                        if ( enode->getStyle()->list_style_position == css_lsp_outside &&
                            enode->getStyle()->text_align != css_ta_center && enode->getStyle()->text_align != css_ta_right) {
                            // When list_style_position = outside, we have to shift the whole block
                            // to the right and reduce the available width, which is done
                            // below when calling renderBlockElement() for each child
                            // Rendering hack: we treat it just as "inside" when text-align "right" or "center"
                            list_marker_padding = list_marker_width;
                        }
                        else {
                            // When list_style_position = inside, we need to let renderFinalBlock()
                            // know there is a marker to prepend when rendering the first of our
                            // children (or grand-children, depth first) that is erm_final
                            // (caveat: the marker will not be shown if any of the first children
                            // is erm_invisible)
                            ldomNode * tmpnode = enode;
                            while ( tmpnode->hasChildren() ) {
                                tmpnode = tmpnode->getChildNode( 0 );
                                if (tmpnode && tmpnode->getRendMethod() == erm_final) {
                                    // We use NodeNumberingProps to store, for this child node, a reference
                                    // to curent node, so renderFinalBlock() can call renderListItemMarker on
                                    // it and get a marker formatted according to current node style.
                                    // (This is not the regular usage of ListNumberingProps, but we can use it
                                    // without any conflict as it's never used for erm_final nodes; we get the
                                    // benefit that is is saved in the cache, and it's cleaned when re-rendering.
                                    // We store our enode index into the maxCounter slot - and list_marker_width
                                    // into the maxWidth slot, even if we won't use it.)
                                    ListNumberingPropsRef listProps = ListNumberingPropsRef( new ListNumberingProps(enode->getDataIndex(), list_marker_width) );
                                    enode->getDocument()->setNodeNumberingProps( tmpnode->getDataIndex(), listProps );
                                    break;
                                }
                            }
                        }
                    }

                    int block_height = 0;
                    for (int i=0; i<cnt; i++)
                    {
                        ldomNode * child = enode->getChildNode( i );
                        //fmt.push();
                        int h = renderBlockElement( context, child, padding_left + list_marker_padding, y,
                            width - padding_left - padding_right - list_marker_padding );
                        y += h;
                        block_height += h;
                    }
                    // ensure there's enough height to fully display the list marker
                    if (list_marker_height && list_marker_height > block_height) {
                        y += list_marker_height - block_height;
                    }

                    int st_y = lengthToPx( enode->getStyle()->height, em, em );
                    if ( y < st_y )
                        y = st_y;
                    fmt.setHeight( y + padding_bottom ); //+ margin_top + margin_bottom ); //???

                    if (margin_top==0 && padding_top==0) {
                        // If no margin or padding that would have carried the page break above, and
                        // if this page break was not consumed (it is reset to css_pb_auto when used)
                        // by any child node and is still there, add an empty line to carry it
                        int pb_flag = pagebreakhelper(enode,width);
                        if (pb_flag)
                            context.AddLine(r.top, r.top, pb_flag);
                    }

                    lvRect rect;
                    enode->getAbsRect(rect);
                    int pb_flag = CssPageBreak2Flags(getPageBreakAfter(enode))<<RN_SPLIT_AFTER;
                    if(padding_bottom>0)
                        context.AddLine(y+rect.top, y+rect.top+padding_bottom, (margin_bottom>0?RN_SPLIT_AFTER_AUTO:pb_flag)|padding_bottom_split_flag);
                    if(margin_bottom>0)
                        context.AddLine(y+rect.top+padding_bottom, y+rect.top+padding_bottom+margin_bottom, pb_flag);
                    if (margin_bottom==0 && padding_bottom==0 && pb_flag)
                        // If no margin or padding to carry pb_flag, add an empty line
                        context.AddLine(y+rect.top, y+rect.top, pb_flag);
                    if ( isFootNoteBody )
                        context.leaveFootNote();
                    return y + margin_top + margin_bottom + padding_bottom; // return block height
                }
                break;
            case erm_list_item: // obsolete rendering method (used only when gDOMVersionRequested < 20180524)
            case erm_final:
            case erm_table_cell:
                {

                    if ( enode->getStyle()->display == css_d_list_item_block ) {
                        // list_item_block rendered as final (containing only text and inline elements)
                        // Rendering hack: not when text-align "right" or "center", as we treat it just as "inside"
                        if ( enode->getStyle()->list_style_position == css_lsp_outside &&
                            enode->getStyle()->text_align != css_ta_center && enode->getStyle()->text_align != css_ta_right) {
                            // When list_style_position = outside, we have to shift the final block
                            // to the right and reduce its width
                            int list_marker_width;
                            lString16 marker = renderListItemMarker( enode, list_marker_width, NULL, 0, 0 );
                            fmt.setX( fmt.getX() + list_marker_width );
                            width -= list_marker_width;
                        }
                    }

                    if ( isFootNoteBody )
                        context.enterFootNote( footnoteId );
                    // render whole node content as single formatted object
                    fmt.setWidth( width );
                    fmt.setX( fmt.getX() );
                    fmt.setY( fmt.getY() );
                    fmt.push();
                    //if ( CRLog::isTraceEnabled() )
                    //    CRLog::trace("rendering final node: %s %d %s", LCSTR(enode->getNodeName()), enode->getDataIndex(), LCSTR(ldomXPointer(enode,0).toString()) );
                    h = enode->renderFinalBlock( txform, &fmt, width - padding_left - padding_right );
                    context.updateRenderProgress(1);
                    // if ( context.updateRenderProgress(1) )
                    //    CRLog::trace("last rendered node: %s %d", LCSTR(enode->getNodeName()), enode->getDataIndex());
    #ifdef DEBUG_DUMP_ENABLED
                    logfile << "\n";
    #endif
                    //int flags = styleToTextFmtFlags( fmt->getStyle(), 0 );
                    //renderFinalBlock( node, &txform, fmt, flags, 0, 16 );
                    //int h = txform.Format( width, context.getPageHeight() );
                    fmt.push();
                    fmt.setHeight( h + padding_top + padding_bottom );
                    flgSplit = true;
                }
                break;
            case erm_invisible:
                // don't render invisible blocks
                return 0;
            default:
                CRLog::error("Unsupported render method %d", m);
                crFatalError(); // error
                break;
            }
        }
        if ( flgSplit ) {
            lvRect rect;
            enode->getAbsRect(rect);
            // split pages
            if ( context.getPageList() != NULL ) {
                if (margin_top>0)
                        context.AddLine(rect.top-margin_top,rect.top,pagebreakhelper(enode,width));
                if (padding_top>0)
                        context.AddLine(rect.top,rect.top+padding_top,pagebreakhelper(enode,width)|padding_top_split_flag);
                css_page_break_t before, inside, after;
                //before = inside = after = css_pb_auto;
                before = getPageBreakBefore( enode );
                after = getPageBreakAfter( enode );
                inside = getPageBreakInside( enode );

//                if (before!=css_pb_auto) {
//                    CRLog::trace("page break before node %s class=%s text=%s", LCSTR(enode->getNodeName()), LCSTR(enode->getAttributeValue(L"class")), LCSTR(enode->getText(' ', 120) ));
//                }

                //getPageBreakStyle( enode, before, inside, after );
                int break_before = CssPageBreak2Flags( before );
                int break_after = CssPageBreak2Flags( after );
                int break_inside = CssPageBreak2Flags( inside );
                int count = txform->GetLineCount();
                for (int i=0; i<count; i++)
                {
                    const formatted_line_t * line = txform->GetLineInfo(i);
                    int line_flags = 0; //TODO
                    if (i==0)
                        line_flags |= break_before << RN_SPLIT_BEFORE;
                    else
                        line_flags |= break_inside << RN_SPLIT_BEFORE;
                    if (i==count-1&&(padding_bottom+margin_bottom==0))
                        line_flags |= break_after << RN_SPLIT_AFTER;
                    else
                        line_flags |= break_inside << RN_SPLIT_AFTER;
                    context.AddLine(rect.top+line->y+padding_top, rect.top+line->y+line->height+padding_top, line_flags);
                    if(padding_bottom>0&&i==count-1)
                        context.AddLine(rect.bottom-padding_bottom,rect.bottom,(margin_bottom>0?RN_SPLIT_AFTER_AUTO:CssPageBreak2Flags(getPageBreakAfter(enode))<<RN_SPLIT_AFTER)|padding_bottom_split_flag);
                    if(margin_bottom>0&&i==count-1)
                        context.AddLine(rect.bottom,rect.bottom+margin_bottom,CssPageBreak2Flags(getPageBreakAfter(enode))<<RN_SPLIT_AFTER);
                    // footnote links analysis
                    if ( !isFootNoteBody && enode->getDocument()->getDocFlag(DOC_FLAG_ENABLE_FOOTNOTES) ) { // disable footnotes for footnotes
                        for ( int w=0; w<line->word_count; w++ ) {
                            // check link start flag for every word
                            if ( line->words[w].flags & LTEXT_WORD_IS_LINK_START ) {
                                const src_text_fragment_t * src = txform->GetSrcInfo( line->words[w].src_text_index );
                                if ( src && src->object ) {
                                    ldomNode * node = (ldomNode*)src->object;
                                    ldomNode * parent = node->getParentNode();
                                    if ( parent->getNodeId()==el_a && parent->hasAttribute(LXML_NS_ANY, attr_href ) ) {
                                            // was: && parent->getAttributeValue(LXML_NS_ANY, attr_type ) == "note") {
                                            // but we want to be able to gather in-page footnotes by only
                                            // specifying a -cr-hint: to the footnote target, with no need
                                            // to set one to the link itself
                                        lString16 href = parent->getAttributeValue(LXML_NS_ANY, attr_href );
                                        if ( href.length()>0 && href.at(0)=='#' ) {
                                            href.erase(0,1);
                                            context.addLink( href );
                                        }

                                    }
                                }
                            }
                        }
                    }
                }
            } // has page list
            if ( isFootNoteBody )
                context.leaveFootNote();
            return h + margin_top + margin_bottom + padding_top + padding_bottom;
        }
    }
    else
    {
        crFatalError(111, "Attempting to render Text node");
    }
    return 0;
}

//draw border lines,support color,width,all styles, not support border-collapse
void DrawBorder(ldomNode *enode,LVDrawBuf & drawbuf,int x0,int y0,int doc_x,int doc_y,RenderRectAccessor fmt)
{
    bool hastopBorder = (enode->getStyle()->border_style_top >=css_border_solid&&enode->getStyle()->border_style_top<=css_border_outset);
    bool hasrightBorder = (enode->getStyle()->border_style_right >=css_border_solid&&enode->getStyle()->border_style_right<=css_border_outset);
    bool hasbottomBorder = (enode->getStyle()->border_style_bottom >=css_border_solid&&enode->getStyle()->border_style_bottom<=css_border_outset);
    bool hasleftBorder = (enode->getStyle()->border_style_left >=css_border_solid&&enode->getStyle()->border_style_left<=css_border_outset);

    // Check for explicit 'border-width: 0' which means no border.
    css_length_t bw;
    bw = enode->getStyle()->border_width[0];
    hastopBorder = hastopBorder & !(bw.value == 0 && bw.type > css_val_unspecified);
    bw = enode->getStyle()->border_width[1];
    hasrightBorder = hasrightBorder & !(bw.value == 0 && bw.type > css_val_unspecified);
    bw = enode->getStyle()->border_width[2];
    hasbottomBorder = hasbottomBorder & !(bw.value == 0 && bw.type > css_val_unspecified);
    bw = enode->getStyle()->border_width[3];
    hasleftBorder = hasleftBorder & !(bw.value == 0 && bw.type > css_val_unspecified);

    if (hasbottomBorder or hasleftBorder or hasrightBorder or hastopBorder) {
        lUInt32 shadecolor=0x555555;
        lUInt32 lightcolor=0xAAAAAA;
        int em = enode->getFont()->getSize();
        int width = 0; // values in % are invalid for borders, so we shouldn't get any
        int topBorderwidth = lengthToPx(enode->getStyle()->border_width[0],width,em);
        topBorderwidth = topBorderwidth!=0 ? topBorderwidth : DEFAULT_BORDER_WIDTH;
        int rightBorderwidth = lengthToPx(enode->getStyle()->border_width[1],width,em);
        rightBorderwidth = rightBorderwidth!=0 ? rightBorderwidth : DEFAULT_BORDER_WIDTH;
        int bottomBorderwidth = lengthToPx(enode->getStyle()->border_width[2],width,em);
        bottomBorderwidth = bottomBorderwidth!=0 ? bottomBorderwidth : DEFAULT_BORDER_WIDTH;
        int leftBorderwidth = lengthToPx(enode->getStyle()->border_width[3],width,em);
        leftBorderwidth = leftBorderwidth!=0 ? leftBorderwidth : DEFAULT_BORDER_WIDTH;
        int tbw=topBorderwidth,rbw=rightBorderwidth,bbw=bottomBorderwidth,lbw=leftBorderwidth;
        if (hastopBorder) {
            int dot=1,interval=0;//default style
            lUInt32 topBordercolor = enode->getStyle()->border_color[0].value;
            topBorderwidth=tbw;
            rightBorderwidth=rbw;
            bottomBorderwidth=bbw;
            leftBorderwidth=lbw;
            if (enode->getStyle()->border_color[0].type==css_val_color)
            {
                lUInt32 r,g,b;
                r=g=b=topBordercolor;
                r=r>>16;
                g=g>>8&0xff;
                b=b&0xff;
                shadecolor=(r*160/255)<<16|(g*160/255)<<8|b*160/255;
                lightcolor=topBordercolor;
            }
            int left=1,right=1;
            left=(hasleftBorder)?0:1;
            right=(hasrightBorder)?0:1;
            left=(enode->getStyle()->border_style_left==css_border_dotted||enode->getStyle()->border_style_left==css_border_dashed)?0:left;
            right=(enode->getStyle()->border_style_right==css_border_dotted||enode->getStyle()->border_style_right==css_border_dashed)?0:right;
            lvPoint leftpoint1=lvPoint(x0+doc_x,y0+doc_y),
                    leftpoint2=lvPoint(x0+doc_x,y0+doc_y+0.5*topBorderwidth),
                    leftpoint3=lvPoint(x0+doc_x,doc_y+y0+topBorderwidth),
                    rightpoint1=lvPoint(x0+doc_x+fmt.getWidth()-1,doc_y+y0),
                    rightpoint2=lvPoint(x0+doc_x+fmt.getWidth()-1,doc_y+y0+0.5*topBorderwidth),
                    rightpoint3=lvPoint(x0+doc_x+fmt.getWidth()-1,doc_y+y0+topBorderwidth);
            double leftrate=1,rightrate=1;
            if (left==0) {
                leftpoint1.x=x0+doc_x;
                leftpoint1.y=doc_y+y0;
                leftpoint2.x=x0+doc_x+0.5*leftBorderwidth;
                leftpoint2.y=doc_y+y0+0.5*topBorderwidth;
                leftpoint3.x=x0+doc_x+leftBorderwidth;
                leftpoint3.y=doc_y+y0+topBorderwidth;
            }else leftBorderwidth=0;
            leftrate=(double)leftBorderwidth/(double)topBorderwidth;
            if (right==0) {
                rightpoint1.x=x0+doc_x+fmt.getWidth()-1;
                rightpoint1.y=doc_y+y0;
                rightpoint2.x=x0+doc_x+fmt.getWidth()-1-0.5*rightBorderwidth;
                rightpoint2.y=doc_y+y0+0.5*topBorderwidth;
                rightpoint3.x=x0+doc_x+fmt.getWidth()-1-rightBorderwidth;
                rightpoint3.y=doc_y+y0+topBorderwidth;
            } else rightBorderwidth=0;
            rightrate=(double)rightBorderwidth/(double)topBorderwidth;
            switch (enode->getStyle()->border_style_top){
                case css_border_dotted:
                    dot=interval=topBorderwidth;
                    for(int i=0;i<leftpoint3.y-leftpoint1.y;i++)
                    {drawbuf.DrawLine(leftpoint1.x+i*leftrate, leftpoint1.y+i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y+i+1, topBordercolor,dot,interval,0);}
                    break;
                case css_border_dashed:
                    dot=3*topBorderwidth;
                    interval=3*topBorderwidth;
                    for(int i=0;i<leftpoint3.y-leftpoint1.y;i++)
                    {drawbuf.DrawLine(leftpoint1.x+i*leftrate, leftpoint1.y+i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y+i+1, topBordercolor,dot,interval,0);}
                    break;
                case css_border_solid:
                    for(int i=0;i<leftpoint3.y-leftpoint1.y;i++)
                    {drawbuf.DrawLine(leftpoint1.x+i*leftrate, leftpoint1.y+i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y+i+1, topBordercolor,dot,interval,0);}
                    break;
                case css_border_double:
                    for(int i=0;i<=(leftpoint2.y-leftpoint1.y)/(leftpoint2.y-leftpoint1.y>2?3:2);i++)
                    {drawbuf.FillRect(leftpoint1.x+i*leftrate, leftpoint1.y+i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y+i+1, topBordercolor);}
                    for(int i=0;i<=(leftpoint3.y-leftpoint2.y)/(leftpoint3.y-leftpoint2.y>2?3:2);i++)
                    {drawbuf.FillRect(leftpoint3.x-i*leftrate, leftpoint3.y-i, rightpoint3.x+i*rightrate+1,
                                      rightpoint3.y-i+1, topBordercolor);}
                    break;
                case css_border_groove:
                    for(int i=0;i<=leftpoint2.y-leftpoint1.y;i++)
                    {drawbuf.FillRect(leftpoint1.x+i*leftrate, leftpoint1.y+i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y+i+1, shadecolor);}
                    for(int i=0;i<leftpoint3.y-leftpoint2.y;i++)
                    {drawbuf.FillRect(leftpoint2.x+i*leftrate, leftpoint2.y+i, rightpoint2.x-i*rightrate+1,
                                      rightpoint2.y+i+1, lightcolor);}
                    break;
                case css_border_inset:
                    for(int i=0;i<leftpoint3.y-leftpoint1.y;i++)
                    {drawbuf.DrawLine(leftpoint1.x+i*leftrate, leftpoint1.y+i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y+i+1, shadecolor,dot,interval,0);}
                    break;
                case css_border_outset:
                    for(int i=0;i<leftpoint3.y-leftpoint1.y;i++)
                    {drawbuf.DrawLine(leftpoint1.x+i*leftrate, leftpoint1.y+i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y+i+1, lightcolor,dot,interval,0);}
                    break;
                case css_border_ridge:
                    for(int i=0;i<=leftpoint2.y-leftpoint1.y;i++)
                    {drawbuf.FillRect(leftpoint1.x+i*leftrate, leftpoint1.y+i, rightpoint1.x-i*rightrate+1,
                                     rightpoint1.y+i+1, lightcolor);}
                    for(int i=0;i<leftpoint3.y-leftpoint2.y;i++)
                    {drawbuf.FillRect(leftpoint2.x+i*leftrate, leftpoint2.y+i, rightpoint2.x-i*rightrate+1,
                                      rightpoint2.y+i+1, shadecolor);}
                    break;
                default:
                    break;
            }
        }
        //right
        if (hasrightBorder) {
            int dot=1,interval=0;//default style
            lUInt32 rightBordercolor = enode->getStyle()->border_color[1].value;
            topBorderwidth=tbw;
            rightBorderwidth=rbw;
            bottomBorderwidth=bbw;
            leftBorderwidth=lbw;
            if (enode->getStyle()->border_color[1].type==css_val_color)
            {
                lUInt32 r,g,b;
                r=g=b=rightBordercolor;
                r=r>>16;
                g=g>>8&0xff;
                b=b&0xff;
                shadecolor=(r*160/255)<<16|(g*160/255)<<8|b*160/255;
                lightcolor=rightBordercolor;
            }
            int up=1,down=1;
            up=(hastopBorder)?0:1;
            down=(hasbottomBorder)?0:1;
            up=(enode->getStyle()->border_style_top==css_border_dotted||enode->getStyle()->border_style_top==css_border_dashed)?1:up;
            down=(enode->getStyle()->border_style_bottom==css_border_dotted||enode->getStyle()->border_style_bottom==css_border_dashed)?1:down;
            lvPoint toppoint1=lvPoint(x0+doc_x+fmt.getWidth()-1,doc_y+y0),
                    toppoint2=lvPoint(x0+doc_x+fmt.getWidth()-1-0.5*rightBorderwidth,doc_y+y0),
                    toppoint3=lvPoint(x0+doc_x+fmt.getWidth()-1-rightBorderwidth,doc_y+y0),
                    bottompoint1=lvPoint(x0+doc_x+fmt.getWidth()-1,doc_y+y0+fmt.getHeight()-1),
                    bottompoint2=lvPoint(x0+doc_x+fmt.getWidth()-1-0.5*rightBorderwidth,doc_y+y0+fmt.getHeight()-1),
                    bottompoint3=lvPoint(x0+doc_x+fmt.getWidth()-1-rightBorderwidth,doc_y+y0+fmt.getHeight()-1);
            double toprate=1,bottomrate=1;
            if (up==0) {
                toppoint3.y=doc_y+y0+topBorderwidth;
                toppoint2.y=doc_y+y0+0.5*topBorderwidth;
            } else topBorderwidth=0;
            toprate=(double)topBorderwidth/(double)rightBorderwidth;
            if (down==0) {
                bottompoint3.y=y0+doc_y+fmt.getHeight()-1-bottomBorderwidth;
                bottompoint2.y=y0+doc_y+fmt.getHeight()-1-0.5*bottomBorderwidth;
            } else bottomBorderwidth=0;
            bottomrate=(double)bottomBorderwidth/(double)rightBorderwidth;
            switch (enode->getStyle()->border_style_right){
                case css_border_dotted:
                    dot=interval=rightBorderwidth;
                    for (int i=0;i<toppoint1.x-toppoint3.x;i++){
                        drawbuf.DrawLine(toppoint1.x-i,toppoint1.y+i*toprate,bottompoint1.x-i+1,
                                         bottompoint1.y-i*bottomrate+1, rightBordercolor,dot,interval,1);
                    }
                    break;
                case css_border_dashed:
                    dot=3*rightBorderwidth;
                    interval=3*rightBorderwidth;
                    for (int i=0;i<toppoint1.x-toppoint3.x;i++){
                        drawbuf.DrawLine(toppoint1.x-i,toppoint1.y+i*toprate,bottompoint1.x-i+1,
                                         bottompoint1.y-i*bottomrate+1, rightBordercolor,dot,interval,1);
                    }
                    break;
                case css_border_solid:
                    for (int i=0;i<toppoint1.x-toppoint3.x;i++){
                        drawbuf.DrawLine(toppoint1.x-i,toppoint1.y+i*toprate,bottompoint1.x-i+1,
                                         bottompoint1.y-i*bottomrate+1, rightBordercolor,dot,interval,1);
                    }
                    break;
                case css_border_double:
                    for (int i=0;i<=(toppoint1.x-toppoint2.x)/(toppoint1.x-toppoint2.x>2?3:2);i++){
                        drawbuf.FillRect(toppoint1.x-i,toppoint1.y+i*toprate,bottompoint1.x-i+1,
                                         bottompoint1.y-i*bottomrate+1, rightBordercolor);
                    }
                    for (int i=0;i<=(toppoint2.x-toppoint3.x)/(toppoint2.x-toppoint3.x>2?3:2);i++){
                        drawbuf.FillRect(toppoint3.x+i,toppoint3.y-i*toprate,bottompoint3.x+i+1,
                                         bottompoint3.y+i*bottomrate+1, rightBordercolor);
                    }
                    break;
                case css_border_groove:
                    for (int i=0;i<toppoint1.x-toppoint2.x;i++){
                        drawbuf.FillRect(toppoint1.x-i,toppoint1.y+i*toprate,bottompoint1.x-i+1,
                                         bottompoint1.y-i*bottomrate+1, lightcolor);
                    }
                    for (int i=0;i<=toppoint2.x-toppoint3.x;i++){
                        drawbuf.FillRect(toppoint2.x-i,toppoint2.y+i*toprate,bottompoint2.x-i+1,
                                         bottompoint2.y-i*bottomrate+1, shadecolor);
                    }
                    break;
                case css_border_inset:
                    for (int i=0;i<toppoint1.x-toppoint3.x;i++){
                        drawbuf.DrawLine(toppoint1.x-i,toppoint1.y+i*toprate,bottompoint1.x-i+1,
                                         bottompoint1.y-i*bottomrate+1, lightcolor,dot,interval,1);
                    }
                    break;
                case css_border_outset:
                    for (int i=0;i<toppoint1.x-toppoint3.x;i++){
                        drawbuf.DrawLine(toppoint1.x-i,toppoint1.y+i*toprate,bottompoint1.x-i+1,
                                         bottompoint1.y-i*bottomrate+1, shadecolor,dot,interval,1);
                    }
                    break;
                case css_border_ridge:
                    for (int i=0;i<toppoint1.x-toppoint2.x;i++){
                        drawbuf.FillRect(toppoint1.x-i,toppoint1.y+i*toprate,bottompoint1.x-i+1,
                                         bottompoint1.y-i*bottomrate+1, shadecolor);
                    }
                    for (int i=0;i<=toppoint2.x-toppoint3.x;i++){
                        drawbuf.FillRect(toppoint2.x-i,toppoint2.y+i*toprate,bottompoint2.x-i+1,
                                         bottompoint2.y-i*bottomrate+1,lightcolor);
                    }
                    break;
                default:break;
            }
        }
        //bottom
        if (hasbottomBorder) {
            int dot=1,interval=0;//default style
            lUInt32 bottomBordercolor = enode->getStyle()->border_color[2].value;
            topBorderwidth=tbw;
            rightBorderwidth=rbw;
            bottomBorderwidth=bbw;
            leftBorderwidth=lbw;
            if (enode->getStyle()->border_color[2].type==css_val_color)
            {
                lUInt32 r,g,b;
                r=g=b=bottomBordercolor;
                r=r>>16;
                g=g>>8&0xff;
                b=b&0xff;
                shadecolor=(r*160/255)<<16|(g*160/255)<<8|b*160/255;
                lightcolor=bottomBordercolor;
            }
            int left=1,right=1;
            left=(hasleftBorder)?0:1;
            right=(hasrightBorder)?0:1;
            left=(enode->getStyle()->border_style_left==css_border_dotted||enode->getStyle()->border_style_left==css_border_dashed)?1:left;
            right=(enode->getStyle()->border_style_right==css_border_dotted||enode->getStyle()->border_style_right==css_border_dashed)?1:right;
            lvPoint leftpoint1=lvPoint(x0+doc_x,y0+doc_y+fmt.getHeight()-1),
                    leftpoint2=lvPoint(x0+doc_x,y0+doc_y-0.5*bottomBorderwidth+fmt.getHeight()-1),
                    leftpoint3=lvPoint(x0+doc_x,doc_y+y0+fmt.getHeight()-1-bottomBorderwidth),
                    rightpoint1=lvPoint(x0+doc_x+fmt.getWidth()-1,doc_y+y0+fmt.getHeight()-1),
                    rightpoint2=lvPoint(x0+doc_x+fmt.getWidth()-1,doc_y+y0+fmt.getHeight()-1-0.5*bottomBorderwidth),
                    rightpoint3=lvPoint(x0+doc_x+fmt.getWidth()-1,doc_y+y0+fmt.getHeight()-1-bottomBorderwidth);
            double leftrate=1,rightrate=1;
            if (left==0) {
                leftpoint3.x=x0+doc_x+leftBorderwidth;
                leftpoint2.x=x0+doc_x+0.5*leftBorderwidth;
            }else leftBorderwidth=0;
            leftrate=(double)leftBorderwidth/(double)bottomBorderwidth;
            if (right==0) {
                rightpoint3.x=x0+doc_x+fmt.getWidth()-1-rightBorderwidth;
                rightpoint2.x=x0+doc_x+fmt.getWidth()-1-0.5*rightBorderwidth;
            } else rightBorderwidth=0;
            rightrate=(double)rightBorderwidth/(double)bottomBorderwidth;
            switch (enode->getStyle()->border_style_bottom){
                case css_border_dotted:
                    dot=interval=bottomBorderwidth;
                    for(int i=0;i<leftpoint1.y-leftpoint3.y;i++)
                    {drawbuf.DrawLine(leftpoint1.x+i*leftrate, leftpoint1.y-i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y-i+1, bottomBordercolor,dot,interval,0);}
                    break;
                case css_border_dashed:
                    dot=3*bottomBorderwidth;
                    interval=3*bottomBorderwidth;
                    for(int i=0;i<leftpoint1.y-leftpoint3.y;i++)
                    {drawbuf.DrawLine(leftpoint1.x+i*leftrate, leftpoint1.y-i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y-i+1, bottomBordercolor,dot,interval,0);}
                    break;
                case css_border_solid:
                    for(int i=0;i<leftpoint1.y-leftpoint3.y;i++)
                    {drawbuf.DrawLine(leftpoint1.x+i*leftrate, leftpoint1.y-i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y-i+1, bottomBordercolor,dot,interval,0);}
                    break;
                case css_border_double:
                    for(int i=0;i<=(leftpoint1.y-leftpoint2.y)/(leftpoint1.y-leftpoint2.y>2?3:2);i++)
                    {drawbuf.FillRect(leftpoint1.x+i*leftrate, leftpoint1.y-i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y-i+1, bottomBordercolor);}
                    for(int i=0;i<=(leftpoint2.y-leftpoint3.y)/(leftpoint2.y-leftpoint3.y>2?3:2);i++)
                    {drawbuf.FillRect(leftpoint3.x-i*leftrate, leftpoint3.y+i, rightpoint3.x+i*rightrate+1,
                                      rightpoint3.y+i+1, bottomBordercolor);}
                    break;
                case css_border_groove:
                    for(int i=0;i<=leftpoint1.y-leftpoint2.y;i++)
                    {drawbuf.FillRect(leftpoint1.x+i*leftrate, leftpoint1.y-i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y-i+1, lightcolor);}
                    for(int i=0;i<leftpoint2.y-leftpoint3.y;i++)
                    {drawbuf.FillRect(leftpoint2.x+i*leftrate, leftpoint2.y-i, rightpoint2.x-i*rightrate+1,
                                      rightpoint2.y-i+1, shadecolor);}
                    break;
                case css_border_inset:
                    for(int i=0;i<=leftpoint1.y-leftpoint3.y;i++)
                    {drawbuf.DrawLine(leftpoint1.x+i*leftrate, leftpoint1.y-i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y-i+1, lightcolor,dot,interval,0);}
                    break;
                case css_border_outset:
                    for(int i=0;i<=leftpoint1.y-leftpoint3.y;i++)
                    {drawbuf.DrawLine(leftpoint1.x+i*leftrate, leftpoint1.y-i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y-i+1, shadecolor,dot,interval,0);}
                    break;
                case css_border_ridge:
                    for(int i=0;i<=leftpoint1.y-leftpoint2.y;i++)
                    {drawbuf.FillRect(leftpoint1.x+i*leftrate, leftpoint1.y-i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y-i+1, shadecolor);}
                    for(int i=0;i<leftpoint2.y-leftpoint3.y;i++)
                    {drawbuf.FillRect(leftpoint2.x+i*leftrate, leftpoint2.y-i, rightpoint2.x-i*rightrate+1,
                                      rightpoint2.y-i+1, lightcolor);}
                    break;
                default:break;
            }
        }
        //left
        if (hasleftBorder) {
            int dot=1,interval=0;//default style
            lUInt32 leftBordercolor = enode->getStyle()->border_color[3].value;
            topBorderwidth=tbw;
            rightBorderwidth=rbw;
            bottomBorderwidth=bbw;
            leftBorderwidth=lbw;
            if (enode->getStyle()->border_color[3].type==css_val_color)
            {
                lUInt32 r,g,b;
                r=g=b=leftBordercolor;
                r=r>>16;
                g=g>>8&0xff;
                b=b&0xff;
                shadecolor=(r*160/255)<<16|(g*160/255)<<8|b*160/255;
                lightcolor=leftBordercolor;
            }
            int up=1,down=1;
            up=(hastopBorder)?0:1;
            down=(hasbottomBorder)?0:1;
            up=(enode->getStyle()->border_style_top==css_border_dotted||enode->getStyle()->border_style_top==css_border_dashed)?1:up;
            down=(enode->getStyle()->border_style_bottom==css_border_dotted||enode->getStyle()->border_style_bottom==css_border_dashed)?1:down;
            lvPoint toppoint1=lvPoint(x0+doc_x,doc_y+y0),
                    toppoint2=lvPoint(x0+doc_x+0.5*leftBorderwidth,doc_y+y0),
                    toppoint3=lvPoint(x0+doc_x+leftBorderwidth,doc_y+y0),
                    bottompoint1=lvPoint(x0+doc_x,doc_y+y0+fmt.getHeight()-1),
                    bottompoint2=lvPoint(x0+doc_x+0.5*leftBorderwidth,doc_y+y0+fmt.getHeight()-1),
                    bottompoint3=lvPoint(x0+doc_x+leftBorderwidth,doc_y+y0+fmt.getHeight()-1);
            double toprate=1,bottomrate=1;
            if (up==0) {
                toppoint3.y=doc_y+y0+topBorderwidth;
                toppoint2.y=doc_y+y0+0.5*topBorderwidth;
            } else topBorderwidth=0;
            toprate=(double)topBorderwidth/(double)leftBorderwidth;
            if (down==0) {
                bottompoint3.y=y0+doc_y+fmt.getHeight()-1-bottomBorderwidth;
                bottompoint2.y=y0+doc_y+fmt.getHeight()-1-0.5*bottomBorderwidth;
            } else bottomBorderwidth=0;
            bottomrate=(double)bottomBorderwidth/(double)leftBorderwidth;
            switch (enode->getStyle()->border_style_left){
                case css_border_dotted:
                    dot=interval=leftBorderwidth;
                    for (int i=0;i<toppoint3.x-toppoint1.x;i++){
                        drawbuf.DrawLine(toppoint1.x+i,toppoint1.y+i*toprate,bottompoint1.x+i+1,
                                         bottompoint1.y-i*bottomrate+1,leftBordercolor,dot,interval,1);
                    }
                    break;
                case css_border_dashed:
                    dot=3*leftBorderwidth;
                    interval=3*leftBorderwidth;
                    for (int i=0;i<toppoint3.x-toppoint1.x;i++){
                        drawbuf.DrawLine(toppoint1.x+i,toppoint1.y+i*toprate,bottompoint1.x+i+1,
                                         bottompoint1.y-i*bottomrate+1,leftBordercolor,dot,interval,1);
                    }
                    break;
                case css_border_solid:
                    for (int i=0;i<toppoint3.x-toppoint1.x;i++){
                        drawbuf.DrawLine(toppoint1.x+i,toppoint1.y+i*toprate,bottompoint1.x+i+1,
                                         bottompoint1.y-i*bottomrate+1,leftBordercolor,dot,interval,1);
                    }
                    break;
                case css_border_double:
                    for (int i=0;i<=(toppoint2.x-toppoint1.x)/(toppoint2.x-toppoint1.x>2?3:2);i++){
                        drawbuf.FillRect(toppoint1.x+i,toppoint1.y+i*toprate,bottompoint1.x+i+1,
                                         bottompoint1.y-i*bottomrate+1,leftBordercolor);
                    }
                    for (int i=0;i<=(toppoint3.x-toppoint2.x)/(toppoint3.x-toppoint2.x>2?3:2);i++){
                        drawbuf.FillRect(toppoint3.x-i,toppoint3.y-i*toprate,bottompoint3.x-i+1,
                                         bottompoint3.y+i*bottomrate+1,leftBordercolor);
                    }
                    break;
                case css_border_groove:
                    for (int i=0;i<=toppoint2.x-toppoint1.x;i++){
                        drawbuf.FillRect(toppoint1.x+i,toppoint1.y+i*toprate,bottompoint1.x+i+1,
                                         bottompoint1.y-i*bottomrate+1,shadecolor);
                    }
                    for (int i=0;i<toppoint3.x-toppoint2.x;i++){
                        drawbuf.FillRect(toppoint2.x+i,toppoint2.y+i*toprate,bottompoint2.x+i+1,
                                         bottompoint2.y-i*bottomrate+1,lightcolor);
                    }
                    break;
                case css_border_inset:
                    for (int i=0;i<toppoint3.x-toppoint1.x;i++){
                        drawbuf.DrawLine(toppoint1.x+i,toppoint1.y+i*toprate,bottompoint1.x+i+1,
                                         bottompoint1.y-i*bottomrate+1,shadecolor,dot,interval,1);
                    }
                    break;
                case css_border_outset:
                    for (int i=0;i<toppoint3.x-toppoint1.x;i++){
                        drawbuf.DrawLine(toppoint1.x+i,toppoint1.y+i*toprate,bottompoint1.x+i+1,
                                         bottompoint1.y-i*bottomrate+1,lightcolor,dot,interval,1);
                    }
                    break;
                case css_border_ridge:
                    for (int i=0;i<=toppoint2.x-toppoint1.x;i++){
                        drawbuf.FillRect(toppoint1.x+i,toppoint1.y+i*toprate,bottompoint1.x+i+1,
                                         bottompoint1.y-i*bottomrate+1,lightcolor);
                    }
                    for (int i=0;i<toppoint3.x-toppoint2.x;i++){
                        drawbuf.FillRect(toppoint2.x+i,toppoint2.y+i*toprate,bottompoint2.x+i+1,
                                         bottompoint2.y-i*bottomrate+1,shadecolor);
                    }
                    break;
                default:break;
            }
        }
    }
}
void DrawBackgroundImage(ldomNode *enode,LVDrawBuf & drawbuf,int x0,int y0,int doc_x,int doc_y,RenderRectAccessor fmt)
{
    css_style_ref_t style=enode->getStyle();
    if (!style->background_image.empty()) {
        lString16 filename = lString16(style->background_image.c_str());
        {//url("path") to path
            if (lString16(filename).lowercase().startsWith("url")) filename = filename.substr(3);
            filename.trim();
            if (filename.startsWith("(")) filename = filename.substr(1);
            if (filename.endsWith(")")) filename = filename.substr(0, filename.length() - 1);
            filename.trim();
            if (filename.startsWith("\"")) filename = filename.substr(1);
            if (filename.endsWith("\"")) filename = filename.substr(0, filename.length() - 1);
            filename.trim();
            // This is probably wrong: we should have resolved the path at
            // stylesheet parsing time (but the current code does not).
            // Here, all files relative path information is no more accessible.
            if (filename.startsWith("../")) filename = filename.substr(3);
        }
        LVImageSourceRef img = enode->getParentNode()->getDocument()->getObjectImageSource(filename);
        if (!img.isNull()) {
            int repeat_times_x = 0, repeat_times_y = 0;
            int position_x = 0, position_y = 0, direction_x = 1, direction_y = 1, center_x = 0, center_y = 0;
            switch (style->background_repeat) {
                case css_background_no_repeat:
                    repeat_times_x = 0;
                    repeat_times_y = 0;
                    break;
                case css_background_repeat_x:
                    repeat_times_x = fmt.getWidth() / img->GetWidth() ? fmt.getWidth() / img->GetWidth() :
                                     fmt.getWidth() / img->GetWidth() + 1;
                    repeat_times_y = 0;
                    break;
                case css_background_repeat_y:
                    repeat_times_x = 0;
                    repeat_times_y = fmt.getHeight() / img->GetHeight() ? fmt.getHeight() / img->GetHeight() :
                                     fmt.getHeight() / img->GetHeight() + 1;
                    break;
                case css_background_repeat:
                    repeat_times_x = fmt.getWidth() / img->GetWidth() ? fmt.getWidth() / img->GetWidth() :
                                     fmt.getWidth() / img->GetWidth() + 1;
                    repeat_times_y = fmt.getHeight() / img->GetHeight() ? fmt.getHeight() / img->GetHeight() :
                                     fmt.getHeight() / img->GetHeight() + 1;
                    break;
                default:
                    repeat_times_x = fmt.getWidth() / img->GetWidth() ? fmt.getWidth() / img->GetWidth() :
                                     fmt.getWidth() / img->GetWidth() + 1;
                    repeat_times_y = fmt.getHeight() / img->GetHeight() ? fmt.getHeight() / img->GetHeight() :
                                     fmt.getHeight() / img->GetHeight() + 1;
                    break;
            }
            switch (style->background_position) {
                case css_background_center_bottom:
                    position_x = fmt.getWidth() / 2 - img->GetWidth() / 2;
                    position_y = fmt.getHeight() - img->GetHeight();
                    center_x = 1;
                    direction_y = -1;
                    break;
                case css_background_center_center:
                    position_x = fmt.getWidth() / 2 - img->GetWidth() / 2;;
                    position_y = fmt.getHeight() / 2 - img->GetHeight() / 2;
                    center_x = 1;
                    center_y = 1;
                    break;
                case css_background_center_top:
                    position_x = fmt.getWidth() / 2 - img->GetWidth() / 2;;
                    position_y = 0;
                    center_x = 1;
                    break;
                case css_background_left_bottom:
                    position_x = x0;
                    position_y = fmt.getHeight() - img->GetHeight();
                    direction_y = -1;
                    break;
                case css_background_left_center:
                    position_x = 0;
                    position_y = fmt.getHeight() / 2 - img->GetHeight() / 2;
                    center_y = 1;
                    break;
                case css_background_left_top:
                    position_x = 0;
                    position_y = 0;
                    break;
                case css_background_right_bottom:
                    position_x = fmt.getWidth() - img->GetWidth();
                    position_y = fmt.getHeight() - img->GetHeight();
                    direction_x = -1;
                    direction_y = -1;
                    break;
                case css_background_right_center:
                    position_x = fmt.getWidth() - img->GetWidth();
                    position_y = fmt.getHeight() / 2 - img->GetHeight() / 2;
                    direction_x = -1;
                    center_y = 1;
                    break;
                case css_background_right_top:
                    position_x = fmt.getWidth() - img->GetWidth();
                    position_y = 0;
                    direction_x = -1;
                    break;
                default:
                    position_x = 0;
                    position_y = 0;
            }
            LVDrawBuf *tmp = NULL;
            tmp = new LVColorDrawBuf(fmt.getWidth(), fmt.getHeight(), 32);
            for (int i = 0; i < repeat_times_x + 1; i++) {
                for (int j = 0; j < repeat_times_y + 1; j++) {
                    tmp->Draw(img, position_x + i * img->GetWidth() * direction_x,
                              position_y + j * img->GetHeight() * direction_y,
                              img->GetWidth(), img->GetHeight());
                    if (center_x == 1)
                        tmp->Draw(img, position_x - i * img->GetWidth() * direction_x,
                                  position_y + j * img->GetHeight() * direction_y,
                                  img->GetWidth(), img->GetHeight());
                    if (center_y == 1)
                        tmp->Draw(img, position_x + i * img->GetWidth() * direction_x,
                                  position_y - j * img->GetHeight() * direction_y,
                                  img->GetWidth(), img->GetHeight());
                }
            }
            tmp->DrawOnTop(&drawbuf, x0 + doc_x, y0 + doc_y);
            delete tmp;
        }
    }
}

//=======================================================================
// Draw document
//=======================================================================
// Recursively called as children nodes are walked.
// x0, y0 are coordinates of top left point to draw to in buffer
// dx, dy are width and height to draw to in buffer
// doc_x, doc_y are offset coordinates in document:
//   doc_x is initially 0, and doc_y is set to a negative
//   value (- page.start) from the y of the top of the page
//   (in the whole book height) we want to start showing
void DrawDocument( LVDrawBuf & drawbuf, ldomNode * enode, int x0, int y0, int dx, int dy, int doc_x, int doc_y, int page_height, ldomMarkedRangeList * marks,
                   ldomMarkedRangeList *bookmarks)
{
    if ( enode->isElement() )
    {
        RenderRectAccessor fmt( enode );
        doc_x += fmt.getX();
        doc_y += fmt.getY();
        lvdom_element_render_method rm = enode->getRendMethod();
        // A few things differ when done for TR, THEAD, TBODY and TFOOT
        bool isTableRowLike = rm == erm_table_row || rm == erm_table_row_group ||
                              rm == erm_table_header_group || rm == erm_table_footer_group;
        int em = enode->getFont()->getSize();
        int width = fmt.getWidth();
        int height = fmt.getHeight();
        bool draw_padding_bg = true; //( enode->getRendMethod()==erm_final );
        int padding_left = !draw_padding_bg ? 0 : lengthToPx( enode->getStyle()->padding[0], width, em ) + DEBUG_TREE_DRAW+measureBorder(enode,3);
        int padding_right = !draw_padding_bg ? 0 : lengthToPx( enode->getStyle()->padding[1], width, em ) + DEBUG_TREE_DRAW+measureBorder(enode,1);
        int padding_top = !draw_padding_bg ? 0 : lengthToPx( enode->getStyle()->padding[2], width, em ) + DEBUG_TREE_DRAW+measureBorder(enode,0);
        //int padding_bottom = !draw_padding_bg ? 0 : lengthToPx( enode->getStyle()->padding[3], width, em ) + DEBUG_TREE_DRAW;
        if ( (doc_y + height <= 0 || doc_y > 0 + dy) && !isTableRowLike ) {
            // TR may have cells with rowspan>1, and even though this TR
            // is out of range, it must draw a rowspan>1 cell, so it it
            // not empty when a next TR (not out of range) is drawn.
            return; // out of range
        }
        css_length_t bg = enode->getStyle()->background_color;
        lUInt32 oldColor = 0;
        // Don't draw background color for TR and THEAD/TFOOT/TBODY as it could
        // override bgcolor of cells with rowspan > 1. We spread, in setNodeStyle(),
        // the TR bgcolor to its TDs that must have it, as it should be done (the
        // border spacing between cells does not have the bg color of the TR: only
        // cells have it).
        if ( bg.type==css_val_color && !isTableRowLike ) {
            oldColor = drawbuf.GetBackgroundColor();
            drawbuf.SetBackgroundColor( bg.value );
            drawbuf.FillRect( x0 + doc_x, y0 + doc_y, x0 + doc_x+fmt.getWidth(), y0+doc_y+fmt.getHeight(), bg.value );
        }
        lString16 nodename=enode->getNodeName();    // CSS specific: <body> background does not obey margin rules
        if (nodename.lowercase().compare("body")==0&&enode->getStyle()->background_image!=lString8(""))
        {
            int width=fmt.getWidth();
            fmt.setWidth(drawbuf.GetWidth());
            DrawBackgroundImage(enode,drawbuf,0,y0,0,doc_y,fmt);
            fmt.setWidth(width);
        }
        else  DrawBackgroundImage(enode,drawbuf,x0,y0,doc_x,doc_y,fmt);
#if (DEBUG_TREE_DRAW!=0)
        lUInt32 color;
        static lUInt32 const colors2[] = { 0x555555, 0xAAAAAA, 0x555555, 0xAAAAAA, 0x555555, 0xAAAAAA, 0x555555, 0xAAAAAA };
        static lUInt32 const colors4[] = { 0x555555, 0xFF4040, 0x40FF40, 0x4040FF, 0xAAAAAA, 0xFF8000, 0xC0C0C0, 0x808080 };
        if (drawbuf.GetBitsPerPixel()>=16)
            color = colors4[enode->getNodeLevel() & 7];
        else
            color = colors2[enode->getNodeLevel() & 7];
#endif
        switch( enode->getRendMethod() )
        {
        case erm_table:
        case erm_table_row:
        case erm_table_row_group:
        case erm_table_header_group:
        case erm_table_footer_group:
        case erm_block:
            {
                // recursive draw all sub-blocks for blocks
                int cnt = enode->getChildCount();

                // List item marker drawing when css_d_list_item_block and list-style-position = outside
                // and list_item_block rendered as block (containing text and block elements)
                // Rendering hack: not when text-align "right" or "center", as we treat it just as "inside"
                // (if list-style-position = inside, drawing is managed by renderFinalBlock())
                if ( enode->getStyle()->display == css_d_list_item_block &&
                        enode->getStyle()->list_style_position == css_lsp_outside &&
                            enode->getStyle()->text_align != css_ta_center && enode->getStyle()->text_align != css_ta_right) {
                    // We already adjusted all children blocks' left-padding and width in renderBlockElement(),
                    // we just need to draw the marker in the space we made
                    LFormattedTextRef txform( enode->getDocument()->createFormattedText() );
                    int list_marker_width;
                    lString16 marker = renderListItemMarker( enode, list_marker_width, txform.get(), 16, 0);
                    lUInt32 h = txform->Format( (lUInt16)width, (lUInt16)page_height );
                    lvRect clip;
                    drawbuf.GetClipRect( &clip );
                    if (doc_y + h <= clip.bottom) { // draw only if marker fully fits on page
                        DrawBackgroundImage(enode,drawbuf,x0,y0,doc_x,doc_y,fmt);
                        txform->Draw( &drawbuf, doc_x+x0 + padding_left, doc_y+y0 + padding_top, NULL, NULL );
                    }
                }

                for (int i=0; i<cnt; i++)
                {
                    ldomNode * child = enode->getChildNode( i );
                    DrawDocument( drawbuf, child, x0, y0, dx, dy, doc_x, doc_y, page_height, marks, bookmarks ); //+fmt->getX() +fmt->getY()
                }
#if (DEBUG_TREE_DRAW!=0)
                drawbuf.FillRect( doc_x+x0, doc_y+y0, doc_x+x0+fmt.getWidth(), doc_y+y0+1, color );
                drawbuf.FillRect( doc_x+x0, doc_y+y0, doc_x+x0+1, doc_y+y0+fmt.getHeight(), color );
                drawbuf.FillRect( doc_x+x0+fmt.getWidth()-1, doc_y+y0, doc_x+x0+fmt.getWidth(), doc_y+y0+fmt.getHeight(), color );
                drawbuf.FillRect( doc_x+x0, doc_y+y0+fmt.getHeight()-1, doc_x+x0+fmt.getWidth(), doc_y+y0+fmt.getHeight(), color );
#endif
                /*lUInt32 tableBorderColor = 0xAAAAAA;
                lUInt32 tableBorderColorDark = 0x555555;
                bool needBorder = enode->getRendMethod()==erm_table || enode->getStyle()->display==css_d_table_cell;
                if ( needBorder ) {
                   drawbuf.FillRect( doc_x+x0, doc_y+y0,
                                      doc_x+x0+fmt.getWidth(), doc_y+y0+1, tableBorderColor );
                    drawbuf.FillRect( doc_x+x0, doc_y+y0,
                                      doc_x+x0+1, doc_y+y0+fmt.getHeight(), tableBorderColor );
                    drawbuf.FillRect( doc_x+x0+fmt.getWidth()-1, doc_y+y0,
                                      doc_x+x0+fmt.getWidth(),   doc_y+y0+fmt.getHeight(), tableBorderColorDark );
                    drawbuf.FillRect( doc_x+x0, doc_y+y0+fmt.getHeight()-1,
                                      doc_x+x0+fmt.getWidth(), doc_y+y0+fmt.getHeight(), tableBorderColorDark );
                }*/
                // Don't draw border for TR TBODY... as their borders are never directly
                // rendered by Firefox (they are rendered only when border-collapse, when
                // they did collapse to the cell, and made out the cell border)
                if ( !isTableRowLike )
                    DrawBorder(enode,drawbuf,x0,y0,doc_x,doc_y,fmt);
            	}
            break;
        case erm_list_item: // obsolete rendering method (used only when gDOMVersionRequested < 20180524)
        case erm_final:
        case erm_table_caption:
            {

                // List item marker drawing when css_d_list_item_block and list-style-position = outside
                // and list_item_block rendered as final (containing only text and inline elements)
                // Rendering hack: not when text-align "right" or "center", as we treat it just as "inside"
                // (if list-style-position = inside, drawing is managed by renderFinalBlock())
                if ( enode->getStyle()->display == css_d_list_item_block &&
                        enode->getStyle()->list_style_position == css_lsp_outside &&
                            enode->getStyle()->text_align != css_ta_center && enode->getStyle()->text_align != css_ta_right) {
                    // We already adjusted our block X and width in renderBlockElement(),
                    // we just need to draw the marker in the space we made on the left of
                    // this node.
                    LFormattedTextRef txform( enode->getDocument()->createFormattedText() );
                    int list_marker_width;
                    lString16 marker = renderListItemMarker( enode, list_marker_width, txform.get(), 16, 0);
                    lUInt32 h = txform->Format( (lUInt16)width, (lUInt16)page_height );
                    lvRect clip;
                    drawbuf.GetClipRect( &clip );
                    if (doc_y + h <= clip.bottom) { // draw only if marker fully fits on page
                        DrawBackgroundImage(enode,drawbuf,x0,y0,doc_x,doc_y,fmt);
                        txform->Draw( &drawbuf, doc_x+x0 + padding_left - list_marker_width, doc_y+y0 + padding_top, NULL, NULL );
                    }
                }

                // draw whole node content as single formatted object
                LFormattedTextRef txform;
                enode->renderFinalBlock( txform, &fmt, fmt.getWidth() - padding_left - padding_right );
                fmt.push();
                {
                    lvRect rc;
                    enode->getAbsRect( rc );
                    ldomMarkedRangeList *nbookmarks = NULL;
                    if ( bookmarks && bookmarks->length()) {
                        nbookmarks = new ldomMarkedRangeList( bookmarks, rc );
                    }
                    if ( marks && marks->length() ) {
                        //rc.left -= doc_x;
                        //rc.right -= doc_x;
                        //rc.top -= doc_y;
                        //rc.bottom -= doc_y;
                        ldomMarkedRangeList nmarks( marks, rc );
                        DrawBackgroundImage(enode,drawbuf,x0,y0,doc_x,doc_y,fmt);
                        txform->Draw( &drawbuf, doc_x+x0 + padding_left, doc_y+y0 + padding_top, &nmarks, nbookmarks );
                    } else {
                        DrawBackgroundImage(enode,drawbuf,x0,y0,doc_x,doc_y,fmt);
                        txform->Draw( &drawbuf, doc_x+x0 + padding_left, doc_y+y0 + padding_top, marks, nbookmarks );
                    }
                    if (nbookmarks)
                        delete nbookmarks;
                }
#if (DEBUG_TREE_DRAW!=0)
                drawbuf.FillRect( doc_x+x0, doc_y+y0, doc_x+x0+fmt.getWidth(), doc_y+y0+1, color );
                drawbuf.FillRect( doc_x+x0, doc_y+y0, doc_x+x0+1, doc_y+y0+fmt.getHeight(), color );
                drawbuf.FillRect( doc_x+x0+fmt.getWidth()-1, doc_y+y0, doc_x+x0+fmt.getWidth(), doc_y+y0+fmt.getHeight(), color );
                drawbuf.FillRect( doc_x+x0, doc_y+y0+fmt.getHeight()-1, doc_x+x0+fmt.getWidth(), doc_y+y0+fmt.getHeight(), color );
#endif
                DrawBorder(enode, drawbuf, x0, y0, doc_x, doc_y, fmt);
                /*lUInt32 tableBorderColor = 0x555555;
                lUInt32 tableBorderColorDark = 0xAAAAAA;
                bool needBorder = enode->getStyle()->display==css_d_table_cell;
                if ( needBorder ) {
                    drawbuf.FillRect( doc_x+x0, doc_y+y0,
                                      doc_x+x0+fmt.getWidth(), doc_y+y0+1, tableBorderColor );
                    drawbuf.FillRect( doc_x+x0, doc_y+y0,
                                      doc_x+x0+1, doc_y+y0+fmt.getHeight(), tableBorderColor );
                    drawbuf.FillRect( doc_x+x0+fmt.getWidth()-1, doc_y+y0,
                                      doc_x+x0+fmt.getWidth(),   doc_y+y0+fmt.getHeight(), tableBorderColorDark );
                    drawbuf.FillRect( doc_x+x0, doc_y+y0+fmt.getHeight()-1,
                                      doc_x+x0+fmt.getWidth(), doc_y+y0+fmt.getHeight(), tableBorderColorDark );
                    //drawbuf.FillRect( doc_x+x0, doc_y+y0, doc_x+x0+fmt->getWidth(), doc_y+y0+1, tableBorderColorDark );
                    //drawbuf.FillRect( doc_x+x0, doc_y+y0, doc_x+x0+1, doc_y+y0+fmt->getHeight(), tableBorderColorDark );
                    //drawbuf.FillRect( doc_x+x0+fmt->getWidth()-1, doc_y+y0, doc_x+x0+fmt->getWidth(), doc_y+y0+fmt->getHeight(), tableBorderColor );
                    //drawbuf.FillRect( doc_x+x0, doc_y+y0+fmt->getHeight()-1, doc_x+x0+fmt->getWidth(), doc_y+y0+fmt->getHeight(), tableBorderColor );
                }*/
            }
            break;
        case erm_invisible:
            // don't draw invisible blocks
            break;
        case erm_killed:
            //drawbuf.FillRect( x0 + doc_x, y0 + doc_y, x0 + doc_x+fmt.getWidth(), y0+doc_y+fmt.getHeight(), 0xFF0000 );
            // Draw something that does not look like a bullet
            // This should render in red something like: [\]
            drawbuf.RoundRect( x0 + doc_x, y0 + doc_y, x0 + doc_x+fmt.getWidth(), y0+doc_y+fmt.getHeight(),
                fmt.getWidth()/4, fmt.getWidth()/4, 0xFF0000, 0x9 );
            drawbuf.FillRect( x0 + doc_x + fmt.getWidth()/6, y0 + doc_y + fmt.getHeight()*3/8,
                x0 + doc_x + fmt.getWidth()*5/6, y0+doc_y+fmt.getHeight()*5/8, 0xFF0000 );
            break;
        default:
            break;
            //crFatalError(); // error
        }
        if ( bg.type==css_val_color ) {
            drawbuf.SetBackgroundColor( oldColor );
        }
    }
}

/* Not used anywhere: not updated for absolute length units and *256
void convertLengthToPx( css_length_t & val, int base_px, int base_em )
{
    switch( val.type )
    {
    case css_val_inherited:
        val = css_length_t ( base_px );
        break;
    case css_val_px:
        // nothing to do
        break;
    case css_val_ex: // not implemented: treat as em
    case css_val_em: // value = em*256
        val = css_length_t ( (base_em * val.value) >> 8 );
        break;
    case css_val_percent:
        val = css_length_t ( (base_px * val.value) / 100 );
        break;
    case css_val_unspecified:
    case css_val_in: // 2.54 cm
    case css_val_cm:
    case css_val_mm:
    case css_val_pt: // 1/72 in
    case css_val_pc: // 12 pt
    case css_val_color:
        // not supported: use inherited value
        val = css_length_t ( val.value );
        break;
    }
}
*/

inline void spreadParent( css_length_t & val, css_length_t & parent_val, bool unspecified_is_inherited=true )
{
    if ( val.type == css_val_inherited || (val.type == css_val_unspecified && unspecified_is_inherited) )
        val = parent_val;
}

void setNodeStyle( ldomNode * enode, css_style_ref_t parent_style, LVFontRef parent_font )
{
    CR_UNUSED(parent_font);
    //lvdomElementFormatRec * fmt = node->getRenderData();
    css_style_ref_t style( new css_style_rec_t );
    css_style_rec_t * pstyle = style.get();

    if (gDOMVersionRequested < 20180524) {
        // The display property initial value has been changed from css_d_inherit
        // to css_d_inline (as per spec, and so that an unknown element does not
        // become block when contained in a P, and inline when contained in a SPAN)
        pstyle->display = css_d_inherit;
    }

//    if ( parent_style.isNull() ) {
//        CRLog::error("parent style is null!!!");
//    }

    // init default style attribute values
    const css_elem_def_props_t * type_ptr = enode->getElementTypePtr();
    if (type_ptr)
    {
        pstyle->display = type_ptr->display;
        pstyle->white_space = type_ptr->white_space;

        // Account for backward incompatible changes in fb2def.h
        if (gDOMVersionRequested < 20180528) { // revert what was changed 20180528
            if (enode->getNodeId() == el_form) {
                pstyle->display = css_d_none; // otherwise shown as block, as it may have textual content
            }
            if (enode->getNodeId() == el_code) {
                pstyle->white_space = css_ws_pre; // otherwise white-space: normal, as browsers do
            }
            if (enode->getNodeId() >= el_address && enode->getNodeId() <= el_xmp) { // newly added block elements
                pstyle->display = css_d_inline; // previously unknown and shown as inline
                if (gDOMVersionRequested < 20180524) {
                    pstyle->display = css_d_inherit; // previously unknown and display: inherit
                }
            }
            if (gDOMVersionRequested < 20180524) { // revert what was fixed 20180524
                if (enode->getNodeId() == el_cite) {
                    pstyle->display = css_d_block; // otherwise correctly set to css_d_inline
                }
                if (enode->getNodeId() == el_li) {
                    pstyle->display = css_d_list_item; // otherwise correctly set to css_d_list_item_block
                }
                if (enode->getNodeId() == el_style) {
                    pstyle->display = css_d_inline; // otherwise correctly set to css_d_none (hidden)
                }
            }
        }
    }

    // Firefox resets text-align: to 'left' for table (eg: <center><table>
    // doesn't have its cells' content centered, not even justified if body
    // has "text-align: justify"), while crengine would make them centered.
    // So, we dont wan't table to starts with css_ta_inherit. We could use
    // css_ta_left (as Firefox), but it's best in our context to use the
    // value set to the (or current DocFragment's) BODY node, which starts
    // with css_ta_left but may be set to css_ta_justify by our epub.css.
    if (enode->getNodeId() == el_table) {
        // To do as Firefox:
        // pstyle->text_align = css_ta_left;
        // But we'd rather use the BODY value:
        ldomNode * body = enode->getParentNode();
        while ( body != NULL && body->getNodeId()!=el_body )
            body = body->getParentNode();
        if ( body ) {
            pstyle->text_align = body->getStyle()->text_align;
        }
    }

    if (enode->getNodeNsId() == ns_epub) {
        if (enode->getNodeId() == el_case) { // <epub:case required-namespace="...">
            // As we don't support any specific namespace (like MathML, SVG...), just
            // hide <epub:case> content - it must be followed by a <epub:default>
            // section with usually regular content like some image.
            ldomNode * parent = enode->getParentNode();
            if (parent && parent->getNodeNsId() == ns_epub && parent->getNodeId() == el_switch) {
                // (We can't here check parent's other children for the presence of one
                // el_default, as we can be called while XML is being parsed and the DOM
                // built and siblings not yet there, so just trust there is an el_default.)
                pstyle->display = css_d_none;
            }
        }
    }

    // not used (could be used for 'rem', but we have it in gRootFontSize)
    // int baseFontSize = enode->getDocument()->getDefaultFont()->getSize();

    //////////////////////////////////////////////////////
    // apply style sheet
    //////////////////////////////////////////////////////
    enode->getDocument()->applyStyle( enode, pstyle );

    if ( enode->getDocument()->getDocFlag(DOC_FLAG_ENABLE_INTERNAL_STYLES) && enode->hasAttribute( LXML_NS_ANY, attr_style ) ) {
        lString16 nodeStyle = enode->getAttributeValue( LXML_NS_ANY, attr_style );
        if ( !nodeStyle.empty() ) {
            nodeStyle = cs16("{") + nodeStyle + "}";
            LVCssDeclaration decl;
            lString8 s8 = UnicodeToUtf8(nodeStyle);
            const char * s = s8.c_str();
            if ( decl.parse( s ) ) {
                decl.apply( pstyle );
            }
        }
    }

    // update inherited style attributes
/*
  #define UPDATE_STYLE_FIELD(fld,inherit_value) \
  if (pstyle->fld == inherit_value) \
      pstyle->fld = parent_style->fld
*/
    #define UPDATE_STYLE_FIELD(fld,inherit_value) \
        if (pstyle->fld == inherit_value) \
            pstyle->fld = parent_style->fld
    #define UPDATE_LEN_FIELD(fld) \
        switch( pstyle->fld.type ) \
        { \
        case css_val_inherited: \
            pstyle->fld = parent_style->fld; \
            break; \
        /* relative values to parent style */\
        case css_val_percent: \
            pstyle->fld.type = parent_style->fld.type; \
            pstyle->fld.value = parent_style->fld.value * pstyle->fld.value / 100 / 256; \
            break; \
        case css_val_em: \
            pstyle->fld.type = parent_style->fld.type; \
            pstyle->fld.value = parent_style->font_size.value * pstyle->fld.value / 256; \
            break; \
        case css_val_ex: \
            pstyle->fld.type = parent_style->fld.type; \
            pstyle->fld.value = parent_style->font_size.value * pstyle->fld.value / 512; \
            break; \
        default: \
            /* absolute values, no need to relate to parent style */\
            break; \
        }

    //if ( (pstyle->display == css_d_inline) && (pstyle->text_align==css_ta_inherit))
    //{
        //if (parent_style->text_align==css_ta_inherit)
        //parent_style->text_align = css_ta_center;
    //}

    if (gDOMVersionRequested < 20180524) { // display should not be inherited
        UPDATE_STYLE_FIELD( display, css_d_inherit );
    }
    UPDATE_STYLE_FIELD( white_space, css_ws_inherit );
    UPDATE_STYLE_FIELD( text_align, css_ta_inherit );
    UPDATE_STYLE_FIELD( text_decoration, css_td_inherit );
    UPDATE_STYLE_FIELD( text_transform, css_tt_inherit );
    UPDATE_STYLE_FIELD( hyphenate, css_hyph_inherit );
    UPDATE_STYLE_FIELD( list_style_type, css_lst_inherit );
    UPDATE_STYLE_FIELD( list_style_position, css_lsp_inherit );
    UPDATE_STYLE_FIELD( page_break_before, css_pb_inherit ); // These are not inherited per CSS specs,
    UPDATE_STYLE_FIELD( page_break_after, css_pb_inherit );  // investigate why they are here (might be
    UPDATE_STYLE_FIELD( page_break_inside, css_pb_inherit ); // for processing reasons)
    // vertical_align is not inherited per CSS specs: we fixed its propagation
    // to children with the use of 'valign_dy'
    // UPDATE_STYLE_FIELD( vertical_align, css_va_inherit );
    UPDATE_STYLE_FIELD( font_style, css_fs_inherit );
    UPDATE_STYLE_FIELD( font_weight, css_fw_inherit );
    if ( pstyle->font_family == css_ff_inherit ) {
        //UPDATE_STYLE_FIELD( font_name, "" );
        pstyle->font_name = parent_font.get()->getTypeFace();
    }
    UPDATE_STYLE_FIELD( font_family, css_ff_inherit );
    //UPDATE_LEN_FIELD( font_size ); // this is done below
    //UPDATE_LEN_FIELD( text_indent );
    spreadParent( pstyle->text_indent, parent_style->text_indent );
    switch( pstyle->font_weight )
    {
    case css_fw_inherit:
        pstyle->font_weight = parent_style->font_weight;
        break;
    case css_fw_normal:
        pstyle->font_weight = css_fw_400;
        break;
    case css_fw_bold:
        pstyle->font_weight = css_fw_600;
        break;
    case css_fw_bolder:
        pstyle->font_weight = parent_style->font_weight;
        if (pstyle->font_weight < css_fw_800)
        {
            pstyle->font_weight = (css_font_weight_t)((int)pstyle->font_weight + 2);
        }
        break;
    case css_fw_lighter:
        pstyle->font_weight = parent_style->font_weight;
        if (pstyle->font_weight > css_fw_200)
        {
            pstyle->font_weight = (css_font_weight_t)((int)pstyle->font_weight - 2);
        }
        break;
    case css_fw_100:
    case css_fw_200:
    case css_fw_300:
    case css_fw_400:
    case css_fw_500:
    case css_fw_600:
    case css_fw_700:
    case css_fw_800:
    case css_fw_900:
        break;
    }
    switch( pstyle->font_size.type )
    { // ordered here as most likely to be met
    case css_val_inherited:
        pstyle->font_size = parent_style->font_size;
        break;
    case css_val_screen_px:
    case css_val_px:
        // absolute size, nothing to do
        break;
    case css_val_em: // value = em*256 ; 256 = 1em = x1
        pstyle->font_size.type = parent_style->font_size.type;
        pstyle->font_size.value = parent_style->font_size.value * pstyle->font_size.value / 256;
        break;
    case css_val_ex: // value = ex*256 ; 512 = 2ex = 1em = x1
        pstyle->font_size.type = parent_style->font_size.type;
        pstyle->font_size.value = parent_style->font_size.value * pstyle->font_size.value / 512;
        break;
    case css_val_percent: // value = percent number ; 100 = 100% => x1
        pstyle->font_size.type = parent_style->font_size.type;
        pstyle->font_size.value = parent_style->font_size.value * pstyle->font_size.value / 100 / 256;
        break;
    case css_val_rem:
        // not relative to parent, nothing to do
        break;
    case css_val_in:
    case css_val_cm:
    case css_val_mm:
    case css_val_pt:
    case css_val_pc:
        // absolute size, nothing to do
        break;
    case css_val_unspecified:
    case css_val_color:
        // not supported: use inherited value
        pstyle->font_size = parent_style->font_size;
        // printf("CRE WARNING: font-size css_val_unspecified or color, fallback to inherited\n");
        break;
    }
    // line_height
    spreadParent( pstyle->letter_spacing, parent_style->letter_spacing );
    spreadParent( pstyle->line_height, parent_style->line_height, false ); // css_val_unspecified is a valid unit
    spreadParent( pstyle->color, parent_style->color );

    // background_color
    // Should not be inherited: elements start with unspecified.
    // The code will fill the rect of a parent element, and will
    // simply draw its children over the already filled rect.
    bool spread_background_color = false;
    // But for some elements, it should be propagated to children,
    // and we explicitely don't have the parent fill the rect with
    // its background-color:
    // - a TD or TH with unspecified background-color does inherit
    //   it from its parent TR. We don't draw bgcolor for TR.
    if ( pstyle->display == css_d_table_cell )
        spread_background_color = true;
    // - a TR with unspecified background-color may inherit it from
    //   its THEAD/TBODY/TFOOT parent (but not from its parent TABLE).
    //   It may then again be propagated to the TD by the previous rule.
    //   We don't draw bgcolor for THEAD/TBODY/TFOOT.
    if ( pstyle->display == css_d_table_row &&
            ( parent_style->display == css_d_table_row_group ||
              parent_style->display == css_d_table_header_group ||
              parent_style->display == css_d_table_footer_group ) )
        spread_background_color = true;
    if ( spread_background_color )
        spreadParent( pstyle->background_color, parent_style->background_color, true );

    // set calculated style
    //enode->getDocument()->cacheStyle( style );
    enode->setStyle( style );
    if ( enode->getStyle().isNull() ) {
        CRLog::error("NULL style set!!!");
        enode->setStyle( style );
    }

    // set font
    enode->initNodeFont();
}

// Uncomment for debugging getRenderedWidths():
// #define DEBUG_GETRENDEREDWIDTHS

// Estimate width of node when rendered:
//   maxWidth: width if it would be rendered on an infinite width area
//   minWidth: width with a wrap on all spaces (no hyphenation), so width taken by the longest word
void getRenderedWidths(ldomNode * node, int &maxWidth, int &minWidth, bool ignorePadding) {
    // Setup passed-by-reference parameters for recursive calls
    int curMaxWidth = 0;    // reset on <BR/> or on new block nodes
    int curWordWidth = 0;   // may not be reset to correctly estimate multi-nodes single-word ("I<sup>er</sup>")
    bool collapseNextSpace = true; // collapse leading spaces
    int lastSpaceWidth = 0; // trailing spaces width to remove
    // These do not need to be passed by reference, as they are only valid for inner nodes/calls
    int indent = 0;         // text-indent: used on first text, and set again on <BR/>
    // Start measurements and recursions:
    getRenderedWidths(node, maxWidth, minWidth, ignorePadding,
        curMaxWidth, curWordWidth, collapseNextSpace, lastSpaceWidth, indent);
}

void getRenderedWidths(ldomNode * node, int &maxWidth, int &minWidth, bool ignorePadding,
    int &curMaxWidth, int &curWordWidth, bool &collapseNextSpace, int &lastSpaceWidth, int indent)
{
    // This does mostly what renderBlockElement, renderFinalBlock and lvtextfm.cpp
    // do, but only with widths and horizontal margin/border/padding and indent
    // (with no width constraint, so no line splitting and hyphenation - and we
    // don't care about vertical spacing and alignment).
    // Limitations: no support for css_d_run_in (hardly ever used, we don't care)

    if ( node->isElement() ) {
        int m = node->getRendMethod();
        if (m == erm_invisible)
            return;

        // Get image size early
        bool is_img = false;
        lInt16 img_width = 0;
        if ( node->getNodeId()==el_img ) {
            is_img = true;
            // (as in lvtextfm.cpp LFormattedText::AddSourceObject)
            #define DUMMY_IMAGE_SIZE 16
            LVImageSourceRef img = node->getObjectImageSource();
            if ( img.isNull() )
                img = LVCreateDummyImageSource( node, DUMMY_IMAGE_SIZE, DUMMY_IMAGE_SIZE );
            lInt16 width = (lUInt16)img->GetWidth();
            lInt16 height = (lUInt16)img->GetHeight();
            // Scale image native size according to gRenderDPI
            width = scaleForRenderDPI(width);
            height = scaleForRenderDPI(height);
            // Adjust if size defined by CSS
            css_style_ref_t style = node->getStyle();
            int w = 0, h = 0;
            int em = node->getFont()->getSize();
            w = lengthToPx(style->width, 100, em);
            h = lengthToPx(style->height, 100, em);
            if (style->width.type==css_val_percent) w = -w;
            if (style->height.type==css_val_percent) h = w*height/width;
            if ( w*h==0 ) {
                if ( w==0 ) {
                    if ( h==0 ) { // use image native size
                        h = height;
                        w = width;
                    } else { // use style height, keep aspect ratio
                        w = width*h/height;
                    }
                } else if ( h==0 ) { // use style width, keep aspect ratio
                    h = w*height/width;
                    if (h == 0) h = height;
                }
            }
            if (w > 0)
                img_width = w;
            else { // 0 or styles were in %
                // This is a bit tricky...
                // When w < 0, the style width was in %, which means % of the
                // container width.
                // So it does not influence the width we're trying to guess, and will
                // adjust to the final width. So, we could let it to be 0.
                // But, if this image is the single element of this block, we would
                // end up with a minWidth of 0, with no room for the image, and the
                // image would be scaled as a % of 0, so to 0.
                // So, consider we want the image to be shown as a % of its original
                // size: so, our width should be the original image width.
                img_width = width;
                // With that, it looks like we behave exactly as Firefox, whether
                // the image is single in a cell, or surrounded, in this cell
                // and/or sibling cells, by small or long text!
                // We ensure a minimal size of 1em (so it shows as least the size
                // of a letter).
                int em = node->getFont()->getSize();
                if (img_width < em)
                    img_width = em;
            }
        }

        if (m == erm_inline) {
            if ( is_img ) {
                // Get done with previous word
                if (curWordWidth > minWidth)
                    minWidth = curWordWidth;
                curWordWidth = 0;
                collapseNextSpace = false;
                lastSpaceWidth = 0;
                if (img_width > 0) { // inline img with a fixed width
                    maxWidth += img_width;
                    if (img_width > minWidth)
                        minWidth = img_width;
                }
                return;
            }
            if ( node->getNodeId()==el_br ) {
                #ifdef DEBUG_GETRENDEREDWIDTHS
                    printf("GRW: BR\n");
                #endif
                // Get done with current word
                if (lastSpaceWidth)
                    curMaxWidth -= lastSpaceWidth;
                if (curMaxWidth > maxWidth)
                    maxWidth = curMaxWidth;
                if (curWordWidth > minWidth)
                    minWidth = curWordWidth;
                // Next word on new line has text-indent in its width
                curMaxWidth = indent;
                curWordWidth = indent;
                collapseNextSpace = true; // skip leading spaces
                lastSpaceWidth = 0;
                return;
            }
            // Contains only other inline or text nodes:
            // add to our passed by ref *Width
            for (int i = 0; i < node->getChildCount(); i++) {
                ldomNode * child = node->getChildNode(i);
                // Nothing more to do with inline elements: they just carry some
                // styles that will be grabbed by children text nodes
                getRenderedWidths(child, maxWidth, minWidth, false,
                    curMaxWidth, curWordWidth, collapseNextSpace, lastSpaceWidth, indent);
            }
            return;
        }

        // For erm_block and erm_final:
        // We may have padding/margin/border, that we can simply add to
        // the widths that will be computed by our children.
        // Also, if these have % as their CSS unit, we need a width to
        // apply the % to, so we can only do that when we get a maxWidth
        // and minWidth from children.

        // For list-items, we need to compute the bullet width to use either
        // as indent, or as left padding
        int list_marker_width = 0;
        bool list_marker_width_as_padding = false;
        if ( node->getStyle()->display == css_d_list_item_block ) {
            LFormattedTextRef txform( node->getDocument()->createFormattedText() );
            lString16 marker = renderListItemMarker( node, list_marker_width, txform.get(), 16, 0);
            #ifdef DEBUG_GETRENDEREDWIDTHS
                printf("GRW: list_marker_width: %d\n", list_marker_width);
            #endif
            if ( node->getStyle()->list_style_position == css_lsp_outside &&
                    node->getStyle()->text_align != css_ta_center &&
                    node->getStyle()->text_align != css_ta_right) {
                // (same hack as in rendering code: we render 'outside' just
                // like 'inside' when center or right aligned)
                list_marker_width_as_padding = true;
            }
        }

        // We use temporary parameters, that we'll add our padding/margin/border to
        int _maxWidth = 0;
        int _minWidth = 0;

        if (m == erm_final) { // Block node that contains only inline or text nodes:
            if ( is_img ) { // img with display: block always become erm_final (never erm_block)
                if (img_width > 0) { // block img with a fixed width
                    _maxWidth = img_width;
                    _minWidth = img_width;
                }
            }
            else {
                // We don't have any width yet to use for text-indent in % units,
                // but this is very rare - use em as we must use something
                int em = node->getFont()->getSize();
                indent = lengthToPx(node->getStyle()->text_indent, em, em);
                // curMaxWidth and curWordWidth are not used in our parents (which
                // are block-like elements), we can just reset them.
                // First word will have text-indent has its width
                curMaxWidth = indent;
                curWordWidth = indent;
                if (list_marker_width > 0 && !list_marker_width_as_padding) {
                    // with additional list marker if list-style-position: inside
                    curMaxWidth += list_marker_width;
                    curWordWidth += list_marker_width;
                }
                collapseNextSpace = true; // skip leading spaces
                lastSpaceWidth = 0;
                // Process children, which are either erm_inline or text nodes
                for (int i = 0; i < node->getChildCount(); i++) {
                    ldomNode * child = node->getChildNode(i);
                    getRenderedWidths(child, _maxWidth, _minWidth, false,
                        curMaxWidth, curWordWidth, collapseNextSpace, lastSpaceWidth, indent);
                    // A <BR/> can happen deep among our children, so we deal with that when erm_inline above
                }
                if (lastSpaceWidth)
                    curMaxWidth -= lastSpaceWidth;
                // Add current word as we're leaving a block node, so it can't be followed by some other text
                if (curMaxWidth > _maxWidth)
                    _maxWidth = curMaxWidth;
                if (curWordWidth > _minWidth)
                    _minWidth = curWordWidth;
            }
        }
        // if (m == erm_block) // Block node that contains other stacked block or final nodes
        // Not dealing with other rendering methods may return widths of 0 (and
        // on a nested inner table with erm_table, would ask to render this inner
        // table in a width of 0).
        // So, we treat all other erm_* as erm_block (which will obviously be
        // wrong for erm_table* with more than 1 column, but it should give a
        // positive enough width to draw something).
        else {
            // Process children, which are all block-like nodes:
            // our *Width are the max of our children *Width
            for (int i = 0; i < node->getChildCount(); i++) {
                // New temporary parameters
                int _maxw = 0;
                int _minw = 0;
                ldomNode * child = node->getChildNode(i);
                getRenderedWidths(child, _maxw, _minw, false,
                    curMaxWidth, curWordWidth, collapseNextSpace, lastSpaceWidth, indent);
                if (_maxw > _maxWidth)
                    _maxWidth = _maxw;
                if (_minw > _minWidth)
                    _minWidth = _minw;
            }
        }

        // For both erm_block or erm_final, adds padding/margin/border
        // to _maxWidth and _minWidth (see comment above)
        if (!ignorePadding) {
            int padLeft = 0; // these will include padding, border and margin
            int padRight = 0;
            if (list_marker_width > 0 && list_marker_width_as_padding) {
                // with additional left padding for marker if list-style-position: outside
                padLeft += list_marker_width;
            }
            // For % values, we need to reverse-apply them as a whole.
            // We can'd do that individually for each, so we aggregate
            // the % values.
            // (And as we can't ceil() each individually, we'll add 1px
            // below for each one to counterbalance rounding errors.)
            int padPct = 0; // cumulative percent
            int padPctNb = 0; // nb of styles in % (to add 1px)
            int em = node->getFont()->getSize();
            css_style_ref_t style = node->getStyle();
            // margin
            if (style->margin[0].type == css_val_percent) {
                padPct += style->margin[0].value;
                padPctNb += 1;
            }
            else
                padLeft += lengthToPx( style->margin[0], 0, em );
            if (style->margin[1].type == css_val_percent) {
                padPct += style->margin[1].value;
                padPctNb += 1;
            }
            else
                padRight += lengthToPx( style->margin[1], 0, em );
            // padding
            if (style->padding[0].type == css_val_percent) {
                padPct += style->padding[0].value;
                padPctNb += 1;
            }
            else
                padLeft += lengthToPx( style->padding[0], 0, em );
            if (style->padding[1].type == css_val_percent) {
                padPct += style->padding[1].value;
                padPctNb += 1;
            }
            else
                padRight += lengthToPx( style->padding[1], 0, em );
            // border (which does not accept units in %)
            padLeft += measureBorder(node,3);
            padRight += measureBorder(node,1);
            // Add the non-pct values to make our base to invert-apply padPct
            _minWidth += padLeft + padRight;
            _maxWidth += padLeft + padRight;
            // For length in %, the % (P, padPct) should be from the outer width (L),
            // but we have only the inner width (w). We have w and P, we want L-w (m).
            //   m = L*P  and  w = L - m
            //   w = L - L*P  =  L*(1-P)
            //   L = w/(1-P)
            //   m = L - w  =  w/(1-P) - w  =  w*(1 - (1-P))/(1-P) = w*P/(1-P)
            // css_val_percent value are *256 (100% = 100*256)
            // We ignore a total of 100% (no space for content, and division by zero here
            int minPadPctLen = 0;
            int maxPadPctLen = 0;
            if (padPct != 100*256) {
                // add padPctNb: 1px for each value in %
                minPadPctLen = _minWidth * padPct / (100*256-padPct) + padPctNb;
                maxPadPctLen = _maxWidth * padPct / (100*256-padPct) + padPctNb;
                _minWidth += minPadPctLen;
                _maxWidth += maxPadPctLen;
            }
            #ifdef DEBUG_GETRENDEREDWIDTHS
                printf("GRW blk:  pad min+ %d %d +%d%%=%d\n", padLeft, padRight, padPct, minPadPctLen);
                printf("GRW blk:  pad max+ %d %d +%d%%=%d\n", padLeft, padRight, padPct, maxPadPctLen);
            #endif
        }
        // We must have been provided with maxWidth=0 and minWidth=0 (temporary
        // parameters set by outer calls), but do these regular checks anyway.
        if (_maxWidth > maxWidth)
            maxWidth = _maxWidth;
        if (_minWidth > minWidth)
            minWidth = _minWidth;
    }
    else if (node->isText() ) {
        lString16 nodeText = node->getText();
        int start = 0;
        int len = nodeText.length();
        if ( len == 0 )
            return;
        ldomNode *parent = node->getParentNode();
        // letter-spacing
        LVFont * font = parent->getFont().get();
        int em = font->getSize();
        lInt8 letter_spacing;
        letter_spacing = lengthToPx(parent->getStyle()->letter_spacing, em, em);
        // text-transform
        switch (parent->getStyle()->text_transform) {
            case css_tt_uppercase:
                nodeText.uppercase();
                break;
            case css_tt_lowercase:
                nodeText.lowercase();
                break;
            case css_tt_capitalize:
                nodeText.capitalize();
                break;
            case css_tt_full_width:
                // nodeText.fullWidthChars(); // disabled for now (may change CJK rendering)
                break;
            case css_tt_none:
            case css_tt_inherit:
                break;
        }
        // measure text
        const lChar16 * txt = nodeText.c_str();
        #ifdef DEBUG_GETRENDEREDWIDTHS
            printf("GRW text: |%s|\n", UnicodeToLocal(nodeText).c_str());
            printf("GRW text:  (dumb text size=%d)\n", node->getParentNode()->getFont()->getTextWidth(txt, len));
        #endif
        #define MAX_TEXT_CHUNK_SIZE 4096
        static lUInt16 widths[MAX_TEXT_CHUNK_SIZE+1];
        static lUInt8 flags[MAX_TEXT_CHUNK_SIZE+1];
        while (true) {
            LVFont * font = node->getParentNode()->getFont().get();
            // Italic glyphs may need a little added width.
            // Get it if needed as done in lvtextfm.cpp getAdditionalCharWidth()
            // and getAdditionalCharWidthOnLeft()
            bool is_italic_font = font->getItalic();
            LVFont::glyph_info_t glyph; // slot for measuring one italic glyph
            int chars_measured = font->measureText(
                    txt + start,
                    len,
                    widths, flags,
                    0x7FFF, // very wide width
                    '?',    // replacement char
                    letter_spacing,
                    false); // no hyphenation
            for (int i=0; i<chars_measured; i++) {
                int w = widths[i] - (i>0 ? widths[i-1] : 0);
                lChar16 c = *(txt + start + i);
                bool is_cjk = (c >= UNICODE_CJK_IDEOGRAPHS_BEGIN && c <= UNICODE_CJK_IDEOGRAPHS_END
                            && ( c<=UNICODE_CJK_PUNCTUATION_HALF_AND_FULL_WIDTH_BEGIN
                                || c>=UNICODE_CJK_PUNCTUATION_HALF_AND_FULL_WIDTH_END) );
                            // Do we need to do something about CJK punctuation?
                    // Having CJK columns min_width the width of a single CJK char
                    // may, on some pages, make some table cells have a single
                    // CJK char per line, which can look uglier than when not
                    // dealing with them specifically (see with: bool is_cjk=false).
                    // But Firefox does that too, may be a bit less radically than
                    // us, so our table algorithm may need some tweaking...
                if (flags[i] & LCHAR_ALLOW_WRAP_AFTER) { // A space
                    if (collapseNextSpace) // ignore this space
                        continue;
                    collapseNextSpace = true; // ignore next spaces, even if in another node
                    lastSpaceWidth = w;
                    curMaxWidth += w; // add this space to non-wrap width
                    if (curWordWidth > 0) { // there was a word before this space
                        if ( is_italic_font && (start+i > 0) ) {
                            // adjust if last word's last char was italic
                            lChar16 prevc = *(txt + start + i - 1);
                            if (font->getGlyphInfo(prevc, &glyph, '?') ) {
                                int delta = glyph.originX - glyph.width + glyph.blackBoxX;
                                curWordWidth += delta > 0 ? delta : 0;
                            }
                        }
                    }
                    if (curWordWidth > minWidth) // done with previous word
                        minWidth = curWordWidth; // longest word found
                    curWordWidth = 0;
                }
                else if (is_cjk) { // CJK chars are themselves a word
                    collapseNextSpace = false; // next space should not be ignored
                    lastSpaceWidth = 0; // no width to take off if we stop with this char
                    curMaxWidth += w;
                    if (curWordWidth > 0) { // there was a word or CJK char before this CJK char
                        if ( is_italic_font && (start+i > 0) ) {
                            // adjust if last word's last char or previous CJK char was italic
                            lChar16 prevc = *(txt + start + i - 1);
                            if (font->getGlyphInfo(prevc, &glyph, '?') ) {
                                int delta = glyph.originX - glyph.width + glyph.blackBoxX;
                                curWordWidth += delta > 0 ? delta : 0;
                            }
                        }
                    }
                    if (curWordWidth > minWidth) // done with previous word
                        minWidth = curWordWidth; // longest word found
                    curWordWidth = w;
                    if ( is_italic_font ) {
                        if (font->getGlyphInfo(c, &glyph, '?') ) {
                            // adjust for negative leading offset if current CJK char is italic
                            int delta = -glyph.originX;
                            delta = delta > 0 ? delta : 0;
                            curWordWidth += delta;
                            if (start + i == 0) // at start of text only? (not sure)
                                curMaxWidth += delta; // also add it to max width
                        }
                    }
                    // CJK may or may not need this italic treatment, not sure
                }
                else { // A char part of a word
                    collapseNextSpace = false; // next space should not be ignored
                    lastSpaceWidth = 0; // no width to take off if we stop with this char
                    if (curWordWidth == 0) { // first char of a word
                        if ( is_italic_font ) {
                            // adjust for negative leading offset on first char of a word
                            if (font->getGlyphInfo(c, &glyph, '?') ) {
                                int delta = -glyph.originX;
                                delta = delta > 0 ? delta : 0;
                                curWordWidth += delta;
                                if (start + i == 0) // at start of text only? (not sure)
                                    curMaxWidth += delta; // also add it to max width
                            }
                        }
                    }
                    curMaxWidth += w;
                    curWordWidth += w;
                    // Try to guess long urls or hostnames, and split a word on
                    // each / or dot not followed by a space, so they are not
                    // a super long word and don't over extend minWidth.
                    if ( (c == '/' || c == '.') &&
                            i < start+len-1 && *(txt + start + i + 1) != ' ') {
                        if (curWordWidth > minWidth)
                            minWidth = curWordWidth;
                        curWordWidth = 0;
                    }
                }
            }
            if ( chars_measured == len ) { // done with this text node
                if (curWordWidth > 0) { // we end with a word
                    if ( is_italic_font && (start+len > 0) ) {
                        // adjust if word last char was italic
                        lChar16 prevc = *(txt + start + len - 1);
                        if (font->getGlyphInfo(prevc, &glyph, '?') ) {
                            int delta = glyph.originX - glyph.width + glyph.blackBoxX;
                            delta = delta > 0 ? delta : 0;
                            curWordWidth += delta;
                            curMaxWidth += delta; // also add it to max width
                        }
                    }
                }
                break;
            }
            // continue measuring
            len -= chars_measured;
            start += chars_measured;
        }
    }
    #ifdef DEBUG_GETRENDEREDWIDTHS
        printf("GRW current: max=%d word=%d (max=%d, min=%d)\n", curMaxWidth, curWordWidth, maxWidth, minWidth);
    #endif
}
