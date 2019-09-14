/*******************************************************

   CoolReader Engine C-compatible API

   lvtextfm.cpp:  Text formatter

   (c) Vadim Lopatin, 2000-2011
   This source code is distributed under the terms of
   GNU General Public License
   See LICENSE file for details

*******************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <lvtextfm.h>
#include "../include/crsetup.h"
#include "../include/lvfnt.h"
#include "../include/lvtextfm.h"
#include "../include/lvdrawbuf.h"
#include "../include/fb2def.h"

#ifdef __cplusplus
#include "../include/lvimg.h"
#include "../include/lvtinydom.h"
#include "../include/lvrend.h"
#endif

#if (USE_FRIBIDI==1)
#include <fribidi/fribidi.h>
#endif

#define MIN_SPACE_CONDENSING_PERCENT 50


// to debug formatter

#if defined(_DEBUG) && 0
#define TRACE_LINE_SPLITTING 1
#else
#define TRACE_LINE_SPLITTING 0
#endif

#if TRACE_LINE_SPLITTING==1
#ifdef _MSC_VER
#define TR(...) CRLog::trace(__VA_ARGS__)
#else
#define TR(x...) CRLog::trace(x)
#endif
#else
#ifdef _MSC_VER
#define TR(...)
#else
#define TR(x...)
#endif
#endif

#define FRM_ALLOC_SIZE 16
#define FLT_ALLOC_SIZE 4

formatted_line_t * lvtextAllocFormattedLine( )
{
    formatted_line_t * pline = (formatted_line_t *)calloc(1, sizeof(*pline));
    return pline;
}

formatted_line_t * lvtextAllocFormattedLineCopy( formatted_word_t * words, int word_count )
{
    formatted_line_t * pline = (formatted_line_t *)calloc(1, sizeof(*pline));
    lUInt32 size = (word_count + FRM_ALLOC_SIZE-1) / FRM_ALLOC_SIZE * FRM_ALLOC_SIZE;
    pline->words = (formatted_word_t*)malloc( sizeof(formatted_word_t)*(size) );
    memcpy( pline->words, words, word_count * sizeof(formatted_word_t) );
    return pline;
}

void lvtextFreeFormattedLine( formatted_line_t * pline )
{
    if (pline->words)
        free( pline->words );
    free(pline);
}

formatted_word_t * lvtextAddFormattedWord( formatted_line_t * pline )
{
    int size = (pline->word_count + FRM_ALLOC_SIZE-1) / FRM_ALLOC_SIZE * FRM_ALLOC_SIZE;
    if ( pline->word_count >= size)
    {
        size += FRM_ALLOC_SIZE;
        pline->words = cr_realloc( pline->words, size );
    }
    return &pline->words[ pline->word_count++ ];
}

formatted_line_t * lvtextAddFormattedLine( formatted_text_fragment_t * pbuffer )
{
    int size = (pbuffer->frmlinecount + FRM_ALLOC_SIZE-1) / FRM_ALLOC_SIZE * FRM_ALLOC_SIZE;
    if (pbuffer->frmlinecount >= size)
    {
        size += FRM_ALLOC_SIZE;
        pbuffer->frmlines = cr_realloc( pbuffer->frmlines, size );
    }
    return (pbuffer->frmlines[ pbuffer->frmlinecount++ ] = lvtextAllocFormattedLine());
}

formatted_line_t * lvtextAddFormattedLineCopy( formatted_text_fragment_t * pbuffer, formatted_word_t * words, int words_count )
{
    int size = (pbuffer->frmlinecount + FRM_ALLOC_SIZE-1) / FRM_ALLOC_SIZE * FRM_ALLOC_SIZE;
    if ( pbuffer->frmlinecount >= size)
    {
        size += FRM_ALLOC_SIZE;
        pbuffer->frmlines = cr_realloc( pbuffer->frmlines, size );
    }
    return (pbuffer->frmlines[ pbuffer->frmlinecount++ ] = lvtextAllocFormattedLineCopy(words, words_count));
}

embedded_float_t * lvtextAllocEmbeddedFloat( )
{
    embedded_float_t * flt = (embedded_float_t *)calloc(1, sizeof(*flt));
    return flt;
}

embedded_float_t * lvtextAddEmbeddedFloat( formatted_text_fragment_t * pbuffer )
{
    int size = (pbuffer->floatcount + FLT_ALLOC_SIZE-1) / FLT_ALLOC_SIZE * FLT_ALLOC_SIZE;
    if (pbuffer->floatcount >= size)
    {
        size += FLT_ALLOC_SIZE;
        pbuffer->floats = cr_realloc( pbuffer->floats, size );
    }
    return (pbuffer->floats[ pbuffer->floatcount++ ] = lvtextAllocEmbeddedFloat());
}


formatted_text_fragment_t * lvtextAllocFormatter( lUInt16 width )
{
    formatted_text_fragment_t * pbuffer = (formatted_text_fragment_t*)calloc(1, sizeof(*pbuffer));
    pbuffer->width = width;
    pbuffer->strut_height = 0;
    pbuffer->strut_baseline = 0;
    int defMode = MAX_IMAGE_SCALE_MUL > 1 ? (ARBITRARY_IMAGE_SCALE_ENABLED==1 ? 2 : 1) : 0;
    int defMult = MAX_IMAGE_SCALE_MUL;
    // Notes from thornyreader:
    // mode: 0=disabled, 1=integer scaling factors, 2=free scaling
    // scale: 0=auto based on font size, 1=no zoom, 2=scale up to *2, 3=scale up to *3
    pbuffer->img_zoom_in_mode_block = defMode; /**< can zoom in block images: 0=disabled, 1=integer scale, 2=free scale */
    pbuffer->img_zoom_in_scale_block = defMult; /**< max scale for block images zoom in: 1, 2, 3 */
    pbuffer->img_zoom_in_mode_inline = defMode; /**< can zoom in inline images: 0=disabled, 1=integer scale, 2=free scale */
    pbuffer->img_zoom_in_scale_inline = defMult; /**< max scale for inline images zoom in: 1, 2, 3 */
    pbuffer->img_zoom_out_mode_block = defMode; /**< can zoom out block images: 0=disabled, 1=integer scale, 2=free scale */
    pbuffer->img_zoom_out_scale_block = defMult; /**< max scale for block images zoom out: 1, 2, 3 */
    pbuffer->img_zoom_out_mode_inline = defMode; /**< can zoom out inline images: 0=disabled, 1=integer scale, 2=free scale */
    pbuffer->img_zoom_out_scale_inline = defMult; /**< max scale for inline images zoom out: 1, 2, 3 */
    pbuffer->min_space_condensing_percent = MIN_SPACE_CONDENSING_PERCENT; // 50%

    return pbuffer;
}

void lvtextFreeFormatter( formatted_text_fragment_t * pbuffer )
{
    if (pbuffer->srctext)
    {
        for (int i=0; i<pbuffer->srctextlen; i++)
        {
            if (pbuffer->srctext[i].flags & LTEXT_FLAG_OWNTEXT)
                free( (void*)pbuffer->srctext[i].t.text );
        }
        free( pbuffer->srctext );
    }
    if (pbuffer->frmlines)
    {
        for (int i=0; i<pbuffer->frmlinecount; i++)
        {
            lvtextFreeFormattedLine( pbuffer->frmlines[i] );
        }
        free( pbuffer->frmlines );
    }
    if (pbuffer->floats)
    {
        for (int i=0; i<pbuffer->floatcount; i++)
        {
            if (pbuffer->floats[i]->links) {
                delete pbuffer->floats[i]->links;
            }
            free(pbuffer->floats[i]);
        }
        free( pbuffer->floats );
    }
    free(pbuffer);
}


void lvtextAddSourceLine( formatted_text_fragment_t * pbuffer,
   lvfont_handle   font,     /* handle of font to draw string */
   const lChar16 * text,     /* pointer to unicode text string */
   lUInt32         len,      /* number of chars in text, 0 for auto(strlen) */
   lUInt32         color,    /* color */
   lUInt32         bgcolor,  /* bgcolor */
   lUInt32         flags,    /* flags */
   lInt16          interval, /* line height in screen pixels */
   lInt16          valign_dy, /* drift y from baseline */
   lUInt16         margin,   /* first line margin */
   void *          object,    /* pointer to custom object */
   lUInt16         offset,
   lInt16          letter_spacing
                         )
{
    int srctextsize = (pbuffer->srctextlen + FRM_ALLOC_SIZE-1) / FRM_ALLOC_SIZE * FRM_ALLOC_SIZE;
    if ( pbuffer->srctextlen >= srctextsize)
    {
        srctextsize += FRM_ALLOC_SIZE;
        pbuffer->srctext = cr_realloc( pbuffer->srctext, srctextsize );
    }
    src_text_fragment_t * pline = &pbuffer->srctext[ pbuffer->srctextlen++ ];
    pline->t.font = font;
//    if (font) {
//        // DEBUG: check for crash
//        CRLog::trace("c font = %08x  txt = %08x", (lUInt32)font, (lUInt32)text);
//        ((LVFont*)font)->getVisualAligmentWidth();
//    }
//    if (font == NULL && ((flags & LTEXT_WORD_IS_OBJECT) == 0)) {
//        CRLog::fatal("No font specified for text");
//    }
    if (!len) for (len=0; text[len]; len++) ;
    if (flags & LTEXT_FLAG_OWNTEXT)
    {
        /* make own copy of text */
        pline->t.text = (lChar16*)malloc( len * sizeof(lChar16) );
        memcpy((void*)pline->t.text, text, len * sizeof(lChar16));
    }
    else
    {
        pline->t.text = text;
    }
    pline->index = (lUInt16)(pbuffer->srctextlen-1);
    pline->object = object;
    pline->t.len = (lUInt16)len;
    pline->margin = margin;
    pline->flags = flags;
    pline->interval = interval;
    pline->valign_dy = valign_dy;
    pline->t.offset = offset;
    pline->color = color;
    pline->bgcolor = bgcolor;
    pline->letter_spacing = letter_spacing;
}

void lvtextAddSourceObject(
   formatted_text_fragment_t * pbuffer,
   lInt16         width,
   lInt16         height,
   lUInt32         flags,     /* flags */
   lInt16          interval,  /* line height in screen pixels */
   lInt16          valign_dy, /* drift y from baseline */
   lUInt16         margin,    /* first line margin */
   void *          object,    /* pointer to custom object */
   lInt16          letter_spacing
                         )
{
    int srctextsize = (pbuffer->srctextlen + FRM_ALLOC_SIZE-1) / FRM_ALLOC_SIZE * FRM_ALLOC_SIZE;
    if ( pbuffer->srctextlen >= srctextsize)
    {
        srctextsize += FRM_ALLOC_SIZE;
        pbuffer->srctext = cr_realloc( pbuffer->srctext, srctextsize );
    }
    src_text_fragment_t * pline = &pbuffer->srctext[ pbuffer->srctextlen++ ];
    pline->index = (lUInt16)(pbuffer->srctextlen-1);
    pline->o.width = width;
    pline->o.height = height;
    pline->object = object;
    pline->margin = margin;
    pline->flags = flags | LTEXT_SRC_IS_OBJECT;
    pline->interval = interval;
    pline->valign_dy = valign_dy;
    pline->letter_spacing = letter_spacing;
}


#define DEPRECATED_LINE_BREAK_WORD_COUNT    3
#define DEPRECATED_LINE_BREAK_SPACE_LIMIT   64


#ifdef __cplusplus

#define DUMMY_IMAGE_SIZE 16

bool gFlgFloatingPunctuationEnabled = true;

void LFormattedText::AddSourceObject(
            lUInt32         flags,     /* flags */
            lInt16          interval,  /* line height in screen pixels */
            lInt16          valign_dy, /* drift y from baseline */
            lUInt16         margin,    /* first line margin */
            void *          object,    /* pointer to custom object */
            lInt16          letter_spacing
     )
{
    ldomNode * node = (ldomNode*)object;
    if (!node || node->isNull()) {
        TR("LFormattedText::AddSourceObject(): node is NULL!");
        return;
    }

    if (flags & LTEXT_SRC_IS_FLOAT) { // not an image but a float:'ing node
        // Nothing much to do with it at this point
        lvtextAddSourceObject(m_pbuffer, 0, 0,
            flags, interval, valign_dy, margin, object, letter_spacing );
            // lvtextAddSourceObject will itself add to flags: | LTEXT_SRC_IS_OBJECT
            // (only flags & object parameter will be used, the others are not,
            // but they matter if this float is the first node in a paragraph,
            // as the code may grab them from the first source)
        return;
    }

    LVImageSourceRef img = node->getObjectImageSource();
    if ( img.isNull() )
        img = LVCreateDummyImageSource( node, DUMMY_IMAGE_SIZE, DUMMY_IMAGE_SIZE );
    lInt16 width = (lUInt16)img->GetWidth();
    lInt16 height = (lUInt16)img->GetHeight();

    // Scale image native size according to gRenderDPI
    width = scaleForRenderDPI(width);
    height = scaleForRenderDPI(height);

    css_style_ref_t style = node->getStyle();
    lInt16 w = 0, h = 0;
    int em = node->getFont()->getSize();
    lString16 nodename = node->getNodeName();
    if ((nodename.lowercase().compare("sub")==0
                || nodename.lowercase().compare("sup")==0)
            && (style->font_size.type==css_val_percent)) {
        em = em * 100 * 256 / style->font_size.value ; // value is %*256
    }
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
    width = w;
    height = h;

    lvtextAddSourceObject(m_pbuffer, width, height,
        flags, interval, valign_dy, margin, object, letter_spacing );
}

class LVFormatter {
public:
    //LVArray<lUInt16>  widths_buf;
    //LVArray<lUInt8>   flags_buf;
    formatted_text_fragment_t * m_pbuffer;
    int       m_length;
    int       m_size;
    bool      m_staticBufs;
    static bool      m_staticBufs_inUse;
    lChar16 * m_text;
    lUInt16 * m_flags;
    src_text_fragment_t * * m_srcs;
    lUInt16 * m_charindex;
    int *     m_widths;
    int m_y;
    int m_max_img_height;
    bool m_has_images;
    bool m_has_float_to_position;
    bool m_has_ongoing_float;
    bool m_no_clear_own_floats;
    #if (USE_FRIBIDI==1)
        // Bidi/RTL support
        FriBidiCharType *    m_bidi_ctypes;
        FriBidiBracketType * m_bidi_btypes;
        FriBidiLevel *       m_bidi_levels;
        FriBidiParType       m_para_bidi_type;
    #endif
    // These default to false and LTR when USE_FRIBIDI==0,
    // just to avoid too many "#if (USE_FRIBIDI==1)"
    bool m_has_bidi; // true when Bidi (or pure RTL) detected
    bool m_para_dir_is_rtl; // boolean shortcut of m_para_bidi_type

#define OBJECT_CHAR_INDEX ((lUInt16)0xFFFF)
#define FLOAT_CHAR_INDEX  ((lUInt16)0xFFFE)

    LVFormatter(formatted_text_fragment_t * pbuffer)
    : m_pbuffer(pbuffer), m_length(0), m_size(0), m_staticBufs(true), m_y(0)
    {
        if (m_staticBufs_inUse)
            m_staticBufs = false;
        m_text = NULL;
        m_flags = NULL;
        m_srcs = NULL;
        m_charindex = NULL;
        m_widths = NULL;
        m_has_images = false,
        m_max_img_height = -1;
        m_has_float_to_position = false;
        m_has_ongoing_float = false;
        m_no_clear_own_floats = false;
        #if (USE_FRIBIDI==1)
            m_bidi_ctypes = NULL;
            m_bidi_btypes = NULL;
            m_bidi_levels = NULL;
        #endif
    }

    ~LVFormatter()
    {
    }

    // Embedded floats positionning helpers.
    // Returns y of the bottom of the lowest float
    int getFloatsMaxBottomY() {
        int max_b_y = m_y;
        for (int i=0; i<m_pbuffer->floatcount; i++) {
            embedded_float_t * flt = m_pbuffer->floats[i];
            // Ignore fake floats (no src) made from outer floats footprint
            if ( flt->srctext != NULL ) {
                int b_y = flt->y + flt->height;
                if (b_y > max_b_y)
                    max_b_y = b_y;
            }
        }
        return max_b_y;
    }
    // Returns min y for next float
    int getNextFloatMinY(css_clear_t clear) {
        int y = m_y; // current line y
        for (int i=0; i<m_pbuffer->floatcount; i++) {
            embedded_float_t * flt = m_pbuffer->floats[i];
            if (flt->to_position) // ignore not yet positionned floats
                continue;
            // A later float should never be positionned above an earlier float
            if ( flt->y > y )
                y = flt->y;
            if ( clear > css_c_none) {
                if ( (clear == css_c_both) || (clear == css_c_left && !flt->is_right)
                                           || (clear == css_c_right && flt->is_right) ) {
                    int b_y = flt->y + flt->height;
                    if (b_y > y)
                        y = b_y;
                }
            }
        }
        return y;
    }
    // Returns available width (for text or a new float) available at y
    // and between y and y+h
    // Also set offset_x to the x where this width is available
    int getAvailableWidthAtY(int start_y, int h, int & offset_x) {
        if (m_pbuffer->floatcount == 0) { // common short path when no float
            offset_x = 0;
            return m_pbuffer->width;
        }
        int fl_left_max_x = 0;
        int fl_right_min_x = m_pbuffer->width;
        // We need to scan line by line from start_y to start_y+h to be sure
        int y = start_y;
        while (y <= start_y + h) {
            for (int i=0; i<m_pbuffer->floatcount; i++) {
                embedded_float_t * flt = m_pbuffer->floats[i];
                if (flt->to_position) // ignore not yet positionned floats
                    continue;
                if (flt->y <= y && flt->y + flt->height > y) { // this float is spanning this y
                    if (flt->is_right) {
                        if (flt->x < fl_right_min_x)
                            fl_right_min_x = flt->x;
                    }
                    else {
                        if (flt->x + flt->width > fl_left_max_x)
                            fl_left_max_x = flt->x + flt->width;
                    }
                }
            }
            y += 1;
        }
        offset_x = fl_left_max_x;
        return fl_right_min_x - fl_left_max_x;
    }
    // Returns next y after start_y where required_width is available
    // Also set offset_x to the x where that width is available
    int getYWithAvailableWidth(int start_y, int required_width, int required_height, int & offset_x, bool get_right_offset_x=false) {
        int y = start_y;
        int w;
        while (true) {
            w = getAvailableWidthAtY(y, required_height, offset_x);
            if (w >= required_width) // found it
                break;
            if (w == m_pbuffer->width) { // We're past all floats
                // returns this y even if required_width is larger than
                // m_pbuffer->width and it will overflow
                offset_x = 0;
                break;
            }
            y += 1;
        }
        if (get_right_offset_x) {
            int left_floats_w = offset_x;
            int right_floats_w = m_pbuffer->width - left_floats_w - w;
            offset_x = m_pbuffer->width - right_floats_w - required_width;
            if (offset_x < 0) // overflow
                offset_x = 0;
        }
        return y;
    }
    // The following positionning codes is not the most efficient, as we
    // call the previous functions that do many of the same kind of loops.
    // But it's the clearest to express the decision flow

