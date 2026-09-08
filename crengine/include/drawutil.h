// Border and background painting for lvrend.cpp's DrawDocument().
//
// This is the drawing counterpart to lvrend.cpp: DrawDocument() calls into
// DrawBorder()/FillBackgroundRect()/DrawBackgroundImage()/DrawBodyBackground()
// (declared here, defined in drawutil.cpp) as it walks the tree, but the
// border/background rasterization has no bearing on how that walk itself is
// driven, so it lives in its own file instead of adding to lvrend.cpp's length.

#ifndef __DRAWUTIL_H_INCLUDED__
#define __DRAWUTIL_H_INCLUDED__

#include "crsetup.h"

#include "lvtinydom.h"

// Default border width in screen px when border requested but no width specified
// Note: the default style for border_width used to be (css_val_unspecified, 0),
// and this was used when this value was met.
// We since updated that default style to (css_val_px, 3) (3px, "medium), so this
// would now only be used if lengthToPx() would round to 0 a non-zero value (which
// it currently ensures to not have this happen...).
// Let's keep the logic below, and set this to 1 as an added security, so we get
// non-zero border always shown with a width of at least 1 screen px.
// Shared with measureBorder() in lvrend.cpp.
#define DEFAULT_BORDER_WIDTH 1

//draw border lines,support color,width,all styles, not support border-collapse
void DrawBorder(ldomNode *enode, LVDrawBuf & drawbuf, int x0, int y0, int doc_x, int doc_y, RenderRectAccessor fmt);

// Fills enode's background-color rect (its border box) at absolute position
// (abs_x0, abs_y0), rounding the corners to match border-radius when
// DrawBorder() will (or would, absent a border) paint that box's corners
// rounded -- see the .cpp for the exact rule this mirrors.
void FillBackgroundRect(LVDrawBuf & drawbuf, ldomNode * enode, css_style_ref_t style, RenderRectAccessor fmt, int abs_x0, int abs_y0, lUInt32 bg_color);

// Paints a node's background-image, honoring background-position/-size/
// -repeat, inset to the padding box.
void DrawBackgroundImage(ldomNode *enode, LVDrawBuf & drawbuf, int x0, int y0, int doc_x, int doc_y, int width, int height, bool clip_to_target=true);

// Paints the <body> background (color and/or image), which does not obey
// margin rules and is drawn on the whole canvas/viewport instead.
void DrawBodyBackground( LVDrawBuf & drawbuf, bool draw_bg_color, bool draw_bg_image, ldomNode * enode, int x0, int y0, int dx, int dy, int doc_x, int doc_y);

#endif // __DRAWUTIL_H_INCLUDED__