    /// Embedded (among other inline elements) floats management
    void addFloat(src_text_fragment_t * src, int currentTextWidth) {
        embedded_float_t * flt =  lvtextAddEmbeddedFloat( m_pbuffer );
        flt->srctext = src;

        ldomNode * node = (ldomNode *) src->object;
        flt->is_right = node->getStyle()->float_ == css_f_right;
        // clear was not moved to the floatBox: get it from its single child
        flt->clear = node->getChildNode(0)->getStyle()->clear;

        // Thanks to the wrapping floatBox element, which has no
        // margin, we can set its RenderRectAccessor to be exactly
        // our embedded_float coordinates and sizes.
        //   If the wrapped element has margins, its renderRectAccessor
        //   will be positionned/sized at the level of borders or padding,
        //   as crengine does naturally with:
        //       fmt.setWidth(width - margin_left - margin_right);
        //       fmt.setHeight(height - margin_top - margin_bottom);
        //       fmt.setX(x + margin_left);
        //       fmt.setY(y + margin_top);
        // So, the RenderRectAccessor(floatBox) can act as a cache
        // of previously rendered and positionned floats!
        int width;
        int height;
        // This formatting code is called when rendering, but can also be called when
        // looking for links, highlighting... so it may happen that floats have
        // already been rendered and positionnned, and we already know their width
        // and height.
        bool already_rendered = false;
        { // in its own scope, so this RenderRectAccessor is forgotten when left
            RenderRectAccessor fmt( node );
            if ( RENDER_RECT_HAS_FLAG(fmt, FLOATBOX_IS_RENDERED) )
                already_rendered = true;
            // We could also directly use fmt.getX/Y() if it has already been
            // positionned, and avoid the positionning code below.
            // But let's be fully deterministic with that, and redo it.
        }
        if ( !already_rendered ) {
            LVRendPageContext emptycontext( NULL, m_pbuffer->page_height );
            renderBlockElement( emptycontext, node, 0, 0, m_pbuffer->width );
            // (renderBlockElement will ensure style->height if requested.)
            // Gather footnotes links accumulated by emptycontext
            // (We only need to gather links in the rendering phase, for
            // page splitting, so no worry if we don't when already_rendered)
            lString16Collection * link_ids = emptycontext.getLinkIds();
            if (link_ids->length() > 0) {
                flt->links = new lString16Collection();
                for ( int n=0; n<link_ids->length(); n++ ) {
                    flt->links->add( link_ids->at(n) );
                }
            }
        }
        // (renderBlockElement() above may update our RenderRectAccessor(),
        // so (re)get it only now)
        RenderRectAccessor fmt( node );
        width = fmt.getWidth();
        height = fmt.getHeight();

        flt->width = width;
        flt->height = height;
        flt->to_position = true;

        // If there are already floats to position, don't position any more for now
        if ( !m_has_float_to_position ) {
            if ( getNextFloatMinY(flt->clear) == m_y ) {
                // No previous float, nor any clear:'ing, prevents having this one
                // on current line,
                // See if it can still fit on this line, accounting for the current
                // width used by the text before this inline float (getCurrentLineWidth()
                // accounts for already positionned floats on this line)
                if ( currentTextWidth + flt->width <= getCurrentLineWidth() ) {
                    // Call getYWithAvailableWidth() just to get x
                    int x;
                    int y = getYWithAvailableWidth(m_y, flt->width + currentTextWidth, 0, x, flt->is_right);
                    if (y == m_y) { // should always be true, but just to be sure
                        if (flt->is_right) // correct x: add currentTextWidth we added
                            x = x + currentTextWidth;  // to the width for computation
                        flt->x = x;
                        flt->y = y;
                        flt->to_position = false;
                        fmt.setX(flt->x);
                        fmt.setY(flt->y);
                        if (flt->is_right)
                            RENDER_RECT_SET_FLAG(fmt, FLOATBOX_IS_RIGHT);
                        else
                            RENDER_RECT_UNSET_FLAG(fmt, FLOATBOX_IS_RIGHT);
                        RENDER_RECT_SET_FLAG(fmt, FLOATBOX_IS_RENDERED);
                        // Small trick for elements with negative margins (invert dropcaps)
                        // that would overflow above flt->x, to avoid a page split by
                        // sticking the line to the hopefully present margin-top that
                        // precedes this paragraph
                        // (we may want to deal with that more generically by storing these
                        // overflows so we can ensure no page split on the other following
                        // lines as long as they are not consumed)
                        RenderRectAccessor cfmt( node->getChildNode(0));
                        if (cfmt.getY() < 0)
                            m_has_ongoing_float = true;
                        return; // all done with this float
                    }
                }
            }
            m_has_float_to_position = true;
        }
    }
    void positionDelayedFloats() {
        // m_y has been updated, position delayed floats
        if (!m_has_float_to_position)
            return;
        for (int i=0; i<m_pbuffer->floatcount; i++) {
            embedded_float_t * flt = m_pbuffer->floats[i];
            if (!flt->to_position)
                continue;
            int x = 0;
            int y = getNextFloatMinY(flt->clear);
            y = getYWithAvailableWidth(y, flt->width, flt->height, x, flt->is_right);
            flt->x = x;
            flt->y = y;
            flt->to_position = false;
            ldomNode * node = (ldomNode *) flt->srctext->object;
            RenderRectAccessor fmt( node );
            fmt.setX(flt->x);
            fmt.setY(flt->y);
            if (flt->is_right)
                RENDER_RECT_SET_FLAG(fmt, FLOATBOX_IS_RIGHT);
            else
                RENDER_RECT_UNSET_FLAG(fmt, FLOATBOX_IS_RIGHT);
            RENDER_RECT_SET_FLAG(fmt, FLOATBOX_IS_RENDERED);
        }
        m_has_float_to_position = false;
    }
    void finalizeFloats() {
        // Adds blank lines to fill the vertical space still occupied by our own
        // inner floats (we don't fill the height of outer floats (float_footprint)
        // as they can still apply over our siblings.)
        fillAndMoveToY( getFloatsMaxBottomY() );
    }
    void fillAndMoveToY(int target_y) {
        // Adds blank lines to fill the vertical space from current m_y to target_y.
        // We need to use 1px lines to get a chance to allow a page wrap at
        // vertically stacked floats boundaries
        if ( target_y <= m_y ) // bogus: we won't rewind y
            return;
        bool has_ongoing_float;
        while ( m_y < target_y ) {
            formatted_line_t * frmline =  lvtextAddFormattedLine( m_pbuffer );
            frmline->y = m_y;
            frmline->x = 0;
            frmline->height = 1;
            frmline->baseline = 1; // no word to draw, does not matter
            // Check if there are floats spanning that y, so we
            // can avoid a page split
            has_ongoing_float = false;
            for (int i=0; i<m_pbuffer->floatcount; i++) {
                embedded_float_t * flt = m_pbuffer->floats[i];
                if (flt->to_position) // ignore not yet positionned floats (even if
                    continue;         // there shouldn't be any when this is called)
                if (flt->y < m_y && flt->y + flt->height > m_y) {
                    has_ongoing_float = true;
                    break;
                }
                // flt->y == m_y is fine: the float starts on this line,
                // we can split on it
            }
            if (has_ongoing_float) {
                frmline->flags |= LTEXT_LINE_SPLIT_AVOID_BEFORE;
            }
            m_y += 1;
            m_pbuffer->height = m_y;
        }
        checkOngoingFloat();
    }
    void floatClearText( int flags ) {
        // Handling of "clear: left/right/both" is different if the 'clear:'
        // is carried by a <BR> or by a float'ing box (for floating boxes, it
        // is done in addFloat()). Here, we deal with <BR style="clear:..">.
        // If a <BR/> has a "clear:", it moves the text below the floats, and the
        // text continues from there.
        // (Only a <BR> can carry a clear: among the non-floating inline elements.)
        if ( flags & LTEXT_SRC_IS_CLEAR_LEFT ) {
            int y = getNextFloatMinY( css_c_left );
            if (y > m_y)
                fillAndMoveToY( y );
        }
        if ( flags & LTEXT_SRC_IS_CLEAR_RIGHT ) {
            int y = getNextFloatMinY( css_c_right );
            if (y > m_y)
                fillAndMoveToY( y );
        }
    }
    int getCurrentLineWidth() {
        int x;
        // m_pbuffer->strut_height is all we can check for at this point,
        // but the text that will be put on this line may exceed it if
        // there's some vertical-align or font size change involved.
        // So, the line could be pushed down and conflict with a float below.
        // But this will do for now...
        return getAvailableWidthAtY(m_y, m_pbuffer->strut_height, x);
    }
    int getCurrentLineX() {
        int x;
        getAvailableWidthAtY(m_y, m_pbuffer->strut_height, x);
        return x;
    }
    bool isCurrentLineWithFloat() {
        int x;
        int w = getAvailableWidthAtY(m_y, m_pbuffer->strut_height, x);
        return w < m_pbuffer->width;
    }
    void checkOngoingFloat() {
        // Check if there is still some float spanning at current m_y
        // If there is, next added line will ensure no page split
        // between it and the previous line
        m_has_ongoing_float = false;
        for (int i=0; i<m_pbuffer->floatcount; i++) {
            embedded_float_t * flt = m_pbuffer->floats[i];
            if (flt->to_position) // ignore not yet positionned floats, as they
                continue;         // are not yet running past m_y
            if (flt->y < m_y && flt->y + flt->height > m_y) {
                m_has_ongoing_float = true;
                break;
            }
            // flt->y == m_y is fine: the float starts on this line,
            // no need to avoid page split by next line
        }
    }

    /// allocate buffers for paragraph
    void allocate( int start, int end )
    {
        int pos = 0;
        int i;
        // PASS 1: calculate total length (characters + objects)
        for ( i=start; i<end; i++ ) {
            src_text_fragment_t * src = &m_pbuffer->srctext[i];
            if ( src->flags & LTEXT_SRC_IS_FLOAT ) {
                pos++;
            }
            else if ( src->flags & LTEXT_SRC_IS_OBJECT ) {
                pos++;
                if (!m_has_images) {
                    // Compute images max height only when we meet an image,
                    // and only for the first one as it's the same for all
                    // images in this paragraph
                    ldomNode * node = (ldomNode *) src->object;
                    if ( node && !node->isNull() ) {
                        // We have to limit the image height so that the line
                        // that contains it does fit in the page without any
                        // uneeded page break
                        m_max_img_height = m_pbuffer->page_height;
                        // remove parent nodes' margin/border/padding
                        m_max_img_height -= node->getSurroundingAddedHeight();
                        // remove height taken by the strut baseline
                        m_max_img_height -= (m_pbuffer->strut_height - m_pbuffer->strut_baseline);
                        m_has_images = true;
                    }
                }
            } else {
                pos += src->t.len;
            }
        }

        // allocate buffers
        m_length = pos;

        TR("allocate(%d)", m_length);
        // We start with static buffers, but when m_length reaches STATIC_BUFS_SIZE,
        // we switch to dynamic buffers and we keep using them (realloc'ating when
        // needed).
        // The code in this file will fill these buffers with m_length items, so
        // from index [0] to [m_length-1], and read them back.
        // Willingly or not (bug?), this code may also access the buffer one slot
        // further at [m_length], and we need to set this slot to zero to avoid
        // a segfault. So, we need to reserve this additional slot when
        // allocating dynamic buffers, or checking if the static buffers can be
        // used.
        // (memset()'ing all buffers on their full allocated size to 0 would work
        // too, but there's a small performance hit when doing so. Just setting
        // to zero the additional slot seems enough, as all previous slots seems
        // to be correctly filled.)

#define STATIC_BUFS_SIZE 8192
#define ITEMS_RESERVED 16

        // "m_length+1" to keep room for the additional slot to be zero'ed
        if ( !m_staticBufs || m_length+1 > STATIC_BUFS_SIZE ) {
            // if (!m_staticBufs && m_text == NULL) printf("allocating dynamic buffers\n");
            if ( m_length+1 > m_size ) {
                // realloc
                m_size = m_length+ITEMS_RESERVED;
                m_text = cr_realloc(m_staticBufs ? NULL : m_text, m_size);
                m_flags = cr_realloc(m_staticBufs ? NULL : m_flags, m_size);
                m_charindex = cr_realloc(m_staticBufs ? NULL : m_charindex, m_size);
                m_srcs = cr_realloc(m_staticBufs ? NULL : m_srcs, m_size);
                m_widths = cr_realloc(m_staticBufs ? NULL : m_widths, m_size);
                #if (USE_FRIBIDI==1)
                    // Note: we could here check for RTL chars (and have a flag
                    // to then not do it in copyText()) so we don't need to allocate
                    // the following ones if we won't be using them.
                    m_bidi_ctypes = cr_realloc(m_staticBufs ? NULL : m_bidi_ctypes, m_size);
                    m_bidi_btypes = cr_realloc(m_staticBufs ? NULL : m_bidi_btypes, m_size);
                    m_bidi_levels = cr_realloc(m_staticBufs ? NULL : m_bidi_levels, m_size);
                #endif
            }
            m_staticBufs = false;
        } else {
            // static buffer space
            static lChar16 m_static_text[STATIC_BUFS_SIZE];
            static lUInt16 m_static_flags[STATIC_BUFS_SIZE];
            static src_text_fragment_t * m_static_srcs[STATIC_BUFS_SIZE];
            static lUInt16 m_static_charindex[STATIC_BUFS_SIZE];
            static int m_static_widths[STATIC_BUFS_SIZE];
            #if (USE_FRIBIDI==1)
                static FriBidiCharType m_static_bidi_ctypes[STATIC_BUFS_SIZE];
                static FriBidiBracketType m_static_bidi_btypes[STATIC_BUFS_SIZE];
                static FriBidiLevel m_static_bidi_levels[STATIC_BUFS_SIZE];
            #endif
            m_text = m_static_text;
            m_flags = m_static_flags;
            m_charindex = m_static_charindex;
            m_srcs = m_static_srcs;
            m_widths = m_static_widths;
            m_staticBufs = true;
            m_staticBufs_inUse = true;
            // printf("using static buffers\n");
            #if (USE_FRIBIDI==1)
                m_bidi_ctypes = m_static_bidi_ctypes;
                m_bidi_btypes = m_static_bidi_btypes;
                m_bidi_levels = m_static_bidi_levels;
            #endif
        }
        memset( m_flags, 0, sizeof(lUInt16)*m_length ); // start with all flags set to zero
        pos = 0;

        // We set to zero the additional slot that the code may peek at (with
        // the checks against m_length we did, we know this slot is allocated).
        // (This can be removed if we find this was a bug and can fix it)
        m_flags[m_length] = 0;
        m_text[m_length] = 0;
        m_charindex[m_length] = 0;
        m_srcs[m_length] = NULL;
        m_widths[m_length] = 0;
        #if (USE_FRIBIDI==1)
            m_bidi_ctypes[m_length] = 0;
            m_bidi_btypes[m_length] = 0;
            m_bidi_levels[m_length] = 0;
        #endif
    }

    /// copy text of current paragraph to buffers
    void copyText( int start, int end )
    {
        m_has_bidi = false; // will be set if fribidi detects it is bidirectionnal text
        m_para_dir_is_rtl = false;
        bool has_rtl = false; // if no RTL char, no need for expensive bidi processing
        // todo: according to https://www.w3.org/TR/css-text-3/#bidi-linebox
        // the bidi direction, if determined from the text itself (no dir= from
        // outer containers) must follow up to next paragraphs (separated by <BR/> or newlines).
        // Here in lvtextfm, each gets its own call to copyText(), so we might need some state.
        // This link also points out that line box direction and its text content direction
        // might be different...

        int pos = 0;
        int i;
        bool prev_was_space = true; // start with true, to get rid of all leading spaces
        int last_non_space_pos = -1; // to get rid of all trailing spaces
        for ( i=start; i<end; i++ ) {
            src_text_fragment_t * src = &m_pbuffer->srctext[i];
            if ( src->flags & LTEXT_SRC_IS_FLOAT ) {
                m_text[pos] = 0;
                m_srcs[pos] = src;
                m_flags[pos] = LCHAR_IS_OBJECT;
                m_charindex[pos] = FLOAT_CHAR_INDEX; //0xFFFE;
                    // Note: m_flags was a lUInt8, and there were already 8 LCHAR_IS_* bits/flags
                    //   so we couldn't add our own. But using LCHAR_IS_OBJECT should not hurt,
                    //   as we do the FLOAT tests before it is used.
                    //   m_charindex[pos] is the one to use to detect FLOATs
                    // m_flags has since be updated to lUint16, but no real need
                    // to change what we did for floats to use a new flag.
                pos++;
                // No need to update prev_was_space or last_non_space_pos
            }
            else if ( src->flags & LTEXT_SRC_IS_OBJECT ) {
                m_text[pos] = 0;
                m_srcs[pos] = src;
                m_flags[pos] = LCHAR_IS_OBJECT | LCHAR_ALLOW_WRAP_AFTER;
                m_charindex[pos] = OBJECT_CHAR_INDEX; //0xFFFF;
                last_non_space_pos = pos;
                prev_was_space = false;
                pos++;
            }
            else {
                int len = src->t.len;
                lStr_ncpy( m_text+pos, src->t.text, len );
                if ( i==0 || (src->flags & LTEXT_FLAG_NEWLINE) )
                    m_flags[pos] = LCHAR_MANDATORY_NEWLINE;

                // On non PRE-formatted text, our XML parser have already removed
                // consecutive spaces, \t, \r and \n in each single text node
                // (inside and at boundaries), keeping only (if any) one leading
                // space and one trailing space.
                // These text nodes were simply appended (by lvrend) as is into
                // the src_text_fragment_t->t.text that we are processing here.
                // It may happen then that we, here, do get consecutive spaces, eg with:
                //   "<div> Some <span> text </span> and <span> </span> even more. </div>"
                // which would give us here:
                //   " Some  text  and   even more "
                //
                // https://www.w3.org/TR/css-text-3/#white-space-processing states, for
                // non-PRE paragraphs:
                // (a "segment break" is just a \n in the HTML source)
                //   (a) A sequence of segment breaks and other white space between two Chinese,
                //       Japanese, or Yi characters collapses into nothing.
                // (So it looks like CJY is CJK minus K - with Korean, if there is a
                // space between K chars, it should be kept.)
                //   (b) A zero width space before or after a white space sequence containing a
                //       segment break causes the entire sequence of white space to collapse
                //       into a zero width space.
                //   (c) Otherwise, consecutive white space collapses into a single space.
                //
                // For now, we only implement (c).
                // (b) can't really be implemented, as we don't know at this point
                // if there was a segment break or not, as any would have already been
                // converted to a space.
                // (a) is not implemented, but some notes and comments are below (may be
                // not too much bothering for CJK users if nothing was done to fix that?)
                //
                // It also states:
                //     Any space immediately following another collapsible space - even one
                //     outside the boundary of the inline containing that space, provided both
                //     spaces are within the same inline formatting context - is collapsed to
                //     have zero advance width. (It is invisible, but retains its soft wrap
                //     opportunity, if any.)
                // (lvtextfm actually deals with a single "inline formatting context", what
                // crengine calls a "final block".)
                //
                // It also states:
                //     - A sequence of collapsible spaces at the beginning of a line is removed.
                //     - A sequence of collapsible spaces at the end of a line is removed.
                //
                // The specs don't say which, among the consecutive collapsible spaces, to
                // keep, so let's keep the first one (they may have different width,
                // eg with: <big> some </big> <small> text </small> )
                //
                // Note: we can't "remove" any char: m_text, src_text_fragment_t->t.text
                // and the ldomNode text node own text need all to be in-sync: a shift
                // because of a removed char in any of them will cause wrong XPointers
                // and Rects (displaced highlights, etc...)
                // We can just "replace" a char (only in m_text, gone after this paragraph
                // processing) or flag (in m_flags for the time of paragraph processing,
                // in word->flags if needed later for drawing).

                bool preformatted = (src->flags & LTEXT_FLAG_PREFORMATTED);
                for ( int k=0; k<len; k++ ) {
                    lChar16 c = m_text[pos];

                    bool is_space = (c == ' ');
                    if ( is_space && prev_was_space && !preformatted ) {
                        // On non-pre paragraphs, flag spaces following a space
                        // so we can discard them later.
                        m_flags[pos] = LCHAR_IS_COLLAPSED_SPACE | LCHAR_ALLOW_WRAP_AFTER;
                        // m_text[pos] = '_'; // uncomment when debugging
                        // (We can replace the char to see it in printf() (m_text is not the
                        // text that is drawn, it's measured but we correct the measure
                        // by setting a zero width, it's just used here for analysis.
                        // But best to let it as-is except for debugging)
                    }
                    if ( !is_space || preformatted ) // don't strip traling spaces if pre
                        last_non_space_pos = pos;
                    prev_was_space = is_space;

                    /* non-optimized implementation of "(a) A sequence of segment breaks
                     * and other white space between two Chinese, Japanese, or Yi characters
                     * collapses into nothing", not excluding Korea chars
                     * (to be tested/optimized by a CJK dev)
                    if ( ch == ' ' && k>0 && k<len-1
                            && (isCJKIdeograph(m_text[pos-1]) || isCJKIdeograph(m_text[pos+1])) ) {
                        m_flags[pos] = LCHAR_IS_COLLAPSED_SPACE | LCHAR_ALLOW_WRAP_AFTER;
                        // m_text[pos] = '_';
                    }
                    */

                    // if ( ch == '-' || ch == 0x2010 || ch == '.' || ch == '+' || ch==UNICODE_NO_BREAK_SPACE )
                    //     m_flags[pos] |= LCHAR_DEPRECATED_WRAP_AFTER;

                    // Some of these (in the 2 commented lines just above) will be set
                    // in lvfntman measureText().
                    // We might want to have them all done here, for clarity.
                    // We may also want to flags CJK chars to distinguish
                    // left|right punctuations, and those that can have their
                    // ideograph width expanded/collapsed if needed.

                    // We flag some chars as we want them to be ignored: some font
                    // would render a glyph (like "[PDI]") for some control chars
                    // that shouldn't be rendered (Harfbuzz would skip them by itself,
                    // but we also want to skip them when using FreeType directly).
                    // We don't skip them when filling these buffer, as some of them
                    // can give valuable information to the bidi algorithm.
                    // Ignore the unicode direction hints (that we may have added ourselves
                    // in lvrend.cpp when processing <bdi>, <bdo> and the dir= attribute).
                    // Try to balance the searches:
                    if ( c >= 0x202A ) {
                        if ( c <= 0x2069 ) {
                            if ( c <= 0x202E ) m_flags[pos] = LCHAR_IS_TO_IGNORE;      // 202A>202E
                            else if ( c >= 0x2066 ) m_flags[pos] = LCHAR_IS_TO_IGNORE; // 2066>2069
                        }
                    }
                    // We might want to add some others when we happen to meet them.
                    // todo: see harfbuzz hb-unicode.hh is_default_ignorable() for how
                    // to do this kind of check fast

                    // Note: the overhead of using one of the following is quite minimal, so do if needed
                    /*
                    utf8proc_category_t uc = utf8proc_category(c);
                    if (uc == UTF8PROC_CATEGORY_CF)
                        printf("format char %x\n", c);
                    else if (uc == UTF8PROC_CATEGORY_CC)
                        printf("control char %x\n", c);
                    // Alternative, using HarfBuzz:
                    int uc = hb_unicode_general_category(hb_unicode_funcs_get_default(), c);
                    if (uc == HB_UNICODE_GENERAL_CATEGORY_FORMAT)
                        printf("format char %x\n", c);
                    else if (uc == HB_UNICODE_GENERAL_CATEGORY_CONTROL)
                        printf("control char %x\n", c);
                    */

                    #if (USE_FRIBIDI==1)
                        // Also try to detect if we have RTL chars, so that if we don't have any,
                        // we don't need to invoke expensive fribidi processing below (which
                        // may add a 50% duration increase to the text rendering phase).
                        // Looking at fribidi/lib/bidi-type.tab.i and its rules for tagging
                        // a char as RTL, only the following ranges will trigger it:
                        //   0590>08FF      Hebrew, Arabic, Syriac, Thaana, Nko, Samaritan...
                        //   200F 202B 202E Right-To-Left mark, embedding, override format flags
                        //   2067 2068      Right-To-Left isolate, first strong isolate format flags
                        //   FB1D>FDFF      Hebrew and Arabic presentation forms
                        //   FE70>FEFF      Arabic presentation forms
                        //   10800>10FFF    Other rare scripts possibly RTL
                        //   1E800>1EEBB    Other rare scripts possibly RTL
                        // (There may be LTR chars in these ranges, but it's fine, we'll
                        // invoke fribidi, which will say there's no bidi.)
                        if ( !has_rtl ) {
                            // Try to balance the searches
                            if ( c >= 0x0590 ) {
                                if ( c <= 0x2068 ) {
                                    if ( c <= 0x08FF ) has_rtl = true;
                                    else if ( c >= 0x200F ) {
                                        if ( c >= 0x2067 ) has_rtl = true;
                                        else if ( c == 0x200F || c == 0x202B || c == 0x202E ) has_rtl = true;
                                    }
                                }
                                else if ( c >= 0xFB1D ) {
                                    if ( c <= 0xFDFF ) has_rtl = true;
                                    else if ( c <= 0xFEFF ) {
                                        if ( c >= 0xFE70) has_rtl = true;
                                    }
                                    else if ( c <= 0x1EEBB ) {
                                        if (c >= 0x1E800) has_rtl = true;
                                        else if ( c <= 0x10FFF && c >= 0x10800 ) has_rtl = true;
                                    }
                                }
                            }
                        }
                    #endif

                    m_charindex[pos] = k;
                    m_srcs[pos] = src;
                    pos++;
                }
            }
        }
        // Also flag as collapsed all spaces at the end of text
        pos = pos-1; // get back last pos++
        if (last_non_space_pos >= 0 && last_non_space_pos+1 <= pos) {
            for ( int k=last_non_space_pos+1; k<=pos; k++ ) {
                if (m_flags[k] == LCHAR_IS_OBJECT)
                    continue; // don't unflag floats
                m_flags[k] = LCHAR_IS_COLLAPSED_SPACE | LCHAR_ALLOW_WRAP_AFTER;
                // m_text[k] = '='; // uncomment when debugging
            }
        }
        TR("%s", LCSTR(lString16(m_text, m_length)));

        #if (USE_FRIBIDI==1)
        if ( has_rtl ) {
            // HarfBuzz reordering and shaping is different if paragraph is set
            // to LTR or RTL, so we must get it right.
            // m_para_bidi_type = FRIBIDI_PAR_LTR;
            // m_para_bidi_type = FRIBIDI_PAR_RTL;
            m_para_bidi_type = FRIBIDI_PAR_WLTR; // Weak LTR (= auto with a bias toward LTR)
            // More work is needed in lvrend/lvstyles/lvstsheet to propertly support text
            // direction via CSS and attributes (dir="rtl") according to the specs.
            // But some specs say content creators should prefer setting text direction
            // via the dir= attribute, rather than via CSS, so, for now, let's ensure just that.
            // From the first text node of this paragraph, look at its parents until
            // we find one with a dir= attribute. (This might be a bit expensive, but well,
            // proper rtl can cost a bit more...)
            // Note: this should better be done by renderBlockElementEnhanced and FlowState,
            // and stored in the final block RenderRectAccessor.
            for ( i=start; i<end; i++ ) {
                src_text_fragment_t * src = &m_pbuffer->srctext[i];
                if ( !src->object ) // might be NULL for added text (like list-item bullets)
                    continue;
                ldomNode * node = (ldomNode *) src->object; // text node or image
                // todo: if we're going to keep the following, make it a
                // ldomNode::getWrittingDirection() method, returning:
                //   1=LTR -1=RTL 0=unspecified
                while (node) {
                    if ( node->getRendMethod() == erm_inline) {
                        // Ignore inline wrapping nodes: we're interested only in
                        // the attribute set on an upper block element (erm_final
                        // and upper erm_block).
                        node = node->getParentNode();
                        continue;
                    }
                    if ( !node->hasAttribute( attr_dir ) ) {
                        node = node->getParentNode();
                        continue;
                    }
                    lString16 dir = node->getAttributeValue( attr_dir );
                    dir = dir.lowercase(); // (no need for trim(), it's done by the XMLParser)
                    if ( dir.compare("rtl") == 0 ) {
                        m_para_bidi_type = FRIBIDI_PAR_RTL; // Strong RTL
                    }
                    else if ( dir.compare("ltr") == 0 ) {
                        m_para_bidi_type = FRIBIDI_PAR_LTR; // Strong LTR
                    }
                    // otherwise ("auto" or bad value), stay with FRIBIDI_PAR_WLTR Weak LTR
                    break;
                }
                break;
            }

            // Compute bidi levels
            fribidi_get_bidi_types( (const FriBidiChar*)m_text, m_length, m_bidi_ctypes);
            fribidi_get_bracket_types( (const FriBidiChar*)m_text, m_length, m_bidi_ctypes, m_bidi_btypes);
            int max_level = fribidi_get_par_embedding_levels_ex(m_bidi_ctypes, m_bidi_btypes,
                                m_length, (FriBidiParType*)&m_para_bidi_type, m_bidi_levels);
            // If computed max level == 1, we are in plain and only LTR, so no need for
            // more bidi work later.
            if ( max_level > 1 ) {
                m_has_bidi = true;
            }
            if ( m_para_bidi_type == FRIBIDI_PAR_RTL || m_para_bidi_type == FRIBIDI_PAR_WRTL )
                m_para_dir_is_rtl = true;

            // fribidi_shape(FRIBIDI_FLAG_SHAPE_MIRRORING, m_bidi_levels, m_length, NULL, (FriBidiChar*)m_text);
            // No use mirroring at this point I think, as it's not the text that will
            // be drawn. Hoping parens & al. have the same widths when mirrored.
            // We'll do that in addLine() when processing words when meeting
            // a rtl one, with fribidi_get_mirror_char().

            /* For debugging:
                printf("par_type %d , max_level %d\n", m_para_bidi_type, max_level);
                for (int i=0; i<m_length; i++)
                    printf("%d", m_bidi_levels[i]);
                printf("\n");
            // We get:
            //   pure LTR: par_type 272 , max_level 1  0000000000
            //   pure RTL: par_type 273 , max_level 2  1111111111
            //   LTR at start with later some RTL: par_type 272 , max_level 2  00000111111000000000000000
            //   RTL at start with later some LTR: par_type 273 , max_level 3  1111111111112222222222222221
            */

            // todo: other layout things we should adapt for RTL:
            // - list items bullets go to the right of text
            // - tables cells in a row should be reversed order
            // - Supports CSS :dir() selector: https://developer.mozilla.org/en-US/docs/Web/CSS/:dir
            //   looks impossible (styles are applied before text direction detection)
        }
        #endif
    }

    void resizeImage( int & width, int & height, int maxw, int maxh, bool isInline )
    {
        //CRLog::trace("Resize image (%dx%d) max %dx%d %s", width, height, maxw, maxh, isInline ? "inline" : "block");
        bool arbitraryImageScaling = false;
        int maxScale = 1;
        bool zoomIn = width<maxw && height<maxh;
        if ( isInline ) {
            if ( zoomIn ) {
                if ( m_pbuffer->img_zoom_in_mode_inline==0 )
                    return; // no zoom
                arbitraryImageScaling = m_pbuffer->img_zoom_in_mode_inline == 2;
                // maxScale = m_pbuffer->img_zoom_in_scale_inline;
            } else {
//                if ( m_pbuffer->img_zoom_out_mode_inline==0 )
//                    return; // no zoom
                arbitraryImageScaling = m_pbuffer->img_zoom_out_mode_inline == 2;
                // maxScale = m_pbuffer->img_zoom_out_scale_inline;
            }
        } else {
            if ( zoomIn ) {
                if ( m_pbuffer->img_zoom_in_mode_block==0 )
                    return; // no zoom
                arbitraryImageScaling = m_pbuffer->img_zoom_in_mode_block == 2;
                // maxScale = m_pbuffer->img_zoom_in_scale_block;
            } else {
//                if ( m_pbuffer->img_zoom_out_mode_block==0 )
//                    return; // no zoom
                arbitraryImageScaling = m_pbuffer->img_zoom_out_mode_block == 2;
                // maxScale = m_pbuffer->img_zoom_out_scale_block;
            }
        }
        resizeImage( width, height, maxw, maxh, arbitraryImageScaling, maxScale );
    }

    void resizeImage( int & width, int & height, int maxw, int maxh, bool arbitraryImageScaling, int maxScaleMult )
    {
        if (width == 0 || height == 0) {
            // Avoid a floating point exception (division by zero) crash.
            printf("CRE WARNING: resizeImage(width=0 or height=0)\n");
            return;
        }
        if (width < 0 || height < 0) {
            // Avoid invalid resizing if we are provided with negative values
            printf("CRE WARNING: resizeImage(width<0 or height<0)\n");
            return;
        }
        if (maxw < 0 || maxh < 0) {
            // Avoid invalid resizing if we are provided with negative max values
            printf("CRE WARNING: resizeImage(maxw<0 or maxh<0)\n");
            return;
        }
        //CRLog::trace("Resize image (%dx%d) max %dx%d %s  *%d", width, height, maxw, maxh, arbitraryImageScaling ? "arbitrary" : "integer", maxScaleMult);
        if ( maxScaleMult<1 ) maxScaleMult = 1;
        if ( arbitraryImageScaling ) {
            int pscale_x = 1000 * maxw / width;
            int pscale_y = 1000 * maxh / height;
            int pscale = pscale_x < pscale_y ? pscale_x : pscale_y;
            int maxscale = maxScaleMult * 1000;
            if ( pscale>maxscale )
                pscale = maxscale;
            height = height * pscale / 1000;
            width = width * pscale / 1000;
        } else {
            if (maxw == 0 || maxh == 0) {
                // Avoid a floating point exception (division by zero) crash.
                printf("CRE WARNING: resizeImage(maxw=0 or maxh=0)\n");
                return;
            }
            int scale_div = 1;
            int scale_mul = 1;
            int div_x = (width * 1000 / maxw);
            int div_y = (height * 1000 / maxh);
            if ( maxScaleMult>=3 && height*3 < maxh - 20
                    && width*3 < maxw - 20 ) {
                scale_mul = 3;
            } else if ( maxScaleMult>=2 && height * 2 < maxh - 20
                    && width * 2 < maxw - 20 ) {
                scale_mul = 2;
            } else if (div_x>1 || div_y>1) {
                if (div_x>div_y)
                    scale_div = div_x;
                else
                    scale_div = div_y;
            }
            height = height * 1000 * scale_mul / scale_div;
            width = width * 1000 * scale_mul / scale_div;
        }
    }

    /// checks whether to add more space after italic character
    /// (this could be used to shift some regular font glyphs
    /// like 'f' that often overflows the glyph - but we let
    /// such glyphs overflow in the padding/margin, as it is
    /// quite small and possibly intended by the font designer;
    /// italic overflows are often larger, and need to be
    /// corrected at end of line or end of italic node)
    int getAdditionalCharWidth( int pos, int maxpos ) {
        if (m_text[pos]==0) // object
            return 0; // no additional space
        LVFont * font = (LVFont*)m_srcs[pos]->t.font;
        if (!font)
            return 0; // no font
        if ( pos<maxpos-1 && m_srcs[pos+1]==m_srcs[pos] )
            return 0; // the same font, non-last char
        // Correct italic_only, only if overflow
        int glyph_overflow = - font->getRightSideBearing(m_text[pos], true, true);
        // if (glyph_overflow > 0) printf("right overflow: %c %d\n", m_text[pos], glyph_overflow);
        return glyph_overflow;
    }

    /// checks whether to add more space on left before italic character
    /// (this could be used to shift some regular font glyphs
    /// like 'J' whose foot often overflows the glyph - but we let
    /// such glyphs overflow in the padding/margin, as it is
    /// quite small and possibly intended by the font designer;
    /// italic underflows and overflows are often larger, and need
    /// to be corrected at start of line)
    int getAdditionalCharWidthOnLeft( int pos ) {
        if (m_text[pos]==0) // object
            return 0; // no additional space
        LVFont * font = (LVFont*)m_srcs[pos]->t.font;
        // Correct italic_only, including removal of positive leading space,
        int glyph_overflow = - font->getLeftSideBearing(m_text[pos], false, true);
        // if (glyph_overflow != 0) printf("left overflow %c: %d\n", m_text[pos], glyph_overflow);
        return glyph_overflow;
    }

    /// measure word
    bool measureWord(formatted_word_t * word, int & width)
    {
        src_text_fragment_t * srcline = &m_pbuffer->srctext[word->src_text_index];
        LVFont * srcfont= (LVFont *) srcline->t.font;
        const lChar16 * str = srcline->t.text + word->t.start;
        // Avoid malloc by using static buffers. Returns false if word too long.
        #define MAX_MEASURED_WORD_SIZE 127
        static lUInt16 widths[MAX_MEASURED_WORD_SIZE+1];
        static lUInt8 flags[MAX_MEASURED_WORD_SIZE+1];
        if (word->t.len > MAX_MEASURED_WORD_SIZE)
            return false;
        lUInt32 hints = WORD_FLAGS_TO_FNT_FLAGS(word->flags);
        int chars_measured = srcfont->measureText(
                str,
                word->t.len,
                widths, flags,
                0x7FFF,
                '?',
                srcline->letter_spacing,
                false,
                hints );
        width = widths[word->t.len-1];
        return true;
    }

    /// measure text of current paragraph
    void measureText()
    {
        int i;
        LVFont * lastFont = NULL;
        lInt16 lastLetterSpacing = 0;
        //src_text_fragment_t * lastSrc = NULL;
        int start = 0;
        int lastWidth = 0;
#define MAX_TEXT_CHUNK_SIZE 4096
        static lUInt16 widths[MAX_TEXT_CHUNK_SIZE+1];
        static lUInt8 flags[MAX_TEXT_CHUNK_SIZE+1];
        int tabIndex = -1;
        #if (USE_FRIBIDI==1)
            FriBidiLevel lastBidiLevel = 0;
            FriBidiLevel newBidiLevel;
        #endif
        for ( i=0; i<=m_length; i++ ) {
            LVFont * newFont = NULL;
            lInt16 newLetterSpacing = 0;
            src_text_fragment_t * newSrc = NULL;
            if ( tabIndex<0 && m_text[i]=='\t' ) {
                tabIndex = i;
            }
            bool isObject = false;
            bool prevCharIsObject = false;
            if ( i<m_length ) {
                newSrc = m_srcs[i];
                isObject = m_charindex[i] == OBJECT_CHAR_INDEX ||
                           m_charindex[i] == FLOAT_CHAR_INDEX;
                newFont = isObject ? NULL : (LVFont *)newSrc->t.font;
                newLetterSpacing = newSrc->letter_spacing; // 0 for objects
            }
            if (i > 0)
                prevCharIsObject = m_charindex[i-1] == OBJECT_CHAR_INDEX ||
                                   m_charindex[i-1] == FLOAT_CHAR_INDEX;
            if ( !lastFont )
                lastFont = newFont;
            if (i == 0)
                lastLetterSpacing = newLetterSpacing;
            bool bidiLevelChanged = false;
            int lastDirection = 0; // unknown
            #if (USE_FRIBIDI==1)
                lastDirection = 1; // direction known: LTR if no bidi found
                if (m_has_bidi) {
                    newBidiLevel = m_bidi_levels[i];
                    if (i == 0)
                        lastBidiLevel = newBidiLevel;
                    else if ( newBidiLevel != lastBidiLevel )
                        bidiLevelChanged = true;
                    if ( FRIBIDI_LEVEL_IS_RTL(lastBidiLevel) )
                        lastDirection = -1; // RTL
                }
            #endif
            // Note: some additional tweaks (like disabling letter-spacing when
            // a cursive script is detected) are done in measureText() and drawTextString().

            // Make a new segment to measure when any property changes from previous char
            if ( i>start && (   newFont != lastFont
                             || newLetterSpacing != lastLetterSpacing
                             || bidiLevelChanged
                             || isObject
                             || prevCharIsObject
                             || i >= start+MAX_TEXT_CHUNK_SIZE
                             || (m_flags[i] & LCHAR_IS_TO_IGNORE)
                             || (m_flags[i] & LCHAR_MANDATORY_NEWLINE) ) ) {
                // measure start..i-1 chars
                if ( m_charindex[i-1]!=OBJECT_CHAR_INDEX && m_charindex[i-1]!=FLOAT_CHAR_INDEX ) {
                    // measure text
                    // Note: we provide text in the logical order, and measureText()
                    // will apply kerning in that order, which might be wrong if some
                    // text fragment happens to be RTL (except for Harfbuzz which will
                    // do the right thing).
                    int len = i - start;
                    // Provide direction and start/end of paragraph hints, for Harfbuzz
                    lUInt32 hints = 0;
                    if ( start == 0 ) hints |= LFNT_HINT_BEGINS_PARAGRAPH;
                    if ( i == m_length ) hints |= LFNT_HINT_ENDS_PARAGRAPH;
                    if ( lastDirection ) {
                        hints |= LFNT_HINT_DIRECTION_KNOWN;
                        if ( lastDirection < 0 )
                            hints |= LFNT_HINT_DIRECTION_IS_RTL;
                    }
                    int chars_measured = lastFont->measureText(
                            m_text + start,
                            len,
                            widths, flags,
                            0x7FFF, //pbuffer->width,
                            '?',
                            lastLetterSpacing,
                            false,
                            hints
                            );
                    if ( chars_measured<len ) {
                        // printf("######### chars_measured %d < %d\n", chars_measured, len);
                        // too long line
                        int newlen = chars_measured; // TODO: find best wrap position
                        i = start + newlen;
                        len = newlen;
                        // As we're going to continue measuring this text node,
                        // reset newFont (the font of the next text node), so
                        // it does not replace lastFont at the end of the loop.
                        newFont = NULL;
                        // If we didn't measure the full text, letter spacing and
                        // bidi level are to stay the same
                        newLetterSpacing = lastLetterSpacing;
                        #if (USE_FRIBIDI==1)
                            if (m_has_bidi)
                                newBidiLevel = lastBidiLevel;
                        #endif
                    }

                    // Deal with chars flagged as collapsed spaces:
                    // make each zero-width, so they are not accounted
                    // in the words width and position calculation.
                    // Note: widths[] (obtained from lastFont->measureText)
                    // and the m_widths[] we build have cumulative widths
                    // (width[k] is the length of the rendered text from
                    // chars 0 to k included)
                    int cumulative_width_removed = 0;
                    int prev_orig_measured_width = 0;
                    int char_width = 0; // current single char width
                    for ( int k=0; k<len; k++ ) {
                        // printf("%c %x f=%d w=%d\n", m_text[start+k], m_text[start+k], flags[k], widths[k]);
                        char_width = widths[k] - prev_orig_measured_width;
                        prev_orig_measured_width = widths[k];
                        if ( m_flags[start + k] & LCHAR_IS_COLLAPSED_SPACE) {
                            cumulative_width_removed += char_width;
                            // make it zero width: same cumulative width as previous char's
                            widths[k] = k>0 ? widths[k-1] : 0;
                            flags[k] = 0; // remove SPACE/WRAP/... flags
                        }
                        else {
                            // remove, from the measured cumulative width, what we previously removed
                            widths[k] -= cumulative_width_removed;
                        }
                        m_widths[start + k] = lastWidth + widths[k];
                        m_flags[start + k] |= flags[k];
                        // printf("  => w=%d\n", m_widths[start + k]);
                    }

                    int dw = getAdditionalCharWidth(i-1, m_length);
                    if ( lastDirection < 0 ) // ignore it for RTL (as right side bearing is measured)
                        dw = 0;
                    if ( dw ) {
                        m_widths[i-1] += dw;
                        lastWidth += dw;
                    }

                    lastWidth += widths[len-1]; //len<m_length ? len : len-1];

                    // ?????? WTF
                    //m_flags[len] = 0;
                    // TODO: letter spacing letter_spacing
                }
                else if ( m_charindex[start] == FLOAT_CHAR_INDEX) {
                    // Embedded floats can have a zero width in this process of
                    // text measurement. They'll be measured when positionned.
                    m_widths[start] = lastWidth;
                }
                else {
                    // measure object
                    // assume i==start+1
                    int width = m_srcs[start]->o.width;
                    int height = m_srcs[start]->o.height;
                    width=width<0?-width*(m_pbuffer->width)/100:width;
                    height=height<0?-height*(m_pbuffer->width)/100:height;
                    /*
                    printf("measureText img: o.w=%d o.h=%d > w=%d h=%d (max %d %d is_inline=%d) %s\n",
                        m_srcs[start]->o.width, m_srcs[start]->o.height, width, height,
                        m_pbuffer->width, m_max_img_height, m_length>1,
                        UnicodeToLocal(ldomXPointer((ldomNode*)m_srcs[start]->object, 0).toString()).c_str());
                    */
                    resizeImage(width, height, m_pbuffer->width, m_max_img_height, m_length>1);
                    lastWidth += width;
                    m_widths[start] = lastWidth;
                }
                start = i;
            }
            // Skip measuring chars to ignore.
            if ( m_flags[i] & LCHAR_IS_TO_IGNORE) {
                m_widths[start] = lastWidth;
                start++;
                // This whole function here is very convoluted, it could really
                // be made simpler and be more readable.
                // This simple test here feels out of place, but it seems to
                // work in the various cases (ignorable char at start, standalone,
                // multiples, or at end).
            }
            //
            if (newFont)
                lastFont = newFont;
            //lastSrc = newSrc;
            lastLetterSpacing = newLetterSpacing;
            #if (USE_FRIBIDI==1)
                if (m_has_bidi)
                    lastBidiLevel = newBidiLevel;
            #endif
        }
        if ( tabIndex>=0 ) {
            int tabPosition = -m_srcs[0]->margin;
            if ( tabPosition>0 && tabPosition > m_widths[tabIndex] ) {
                int dx = tabPosition - m_widths[tabIndex];
                for ( i=tabIndex; i<m_length; i++ )
                    m_widths[i] += dx;
            }
        }
//        // debug dump
//        lString16 buf;
//        for ( int i=0; i<m_length; i++ ) {
//            buf << L" " << lChar16(m_text[i]) << L" " << lString16::itoa(m_widths[i]);
//        }
//        TR("%s", LCSTR(buf));
    }

#define MIN_WORD_LEN_TO_HYPHENATE 4
#define MAX_WORD_SIZE 64

    /// align line: add or reduce widths of spaces to achieve desired text alignment
    void alignLine( formatted_line_t * frmline, int alignment, int rightIndent=0 ) {
        // Fetch current line x offset and max width
        int x_offset;
        int width = getAvailableWidthAtY(m_y, m_pbuffer->strut_height, x_offset);
        // printf("alignLine %d+%d < %d\n", frmline->x, frmline->width, width);

        // (frmline->x may be different from x_offset when non-zero text-indent)
        int available_width = x_offset + width - (frmline->x + frmline->width) - rightIndent;
        if ( available_width < 0 ) {
            // line is too wide
            // reduce spaces to fit line
            int extraSpace = -available_width;
            int totalSpace = 0;
            int i;
            for ( i=0; i<(int)frmline->word_count-1; i++ ) {
                if ( frmline->words[i].flags & LTEXT_WORD_CAN_ADD_SPACE_AFTER ) {
                    int dw = frmline->words[i].width - frmline->words[i].min_width;
                    if (dw>0) {
                        totalSpace += dw;
                    }
                }
            }
            if ( totalSpace>0 ) {
                int delta = 0;
                for ( i=0; i<(int)frmline->word_count; i++ ) {
                    frmline->words[i].x -= delta;
                    if ( frmline->words[i].flags & LTEXT_WORD_CAN_ADD_SPACE_AFTER ) {
                        int dw = frmline->words[i].width - frmline->words[i].min_width;
                        if (dw>0 && totalSpace>0) {
                            int n = dw * extraSpace / totalSpace;
                            totalSpace -= dw;
                            extraSpace -= n;
                            delta += n;
                            frmline->width -= n;
                        }
                    }
                }
            }
        } else if ( alignment==LTEXT_ALIGN_LEFT )
            return; // no additional alignment necessary
        else if ( alignment==LTEXT_ALIGN_CENTER ) {
            frmline->x += available_width / 2;
        } else if ( alignment==LTEXT_ALIGN_RIGHT ) {
            frmline->x += available_width;
        } else {
            // LTEXT_ALIGN_WIDTH
            if ( available_width <= 0 )
                return; // no space to distribute
            int extraSpace = available_width;
            int addSpacePoints = 0;
            int i;
            for ( i=0; i<(int)frmline->word_count-1; i++ ) {
                if ( frmline->words[i].flags & LTEXT_WORD_CAN_ADD_SPACE_AFTER )
                    addSpacePoints++;
            }
            if ( addSpacePoints>0 ) {
                int addSpaceDiv = extraSpace / addSpacePoints;
                int addSpaceMod = extraSpace % addSpacePoints;
                int delta = 0;
                for ( i=0; i<(int)frmline->word_count; i++ ) {
                    frmline->words[i].x += delta;
                    if ( frmline->words[i].flags & LTEXT_WORD_CAN_ADD_SPACE_AFTER ) {
                        delta += addSpaceDiv;
                        if ( addSpaceMod>0 ) {
                            addSpaceMod--;
                            delta++;
                        }
                    }
                }
                frmline->width += extraSpace;
            }
        }
    }

    /// split line into words, add space for width alignment
    void addLine( int start, int end, int x, src_text_fragment_t * para, int interval, bool first, bool last, bool preFormattedOnly, bool needReduceSpace, bool isLastPara )
    {
        // Note: provided 'interval' is no more used
        int maxWidth = getCurrentLineWidth();
        int rightIndent = 0;
        if ( m_para_dir_is_rtl ) {
            rightIndent = x;
            maxWidth -= x; // put x/first char indent on the right: reduce width
            x = getCurrentLineX(); // use shift induced by left floats
        }
        else {
            x += getCurrentLineX(); // add shift induced by left floats
        }
        //int w0 = start>0 ? m_widths[start-1] : 0;
        int align = para->flags & LTEXT_FLAG_NEWLINE;
        TR("addLine(%d, %d) y=%d  align=%d", start, end, m_y, align);
        // printf("addLine(%d, %d) y=%d  align=%d maxWidth=%d\n", start, end, m_y, align, maxWidth);

        // For some reason, text_align_last inheritance is not ensured in lvrend.cpp,
        // may be to be able to kill justification for the last (or a single) line as
        // easily as what follows below.
        // Here, text_align_last = 0 when it has not explicitely been set by the style
        // of the erm_final node.
        int text_align_last = (para->flags >> LTEXT_LAST_LINE_ALIGN_SHIFT) & LTEXT_FLAG_NEWLINE;
        if ( last && !first && align==LTEXT_ALIGN_WIDTH && text_align_last!=0 )
            align = text_align_last;
        else if ( align==LTEXT_ALIGN_WIDTH && last ) {
            // text-align-last: not specified, justification is in use, and this line
            // is the last (or a single line): align it to the left.
            align = LTEXT_ALIGN_LEFT;
            // Unless fribidi detected this paragraph is RTL: align it to the right
            if ( m_para_dir_is_rtl )
                align = LTEXT_ALIGN_RIGHT;
        }
        if ( preFormattedOnly || !align )
            align = LTEXT_ALIGN_LEFT;

        // Note: in the code and comments, all these mean the same thing:
        // visual alignment enabled, floating punctuation, hanging punctuation
        bool visualAlignmentEnabled = gFlgFloatingPunctuationEnabled!=0 && (align == LTEXT_ALIGN_WIDTH || align == LTEXT_ALIGN_RIGHT ||align==LTEXT_ALIGN_LEFT);

        bool splitBySpaces = (align == LTEXT_ALIGN_WIDTH) || needReduceSpace; // always true with current code

        if ( last && !first ) {
            int last_align = (para->flags>>16) & LTEXT_FLAG_NEWLINE;
            if ( last_align )
                align = last_align;
        }

        bool trustDirection = false;
        bool lineIsBidi = false;
        #if (USE_FRIBIDI==1)
        trustDirection = true;
        bool restore_last_width = false;
        int last_width_to_restore;
        if (m_has_bidi) {
            // We don't want to mess too much with the follow up code, so we
            // do the following, which might be expensive for full RTL documents:
            // we just reorder all chars, flags, width and references to
            // the original nodes, according to how fribidi decides the visual
            // order of chars should be.
            // We can mess with the m_* arrays (the range that spans the current
            // line) as they won't be used anymore after this function.
            // Except for the width of the last char (that we may modify
            // while zeroing the widths of collapsed spaces) that will be
            // used as the starting width of next line. We'll restore it
            // when done with this line.
            last_width_to_restore = m_widths[end-1];
            restore_last_width = true;

            // From fribidi documentation:
            // fribidi_reorder_line() reorders the characters in a line of text
            // from logical to final visual order. Note:
            // - the embedding levels may change a bit
            // - the bidi types and embedding levels are not reordered
            // - last parameter is a map of string indices which is reordered to
            //   reflect where each glyph ends up
            //
            // For re-ordering, we need some temporary buffers.
            // We use static buffers, and don't bother with dynamic buffers
            // in case we would overflow the static buffers.
            // (4096, if some glyphs spans 4 composing unicode codepoints, would
            // make 1000 glyphs, which with a small font of width 4px, would
            // allow them to be displayed on a 4000px screen.
            // Increase that if not enough.)
            #define MAX_LINE_SIZE 4096
            if ( end-start > MAX_LINE_SIZE ) {
                // Show a warning and truncate to avoid a segfault.
                printf("CRE WARNING: bidi processing line overflow (%d > %d)\n", end-start, MAX_LINE_SIZE);
                end = start + MAX_LINE_SIZE;
            }
            static lChar16 bidi_tmp_text[MAX_LINE_SIZE];
            static lUInt16 bidi_tmp_flags[MAX_LINE_SIZE];
            static src_text_fragment_t * bidi_tmp_srcs[MAX_LINE_SIZE];
            static lUInt16 bidi_tmp_charindex[MAX_LINE_SIZE];
            static int     bidi_tmp_widths[MAX_LINE_SIZE];
            // Map of string indices which is reordered to reflect where each
            // glyph ends up. Note that fribidi will access it starting
            // from 0 (and not from 'start'): this would need us to allocate
            // it the size of the full m_text (instead of MAX_LINE_SIZE)!
            // But we can trick that by providing a fake start address,
            // shifted by 'start' (which is ugly and could cause a segfault
            // if some other part than [start:end] would be accessed, but
            // we know fribid doesn't - by contract as it shouldn't reorder
            // any other part except between start:end).
            static FriBidiStrIndex bidi_indices_map[MAX_LINE_SIZE];
            for (int i=start; i<end; i++) {
                bidi_indices_map[i-start] = i;
            }
            FriBidiStrIndex * _virtual_bidi_indices_map = bidi_indices_map - start;

            FriBidiFlags bibi_flags = 0;
            // We're not using bibi_flags=FRIBIDI_FLAG_REORDER_NSM (which is mostly
            // needed for code drawing the resulting reordered result) as it would
            // mess with our indices map, and the final result would be messy.
            // (Looks like even Freetype drawing does not need the BIDI rule
            // L3 (combining-marks-must-come-after-base-char) as it draws finely
            // RTL when we draw the combining marks before base char.)
            int max_level = fribidi_reorder_line(bibi_flags, m_bidi_ctypes, end-start, start,
                                m_para_bidi_type, m_bidi_levels, NULL, _virtual_bidi_indices_map);
            if (max_level > 1) {
                lineIsBidi = true;
                // bidi_tmp_* will contain things in the visual order, from which
                // we will make words (exactly as if it had been LTR that way)
                for (int i=start; i<end; i++) {
                    int bidx = i - start;
                    int j = bidi_indices_map[bidx]; // original indice in m_text, m_flags, m_bidi_levels...
                    bidi_tmp_text[bidx] = m_text[j];
                    bidi_tmp_srcs[bidx] = m_srcs[j];
                    bidi_tmp_charindex[bidx] = m_charindex[j];
                    // Add a flag if this char is part of a RTL segment
                    if ( FRIBIDI_LEVEL_IS_RTL( m_bidi_levels[j] ) )
                        m_flags[j] |= LCHAR_IS_RTL;
                    else
                        m_flags[j] &= ~LCHAR_IS_RTL;
                    bidi_tmp_flags[bidx] = m_flags[j];
                    // bidi_tmp_widths will contains each individual char width, that we
                    // compute from the accumulated width. We'll make it a new
                    // accumulated width in next loop
                    bidi_tmp_widths[bidx] = m_widths[j] - (j > 0 ? m_widths[j-1] : 0);
                    // todo: we should probably also need to update/move the
                    // LCHAR_IS_CLUSTER_TAIL flag... haven't really checked
                    // (might be easier or harder due to the fact that we
                    // don't use FRIBIDI_FLAG_REORDER_NSM?)
                }

                // It looks like fribidi is quite good enough at taking
                // care of collapsed spaces! No real extra space seen
                // when testing, except at start and end.
                // Anyway, we handle collapsed spaces and their widths
                // as we would expect them to be with LTR text just out
                // of copyText().
                bool prev_was_space = true; // start as true to make leading spaces collapsed
                int prev_non_collapsed_space = -1;
                int w = start > 0 ? m_widths[start-1] : 0;
                for (int i=start; i<end; i++) {
                    int bidx = i - start;
                    m_text[i] = bidi_tmp_text[bidx];
                    m_flags[i] = bidi_tmp_flags[bidx];
                    m_srcs[i] = bidi_tmp_srcs[bidx];
                    m_charindex[i] = bidi_tmp_charindex[bidx];
                    // Handle consecutive spaces at start and in the text
                    if ( (m_srcs[i]->flags & LTEXT_FLAG_PREFORMATTED) ) {
                        prev_was_space = false;
                        prev_non_collapsed_space = -1;
                        m_flags[i] &= ~LCHAR_IS_COLLAPSED_SPACE;
                    }
                    else {
                        if ( m_text[i] == ' ' ) {
                            if (prev_was_space) {
                                m_flags[i] |= LCHAR_IS_COLLAPSED_SPACE;
                                // Put this (now collapsed, but possibly previously non-collapsed)
                                // space width on the preceeding now non-collapsed space
                                int w_orig = bidi_tmp_widths[bidx];
                                bidi_tmp_widths[bidx] = 0;
                                if ( prev_non_collapsed_space >= 0 ) {
                                    m_widths[prev_non_collapsed_space] += w_orig;
                                    w += w_orig;
                                }
                            }
                            else {
                                m_flags[i] &= ~LCHAR_IS_COLLAPSED_SPACE;
                                prev_was_space = true;
                                prev_non_collapsed_space = i;
                            }
                        }
                        else {
                            prev_was_space = false;
                            prev_non_collapsed_space = -1;
                            m_flags[i] &= ~LCHAR_IS_COLLAPSED_SPACE;
                        }
                    }
                    w += bidi_tmp_widths[bidx];
                    m_widths[i] = w;
                    // printf("%x:f%x,w%d ", m_text[i], m_flags[i], m_widths[i]);
                }
                // Also flag as collapsed the trailing spaces on the reordered line
                if (prev_non_collapsed_space >= 0) {
                    int prev_width = prev_non_collapsed_space > 0 ? m_widths[prev_non_collapsed_space-1] :0 ;
                    for (int i=prev_non_collapsed_space; i<end; i++) {
                        m_flags[i] |= LCHAR_IS_COLLAPSED_SPACE;
                        m_widths[i] = prev_width;
                    }
                }

            }
            // Note: we reordered m_text and others, which are used from now on only
            // to properly split words. When drawing the text, these are no more used,
            // and the string is taken directly from the copy of the text node string
            // stored as src_text_fragment_t->t.text, so FreeType and HarfBuzz will
            // get the text in logical order (as HarfBuzz expects it).
            // Also, when parens/brackets are involved in RTL text, only HarfBuzz
            // will correctly mirror them. When not using Harfbuzz, we'll mirror
            // mirrorable chars below when a word is RTL.
        }
        #endif

        int lastnonspace = 0;
        if ( align==LTEXT_ALIGN_WIDTH || splitBySpaces ) {
            for ( int i=start; i<end; i++ )
                if ( !((m_flags[i] & LCHAR_IS_SPACE) && !(m_flags[i] & LCHAR_IS_OBJECT)) )
                    lastnonspace = i;
                // This "!( SPACE && !OBJECT)" looks wrong, as an OBJECT can't be also a SPACE,
                // and it feels it's a parens error and should be "(!SPACE && !OBJECT)", but with
                // that, we'll be ignoring multiple stuck OBJECTs at end of line.
                // So, not touching it...
        }

        formatted_line_t * frmline =  lvtextAddFormattedLine( m_pbuffer );
        frmline->y = m_y;
        frmline->x = x;
        // This new line starts with a minimal height and baseline, as set from the
        // paragraph parent node (by lvrend.cpp renderFinalBlock()). These may get
        // increased if some inline elements need more, but not decreased.
        frmline->height = m_pbuffer->strut_height;
        frmline->baseline = m_pbuffer->strut_baseline;
        if (m_has_ongoing_float)
            // Avoid page split when some float that started on a previous line
            // still spans this line
            frmline->flags |= LTEXT_LINE_SPLIT_AVOID_BEFORE;

        if ( preFormattedOnly && (start == end) ) {
            // Specific for preformatted text when consecutive \n\n:
            // start == end, and we have no source text to point to,
            // but we should draw en empty line (we can't just simply
            // increase m_y and m_pbuffer->height, we need to have
            // a frmline as Draw() loops thru these lines - a frmline
            // with no word will do).
            src_text_fragment_t * srcline = m_srcs[start];
            if (srcline->interval > 0) { // should always be the case
                if (srcline->interval > frmline->height) // keep strut_height if greater
                    frmline->height = srcline->interval;
            }
            else { // fall back to line-height: normal
                LVFont * font = (LVFont*)srcline->t.font;
                frmline->height = font->getHeight();
            }
            m_y += frmline->height;
            m_pbuffer->height = m_y;
            return;
        }

        src_text_fragment_t * lastSrc = m_srcs[start];

        // We can just skip FLOATs in addLine(), as they were taken
        // care of in processParagraph() to just reduce the available width
        // So skip floats at start:
        while (lastSrc && (lastSrc->flags & LTEXT_SRC_IS_FLOAT) ) {
            start++;
            lastSrc = m_srcs[start];
        }
        if (!lastSrc) { // nothing but floats
            // A line has already been added: just make it zero-height.
            frmline->height = 0;
            return;
        }

        if ( lineIsBidi ) {
            // Flag that line, so createXPointer() and getRect() know it's not
            // a regular one and can't assume words and text nodes are linear.
            frmline->flags |= LTEXT_LINE_IS_BIDI;
        }
        if ( m_para_dir_is_rtl ) {
            frmline->flags |= LTEXT_LINE_PARA_IS_RTL;
            // Not used yet, but might be useful (we may have a bidi line
            // in a LTR paragraph).
        }

        // Ignore space at start of line (this rarely happens, as line
        // splitting discards the space on which a split is made - but it
        // can happen in other rare wrap cases like lastDeprecatedWrap)
        if ( (m_flags[start] & LCHAR_IS_SPACE) && !(lastSrc->flags & LTEXT_FLAG_PREFORMATTED) ) {
            // But do it only if we're going to stay in same text node (if not
            // the space may have some reason - there's sometimes a no-break-space
            // before an image)
            if (start < end-1 && m_srcs[start+1] == m_srcs[start]) {
                start++;
                lastSrc = m_srcs[start];
            }
        }
        int wstart = start;
        bool lastIsSpace = false;
        bool lastWord = false;
        //bool isObject = false;
        bool isSpace = false;
        //bool nextIsSpace = false;
        bool space = false;
        // Bidi
        bool lastIsRTL = false;
        bool isRTL = false;
        // Ignorables
        bool isToIgnore = false;
        for ( int i=start; i<=end; i++ ) { // loop thru each char
            src_text_fragment_t * newSrc = i<end ? m_srcs[i] : NULL;
            if ( i<end ) {
                //isObject = (m_flags[i] & LCHAR_IS_OBJECT)!=0;
                isSpace = (m_flags[i] & LCHAR_IS_SPACE)!=0; // current char is a space
                //nextIsSpace = i<end-1 && (m_flags[i+1] & LCHAR_IS_SPACE);
                space = splitBySpaces && lastIsSpace && !isSpace && i<=lastnonspace;
                // /\ previous char was a space, current char is not a space
                //     Note: last check was initially "&& i<lastnonspace", but with
                //     this, a line containing "thing inside a " (ending with a
                //     1-char word) would be considered only 2 words ("thing" and
                //     "inside a") and, when justify'ing text, space would not be
                //     distributed between "inside" and "a"...
                //     Not really sure what's the purpose of this last test...
                isToIgnore = m_flags[i] & LCHAR_IS_TO_IGNORE;
            } else {
                lastWord = true;
            }
            isRTL = m_flags[i] & LCHAR_IS_RTL;

            // This loop goes thru each char, and create a new word when it meets:
            // - a non-space char that follows a space (this non-space char will be
            //   the first char of next word).
            // - a char from a different text node (eg: "<span>first</span>next")
            // - a CJK char (whether or not preceded by a space): each becomes a word
            // - the end of text, which makes the last word
            //
            // It so grabs all spaces (0 or 1 with our XML parser) following
            // the current real word, and includes it in the word. So a word
            // includes its following space if any, but should not start with
            // a space. The trailing space is needed for the word processing
            // code below to properly set flags and guess the amount of spaces
            // that can be increased or reduced for proper alignment.
            // Also, these words being then stacked to each other to build the
            // line, the ending space should be kept to be drawn and seen
            // between each word (some words may not be separated by space when
            // from different text nodes or CJK).
            // Note: a "word" in our current context is just a unit of text that
            // should be rendered together, and can be moved on the x-axis for
            // alignment purpose (the 2 french words "qu'autrefois" make a
            // single "word" here, the single word "quelconque", if hyphentaded
            // as "quel-conque" will make one "word" on this line and another
            // "word" on the next line.
            //
            // In a sequence of collapsing spaces, only the first was kept as
            // a LCHAR_IS_SPACE. The following ones were flagged as
            // LCHAR_IS_COLLAPSED_SPACE, and thus are not LCHAR_IS_SPACE.
            // With the algorithm described just above, these collapsed spaces
            // can then only be at the start of a word.
            // Their calculated width has been made to 0, but the drawing code
            // (LFormattedText::Draw() below) will use the original srctext text
            // to draw the string: we can't override this original text (it is
            // made read-only with the use of 'const') to replace the space with
            // a zero-width char (which may not be zero-width in a monospace font).
            // So, we need to adjust each word start index to get rid of the
            // collapsed spaces.
            //
            // Note: if we were to make a space between 2 CJY chars a collapsed
            // space, we would have it at the end of each word, which may
            // be fine without additional work needed (not verified):
            // having a zero-width, it would not change the width of the
            // CJKchar/word, and would not affect the next CJKchar/word position.
            // It would be drawn as a space, but the next CJKchar would override
            // it when it is drawn next.

            if ( i>wstart && (   newSrc!=lastSrc
                              || space
                              || lastWord
                              || isCJKIdeograph(m_text[i])
                              || isRTL != lastIsRTL
                              || isToIgnore
                             ) ) {
                // New HTML source node, space met just before, last word, or CJK char:
                // create and add new word with chars from wstart to i-1

                // Remove any collapsed space at start of word: they
                // may have a zero width and not influence positionning,
                // but they will be drawn as a space by Draw(). We need
                // to increment the start index into the src_text_fragment_t
                // for Draw() to start rendering the text from this position.
                // Also skip floating nodes and chars flagged as to be ignored.
                while (wstart < i) {
                    if ( !(m_flags[wstart] & LCHAR_IS_COLLAPSED_SPACE) &&
                         !(m_flags[wstart] & LCHAR_IS_TO_IGNORE) &&
                            !(m_srcs[wstart]->flags & LTEXT_SRC_IS_FLOAT) )
                        break;
                    // printf("_"); // to see when we remove one, before the TR() below
                    wstart++;
                }
                if (wstart == i) { // word is only collapsed spaces
                    // No need to create it.
                    // Except if it is the last word, and we have not yet added any:
                    // we need a word for the line to have a height (frmline->height)
                    // so that the following line is one line below the empty line we
                    // made (eg, when <br/><br/>)
                    // However, we don't do that if it would be the last empty line in
                    // the last paragraph (paragraphs here are just sections of the final
                    // block cut by <BR>): most browsers don't display the line break
                    // implied by the BR when we have: "<div>some text<br/> </div>more text"
                    // or "<div>some text<br/> <span> </span> </div>more text".
                    if (lastWord && frmline->word_count == 0) {
                        if (!isLastPara) {
                            wstart--; // make a single word with a single collapsed space
                            if (m_flags[wstart] & LCHAR_IS_TO_IGNORE) {
                                // In this (edgy) case, we would be rendering this char we
                                // want to ignore.
                                // This is a bit hacky, but no other solution: just
                                // replace that ignorable char with a space in the
                                // src text
                                *((lChar16 *) (m_srcs[wstart]->t.text + m_charindex[wstart])) = L' ';
                            }
                        }
                        else { // Last or single para with no word
                            // A line has already been added: just make
                            // it zero height.
                            frmline->height = 0;
                            frmline->baseline = 0;
                            continue;
                            // We'll then just exit the loop as we are lastWord
                        }
                    }
                    else {
                        // no word made, get ready for next loop
                        lastSrc = newSrc;
                        lastIsSpace = isSpace;
                        lastIsRTL = isRTL;
                        continue;
                    }
                }

                formatted_word_t * word = lvtextAddFormattedWord(frmline);
                src_text_fragment_t * srcline = m_srcs[wstart];
                // This LTEXT_VALIGN_ flag is now only of use with objects (images)
                int vertical_align_flag = srcline->flags & LTEXT_VALIGN_MASK;
                // These will be used later to adjust the main line baseline and height:
                int top_to_baseline; // distance from this word top to its own baseline (formerly named 'b')
                int baseline_to_bottom; // descender below baseline for this word (formerly named 'h')
                word->src_text_index = m_srcs[wstart]->index;

                if ( lastSrc->flags & LTEXT_SRC_IS_OBJECT ) {
                    // object
                    word->x = frmline->width;
                    word->flags = LTEXT_WORD_IS_OBJECT;
                    word->width = lastSrc->o.width;
                    word->min_width = word->width;
                    word->o.height = lastSrc->o.height;
                    //int maxw = m_pbuffer->width - x;

                    int width = lastSrc->o.width;
                    int height = lastSrc->o.height;
                    width = width<0? -width*(m_pbuffer->width-x)/100 : width;
                    height = height<0? -height*(m_pbuffer->width-x)/100 : height;
                    // todo: adjust m_max_img_height with this image valign_dy/vertical_align_flag
                    resizeImage(width, height, m_pbuffer->width - x, m_max_img_height, m_length>1);
                        // Note: it can happen with a standalone image in a small container
                        // where text-indent is greater than width, that 'm_pbuffer->width - x'
                        // can be negative. We could cap it to zero and resize the image to 0,
                        // but let it be shown un-resized, possibly overflowing or overriding
                        // other content.
                    word->width = width;
                    word->o.height = height;

                    // For images, the baseline is the bottom of the image
                    // srcline->valign_dy sets the baseline, except in a few specific cases
                    // word->y has to be set to where the baseline should be
                    top_to_baseline = word->o.height;
                    baseline_to_bottom = 0;
                    if ( vertical_align_flag == LTEXT_VALIGN_MIDDLE ) {
                        // srcline->valign_dy has been set to where the middle of image should be
                        word->y = srcline->valign_dy + top_to_baseline/2;
                    }
                    else if ( vertical_align_flag == LTEXT_VALIGN_TEXT_TOP ) {
                        // srcline->valign_dy has been set to where top of image should be
                        word->y = srcline->valign_dy + top_to_baseline;
                    }
                    else if ( vertical_align_flag == LTEXT_VALIGN_TOP ) {
                        word->y = top_to_baseline - frmline->baseline;
                    }
                    else if ( vertical_align_flag == LTEXT_VALIGN_BOTTOM ) {
                        word->y = frmline->height - frmline->baseline;
                    }
                    else { // in all other cases, bottom of image is its baseline
                        word->y = srcline->valign_dy;
                    }
                } else {
                    // word
                    // wstart points to the previous first non-space char
                    // i points to a non-space char that will be in next word
                    // i-1 may be a space, or not (when different html tag/text nodes stuck to each other)
                    src_text_fragment_t * srcline = m_srcs[wstart];
                    LVFont * font = (LVFont*)srcline->t.font;

                    int vertical_align_flag = srcline->flags & LTEXT_VALIGN_MASK;
                    int line_height = srcline->interval;
                    int fh = font->getHeight();
                    // As we do only +/- arithmetic, the following values being negative should be fine.
                    // Accounts for line-height (adds what most documentation calls half-leading to top and to bottom):
                    int half_leading = (line_height - fh) / 2;
                    top_to_baseline = font->getBaseline() + half_leading;
                    baseline_to_bottom = line_height - top_to_baseline;
                    // For vertical-align: top or bottom, align to the current frmline as it is at
                    // this point (at minima, the strut), even if frmline height and baseline might
                    // be moved by some coming up words
                    if ( vertical_align_flag == LTEXT_VALIGN_TOP ) {
                        word->y = font->getBaseline() - frmline->baseline + half_leading;
                    }
                    else if ( vertical_align_flag == LTEXT_VALIGN_BOTTOM ) {
                        word->y = frmline->height - fh + font->getBaseline() - frmline->baseline - half_leading;
                    }
                    else {
                        // For others, vertical-align computation is done in lvrend.cpp renderFinalBlock()
                        word->y = srcline->valign_dy;
                    }
                    // printf("baseline_to_bottom=%d top_to_baseline=%d word->y=%d txt=|%s|\n", baseline_to_bottom,
                    //   top_to_baseline, word->y, UnicodeToLocal(lString16(srcline->t.text, srcline->t.len)).c_str());

                    word->x = frmline->width;
                    word->flags = 0;

                    // For Harfbuzz, which may shape differently words at start or end of paragraph
                    if (first && frmline->word_count == 1) // first line of paragraph + first word of line
                        word->flags |= LTEXT_WORD_BEGINS_PARAGRAPH;
                    if (last && lastWord) // last line of paragraph + last word of line
                        word->flags |= LTEXT_WORD_ENDS_PARAGRAPH;

                    if ( trustDirection)
                        word->flags |= LTEXT_WORD_DIRECTION_KNOWN;
                    if ( !m_has_bidi ) {
                        // No bidi, everything is linear
                        word->t.start = m_charindex[wstart];
                        word->t.len = i - wstart;
                    }
                    else if ( m_flags[wstart] & LCHAR_IS_RTL ) {
                        // Bidi and first char RTL.
                        // As we split on bidi level change, the full word is RTL.
                        // As we split on src text fragment, we are sure all chars
                        // are in the same text node.
                        // charindex may have been reordered, and may not be sync'ed with wstart/i-1,
                        // but it is linearly decreasing between i-1 and wstart
                        word->t.start = m_charindex[i-1];
                        word->t.len = m_charindex[wstart] - m_charindex[i-1] + 1;
                        word->flags |= LTEXT_WORD_DIRECTION_IS_RTL; // Draw glyphs in reverse order
                        #if (USE_FRIBIDI==1)
                        // If not using Harfbuzz, procede to mirror parens & al (don't
                        // do that if Harfbuzz is used, as it does that by itself, and
                        // would mirror back our mirrored chars!)
                        if ( font->getKerningMode() != KERNING_MODE_HARFBUZZ) {
                            lChar16 * str = (lChar16*)(srcline->t.text + word->t.start);
                            FriBidiChar mirror;
                            for (int i=0; i < word->t.len; i++) {
                                if ( fribidi_get_mirror_char( (FriBidiChar)(str[i]), &mirror) )
                                    str[i] = (lChar16)mirror;
                            }
                        }
                        #endif
                    }
                    else {
                        // Bidi and first char LTR. Same comments as above, except for last one:
                        // it is linearly increasing between wstart and i-1
                        word->t.start = m_charindex[wstart];
                        word->t.len = m_charindex[i-1] + 1 - m_charindex[wstart];
                    }

                    word->width = m_widths[i>0 ? i-1 : 0] - (wstart>0 ? m_widths[wstart-1] : 0);
                    word->min_width = word->width;
                    TR("addLine - word(%d, %d) x=%d (%d..%d)[%d] |%s|", wstart, i, frmline->width, wstart>0 ? m_widths[wstart-1] : 0, m_widths[i-1], word->width, LCSTR(lString16(m_text+wstart, i-wstart)));
//                    lChar16 lastch = m_text[i-1];
//                    if ( lastch==UNICODE_NO_BREAK_SPACE )
//                        CRLog::trace("last char is UNICODE_NO_BREAK_SPACE");
                    if ( m_flags[wstart] & LCHAR_IS_CLUSTER_TAIL ) {
                        // The start of this word is part of a ligature that started
                        // in a previous word: some hyphenation wrap happened on
                        // this ligature, which will not be rendered as such.
                        // We are the second part of the hyphenated word, and our first
                        // char(s) have a width of 0 (for being part of the ligature):
                        // we need to re-measure this half of the original word.
                        int new_width;
                        if ( measureWord(word, new_width) ) {
                            word->width = new_width;
                            word->min_width = word->width;
                        }
                    }
                    if ( m_flags[i-1] & LCHAR_ALLOW_HYPH_WRAP_AFTER ) {
                        if ( m_flags[i] & LCHAR_IS_CLUSTER_TAIL ) {
                            // The end of this word is part of a ligature that, because
                            // of hyphenation, has been splitted onto next word.
                            // We are the first part of the hyphenated word, and
                            // our last char(s) have been assigned the width of the
                            // ligature glyph, which will not be rendered as such:
                            // we need to re-measure this half of the original word.
                            int new_width;
                            if ( measureWord(word, new_width) ) {
                                word->width = new_width;
                            }
                        }
                        word->width += font->getHyphenWidth();
                        word->min_width = word->width;
                        word->flags |= LTEXT_WORD_CAN_HYPH_BREAK_LINE_AFTER;
                    }
                    if ( m_flags[i-1] & LCHAR_IS_SPACE) { // Current word ends with a space
                        // condition for "- " at beginning of paragraph
                        if ( wstart!=0 || word->t.len!=2 || !(lGetCharProps(m_text[wstart]) & CH_PROP_DASH) ) {
                            // condition for double nbsp after run-in footnote title
                            if ( !(word->t.len>=2 && m_text[i-1]==UNICODE_NO_BREAK_SPACE && m_text[i-2]==UNICODE_NO_BREAK_SPACE)
                                    && !( m_text[i]==UNICODE_NO_BREAK_SPACE && m_text[i+1]==UNICODE_NO_BREAK_SPACE) ) {
                                // Each word ending with a space (except for the 2 conditions above)
                                // can have its width reduced by a fraction of this space width.
                                word->flags |= LTEXT_WORD_CAN_ADD_SPACE_AFTER;
                                int dw = getMaxCondensedSpaceTruncation(i-1);
                                if (dw>0) {
                                    word->min_width = word->width - dw;
                                }
                            }
                        }
                        if ( !visualAlignmentEnabled && lastWord ) {
                            // If last word of line, remove any trailing space from word's width
                            word->width = m_widths[i>1 ? i-2 : 0] - (wstart>0 ? m_widths[wstart-1] : 0);
                            word->min_width = word->width;
                        }
                    } else if ( frmline->word_count>1 && m_flags[wstart] & LCHAR_IS_SPACE ) {
                        // Current word starts with a space (looks like this should not happen):
                        // we can increase the space between previous word and this one if needed
                        //if ( word->t.len<2 || m_text[i-1]!=UNICODE_NO_BREAK_SPACE || m_text[i-2]!=UNICODE_NO_BREAK_SPACE)
//                        if ( m_text[wstart]==UNICODE_NO_BREAK_SPACE && m_text[wstart+1]==UNICODE_NO_BREAK_SPACE)
//                            CRLog::trace("Double nbsp text[-1]=%04x", m_text[wstart-1]);
//                        else
                        frmline->words[frmline->word_count-2].flags |= LTEXT_WORD_CAN_ADD_SPACE_AFTER;
                    } else if (frmline->word_count>1 && isCJKIdeograph(m_text[i])) {
                        // Current word is a CJK char: we can increase the space
                        // between previous word and this one if needed
                        frmline->words[frmline->word_count-2].flags |= LTEXT_WORD_CAN_ADD_SPACE_AFTER;
                    }
                    if ( m_flags[i-1] & LCHAR_ALLOW_WRAP_AFTER )
                        word->flags |= LTEXT_WORD_CAN_BREAK_LINE_AFTER; // not used anywhere
                    if ( word->t.start==0 && srcline->flags & LTEXT_IS_LINK )
                        word->flags |= LTEXT_WORD_IS_LINK_START; // for in-page footnotes

                    if ( visualAlignmentEnabled && lastWord ) { // if floating punctuation enabled
                        int endp = i-1;
                        int lastc = m_text[endp];
                        int wAlign = font->getVisualAligmentWidth();
                        word->width += wAlign/2;
                        while ( (m_flags[endp] & LCHAR_IS_SPACE) && endp>0 ) { // || lastc=='\r' || lastc=='\n'
                            word->width -= m_widths[endp] - m_widths[endp-1];
                            endp--;
                            lastc = m_text[endp];
                        }
                        // We reduce the word width from the hanging char width, so it's naturally pushed
                        // outside in the margin by the alignLine code
                        if ( word->flags & LTEXT_WORD_CAN_HYPH_BREAK_LINE_AFTER ) {
                            word->width -= font->getHyphenWidth(); // TODO: strange fix - need some other solution
                        }
                        else if ( lastc=='.' || lastc==',' || lastc=='!' || lastc==':' || lastc==';' || lastc=='?') {
                            FONT_GUARD
                            int w = font->getCharWidth(lastc);
                            TR("floating: %c w=%d", lastc, w);
                            if (frmline->width + w + wAlign + x >= maxWidth)
                                word->width -= w; //fix russian "?" at line end
                        }
                        else if ( lastc==0x2019 || lastc==0x201d ||   // ’ ” right quotation marks
                                  lastc==0x3001 || lastc==0x3002 ||   // 、 。 ideographic comma and full stop
                                  lastc==0x300d || lastc==0x300f ||   // 」 』 ideographic right bracket
                                  lastc==0xff01 || lastc==0xff0c ||   // ！ ， fullwidth ! and ,
                                  lastc==0xff1a || lastc==0xff1b ) {  // ： ； fullwidth : and ;
                            FONT_GUARD
                            int w = font->getCharWidth(lastc);
                            if (frmline->width + w + wAlign + x >= maxWidth)
                                word->width -= w;
                            else if (w!=0) {
                                // (This looks like some awkward way of detecting if the line
                                // is made out of solely same-fixed-width CJK ideographs,
                                // which will fail if there's enough variable-width western
                                // chars to fail the rounded division vs nb of char comparison.)
                                if (end - start == int((maxWidth - wAlign) / w))
                                    word->width -= w; // Chinese floating punctuation
                                else if (x/w >= 1 && (end-start==int(maxWidth-wAlign-x)/w)-1)
                                    word->width -= w; // first line with text-indent
                            }
                        }
                        if (frmline->width!=0 and last and align!=LTEXT_ALIGN_CENTER) {
                            // (Chinese) add spaces between words in last line or single line
                            // (so they get visually aligned on a grid with the char on the
                            // previous justified lines)
                            FONT_GUARD
                            int properwordcount = maxWidth/font->getSize() - 2;
                            int extraSpace = maxWidth - properwordcount*font->getSize() - wAlign;
                            int exccess = (frmline->width + x + word->width + extraSpace) - maxWidth;
                            if ( exccess>0 && exccess<maxWidth ) { // prevent the line exceeds screen boundary
                                extraSpace -= exccess;
                            }
                            if ( extraSpace>0 ) {
                                int addSpacePoints = 0;
                                int a;
                                int points=0;
                                for ( a=0; a<(int)frmline->word_count-1; a++ ) {
                                    if ( frmline->words[a].flags & LTEXT_WORD_CAN_ADD_SPACE_AFTER )
                                        points++;
                                }
                                addSpacePoints = properwordcount - (frmline->word_count - 1 - points);
                                if (addSpacePoints > 0) {
                                    int addSpaceDiv = extraSpace / addSpacePoints;
                                    int addSpaceMod = extraSpace % addSpacePoints;
                                    int delta = 0;
                                    for (a = 0; a < (int) frmline->word_count; a++) {
                                        frmline->words[a].x +=  delta;
                                        {
                                            delta += addSpaceDiv;
                                            if (addSpaceMod > 0) {
                                                addSpaceMod--;
                                                delta++;
                                            }
                                        }
                                    }
                                }
                            }
                            word->width+=extraSpace;
                        }
                        if ( first && font->getSize()!=0 && (maxWidth/font->getSize()-2)!=0 ) {
                            // proportionally enlarge text-indent when visualAlignment or
                            // floating punctuation is enabled
                            FONT_GUARD
                            int cnt = ((x-wAlign/2)%font->getSize()==0) ? (x-wAlign/2)/font->getSize() : 0;
                                // ugly way to caculate text-indent value, I can not get text-indent from here
                            int p = cnt*(cnt+1)/2;
                            int asd = (2*font->getSize()-font->getCharWidth(lastc)) / (maxWidth/font->getSize()-2);
                            int width = p*asd + cnt; //same math as delta above
                            if (width>0)
                                frmline->x+=width;
                        }
                        word->min_width = word->width;
                    } // done if floating punctuation enabled
                }

                // Adjust full line box height and baseline if needed:
                // frmline->height is the current line height
                // frmline->baseline is the distance from line top to the main baseline of the line
                // top_to_baseline (normally positive number) is the distance from this word top to its own baseline.
                // baseline_to_bottom (normally positive number) is the descender below baseline for this word
                // word->y is the distance from this word baseline to the line main baseline
                //   it is positive when word is subscript, negative when word is superscript
                //
                // negative word->y means it's superscript, so the line's baseline might need to go
                // down (increase) to make room for the superscript
                int needed_baseline = top_to_baseline - word->y;
                if ( needed_baseline > frmline->baseline ) {
                    // shift the line baseline and height by the amount needed at top
                    int shift_down = needed_baseline - frmline->baseline;
                    // if (frmline->baseline) printf("pushed down +%d\n", shift_down);
                    // if (frmline->baseline && lastSrc->object)
                    //     printf("%s\n", UnicodeToLocal(ldomXPointer((ldomNode*)lastSrc->object, 0).toString()).c_str());
                    frmline->baseline += shift_down;
                    frmline->height += shift_down;
                }
                // positive word->y means it's subscript, so the line's baseline does not need to be
                // changed, but more room below might be needed to display the subscript: increase
                // line height so next line is pushed down and dont overwrite the subscript
                int needed_height = frmline->baseline + baseline_to_bottom + word->y;
                if ( needed_height > frmline->height ) {
                    // printf("extended down +%d\n", needed_height-frmline->height);
                    frmline->height = needed_height;
                }

                frmline->width += word->width;

                lastSrc = newSrc;
                wstart = i;
            }
            lastIsSpace = isSpace;
            lastIsRTL = isRTL;
        }
        alignLine( frmline, align, rightIndent );
        m_y += frmline->height;
        m_pbuffer->height = m_y;
        checkOngoingFloat();
        positionDelayedFloats();
        #if (USE_FRIBIDI==1)
        if ( restore_last_width ) // bidi: restore last width to not mess with next line
            m_widths[end-1] = last_width_to_restore;
        #endif
    }

    int getMaxCondensedSpaceTruncation(int pos) {
        if (pos<0 || pos>=m_length || !(m_flags[pos] & LCHAR_IS_SPACE))
            return 0;
        if (m_pbuffer->min_space_condensing_percent==100)
            return 0;
        int w = (m_widths[pos] - (pos > 0 ? m_widths[pos-1] : 0));
        int dw = w * (100 - m_pbuffer->min_space_condensing_percent) / 100;
        if ( dw>0 ) {
            // typographic rule: don't use spaces narrower than 1/4 of font size
            LVFont * fnt = (LVFont *)m_srcs[pos]->t.font;
            int fntBasedSpaceWidthDiv2 = fnt->getSize() * 3 / 4;
            if ( dw>fntBasedSpaceWidthDiv2 )
                dw = fntBasedSpaceWidthDiv2;
            return dw;
        }
        return 0;
    }

    bool isCJKIdeograph(lChar16 c) {
        return c >= UNICODE_CJK_IDEOGRAPHS_BEGIN &&
               c <= UNICODE_CJK_IDEOGRAPHS_END   &&
               ( c <= UNICODE_CJK_PUNCTUATION_HALF_AND_FULL_WIDTH_BEGIN ||
                 c >= UNICODE_CJK_PUNCTUATION_HALF_AND_FULL_WIDTH_END );
    }

    bool isCJKPunctuation(lChar16 c) {
        return ( c >= UNICODE_CJK_PUNCTUATION_BEGIN && c <= UNICODE_CJK_PUNCTUATION_END ) ||
               ( c >= UNICODE_GENERAL_PUNCTUATION_BEGIN && c <= UNICODE_GENERAL_PUNCTUATION_END &&
                    c!=0x2018 && c!=0x201a && c!=0x201b &&    // ‘ ‚ ‛  left quotation marks
                    c!=0x201c && c!=0x201e && c!=0x201f &&    // “ „ ‟  left double quotation marks
                    c!=0x2035 && c!=0x2036 && c!=0x2037 &&    // ‵ ‶ ‷ reversed single/double/triple primes
                    c!=0x2039 && c!=0x2045 && c!=0x204c  ) || // ‹ ⁅ ⁌ left angle quot mark, bracket, bullet
               ( c >= UNICODE_CJK_PUNCTUATION_HALF_AND_FULL_WIDTH_BEGIN &&
                 c <= UNICODE_CJK_PUNCTUATION_HALF_AND_FULL_WIDTH_END ) ||
               ( c == 0x00b7 ); // · middle dot
    }

    bool isCJKLeftPunctuation(lChar16 c) {
        return c==0x2018 || c==0x201c || // ‘ “ left single and double quotation marks
               c==0x3008 || c==0x300a || c==0x300c || c==0x300e || c==0x3010 || // 〈 《 「 『 【 CJK left brackets
               c==0xff08; // （ fullwidth left parenthesis
    }

    /// Split paragraph into lines
    void processParagraph( int start, int end, bool isLastPara )
    {
        TR("processParagraph(%d, %d)", start, end);

        // ensure buffer size is ok for paragraph
        allocate( start, end );
        // copy paragraph text to buffer
        copyText( start, end );
        // measure paragraph text
        measureText();

        // run-in detection
        src_text_fragment_t * para = &m_pbuffer->srctext[start];
        int i;
        for ( i=start; i<end; i++ ) {
            if ( !(m_pbuffer->srctext[i].flags & LTEXT_RUNIN_FLAG) ) {
                para = &m_pbuffer->srctext[i];
                break;
            }
        }

        // detect case with inline preformatted text inside block with line feeds -- override align=left for this case
        bool preFormattedOnly = true;
        for ( i=start; i<end; i++ ) {
            if ( !(m_pbuffer->srctext[i].flags & LTEXT_FLAG_PREFORMATTED) ) {
                preFormattedOnly = false;
                break;
            }
        }
        bool lfFound = false;
        for ( i=0; i<m_length; i++ ) {
            if ( m_text[i]=='\n' ) {
                lfFound = true;
                break;
            }
        }
        preFormattedOnly = preFormattedOnly && lfFound;

        int interval = m_srcs[0]->interval; // Note: no more used inside AddLine()
        int maxWidth = getCurrentLineWidth();

        // reservation of space for floating punctuation
        bool visualAlignmentEnabled = gFlgFloatingPunctuationEnabled!=0;
        int visualAlignmentWidth = 0;
        if ( visualAlignmentEnabled ) {
            // We remove from the available width the max of the max width
            // of -/./,/!/? (and other CJK ones) in all fonts used in that
            // paragraph, to reserve room for it in case we get one hanging.
            // (This will lead to messy variable paragraph widths if some
            // paragraph use some bigger font for some inline parts, and
            // others don't.)
            LVFont * font = NULL;
            for ( int i=start; i<end; i++ ) {
                if ( !(m_pbuffer->srctext[i].flags & LTEXT_SRC_IS_OBJECT) ) {
                    font = (LVFont*)m_pbuffer->srctext[i].t.font;
                    if (font) {
                        int dx = font->getVisualAligmentWidth();
                        if ( dx>visualAlignmentWidth )
                            visualAlignmentWidth = dx;
                    }
                }
            }
            maxWidth -= visualAlignmentWidth;
        }

        // split paragraph into lines, export lines
        int pos = 0;
        int upSkipPos = -1;
        int indent = m_srcs[0]->margin;

        /* We'd rather not have this final node text just dropped if there
         * is not enough width for the indent !
        if (indent > maxWidth) {
            return;
        }
        */

        // int minWidth = 0;
        // Not per-specs, but when floats reduce the available width, skip y until
        // we have the width to draw at least a few chars on a line.
        // We use N x strut_height because it's one easily acccessible font metric here.
        int minWidth = 3 * m_pbuffer->strut_height;

        for (;pos<m_length;) { // each loop makes a line
            // x is the initial line indent: it's set to text-indent value for the
            // first line (when pos=0), or to the others when it is negative.
            // (We use it like a x coordinates below, but we'll use it on the
            // right in addLine() if para is RTL.)
            int x = indent >=0 ? (pos==0 ? indent : 0) : (pos==0 ? 0 : -indent);
            int w0 = pos>0 ? m_widths[pos-1] : 0;
            int i;
            int lastNormalWrap = -1;
            int lastDeprecatedWrap = -1;
            int lastHyphWrap = -1;
            int lastMandatoryWrap = -1;
            int spaceReduceWidth = 0; // max total line width which can be reduced by narrowing of spaces
            int firstCharMargin = getAdditionalCharWidthOnLeft(pos); // for first italic char with elements below baseline
            // We might not need to bother with negative left side bearing, as we now
            // can have them in the margin as we don't clip anymore. So we could just have:
            // int firstCharMargin = 0;
            // and italic "J" or "f" would be drawn a bit in the margin.
            // (But as I don't know about the wanted effect with visualAlignmentEnabled,
            // and given it's only about italic chars, and that we would need to remove
            // stuff in getRenderedWidths... letting it as it is.)

            if ( m_has_bidi ) {
                // If bidi, our first char may be no more the first char
                // inside AddLine, so reset firtCharMargin to 0.
                firstCharMargin = 0;
                // todo: probably some other things to avoid if bidi or
                // if m_para_dir_is_rtl, like hyphenation.
                // Also possible: scan chars as they fit on this line for
                // bidi level > 1: if none, this line is pure LTR
            }

            maxWidth = getCurrentLineWidth();
            if (maxWidth <= minWidth) {
                // Find y with available minWidth
                int unused_x;
                // We need to provide a height to find some width available over
                // this height, but we don't know yet the height of text (that
                // may have some vertical-align or use a bigger font) or images
                // that will end up on this line (line height is handled later,
                // by AddLine()), we can only ask for the only height we know
                // about: m_pbuffer->strut_height...
                // todo: find a way to be sure or react to that
                int new_y = getYWithAvailableWidth(m_y, minWidth, m_pbuffer->strut_height, unused_x);
                fillAndMoveToY( new_y );
                maxWidth = getCurrentLineWidth();
            }

            if ( visualAlignmentEnabled ) { // Floating punctuation
                maxWidth -= visualAlignmentWidth;
                spaceReduceWidth -= visualAlignmentWidth/2;
                firstCharMargin += visualAlignmentWidth/2;
                if (isCJKLeftPunctuation(m_text[pos])) {
                    // Make that left punctuation left-hanging by reducing firstCharMargin
                    LVFont * fnt = (LVFont *)m_srcs[pos]->t.font;
                    if (fnt)
                        firstCharMargin -= fnt->getCharWidth(m_text[pos]);
                    firstCharMargin = (x + firstCharMargin) > 0 ? firstCharMargin : 0;
                }
            }

            // Find candidates where end of line is possible
            bool seen_non_collapsed_space = false;
            for ( i=pos; i<m_length; i++ ) {
                if (m_charindex[i] == FLOAT_CHAR_INDEX) { // float
                    src_text_fragment_t * src = m_srcs[i];
                    // Not sure if we can be called again on the same LVFormatter
                    // object, but the whole code allows for re-formatting and
                    // they should give the same result.
                    // So, use a flag to not re-add already processed floats.
                    if ( !(src->flags & LTEXT_SRC_IS_FLOAT_DONE) ) {
                        int currentWidth = x + m_widths[i]-w0 - spaceReduceWidth + firstCharMargin;
                        addFloat( src, currentWidth );
                        src->flags |= LTEXT_SRC_IS_FLOAT_DONE;
                        maxWidth = getCurrentLineWidth();
                    }
                    // We don't set lastNormalWrap when collapsed spaces,
                    // so let's not for floats either.
                    // But we need to when the float is the last source (as
                    // done below, otherwise we would not update wrapPos and
                    // we'd get another ghost line, and this real last line
                    // might be wrongly justified).
                    if ( i==m_length-1 ) {
                        lastNormalWrap = i;
                    }
                    continue;
                }
                lUInt16 flags = m_flags[i];
                if ( m_text[i]=='\n' ) {
                    lastMandatoryWrap = i;
                    break;
                }
                bool grabbedExceedingSpace = false;
                if ( x + m_widths[i]-w0 > maxWidth + spaceReduceWidth - firstCharMargin) {
                    // It's possible the char at i is a space whose width exceeds maxWidth,
                    // but it should be a candidate for lastNormalWrap (otherwise, the
                    // previous word will be hyphenated and we will get spaces widen for
                    // text justification)
                    if ( (flags & LCHAR_ALLOW_WRAP_AFTER) && !(flags & LCHAR_IS_OBJECT) ) // don't break yet
                        grabbedExceedingSpace = true;
                    else
                        break;
                }
                // Note: upstream has added in:
                //   https://github.com/buggins/coolreader/commit/e2a1cf3306b6b083467d77d99dad751dc3aa07d9
                // to the next if:
                //  || lGetCharProps(m_text[i]) == 0
                // but this does not look right, as any other unicode char would allow wrap.
                //
                // We would not need to bother with LCHAR_IS_COLLAPSED_SPACE, as they have zero
                // width and so can be grabbed here. They carry LCHAR_ALLOW_WRAP_AFTER just like
                // a space, so they will set lastNormalWrap.
                // But we don't want any collapsed space at start to make a new line if the
                // following text is a long word that doesn't fit in the available width (which
                // can happen in a small table cell). So, ignore them at start of line:
                if (!seen_non_collapsed_space) {
                    if (flags & LCHAR_IS_COLLAPSED_SPACE)
                        continue;
                    else
                        seen_non_collapsed_space = true;
                }
                // A space or a CJK ideograph make a normal allowed wrap
                if ((flags & LCHAR_ALLOW_WRAP_AFTER) || isCJKIdeograph(m_text[i])) {
                    // Need to check if previous and next non-space char request a wrap on
                    // this space (or CJK char) to be avoided
                    bool avoidWrap = false;
                    // Look first at following char(s)
                    for (int j = i+1; j < m_length; j++) {
                        if (m_charindex[j] == FLOAT_CHAR_INDEX) // skip floats
                            continue;
                        if ( !(m_flags[j] & LCHAR_ALLOW_WRAP_AFTER) ) { // not another (collapsible) space
                            avoidWrap = lGetCharProps(m_text[j]) & CH_PROP_AVOID_WRAP_BEFORE;
                            break;
                        }
                    }
                    if (!avoidWrap && i < m_length-1) { // Look at preceding char(s)
                        // (but not if it is the last char, where a wrap is fine
                        // even if it ends after a CH_PROP_AVOID_WRAP_AFTER char)
                        for (int j = i-1; j >= 0; j--) {
                            if (m_charindex[j] == FLOAT_CHAR_INDEX) // skip floats
                                continue;
                            if ( !(m_flags[j] & LCHAR_ALLOW_WRAP_AFTER) ) { // not another (collapsible) space
                                avoidWrap = lGetCharProps(m_text[j]) & CH_PROP_AVOID_WRAP_AFTER;
                                break;
                            }
                        }
                    }
                    if (!avoidWrap)
                        lastNormalWrap = i;
                    // We could use lastDeprecatedWrap, but it then get too much real chances to be used:
                    // else lastDeprecatedWrap = i;
                    // Note that a wrap can happen AFTER a '-' (that has CH_PROP_AVOID_WRAP_AFTER)
                    // when lastDeprecatedWrap is prefered below.
                }
                else if ( i==m_length-1 ) // Last char
                    lastNormalWrap = i;
                else if ( flags & LCHAR_DEPRECATED_WRAP_AFTER ) // Hyphens make a less priority wrap
                    lastDeprecatedWrap = i;
                else if ( flags & LCHAR_ALLOW_HYPH_WRAP_AFTER ) // can't happen at this point as we haven't
                    lastHyphWrap = i;                           // gone thru HyphMan::hyphenate()
                if ( !grabbedExceedingSpace &&
                        m_pbuffer->min_space_condensing_percent != 100 &&
                        i < m_length-1 &&
                        ( m_flags[i] & LCHAR_IS_SPACE ) &&
                        ( i==m_length-1 || !(m_flags[i + 1] & LCHAR_IS_SPACE) ) ) {
                    // Each space not followed by a space is candidate for space condensing
                    int dw = getMaxCondensedSpaceTruncation(i);
                    if ( dw>0 )
                        spaceReduceWidth += dw;
                }
                if (grabbedExceedingSpace)
                    break; // delayed break
            }
            // It feels there's no need to do anything if there's been one single float
            // that took all the width: we moved i and can wrap.
            if (i<=pos)
                i = pos + 1; // allow at least one character to be shown on line
            int wordpos = i-1;
            int normalWrapWidth = lastNormalWrap > 0 ? x + m_widths[lastNormalWrap]-w0 : 0;
            int deprecatedWrapWidth = lastDeprecatedWrap > 0 ? x + m_widths[lastDeprecatedWrap]-w0 : 0;
            int unusedSpace = maxWidth - normalWrapWidth - 2*visualAlignmentWidth;
            int unusedPercent = maxWidth > 0 ? unusedSpace * 100 / maxWidth : 0;
            if ( deprecatedWrapWidth>normalWrapWidth && unusedPercent>3 ) {
                lastNormalWrap = lastDeprecatedWrap;
            }
            // If, with normal wrapping, more than 5% of line is occupied by
            // spaces, try to find a word (after where we stopped) to hyphenate,
            // if hyphenation is not forbidden by CSS.
            // todo: decide if we should hyphenate if bidi is happening up to now
            if ( lastMandatoryWrap<0 && lastNormalWrap<m_length-1 && unusedPercent > 5 &&
                !(m_srcs[wordpos]->flags & LTEXT_SRC_IS_OBJECT) && (m_srcs[wordpos]->flags & LTEXT_HYPHENATE) ) {
                // hyphenate word
                int start, end;
                // This will find the word contained at wordpos (or the previous word
                // if wordpos happens to be a space or some punctuation - no issue
                // with that as we'll rightly skip the hyphenation attempt below
                // as 'end' will be < lastNormalWrap)
                lStr_findWordBounds( m_text, m_length, wordpos, start, end );
                int len = end-start;
                if ( len<4 ) {
                    // too short word found, find next one
                    // (This seems wrong and a no-op, as it looks like it will find
                    // the exact same word as the previous call...)
                    lStr_findWordBounds( m_text, m_length, end-1, start, end );
                    len = end-start;
                }
#if TRACE_LINE_SPLITTING==1
                if ( len>0 ) {
                    CRLog::trace("wordBounds(%s) unusedSpace=%d wordWidth=%d", LCSTR(lString16(m_text+start, len)), unusedSpace, m_widths[end]-m_widths[start]);
                    TR("wordBounds(%s) unusedSpace=%d wordWidth=%d", LCSTR(lString16(m_text+start, len)), unusedSpace, m_widths[end]-m_widths[start]);
                }
#endif
                if ( start<end && start<wordpos && end>=lastNormalWrap && len>=MIN_WORD_LEN_TO_HYPHENATE ) {
                    if ( len > MAX_WORD_SIZE )
                        len = MAX_WORD_SIZE;
                    // HyphMan::hyphenate(), which is used by some other parts of the code,
                    // expects a lUInt8 array. We added flagSize=1|2 so it can set the correct
                    // flags on our upgraded (from lUInt8 to lUInt16) m_flags.
                    lUInt8 * flags = (lUInt8*) (m_flags + start);
                    static lUInt16 widths[MAX_WORD_SIZE];
                    int wordStart_w = start>0 ? m_widths[start-1] : 0;
                    for ( int i=0; i<len; i++ ) {
                        widths[i] = m_widths[start+i] - wordStart_w;
                    }
                    int max_width = maxWidth + spaceReduceWidth - x - (wordStart_w - w0) - firstCharMargin;
                    int _hyphen_width = ((LVFont*)m_srcs[wordpos]->t.font)->getHyphenWidth();
                    if ( HyphMan::hyphenate(m_text+start, len, widths, flags, _hyphen_width, max_width, 2) ) {
                        for ( int i=0; i<len; i++ )
                            if ( (m_flags[start+i] & LCHAR_ALLOW_HYPH_WRAP_AFTER)!=0 ) {
                                if ( widths[i]+_hyphen_width>max_width ) {
                                    TR("hyphen found, but max width reached at char %d", i);
                                    break; // hyph is too late
                                }
                                if ( start + i > pos+1 )
                                    lastHyphWrap = start + i;
                            }
                    } else {
                        TR("no hyphen found - max_width=%d", max_width);
                    }
                }
            }
            // Find best position to end this line
            int wrapPos = lastHyphWrap;
            if ( lastMandatoryWrap>=0 )
                wrapPos = lastMandatoryWrap;
            else {
                if ( wrapPos<lastNormalWrap )
                    wrapPos = lastNormalWrap;
                if ( wrapPos<0 )
                    wrapPos = i-1;
                if ( wrapPos<=upSkipPos ) {
                    // Ensure that what, when dealing with previous line, we pushed to
                    // next line (below) is actually on this new line.
                    //CRLog::trace("guard old wrapPos at %d", wrapPos);
                    wrapPos = upSkipPos+1;
                    //CRLog::trace("guard new wrapPos at %d", wrapPos);
                    upSkipPos = -1;
                }
            }
            bool needReduceSpace = true; // todo: calculate whether space reducing required
            int endp = wrapPos+(lastMandatoryWrap<0 ? 1 : 0);
            // The following looks left (up) and right (down) if there are any chars/punctuation
            // that should be prevented from being at the end of line or start of line, and if
            // yes adjust wrapPos so they are pushed to next line, or brought to this line.
            // It might be a bit of a duplication of what's done above (for latin punctuations)
            // in the avoidWrap section.
            int downSkipCount = 0;
            int upSkipCount = 0;
            if (endp > 1 && isCJKLeftPunctuation(*(m_text + endp))) {
                // Next char will be fine at the start of next line.
                //CRLog::trace("skip skip punctuation %s, at index %d", LCSTR(lString16(m_text+endp, 1)), endp);
            } else if (endp > 1 && endp < m_length - 1 && isCJKLeftPunctuation(*(m_text + endp - 1))) {
                // Most right char is left punctuation: go back 1 char so this one
                // goes onto next line.
                upSkipPos = endp;
                endp--; wrapPos--;
                //CRLog::trace("up skip left punctuation %s, at index %d", LCSTR(lString16(m_text+endp, 1)), endp);
            } else if (endp > 1 && isCJKPunctuation(*(m_text + endp))) {
                // Next char (start of next line) is some right punctuation that
                // is not allowed at start of line.
                // Look if it's better to wrap before (up) or after (down), and how
                // much up or down we find an adequate wrap position, and decide
                // which to use.
                for (int epos = endp; epos<m_length; epos++, downSkipCount++) {
                   if ( !isCJKPunctuation(*(m_text + epos)) ) break;
                   //CRLog::trace("down skip punctuation %s, at index %d", LCSTR(lString16(m_text + epos, 1)), epos);
                }
                for (int epos = endp; epos>=start; epos--, upSkipCount++) {
                   if ( !isCJKPunctuation(*(m_text + epos)) ) break;
                   //CRLog::trace("up skip punctuation %s, at index %d", LCSTR(lString16(m_text + epos, 1)), epos);
                }
                if (downSkipCount <= upSkipCount && downSkipCount <= 2 && visualAlignmentEnabled) {
                   // Less skips if we bring next char on this line, and hanging
                   // punctuation is enabled so this punctuation will naturally
                   // find it's place in the reserved right area.
                   endp += downSkipCount;
                   wrapPos += downSkipCount;
                   //CRLog::trace("finally down skip punctuations %d", downSkipCount);
                } else if (upSkipCount <= 2) {
                   // Otherwise put it on next line (spaces or inter-ideograph spaces
                   // will be expanded for justification).
                   upSkipPos = endp;
                   endp -= upSkipCount;
                   wrapPos -= upSkipCount;
                   //CRLog::trace("finally up skip punctuations %d", upSkipCount);
                }
            }
            // Best position to end this line found.
            int lastnonspace = endp-1;
            for ( int k=endp-1; k>=start; k-- ) {
                if ( !((m_flags[k] & LCHAR_IS_SPACE) && !(m_flags[k] & LCHAR_IS_OBJECT)) ) {
                    lastnonspace = k;
                    break;
                }
                // This "!( SPACE && !OBJECT)" looks wrong, as an OBJECT can't be also a SPACE,
                // and it feels it's a parens error and should be "(!SPACE && !OBJECT)", but with
                // that, we'll be ignoring multiple stuck OBJECTs at end of line.
                // So, not touching it...
            }
            // todo: probably need be avoided if bidi/rtl:
            int dw = lastnonspace>=start ? getAdditionalCharWidth(lastnonspace, lastnonspace+1) : 0;
            // If we ended the line with some hyphenation, no need to account for italic
            // right side bearing overflow, as the last glyph will be an hyphen.
            if (m_flags[endp-1] & LCHAR_ALLOW_HYPH_WRAP_AFTER)
                dw = 0;
            if (dw) {
                TR("additional width = %d, after char %s", dw, LCSTR(lString16(m_text + lastnonspace, 1)));
                m_widths[lastnonspace] += dw;
            }
            if (endp>m_length) endp=m_length;
            addLine(pos, endp, x + firstCharMargin, para, interval, pos==0, wrapPos>=m_length-1, preFormattedOnly, needReduceSpace, isLastPara);
            pos = wrapPos + 1;
        }
    }

    /// split source data into paragraphs
    void splitParagraphs()
    {
        int start = 0;
        int i;
//        TR("==== splitParagraphs() ====");
//        for ( i=0; i<m_pbuffer->srctextlen; i++ ) {
//            int flg = m_pbuffer->srctext[i].flags;
//            if ( (flg & LTEXT_RUNIN_FLAG) )
//                TR("run-in found");
//            TR("  %d: flg=%04x al=%d ri=%d '%s'", i, flg, (flg & LTEXT_FLAG_NEWLINE), (flg & LTEXT_RUNIN_FLAG)?1:0, (flg&LTEXT_SRC_IS_OBJECT ? "<image>" : LCSTR(lString16(m_pbuffer->srctext[i].t.text, m_pbuffer->srctext[i].t.len)) ) );
//        }
//        TR("============================");

        int srctextlen = m_pbuffer->srctextlen;
        int clear_after_last_flag = 0;
        if ( srctextlen>0 && (m_pbuffer->srctext[srctextlen-1].flags & LTEXT_SRC_IS_CLEAR_LAST) ) {
            // Ignorable source line added to carry a last <br clear=>.
            clear_after_last_flag = m_pbuffer->srctext[srctextlen-1].flags & LTEXT_SRC_IS_CLEAR_BOTH;
            srctextlen -= 1; // Don't process this last srctext
        }

        bool prevRunIn = srctextlen>0 && (m_pbuffer->srctext[0].flags & LTEXT_RUNIN_FLAG);
        for ( i=1; i<=srctextlen; i++ ) {
            // Split on LTEXT_FLAG_NEWLINE, mostly set when <BR/> met
            // (we check m_pbuffer->srctext[i], the next srctext that we are not
            // adding to the current paragraph, as <BR> and its clear= are carried
            // by the following text.)
            bool isLastPara = (i == srctextlen);
            if ( isLastPara || ((m_pbuffer->srctext[i].flags & LTEXT_FLAG_NEWLINE) && !prevRunIn) ) {
                if ( m_pbuffer->srctext[start].flags & LTEXT_SRC_IS_CLEAR_BOTH ) {
                    // (LTEXT_SRC_IS_CLEAR_BOTH is a mask, will match _LEFT and _RIGHT too)
                    floatClearText( m_pbuffer->srctext[start].flags & LTEXT_SRC_IS_CLEAR_BOTH );
                }
                processParagraph( start, i, isLastPara );
                start = i;
            }
            prevRunIn = (i<srctextlen) && (m_pbuffer->srctext[i].flags & LTEXT_RUNIN_FLAG);
        }
        if ( !m_no_clear_own_floats ) {
            // Clear our own floats so they are fully contained in this final block.
            finalizeFloats();
        }
        if ( clear_after_last_flag ) {
            floatClearText( clear_after_last_flag );
        }
    }

    void dealloc()
    {
        if ( !m_staticBufs ) {
            free( m_text );
            free( m_flags );
            free( m_srcs );
            free( m_charindex );
            free( m_widths );
            m_text = NULL;
            m_flags = NULL;
            m_srcs = NULL;
            m_charindex = NULL;
            m_widths = NULL;
            #if (USE_FRIBIDI==1)
                free( m_bidi_ctypes );
                free( m_bidi_btypes );
                free( m_bidi_levels );
                m_bidi_ctypes = NULL;
                m_bidi_btypes = NULL;
                m_bidi_levels = NULL;
            #endif
            m_staticBufs = true;
            // printf("freeing dynamic buffers\n");
        }
        else {
            m_staticBufs_inUse = false;
            // printf("releasing static buffers\n");
        }
    }

    /// format source data
    int format()
    {
        // split and process all paragraphs
        splitParagraphs();
        // cleanup
        dealloc();
        TR("format() finished: h=%d  lines=%d", m_y, m_pbuffer->frmlinecount);
        return m_y;
    }
};

bool LVFormatter::m_staticBufs_inUse = false;

static void freeFrmLines( formatted_text_fragment_t * m_pbuffer )
{
    // clear existing formatted data, if any
    if (m_pbuffer->frmlines)
    {
        for (int i=0; i<m_pbuffer->frmlinecount; i++)
        {
            lvtextFreeFormattedLine( m_pbuffer->frmlines[i] );
        }
        free( m_pbuffer->frmlines );
    }
    m_pbuffer->frmlines = NULL;
    m_pbuffer->frmlinecount = 0;

    // Also clear floats
    if (m_pbuffer->floats)
    {
        for (int i=0; i<m_pbuffer->floatcount; i++)
        {
            if (m_pbuffer->floats[i]->links) {
                delete m_pbuffer->floats[i]->links;
            }
            free( m_pbuffer->floats[i] );
        }
        free( m_pbuffer->floats );
    }
    m_pbuffer->floats = NULL;
    m_pbuffer->floatcount = 0;
}

// experimental formatter
lUInt32 LFormattedText::Format(lUInt16 width, lUInt16 page_height, BlockFloatFootprint * float_footprint)
{
    // clear existing formatted data, if any
    freeFrmLines( m_pbuffer );
    // setup new page size
    m_pbuffer->width = width;
    m_pbuffer->height = 0;
    m_pbuffer->page_height = page_height;
    // format text
    LVFormatter formatter( m_pbuffer );

    if (float_footprint) {
        formatter.m_no_clear_own_floats = float_footprint->no_clear_own_floats;

        // BlockFloatFootprint provides a set of floats to represent
        // outer floats possibly having some footprint over the final
        // block that is to be formatted.
        // See FlowState->getFloatFootprint() for details.
        // So, for each of them, just add an embedded_float_t (without
        // a scrtext as they are not ours) to the buffer so our
        // positionning code can handle them.
        for (int i=0; i<float_footprint->floats_cnt; i++) {
            embedded_float_t * flt =  lvtextAddEmbeddedFloat( m_pbuffer );
            flt->srctext = NULL; // not our own float
            flt->x = float_footprint->floats[i][0];
            flt->y = float_footprint->floats[i][1];
            flt->width = float_footprint->floats[i][2];
            flt->height = float_footprint->floats[i][3];
            flt->is_right = (bool)(float_footprint->floats[i][4]);
        }
    }

    lUInt32 h = formatter.format();

    if ( float_footprint && float_footprint->no_clear_own_floats ) {
        // If we did not finalize/clear our embedded floats, forward
        // them to FlowState so it can ensure layout around them of
        // other block or final nodes.
        for (int i=0; i<m_pbuffer->floatcount; i++) {
            embedded_float_t * flt = m_pbuffer->floats[i];
            if (flt->srctext == NULL) // ignore outer floats given to us by flow
                continue;
            float_footprint->forwardOverflowingFloat(flt->x, flt->y, flt->width, flt->height,
                                        flt->is_right, (ldomNode *)flt->srctext->object);
        }
    }

    return h;
}

void LFormattedText::setImageScalingOptions( img_scaling_options_t * options )
{
    m_pbuffer->img_zoom_in_mode_block = options->zoom_in_block.mode;
    m_pbuffer->img_zoom_in_scale_block = options->zoom_in_block.max_scale;
    m_pbuffer->img_zoom_in_mode_inline = options->zoom_in_inline.mode;
    m_pbuffer->img_zoom_in_scale_inline = options->zoom_in_inline.max_scale;
    m_pbuffer->img_zoom_out_mode_block = options->zoom_out_block.mode;
    m_pbuffer->img_zoom_out_scale_block = options->zoom_out_block.max_scale;
    m_pbuffer->img_zoom_out_mode_inline = options->zoom_out_inline.mode;
    m_pbuffer->img_zoom_out_scale_inline = options->zoom_out_inline.max_scale;
}

void LFormattedText::setMinSpaceCondensingPercent(int minSpaceWidthPercent)
{
    if (minSpaceWidthPercent>=25 && minSpaceWidthPercent<=100)
        m_pbuffer->min_space_condensing_percent = minSpaceWidthPercent;
}

/// set colors for selection and bookmarks
void LFormattedText::setHighlightOptions(text_highlight_options_t * v)
{
    m_pbuffer->highlight_options.selectionColor = v->selectionColor;
    m_pbuffer->highlight_options.commentColor = v->commentColor;
    m_pbuffer->highlight_options.correctionColor = v->correctionColor;
    m_pbuffer->highlight_options.bookmarkHighlightMode = v->bookmarkHighlightMode;
}


void DrawBookmarkTextUnderline(LVDrawBuf & drawbuf, int x0, int y0, int x1, int y1, int y, int flags, text_highlight_options_t * options) {
    if (!(flags & (4 | 8)))
        return;
    if (options->bookmarkHighlightMode == highlight_mode_none)
        return;
    bool isGray = drawbuf.GetBitsPerPixel() <= 8;
    lUInt32 cl = 0x000000;
    if (isGray) {
        if (options->bookmarkHighlightMode == highlight_mode_solid)
            cl = (flags & 4) ? 0xCCCCCC : 0xAAAAAA;
    } else {
        cl = (flags & 4) ? options->commentColor : options->correctionColor;
    }

    if (options->bookmarkHighlightMode == highlight_mode_solid) {
        // solid fill
        lUInt32 cl2 = (cl & 0xFFFFFF) | 0xA0000000;
        drawbuf.FillRect(x0, y0, x1, y1, cl2);
    }

    if (options->bookmarkHighlightMode == highlight_mode_underline) {
        // underline
        cl = (cl & 0xFFFFFF);
        lUInt32 cl2 = cl | 0x80000000;
        int step = 4;
        int index = 0;
        for (int x = x0; x < x1; x += step ) {

            int x2 = x + step;
            if (x2 > x1)
                x2 = x1;
            if (flags & 8) {
                // correction
                int yy = (index & 1) ? y - 1 : y;
                drawbuf.FillRect(x, yy-1, x+1, yy, cl2);
                drawbuf.FillRect(x+1, yy-1, x2-1, yy, cl);
                drawbuf.FillRect(x2-1, yy-1, x2, yy, cl2);
            } else if (flags & 4) {
                if (index & 1)
                    drawbuf.FillRect(x, y-1, x2 + 1, y, cl);
            }
            index++;
        }
    }
}

void LFormattedText::Draw( LVDrawBuf * buf, int x, int y, ldomMarkedRangeList * marks, ldomMarkedRangeList *bookmarks )
{
    int i, j;
    formatted_line_t * frmline;
    src_text_fragment_t * srcline;
    formatted_word_t * word;
    LVFont * font;
    lvRect clip;
    buf->GetClipRect( &clip );
    const lChar16 * str;
    int line_y = y;

    // printf("x/y: %d/%d clip.top/bottom: %d %d\n", x, y, clip.top, clip.bottom);
    // When drawing a paragraph that spans 3 pages, we may get:
    //   x/y: 9/407 clip.top/bottom: 13 559
    //   x/y: 9/-139 clip.top/bottom: 13 583
    //   x/y: 9/-709 clip.top/bottom: 13 545

    for (i=0; i<m_pbuffer->frmlinecount; i++)
    {
        if (line_y>=clip.bottom)
            break;
        frmline = m_pbuffer->frmlines[i];
        if (line_y + frmline->height>=clip.top)
        {
            // process background

            //lUInt32 bgcl = buf->GetBackgroundColor();
            //buf->FillRect( x+frmline->x, y + frmline->y, x+frmline->x + frmline->width, y + frmline->y + frmline->height, bgcl );

            // draw background for each word
            // (if multiple consecutive words share the same bgcolor, this will
            // actually fill a single rect encompassing these words)
            // todo: the way background color (not inherited in lvrend.cpp) is
            // handled here (only looking at the style of the inline node
            // that contains the word, and not at its other inline parents),
            // some words may not get their proper bgcolor
            lUInt32 lastWordColor = 0xFFFFFFFF;
            int lastWordStart = -1;
            int lastWordEnd = -1;
            for (j=0; j<frmline->word_count; j++)
            {
                word = &frmline->words[j];
                srcline = &m_pbuffer->srctext[word->src_text_index];
                if (word->flags & LTEXT_WORD_IS_OBJECT)
                {
                    // no background, TODO
                }
                else
                {
                    lUInt32 bgcl = srcline->bgcolor;
                    if ( lastWordColor!=bgcl || lastWordStart==-1 ) {
                        if ( lastWordStart!=-1 )
                            if ( ((lastWordColor>>24) & 0xFF) < 128 )
                                buf->FillRect( lastWordStart, y + frmline->y, lastWordEnd, y + frmline->y + frmline->height, lastWordColor );
                        lastWordColor=bgcl;
                        lastWordStart = x+frmline->x+word->x;
                    }
                    lastWordEnd = x+frmline->x+word->x+word->width;
                }
            }
            if ( lastWordStart!=-1 )
                if ( ((lastWordColor>>24) & 0xFF) < 128 )
                    buf->FillRect( lastWordStart, y + frmline->y, lastWordEnd, y + frmline->y + frmline->height, lastWordColor );

            // process marks
#ifndef CR_USE_INVERT_FOR_SELECTION_MARKS
            if ( marks!=NULL && marks->length()>0 ) {
                // Here is drawn the "native highlighting" of a selection in progress
                lvRect lineRect( frmline->x, frmline->y, frmline->x + frmline->width, frmline->y + frmline->height );
                for ( int i=0; i<marks->length(); i++ ) {
                    lvRect mark;
                    ldomMarkedRange * range = marks->get(i);
                    if ( range->intersects( lineRect, mark ) ) {
                        //
                        buf->FillRect(mark.left + x, mark.top + y, mark.right + x, mark.bottom + y, m_pbuffer->highlight_options.selectionColor);
                    }
                }
            }
            if (bookmarks!=NULL && bookmarks->length()>0) {
                lvRect lineRect( frmline->x, frmline->y, frmline->x + frmline->width, frmline->y + frmline->height );
                for ( int i=0; i<bookmarks->length(); i++ ) {
                    lvRect mark;
                    ldomMarkedRange * range = bookmarks->get(i);
                    if ( range->intersects( lineRect, mark ) ) {
                        //
                        DrawBookmarkTextUnderline(*buf, mark.left + x, mark.top + y, mark.right + x, mark.bottom + y, mark.bottom + y - 2, range->flags,
                                                  &m_pbuffer->highlight_options);
                    }
                }
            }
#endif
#ifdef CR_USE_INVERT_FOR_SELECTION_MARKS
            // process bookmarks
            if ( bookmarks != NULL && bookmarks->length() > 0 ) {
                lvRect lineRect( frmline->x, frmline->y, frmline->x + frmline->width, frmline->y + frmline->height );
                for ( int i=0; i<bookmarks->length(); i++ ) {
                    lvRect bookmark_rc;
                    ldomMarkedRange * range = bookmarks->get(i);
                    if ( range->intersects( lineRect, bookmark_rc ) ) {
                        buf->FillRect( bookmark_rc.left + x, bookmark_rc.top + y, bookmark_rc.right + x, bookmark_rc.bottom + y, 0xAAAAAA );
                    }
                }
            }
#endif

            int text_decoration_back_gap;
            lUInt16 lastWordSrcIndex;
            for (j=0; j<frmline->word_count; j++)
            {
                word = &frmline->words[j];
                if (word->flags & LTEXT_WORD_IS_OBJECT)
                {
                    srcline = &m_pbuffer->srctext[word->src_text_index];
                    ldomNode * node = (ldomNode *) srcline->object;
                    if (node) {
                        LVImageSourceRef img = node->getObjectImageSource();
                        if ( img.isNull() )
                            img = LVCreateDummyImageSource( node, word->width, word->o.height );
                        int xx = x + frmline->x + word->x;
                        int yy = line_y + frmline->baseline - word->o.height + word->y;
                        buf->Draw( img, xx, yy, word->width, word->o.height );
                        //buf->FillRect( xx, yy, xx+word->width, yy+word->height, 1 );
                    }
                }
                else
                {
                    bool flgHyphen = false;
                    if ( word->flags&LTEXT_WORD_CAN_HYPH_BREAK_LINE_AFTER) {
                        if (j==frmline->word_count-1)
                            flgHyphen = true;
                        // Also do that even if it's not the last word in the line
                        // AND the line is bidi: the hyphen may be in the middle of
                        // the text, but it's fine for some people with bidi, see
                        // conversation "Bidi reordering of soft hyphen" at:
                        //   https://unicode.org/pipermail/unicode/2014-April/thread.html#348
                        // If that's not desirable, just disable hyphenation lookup
                        // in processParagraph() if m_has_bidi or if chars found in
                        // line span multilple bidi levels (so that we don't get
                        // a blank space for a hyphen not drawn after this word).
                        else if (frmline->flags & LTEXT_LINE_IS_BIDI)
                            flgHyphen = true;
                    }
                    srcline = &m_pbuffer->srctext[word->src_text_index];
                    font = (LVFont *) srcline->t.font;
                    str = srcline->t.text + word->t.start;
                    /*
                    lUInt32 srcFlags = srcline->flags;
                    if ( srcFlags & LTEXT_BACKGROUND_MARK_FLAGS ) {
                        lvRect rc;
                        rc.left = x + frmline->x + word->x;
                        rc.top = line_y + (frmline->baseline - font->getBaseline()) + word->y;
                        rc.right = rc.left + word->width;
                        rc.bottom = rc.top + font->getHeight();
                        buf->FillRect( rc.left, rc.top, rc.right, rc.bottom, 0xAAAAAA );
                    }
                    */
                    // Check if we need to continue the text decoration from previous word.
                    // For now, we only ensure it if this word and previous one are in the
                    // same text node. We wrongly won't when one of these is in a sub <SPAN>
                    // because we can't detect that rightly at this point anymore...
                    text_decoration_back_gap = 0;
                    if (j > 0 && word->src_text_index == lastWordSrcIndex) {
                        text_decoration_back_gap = word->x - lastWordEnd;
                    }
                    lUInt32 oldColor = buf->GetTextColor();
                    lUInt32 oldBgColor = buf->GetBackgroundColor();
                    lUInt32 cl = srcline->color;
                    lUInt32 bgcl = srcline->bgcolor;
                    if ( cl!=0xFFFFFFFF )
                        buf->SetTextColor( cl );
                    if ( bgcl!=0xFFFFFFFF )
                        buf->SetBackgroundColor( bgcl );
                    // Add drawing flags: text decoration (underline...)
                    lUInt32 drawFlags = srcline->flags & LTEXT_TD_MASK;
                    // and chars direction, and if word begins or ends paragraph (for Harfbuzz)
                    drawFlags |= WORD_FLAGS_TO_FNT_FLAGS(word->flags);
                    font->DrawTextString(
                        buf,
                        x + frmline->x + word->x,
                        line_y + (frmline->baseline - font->getBaseline()) + word->y,
                        str,
                        word->t.len,
                        '?',
                        NULL,
                        flgHyphen,
                        drawFlags,
                        srcline->letter_spacing,
                        word->width,
                        text_decoration_back_gap);
                    if ( cl!=0xFFFFFFFF )
                        buf->SetTextColor( oldColor );
                    if ( bgcl!=0xFFFFFFFF )
                        buf->SetBackgroundColor( oldBgColor );
                }
                lastWordSrcIndex = word->src_text_index;
                lastWordEnd = word->x + word->width;
            }

#ifdef CR_USE_INVERT_FOR_SELECTION_MARKS
            // process marks
            if ( marks!=NULL && marks->length()>0 ) {
                lvRect lineRect( frmline->x, frmline->y, frmline->x + frmline->width, frmline->y + frmline->height );
                for ( int i=0; i<marks->length(); i++ ) {
                    lvRect mark;
                    ldomMarkedRange * range = marks->get(i);
                    if ( range->intersects( lineRect, mark ) ) {
                        buf->InvertRect( mark.left + x, mark.top + y, mark.right + x, mark.bottom + y);
                    }
                }
            }
#endif
        }
        line_y += frmline->height;
    }

    // Draw floats if any
    // We'll need to know the current final node to correct and draw "marks"
    // (native highlights): we'll get it when dealing with the first float if any.
    ldomNode * this_final_node = NULL;
    lvRect this_final_node_rect;
    ldomMarkedRangeList * absmarks = new ldomMarkedRangeList();

    for (i=0; i<m_pbuffer->floatcount; i++) {
        embedded_float_t * flt = m_pbuffer->floats[i];
        if (flt->srctext == NULL) {
            // Ignore outer floats (they are either fake footprint floats,
            // or real outer floats not to be drawn by us)
            continue;
        }
        ldomNode * node = (ldomNode *) flt->srctext->object;

        // Only some part of this float needs to be in the clip area.
        // Also account for the overflows, so we can render fully
        // floats with negative margins.
        RenderRectAccessor fmt( node );
        int top_overflow = fmt.getTopOverflow();
        int bottom_overflow = fmt.getBottomOverflow();
        // Note: some dropcaps may still not being draw in spite of this
        // because of the checks with _hidePartialGlyphs in lvdrawbuf.cpp
        // (todo: get rid of these _hidePartialGlyphs checks ?)

        if (y + flt->y - top_overflow < clip.bottom && y + flt->y + flt->height + bottom_overflow >= clip.top) {
            // DrawDocument() parameters (y0 + doc_y must be equal to our y,
            // doc_y just shift the viewport, so anything outside is not drawn).
            int x0 = x + flt->x;
            int y0 = y + flt->y;
            int doc_x = 0 - flt->x;
            int doc_y = 0 - flt->y;
            int dx = m_pbuffer->width;
            int dy = m_pbuffer->page_height;
            int page_height = m_pbuffer->page_height;

            if ( marks!=NULL && marks->length()>0 && this_final_node == NULL ) {
                // Provided ldomMarkedRangeList * marks are ranges made from the words
                // of a selection currently being made (native highlights by crengine).
                // Their coordinates have been translated from absolute to relative
                // to our final node by the DrawDocument() that called us.
                // As we are going to call DrawDocument() to draw the floats, we need
                // to translate them back to absolute coordinates (DrawDocument() will
                // translate them again to relative coordinates in the drawn float).
                // (They are matched above against the lineRect, which have coordinates
                // in the context of where we are drawing.)

                // We need to know the current final node and its absolute coordinates
                this_final_node = node->getParentNode();
                for ( ; this_final_node; this_final_node = this_final_node->getParentNode() ) {
                    int rm = this_final_node->getRendMethod();
                    if ( rm == erm_final || rm == erm_list_item || rm == erm_table_caption )
                        break;
                }
                this_final_node->getAbsRect( this_final_node_rect );

                // Create a new ldomMarkedRangeList with marks in absolute coordinates,
                // that will be used by this float we just met, and the next ones.
                for ( int i=0; i<marks->length(); i++ ) {
                    ldomMarkedRange * mark = marks->get(i);
                    ldomMarkedRange * newmark = new ldomMarkedRange( *mark );
                    newmark->start.y += this_final_node_rect.top;
                    newmark->end.y += this_final_node_rect.top;
                    absmarks->add(newmark);
                }
            }

            DrawDocument( *buf, node, x0, y0, dx, dy, doc_x, doc_y, page_height, absmarks, bookmarks );
        }
    }
    delete absmarks;
}

#endif
