/*******************************************************

   CoolReader Engine

   drawutil.cpp: border and background painting used by DrawDocument()

   This source code is distributed under the terms of
   GNU General Public License
   See LICENSE file for details

*******************************************************/

/*
 * This file holds two mostly-independent pieces of what used to live in
 * lvrend.cpp's DrawDocument() support: rounded-corner-aware border/background
 * painting, and background-image sizing/tiling/positioning. They're grouped
 * here not because they share machinery, but because together they made
 * lvrend.cpp too long, and neither has any bearing on how DrawDocument()
 * walks the tree.
 *
 * Border painting has two independent code paths:
 *   - The general per-side rounded-corner path (fillRoundedRect(),
 *     fillRoundedRectBorder()) draws solid/inset/outset as one mitered ring,
 *     and double/groove/ridge as two independent concentric rings, against
 *     elliptical per-corner radii computed by computeBorderRadiiPx(). It's
 *     used whenever the style requests a border-radius and every present
 *     side is one of those six styles (styleQualifiesForRoundedBorder()).
 *   - The legacy square-corner path (drawBorderSideSquare(),
 *     drawBorderStraightLine()) is DrawBorder()'s original per-side
 *     rendering, generalized so all four sides share one implementation
 *     mitered against whichever neighbor sides are present. It's still used
 *     for dashed/dotted sides (which the rounded path doesn't support yet)
 *     and whenever no border-radius applies.
 *
 * FillBackgroundRect() and DrawBorder() must agree on whether a given box's
 * corners end up rounded or square, so they share the same predicates
 * (styleHasBorderRadii(), styleHasAnyBorder(), styleQualifiesForRoundedBorder()).
 *
 * DrawBackgroundImage()/DrawBodyBackground() are otherwise unrelated: they
 * handle background-position/-size/-repeat resolution and <body>'s
 * viewport-wide (not margin-obeying) background painting.
 */

#include "crsetup.h"

#include <math.h>
#include "../include/lvtinydom.h"
#include "../include/fb2def.h"
#include "../include/lvrend.h"
#include "../include/drawutil.h"

inline int myMax(int a, int b) { return a > b ? a : b; }
inline int myMin(int a, int b) { return a < b ? a : b; }

// Whether a style specifies any non-zero border-radius (on any of the 4 corners).
static inline bool styleHasBorderRadii(const css_style_rec_t * style) {
    for (int i=0; i<4; i++) {
        if (style->border_radius_h[i].value != 0)
            return true;
        if (style->border_radius_v[i].value != 0)
            return true;
    }
    return false;
}

// Whether a single border side is actually "present" for rendering purposes:
// a paintable style, no explicit "border-width: 0", and a non-transparent
// effective color. This is the one place that decision is made -- shared by
// styleHasAnyBorder(), styleQualifiesForRoundedBorder(), and DrawBorder()'s
// own hasXBorder computation -- so those three can't silently drift apart
// (e.g. over how a fully-transparent dashed/dotted side is treated).
static bool styleBorderSidePresent(css_border_style_type_t bs, css_length_t bw, lUInt32 color) {
    if (bs < css_border_solid)
        return false;
    if (bw.value == 0 && bw.type > css_val_unspecified)
        return false; // explicit "border-width: 0"
    if (IS_COLOR_FULLY_TRANSPARENT(color))
        return false;
    return true;
}

// Whether a style will cause DrawBorder() to paint a border on any of the 4 sides.
static bool styleHasAnyBorder(const css_style_rec_t * style) {
    struct { css_border_style_type_t bs; css_length_t bw; css_length_t bc; } sides[4] = {
        { style->border_style_top,    style->border_width[0], style->border_color[0] },
        { style->border_style_right,  style->border_width[1], style->border_color[1] },
        { style->border_style_bottom, style->border_width[2], style->border_color[2] },
        { style->border_style_left,   style->border_width[3], style->border_color[3] },
    };
    for (int i=0; i<4; i++) {
        lUInt32 color = sides[i].bc.type != css_val_unspecified ? sides[i].bc.value : style->color.value;
        if (styleBorderSidePresent(sides[i].bs, sides[i].bw, color))
            return true;
    }
    return false;
}

// Compute per-corner border radii in pixels, applying CSS scaling rules.
// Order of corners: 0=TL, 1=TR, 2=BR, 3=BL. Returns whether any radius is
// non-zero (rx/ry are left as all-zero when it returns false).
static bool computeBorderRadiiPx(ldomNode * node, css_style_rec_t * style, int box_w, int box_h, int rx[4], int ry[4]) {
    if (!styleHasBorderRadii(style))
        return false;
    for (int i=0; i<4; i++) {
        rx[i] = 0; ry[i] = 0;
        css_length_t h = style->border_radius_h[i];
        css_length_t v = style->border_radius_v[i];
        if (h.value != 0)
            rx[i] = lengthToPx(node, h, box_w);
        if (v.value != 0)
            ry[i] = lengthToPx(node, v, box_h);
        if (rx[i] < 0) rx[i] = 0;
        if (ry[i] < 0) ry[i] = 0;
    }
    if (!(rx[0]||rx[1]||rx[2]||rx[3]||ry[0]||ry[1]||ry[2]||ry[3]))
        return false;
    // https://www.w3.org/TR/css-backgrounds-3/#corner-overlap: if the sum of
    // any two adjacent radii along an edge would exceed that edge's length,
    // scale every radius down by the same factor so they fit.
    double fx = 1.0, fy = 1.0;
    int sum_top = rx[0] + rx[1];
    if (sum_top > 0 && box_w > 0)
        fx = fmin(fx, (double)box_w / sum_top);
    int sum_bottom = rx[3] + rx[2];
    if (sum_bottom > 0 && box_w > 0)
        fx = fmin(fx, (double)box_w / sum_bottom);
    int sum_left = ry[0] + ry[3];
    if (sum_left > 0 && box_h > 0)
        fy = fmin(fy, (double)box_h / sum_left);
    int sum_right = ry[1] + ry[2];
    if (sum_right > 0 && box_h > 0)
        fy = fmin(fy, (double)box_h / sum_right);
    double f = fmin(fx, fy);
    if (f < 1.0)
        // Round down (not to nearest).
        // Rounding to nearest instead can let two adjacent radii each round
        // up by up to 0.5px, overshooting the edge by up to 1px.
        for (int i = 0; i < 4; i++) {
            rx[i] = (int)floor(rx[i] * f);
            ry[i] = (int)floor(ry[i] * f);
        }
    return true;
}

// Elliptical corner boundary helper: given a corner's radii and the vertical
// distance from its center (already offset by the -0.5/+0.5 pixel-center
// convention), returns the horizontal inset of the ellipse boundary at that
// scanline (or the full radius once the scanline falls outside the corner's
// vertical span).
static inline int roundedCornerInsetDx(int rxc, int ryc, double dy) {
    double val = 1.0 - (dy*dy) / (double)(ryc*ryc);
    return (val <= 0.0) ? rxc : (int)floor((double)rxc * sqrt(val));
}

// Compute the horizontal span [xl2, xr2) at scanline y for a rounded rectangle
// (defined by corner radii rx[4]/ry[4], TL/TR/BR/BL) inset per-side by
// w_top/w_right/w_bottom/w_left (pass all zero for the outer/unshrunk span).
static void computeInnerSpanPerSide(int y,
                              int x0, int y0, int x1, int y1,
                              const int rx[4], const int ry[4],
                              int w_top, int w_right, int w_bottom, int w_left,
                              int &xl2, int &xr2)
{
    int x0i = x0 + w_left, y0i = y0 + w_top, x1i = x1 - w_right, y1i = y1 - w_bottom;
    if (x0i >= x1i || y0i >= y1i) { xl2 = xr2 = x0i; return; }
    int rxi_tl = rx[0] > w_left ? rx[0] - w_left : 0;
    int ryi_tl = ry[0] > w_top  ? ry[0] - w_top  : 0;
    int rxi_tr = rx[1] > w_right ? rx[1] - w_right : 0;
    int ryi_tr = ry[1] > w_top   ? ry[1] - w_top   : 0;
    int rxi_br = rx[2] > w_right ? rx[2] - w_right : 0;
    int ryi_br = ry[2] > w_bottom? ry[2] - w_bottom: 0;
    int rxi_bl = rx[3] > w_left  ? rx[3] - w_left  : 0;
    int ryi_bl = ry[3] > w_bottom? ry[3] - w_bottom: 0;
    int cxi_tl = x0i + rxi_tl; int cyi_tl = y0i + ryi_tl;
    int cxi_tr = x1i - rxi_tr; int cyi_tr = y0i + ryi_tr;
    int cxi_br = x1i - rxi_br; int cyi_br = y1i - ryi_br;
    int cxi_bl = x0i + rxi_bl; int cyi_bl = y1i - ryi_bl;
    xl2 = x0i; xr2 = x1i;
    if (y < y0i + ryi_tl && rxi_tl && ryi_tl) {
        int dx = roundedCornerInsetDx(rxi_tl, ryi_tl, (double)(cyi_tl - y - 0.5));
        int cand = cxi_tl - dx;
        if (xl2 < cand)
            xl2 = cand;
    }
    else if (y >= y1i - ryi_bl && rxi_bl && ryi_bl) {
        int dx = roundedCornerInsetDx(rxi_bl, ryi_bl, (double)(y - cyi_bl + 0.5));
        int cand = cxi_bl - dx;
        if (xl2 < cand)
            xl2 = cand;
    }
    if (y < y0i + ryi_tr && rxi_tr && ryi_tr) {
        int dx = roundedCornerInsetDx(rxi_tr, ryi_tr, (double)(cyi_tr - y - 0.5));
        int cand = cxi_tr + dx;
        if (xr2 > cand)
            xr2 = cand;
    }
    else if (y >= y1i - ryi_br && rxi_br && ryi_br) {
        int dx = roundedCornerInsetDx(rxi_br, ryi_br, (double)(y - cyi_br + 0.5));
        int cand = cxi_br + dx;
        if (xr2 > cand)
            xr2 = cand;
    }
}

// Fill a rounded rectangle with potentially elliptical radii per corner.
static void fillRoundedRect(LVDrawBuf & drawbuf, int x0, int y0, int x1, int y1, const int rx[4], const int ry[4], lUInt32 color) {
    if (((color >> 24) & 0xFF) == 0xFF) // Fully transparent color: skip the scanline conversion
        return;
    if (!(rx[0]||rx[1]||rx[2]||rx[3]||ry[0]||ry[1]||ry[2]||ry[3])) {
        drawbuf.FillRect(x0, y0, x1, y1, color);
        return;
    }
    for (int y = y0; y < y1; y++) {
        int xl, xr;
        computeInnerSpanPerSide(y, x0, y0, x1, y1, rx, ry, 0, 0, 0, 0, xl, xr);
        if (xl < xr)
            drawbuf.FillRect(xl, y, xr, y+1, color);
    }
}

// Draws one rounded-rect border ring where each side can have its own paint
// width and color. Corners are mitered like a picture frame: the top/bottom
// band and the left/right band are split by a 45-degree line from the box's
// square (radius-ignoring) outer corner, clamped to *this row's* real outer.
//
// The 45-degree miter point measured from the square top-left/bottom-left
// corner (miterFromLeft) or top-right/bottom-right corner (miterFromRight),
// clamped into [lo, hi].
static inline int miterFromLeft(int x0, int vd, int lo, int hi) {
    int m = x0 + vd;
    if (m < lo) m = lo;
    if (m > hi) m = hi;
    return m;
}
static inline int miterFromRight(int x1, int vd, int lo, int hi) {
    int m = x1 - vd;
    if (m < lo) m = lo;
    if (m > hi) m = hi;
    return m;
}

// Each side takes its declared width plus a draw_* bool. The width always
// shapes this call's own corner curve/seam, as if a neighboring side had
// that width; draw_* additionally gates whether that side's own color
// actually gets painted.
static void fillRoundedRectBorder(LVDrawBuf & drawbuf, int x0, int y0, int x1, int y1,
                                   const int rx[4], const int ry[4],
                                   int w_top, bool draw_top,
                                   int w_right, bool draw_right,
                                   int w_bottom, bool draw_bottom,
                                   int w_left, bool draw_left,
                                   const lUInt32 colors[4])
{
    // Nothing to draw if no side is both drawn on this pass and nonzero width.
    if (!(draw_top && w_top > 0) && !(draw_right && w_right > 0) &&
        !(draw_bottom && w_bottom > 0) && !(draw_left && w_left > 0))
        return;

    for (int y = y0; y < y1; y++) {
        // This row's outer (unshrunk) span -- the box's own outline at y, radii included.
        int xl, xr;
        computeInnerSpanPerSide(y, x0, y0, x1, y1, rx, ry, 0, 0, 0, 0, xl, xr);
        // Degenerate row (outside the box entirely) -- nothing to draw.
        if (!(xl < xr))
            continue;

        // Is this row within top's or bottom's band, and is that side actually drawn on
        // this pass?
        bool inTop = draw_top && w_top > 0 && y < y0 + w_top;
        bool inBottom = draw_bottom && w_bottom > 0 && y >= y1 - w_bottom;
        if (inTop || inBottom) {
            // Top/bottom band row: split into left/middle/right by the diagonal miter seam.
            lUInt32 tbColor = inTop ? colors[0] : colors[2];
            int vd = inTop ? (y - y0) : (y1 - 1 - y);

            // The 45-degree miter, clamped directly to *this row's* real outer curve [xl,xr).
            int xlMiter = (w_left > 0) ? miterFromLeft(x0, vd, xl, xr) : xl;
            int xrMiter = (w_right > 0) ? miterFromRight(x1, vd, xl, xr) : xr;
            if (xrMiter < xlMiter)
                xrMiter = xlMiter;

            // The region to the left/right of the miters is painted in the side colors.
            if (draw_left && w_left > 0 && xlMiter > xl)
                drawbuf.FillRect(xl, y, xlMiter, y+1, colors[3]);
            if (xrMiter > xlMiter)
                drawbuf.FillRect(xlMiter, y, xrMiter, y+1, tbColor);
            if (draw_right && w_right > 0 && xr > xrMiter)
                drawbuf.FillRect(xrMiter, y, xr, y+1, colors[1]);
            continue;
        }

        // Middle rows: this row's inner (interior-hole) span, each side shrunk by its own width.
        int xlInterior, xrInterior;
        computeInnerSpanPerSide(y, x0, y0, x1, y1, rx, ry, w_top, w_right, w_bottom, w_left, xlInterior, xrInterior);

        // Each side reduces to three points: the outer edge (xl/xr, already
        // have it), the diagonal miter point, and the interior curve
        // (xlInterior/xrInterior). Side color paints outer-to-miter;
        // miter-to-interior is a wedge that is painted the color of whichever
        // neighbor (top or bottom) it faces. When the side this wedge would
        // otherwise miter against has no width of its own (e.g. left is
        // absent), there's no side-color sliver competing for [xl, miter) --
        // nothing else will paint it -- so the miter collapses to the outer
        // edge and the whole span becomes the neighbor's wedge, same as the
        // "no neighbor at that corner" rule the dashed/dotted arc-ownership
        // logic already applies.
        int xlMiter = xlInterior;
        lUInt32 wedgeColorL = 0;
        bool haveWedgeL = false;
        if (w_top > 0 && (y - y0) < ry[0]) {
            xlMiter = (w_left > 0) ? miterFromLeft(x0, y - y0, xl, xlInterior) : xl;
            wedgeColorL = colors[0];
            haveWedgeL = draw_top;
        } else if (w_bottom > 0 && (y1 - 1 - y) < ry[3]) {
            xlMiter = (w_left > 0) ? miterFromLeft(x0, y1 - 1 - y, xl, xlInterior) : xl;
            wedgeColorL = colors[2];
            haveWedgeL = draw_bottom;
        }

        // Same, mirrored for the right side.
        int xrMiter = xrInterior;
        lUInt32 wedgeColorR = 0;
        bool haveWedgeR = false;
        if (w_top > 0 && (y - y0) < ry[1]) {
            xrMiter = (w_right > 0) ? miterFromRight(x1, y - y0, xrInterior, xr) : xr;
            wedgeColorR = colors[0];
            haveWedgeR = draw_top;
        } else if (w_bottom > 0 && (y1 - 1 - y) < ry[2]) {
            xrMiter = (w_right > 0) ? miterFromRight(x1, y1 - 1 - y, xrInterior, xr) : xr;
            wedgeColorR = colors[2];
            haveWedgeR = draw_bottom;
        }

        // Side-color slivers: outer edge in to each side's own miter point.
        if (draw_left && w_left > 0 && xl < xlMiter)
            drawbuf.FillRect(xl, y, xlMiter, y+1, colors[3]);
        if (draw_right && w_right > 0 && xrMiter < xr)
            drawbuf.FillRect(xrMiter, y, xr, y+1, colors[1]);

        // Top/bottom's own small corner wedge: miter point in to the interior curve.
        if (haveWedgeL && xlMiter < xlInterior) {
            int a = myMax(xl, xlMiter), b = myMin(xr, xlInterior);
            if (a < b)
                drawbuf.FillRect(a, y, b, y + 1, wedgeColorL);
        }
        // Same wedge, mirrored on the right.
        if (haveWedgeR && xrInterior < xrMiter) {
            int a = myMax(xl, xrInterior), b = myMin(xr, xrMiter);
            if (a < b)
                drawbuf.FillRect(a, y, b, y + 1, wedgeColorR);
        }
    }
}

// Whether every *present* border side is one of the styles DrawBorder()'s
// general rounded-corner path covers -- rather than falling back to the legacy
// square-corner code for a dashed/dotted side.
static bool styleQualifiesForRoundedBorder(const css_style_rec_t * style) {
    for (int i=0; i<4; i++) {
        css_border_style_type_t bs = i==0 ? style->border_style_top : i==1 ? style->border_style_right :
                                      i==2 ? style->border_style_bottom : style->border_style_left;
        if (bs != css_border_dotted && bs != css_border_dashed)
            continue;
        css_length_t bc = style->border_color[i];
        lUInt32 color = bc.type != css_val_unspecified ? bc.value : style->color.value;
        if (styleBorderSidePresent(bs, style->border_width[i], color))
            return false; // present dashed/dotted side
    }
    return true;
}

// Shade/light variants of a border color, used for the 3D-look styles (inset/outset/
// groove/ridge), in both the rounded and legacy square-corner rendering paths below.
// Firefox uses fixed near-black shade/light values when the border color is real black
// (0x000000), rather than the generic darkening formula, which would otherwise collapse
// shade == light == black.
static inline lUInt32 makeBorderShade(lUInt32 c)
{
    lUInt32 o = c & 0xFF000000;
    if ((c & 0xFFFFFF) == 0)
        return o | 0x4c4c4cu;
    lUInt32 r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    r = r * 160 / 255;
    g = g * 160 / 255;
    b = b * 160 / 255;
    return o | (r << 16) | (g << 8) | b;
}
static inline lUInt32 makeBorderLight(lUInt32 c)
{
    if ((c & 0xFFFFFF) == 0)
        return (c & 0xFF000000) | 0xb2b2b2u;
    return c;
}

// Splits a DOUBLE border side's declared width into its outer line, gap and inner line
// thicknesses: each half of w gets CSS's classic double-border-line rule (a third of it,
// rounded up via the +1, or half below the >2 threshold where a third would round down to
// nothing), then outer/inner are clamped so they never overlap -- at w=1 this collapses
// the border to a single solid-looking line.
static void splitDoubleBorderWidth(int w, int &outer, int &gap, int &inner)
{
    int half = w / 2;
    outer = half / (half > 2 ? 3 : 2) + 1;
    int rem = w - half;
    inner = rem / (rem > 2 ? 3 : 2) + 1;
    if (outer > w)
        outer = w;
    if (inner > w - outer)
        inner = w - outer;
    gap = w - outer - inner;
}

// Splits a GROOVE/RIDGE border side's declared width in half for its two touching,
// oppositely-shaded rings (outer/inner) -- see the two fillRoundedRectBorder() calls
// below. At least 1px goes to the outer ring even when w is 0 or 1.
static void splitGrooveRidgeBorderWidth(int w, int &outer, int &inner)
{
    outer = myMax(1, w / 2);
    inner = myMax(0, w - outer);
}

// Draws one legacy (square-corner) border side as a stack of single-pixel DrawLine calls,
// each stepping diagonally from p1 towards p2 to miter against whatever's on the two
// perpendicular neighbor sides (rate1/rate2 are 0 when that neighbor doesn't miter against
// this side, making that edge vertical/horizontal instead of slanted).
// horizontal covers top/bottom (the stepped axis is y, offset axis is x); !horizontal
// covers right/left (stepped axis is x, offset axis is y). sign is +1 to step from p1
// towards p2 along the stepped axis (top, right), -1 to step the other way (bottom, left).
// Computes the coordinates of one scanline (step i, 0-based) of a border ring
// spanning from corner point p1 to corner point p2, stepping into the box along
// the cross axis at rate `sign` and along the along-axis at each corner's own
// rate -- shared by drawBorderStraightLine() (as a DrawLine) and, for the
// double/groove/ridge styles, drawBorderSideSquare() (as a FillRect).
static inline void borderRingStepRect(bool horizontal, int sign, lvPoint p1, lvPoint p2,
                                       double rate1, double rate2, int i,
                                       int &x0, int &y0, int &x1, int &y1)
{
    if (horizontal) {
        x0 = p1.x + i*rate1;     y0 = p1.y + sign*i;
        x1 = p2.x - i*rate2 + 1; y1 = p2.y + sign*i + 1;
    } else {
        x0 = p1.x - sign*i;      y0 = p1.y + i*rate1;
        x1 = p2.x - sign*i + 1;  y1 = p2.y - i*rate2 + 1;
    }
}

static void drawBorderStraightLine(LVDrawBuf & drawbuf, bool horizontal, int sign,
                                    lvPoint p1, lvPoint p2, double rate1, double rate2,
                                    int count, lUInt32 color, int dot, int interval)
{
    for (int i = 0; i < count; i++) {
        int x0, y0, x1, y1;
        borderRingStepRect(horizontal, sign, p1, p2, rate1, rate2, i, x0, y0, x1, y1);
        drawbuf.DrawLine(x0, y0, x1, y1, color, dot, interval, horizontal ? 0 : 1);
    }
}

// One neighboring side, as seen by drawBorderSideSquare(): whether it actually
// paints (see styleBorderSidePresent()), its style (checked for dotted/dashed),
// and its own declared width (used for the miter only when mitered).
struct BorderSideNeighbor {
    bool present;
    css_border_style_type_t style;
    int width;
};

// Draws one side of the legacy (square-corner) border -- dotted, dashed, solid,
// double, groove, ridge, inset or outset -- mitered against whichever of its two
// neighboring sides (a = the side at the "start" corner, b = at the "end"
// corner) is present, generalizing what used to be four separate, near-identical
// top/right/bottom/left blocks in DrawBorder().
//
// horizontal/sign follow borderRingStepRect()'s convention: horizontal selects
// this side's own axis (true for top/bottom, false for left/right); sign
// selects the direction this side's ring grows into the box along the cross
// axis (top/right: +1, bottom/left: -1). along0/along1 are this side's own
// along-axis extent (e.g. the box's left/right x for a horizontal side, in
// along0<along1 order); cross0 is this side's own baseline cross-axis
// coordinate (e.g. the box's top y for the top side); own_width/own_style/
// own_color describe this side itself.
//
// outer_ring_inclusive preserves a long-standing per-side asymmetry in this
// legacy code (present before this refactor, not introduced by it -- see the
// callers for what it encodes): where the groove/ridge outer/inner ring
// boundary falls. top_left_shading picks which of the two 3D-bevel shades
// (inset/outset, groove/ridge) this side uses on its "outer" half: true for
// top/left, false for right/bottom -- matching the shading already used for
// these styles in the rounded-corner path above.
static void drawBorderSideSquare(LVDrawBuf & drawbuf, bool invert_colors,
                                  bool horizontal, int sign,
                                  int along0, int along1, int cross0, int own_width,
                                  css_border_style_type_t own_style, lUInt32 own_color,
                                  const BorderSideNeighbor & a, const BorderSideNeighbor & b,
                                  bool outer_ring_inclusive, bool top_left_shading)
{
    int dot = 1, interval = 0; // default style

    lUInt32 shadecolor = makeBorderShade(own_color);
    lUInt32 lightcolor = makeBorderLight(own_color);
    if (invert_colors) {
        own_color = invertNonGrayscaleColor(own_color);
        shadecolor = invertNonGrayscaleColor(shadecolor);
        lightcolor = invertNonGrayscaleColor(lightcolor);
    }
    // The "outer" (top/left-facing) half of a groove/ridge or inset/outset
    // side uses one of these two shades, the "inner" half the other; which
    // physical color each represents just flips between top/left and
    // right/bottom -- see top_left_shading above.
    lUInt32 outerBase = top_left_shading ? shadecolor : lightcolor;
    lUInt32 innerBase = top_left_shading ? lightcolor : shadecolor;

    // A side is "mitered" against a given neighbor only when that neighbor is
    // both present and not itself dashed/dotted -- a dashed/dotted edge has
    // no continuous line to miter against, so it's always treated as a flat
    // (unmitered) corner regardless of its width. When not mitered, the
    // neighbor's width contributes nothing (0) below.
    bool aMiters = a.present && a.style != css_border_dotted && a.style != css_border_dashed;
    bool bMiters = b.present && b.style != css_border_dotted && b.style != css_border_dashed;
    int aw = aMiters ? a.width : 0;
    int bw = bMiters ? b.width : 0;
    double rateA = (double)aw / (double)own_width;
    double rateB = (double)bw / (double)own_width;

    // Maps an (along, cross) pair to a real (x, y) point given this side's
    // orientation -- the same role convention borderRingStepRect() uses.
    auto pt = [&](double along, double cross) {
        return horizontal ? lvPoint((int)along, (int)cross) : lvPoint((int)cross, (int)along);
    };
    // Own width always offsets the cross coordinate in full, in the direction
    // this side's ring grows into the box; the neighbor's (possibly zeroed
    // above) width offsets the along coordinate, towards the interior from
    // each corner -- point 1 is the literal corner, point 2 the half-width
    // point, point 3 the full-width point.
    double crossStep = horizontal ? (double)sign : -(double)sign;
    lvPoint pA1 = pt(along0,        cross0);
    lvPoint pA2 = pt(along0 + 0.5*aw, cross0 + crossStep*0.5*own_width);
    lvPoint pA3 = pt(along0 + aw,      cross0 + crossStep*own_width);
    lvPoint pB1 = pt(along1,        cross0);
    lvPoint pB2 = pt(along1 - 0.5*bw, cross0 + crossStep*0.5*own_width);
    lvPoint pB3 = pt(along1 - bw,      cross0 + crossStep*own_width);

    switch (own_style) {
        case css_border_dotted:
            dot = interval = own_width;
            drawBorderStraightLine(drawbuf, horizontal, sign, pA1, pB1, rateA, rateB, own_width, own_color, dot, interval);
            break;
        case css_border_dashed:
            dot = 3*own_width;
            interval = 3*own_width;
            drawBorderStraightLine(drawbuf, horizontal, sign, pA1, pB1, rateA, rateB, own_width, own_color, dot, interval);
            break;
        case css_border_solid:
            drawBorderStraightLine(drawbuf, horizontal, sign, pA1, pB1, rateA, rateB, own_width, own_color, dot, interval);
            break;
        case css_border_double: {
            int outerN, gapN, innerN;
            splitDoubleBorderWidth(own_width, outerN, gapN, innerN);
            for (int i = 0; i < outerN; i++) {
                int rx0, ry0, rx1, ry1;
                borderRingStepRect(horizontal, sign, pA1, pB1, rateA, rateB, i, rx0, ry0, rx1, ry1);
                drawbuf.FillRect(rx0, ry0, rx1, ry1, own_color);
            }
            // The inner ring grows the other way: outward from pA3/pB3 back
            // towards pA1/pB1, hence the negated sign and rates.
            for (int i = 0; i < innerN; i++) {
                int rx0, ry0, rx1, ry1;
                borderRingStepRect(horizontal, -sign, pA3, pB3, -rateA, -rateB, i, rx0, ry0, rx1, ry1);
                drawbuf.FillRect(rx0, ry0, rx1, ry1, own_color);
            }
            break;
        }
        case css_border_groove:
        case css_border_ridge: {
            bool isGroove = (own_style == css_border_groove);
            lUInt32 outerColor = isGroove ? outerBase : innerBase;
            lUInt32 innerColor = isGroove ? innerBase : outerBase;
            auto crossOf = [&](const lvPoint &p) { return horizontal ? p.y : p.x; };
            int halfDelta = crossOf(pA2) - crossOf(pA1);
            if (halfDelta < 0) halfDelta = -halfDelta;
            int fullDelta = crossOf(pA3) - crossOf(pA2);
            if (fullDelta < 0) fullDelta = -fullDelta;
            int outerCount = outer_ring_inclusive ? halfDelta + 1 : halfDelta;
            int innerCount = outer_ring_inclusive ? fullDelta : fullDelta + 1;
            for (int i = 0; i < outerCount; i++) {
                int rx0, ry0, rx1, ry1;
                borderRingStepRect(horizontal, sign, pA1, pB1, rateA, rateB, i, rx0, ry0, rx1, ry1);
                drawbuf.FillRect(rx0, ry0, rx1, ry1, outerColor);
            }
            for (int i = 0; i < innerCount; i++) {
                int rx0, ry0, rx1, ry1;
                borderRingStepRect(horizontal, sign, pA2, pB2, rateA, rateB, i, rx0, ry0, rx1, ry1);
                drawbuf.FillRect(rx0, ry0, rx1, ry1, innerColor);
            }
            break;
        }
        case css_border_inset:
            drawBorderStraightLine(drawbuf, horizontal, sign, pA1, pB1, rateA, rateB, own_width, outerBase, dot, interval);
            break;
        case css_border_outset:
            drawBorderStraightLine(drawbuf, horizontal, sign, pA1, pB1, rateA, rateB, own_width, innerBase, dot, interval);
            break;
        default:
            break;
    }
}

//draw border lines,support color,width,all styles, not support border-collapse
void DrawBorder(ldomNode *enode,LVDrawBuf & drawbuf,int x0,int y0,int doc_x,int doc_y,RenderRectAccessor fmt)
{
    css_style_ref_t style = enode->getStyle();
    const bool invert_colors = drawbuf.getInvertColors();

    // We have css_val_unspecified only when css_generic_currentcolor, and we should use the current text color.
    lUInt32 topBordercolor = style->border_color[0].type != css_val_unspecified ? style->border_color[0].value : style->color.value;
    lUInt32 rightBordercolor = style->border_color[1].type != css_val_unspecified ? style->border_color[1].value : style->color.value;
    lUInt32 bottomBordercolor = style->border_color[2].type != css_val_unspecified ? style->border_color[2].value : style->color.value;
    lUInt32 leftBordercolor = style->border_color[3].type != css_val_unspecified ? style->border_color[3].value : style->color.value;

    // A side is only actually drawn if its style/width/color make it "present" --
    // see styleBorderSidePresent() for the (shared) rules, e.g. a fully-transparent
    // color means we have nothing to draw on that side.
    bool hastopBorder = styleBorderSidePresent(style->border_style_top, style->border_width[0], topBordercolor);
    bool hasrightBorder = styleBorderSidePresent(style->border_style_right, style->border_width[1], rightBordercolor);
    bool hasbottomBorder = styleBorderSidePresent(style->border_style_bottom, style->border_width[2], bottomBordercolor);
    bool hasleftBorder = styleBorderSidePresent(style->border_style_left, style->border_width[3], leftBordercolor);

    if (hasbottomBorder || hasleftBorder || hasrightBorder || hastopBorder) {
        int width = 0; // values in % are invalid for borders, so we shouldn't get any
        int topBorderwidth = lengthToPx(enode, style->border_width[0],width);
        topBorderwidth = topBorderwidth!=0 ? topBorderwidth : DEFAULT_BORDER_WIDTH;
        int rightBorderwidth = lengthToPx(enode, style->border_width[1],width);
        rightBorderwidth = rightBorderwidth!=0 ? rightBorderwidth : DEFAULT_BORDER_WIDTH;
        int bottomBorderwidth = lengthToPx(enode, style->border_width[2],width);
        bottomBorderwidth = bottomBorderwidth!=0 ? bottomBorderwidth : DEFAULT_BORDER_WIDTH;
        int leftBorderwidth = lengthToPx(enode, style->border_width[3],width);
        leftBorderwidth = leftBorderwidth!=0 ? leftBorderwidth : DEFAULT_BORDER_WIDTH;
        int tbw=topBorderwidth,rbw=rightBorderwidth,bbw=bottomBorderwidth,lbw=leftBorderwidth;

        // Rounded border rendering. Any style/mix this doesn't cover (a
        // present side that's dashed or dotted) falls back to the existing
        // square-corner rendering below, even if the style also specifies a
        // border-radius.
        // A background image is also excluded here: DrawBackgroundImage()
        // always paints it as a plain rectangle, so a rounded border over it
        // would show the image's square corners poking out past the
        // border's curve.
        if (styleHasBorderRadii(style.get()) && style->background_image.empty()) {
            int rx[4]={0,0,0,0}, ry[4]={0,0,0,0};
            if (computeBorderRadiiPx(enode, style.get(), fmt.getWidth(), fmt.getHeight(), rx, ry)) {
                int X0 = x0 + doc_x;
                int Y0 = y0 + doc_y;
                int X1 = X0 + fmt.getWidth();
                int Y1 = Y0 + fmt.getHeight();

                // General per-side rounded rendering for the styles we
                // support so far: SOLID/INSET/OUTSET (mitered per-side, one
                // ring) and DOUBLE/GROOVE/RIDGE (each its own two independent
                // rings). Any mix of these six is fine, with independent
                // colors/widths per side. If any present side is dashed or
                // dotted, this box isn't covered by rounded rendering yet, so
                // the whole box falls back to the legacy square-corner code
                // below instead of painting some sides rounded and silently
                // skipping that one.
                bool side_is_solid[4] = {
                    hastopBorder && style->border_style_top == css_border_solid,
                    hasrightBorder && style->border_style_right == css_border_solid,
                    hasbottomBorder && style->border_style_bottom == css_border_solid,
                    hasleftBorder && style->border_style_left == css_border_solid};
                bool side_is_inset[4] = {
                    hastopBorder && style->border_style_top == css_border_inset,
                    hasrightBorder && style->border_style_right == css_border_inset,
                    hasbottomBorder && style->border_style_bottom == css_border_inset,
                    hasleftBorder && style->border_style_left == css_border_inset};
                bool side_is_outset[4] = {
                    hastopBorder && style->border_style_top == css_border_outset,
                    hasrightBorder && style->border_style_right == css_border_outset,
                    hasbottomBorder && style->border_style_bottom == css_border_outset,
                    hasleftBorder && style->border_style_left == css_border_outset};
                bool side_is_double[4] = {
                    hastopBorder && style->border_style_top == css_border_double,
                    hasrightBorder && style->border_style_right == css_border_double,
                    hasbottomBorder && style->border_style_bottom == css_border_double,
                    hasleftBorder && style->border_style_left == css_border_double};
                bool side_is_groove[4] = {
                    hastopBorder && style->border_style_top == css_border_groove,
                    hasrightBorder && style->border_style_right == css_border_groove,
                    hasbottomBorder && style->border_style_bottom == css_border_groove,
                    hasleftBorder && style->border_style_left == css_border_groove};
                bool side_is_ridge[4] = {
                    hastopBorder && style->border_style_top == css_border_ridge,
                    hasrightBorder && style->border_style_right == css_border_ridge,
                    hasbottomBorder && style->border_style_bottom == css_border_ridge,
                    hasleftBorder && style->border_style_left == css_border_ridge};
                bool side_coverable[4] = {
                    !hastopBorder || side_is_solid[0] || side_is_inset[0] || side_is_outset[0] || side_is_double[0] || side_is_groove[0] || side_is_ridge[0],
                    !hasrightBorder || side_is_solid[1] || side_is_inset[1] || side_is_outset[1] || side_is_double[1] || side_is_groove[1] || side_is_ridge[1],
                    !hasbottomBorder || side_is_solid[2] || side_is_inset[2] || side_is_outset[2] || side_is_double[2] || side_is_groove[2] || side_is_ridge[2],
                    !hasleftBorder || side_is_solid[3] || side_is_inset[3] || side_is_outset[3] || side_is_double[3] || side_is_groove[3] || side_is_ridge[3]};
                if (side_coverable[0] && side_coverable[1] && side_coverable[2] && side_coverable[3]) {
                    // Kept raw (un-inverted) here: invertNonGrayscaleColor() and the shade/light
                    // scaling below don't commute, so shading must happen first and inversion
                    // last, matching the legacy square-corner path's order. Each ring block below
                    // inverts its own final per-side colors right before painting.
                    lUInt32 sideColors[4] = {topBordercolor, rightBordercolor, bottomBordercolor, leftBordercolor};

                    // SOLID/INSET/OUTSET: one ring covers every side using
                    // these three styles (a side is never more than one
                    // style, so there's no cross-side interaction to resolve
                    // here). DOUBLE/GROOVE/RIDGE sides paint themselves via
                    // their own two-ring calls just below instead -- a side
                    // is only drawn here if it's actually solid/inset/outset.
                    // Every side's *true* declared width is still passed
                    // regardless of its own style, so e.g. a solid left
                    // border's corner still miters correctly against a
                    // double/groove/ridge top neighbor's real reach without
                    // this call painting into it -- see
                    // fillRoundedRectBorder's width/draw_* split.
                    {
                        lUInt32 c[4] = {sideColors[0], sideColors[1], sideColors[2], sideColors[3]};
                        // Inset: top/left shade, right/bottom light. Outset: opposite.
                        if (side_is_inset[0] || side_is_outset[0])
                            c[0] = side_is_inset[0] ? makeBorderShade(c[0]) : makeBorderLight(c[0]);
                        if (side_is_inset[1] || side_is_outset[1])
                            c[1] = side_is_inset[1] ? makeBorderLight(c[1]) : makeBorderShade(c[1]);
                        if (side_is_inset[2] || side_is_outset[2])
                            c[2] = side_is_inset[2] ? makeBorderLight(c[2]) : makeBorderShade(c[2]);
                        if (side_is_inset[3] || side_is_outset[3])
                            c[3] = side_is_inset[3] ? makeBorderShade(c[3]) : makeBorderLight(c[3]);
                        if (invert_colors) {
                            for (int i = 0; i < 4; i++)
                                c[i] = invertNonGrayscaleColor(c[i]);
                        }
                        bool draw[4] = {
                            side_is_solid[0] || side_is_inset[0] || side_is_outset[0],
                            side_is_solid[1] || side_is_inset[1] || side_is_outset[1],
                            side_is_solid[2] || side_is_inset[2] || side_is_outset[2],
                            side_is_solid[3] || side_is_inset[3] || side_is_outset[3]};
                        int mw[4] = {hastopBorder ? tbw : 0, hasrightBorder ? rbw : 0, hasbottomBorder ? bbw : 0, hasleftBorder ? lbw : 0};
                        fillRoundedRectBorder(drawbuf, X0, Y0, X1, Y1, rx, ry,
                                                  mw[0], draw[0], mw[1], draw[1], mw[2], draw[2], mw[3], draw[3], c);
                    }

                    // DOUBLE: two fully independent lines -- the outer one drawn as its
                    // own single-band ring on the real box/radii, the inner one drawn as
                    // its own single-band ring on a box shrunk by (outer+gap) per side,
                    // with each corner radius reduced by the same amount.
                    if (side_is_double[0] || side_is_double[1] || side_is_double[2] || side_is_double[3]) {
                        lUInt32 c[4] = {sideColors[0], sideColors[1], sideColors[2], sideColors[3]};
                        if (invert_colors) {
                            for (int i = 0; i < 4; i++)
                                c[i] = invertNonGrayscaleColor(c[i]);
                        }
                        int outer_t, gap_t, inner_t;
                        splitDoubleBorderWidth(tbw, outer_t, gap_t, inner_t);
                        int outer_b, gap_b, inner_b;
                        splitDoubleBorderWidth(bbw, outer_b, gap_b, inner_b);
                        int outer_r, gap_r, inner_r;
                        splitDoubleBorderWidth(rbw, outer_r, gap_r, inner_r);
                        int outer_l, gap_l, inner_l;
                        splitDoubleBorderWidth(lbw, outer_l, gap_l, inner_l);

                        // Widths: every bordered neighbor is treated as if it were
                        // itself double, split the same way from its own real
                        // declared width -- regardless of what style it actually is
                        // (e.g. solid).
                        int w_ot = hastopBorder ? outer_t : 0, w_ob = hasbottomBorder ? outer_b : 0;
                        int w_or = hasrightBorder ? outer_r : 0, w_ol = hasleftBorder ? outer_l : 0;
                        fillRoundedRectBorder(drawbuf, X0, Y0, X1, Y1, rx, ry,
                                                  w_ot, side_is_double[0], w_or, side_is_double[1],
                                                  w_ob, side_is_double[2], w_ol, side_is_double[3], c);

                        int insetT = hastopBorder ? (outer_t + gap_t) : 0, insetB = hasbottomBorder ? (outer_b + gap_b) : 0;
                        int insetR = hasrightBorder ? (outer_r + gap_r) : 0, insetL = hasleftBorder ? (outer_l + gap_l) : 0;
                        int iX0 = X0 + insetL, iY0 = Y0 + insetT, iX1 = X1 - insetR, iY1 = Y1 - insetB;
                        if (iX0 < iX1 && iY0 < iY1) {
                            int irx[4] = {myMax(0, rx[0] - insetL), myMax(0, rx[1] - insetR), myMax(0, rx[2] - insetR), myMax(0, rx[3] - insetL)};
                            int iry[4] = {myMax(0, ry[0] - insetT), myMax(0, ry[1] - insetT), myMax(0, ry[2] - insetB), myMax(0, ry[3] - insetB)};
                            int w_it = hastopBorder ? inner_t : 0, w_ib = hasbottomBorder ? inner_b : 0;
                            int w_ir = hasrightBorder ? inner_r : 0, w_il = hasleftBorder ? inner_l : 0;
                            fillRoundedRectBorder(drawbuf, iX0, iY0, iX1, iY1, irx, iry,
                                                      w_it, side_is_double[0], w_ir, side_is_double[1],
                                                      w_ib, side_is_double[2], w_il, side_is_double[3], c);
                        }
                    }

                    // GROOVE/RIDGE: same trick as DOUBLE just above -- two independent
                    // rings via fillRoundedRectBorder (each with the real, simultaneous,
                    // both-axes-at-once corner inset). Unlike DOUBLE's two lines,
                    // groove/ridge's outer and inner rings are touching (no gap: inner
                    // ring's own width is simply w-outer) and are shaded oppositely per
                    // side (dark/light swapped between outer and inner) rather than
                    // same-colored, to fake a carved-in (groove) or raised (ridge) look.
                    if (side_is_groove[0] || side_is_groove[1] || side_is_groove[2] || side_is_groove[3] ||
                        side_is_ridge[0] || side_is_ridge[1] || side_is_ridge[2] || side_is_ridge[3])
                    {
                        lUInt32 c_outer[4] = {sideColors[0], sideColors[1], sideColors[2], sideColors[3]};
                        lUInt32 c_inner[4] = {sideColors[0], sideColors[1], sideColors[2], sideColors[3]};
                        // groove: outer shade on top/left, light on bottom/right, inner
                        // half reversed; ridge: outer/inner swapped from groove.
                        if (side_is_groove[0] || side_is_ridge[0]) {
                            c_outer[0] = side_is_groove[0] ? makeBorderShade(c_outer[0]) : makeBorderLight(c_outer[0]);
                            c_inner[0] = side_is_groove[0] ? makeBorderLight(c_inner[0]) : makeBorderShade(c_inner[0]);
                        }
                        if (side_is_groove[1] || side_is_ridge[1]) {
                            c_outer[1] = side_is_groove[1] ? makeBorderLight(c_outer[1]) : makeBorderShade(c_outer[1]);
                            c_inner[1] = side_is_groove[1] ? makeBorderShade(c_inner[1]) : makeBorderLight(c_inner[1]);
                        }
                        if (side_is_groove[2] || side_is_ridge[2]) {
                            c_outer[2] = side_is_groove[2] ? makeBorderLight(c_outer[2]) : makeBorderShade(c_outer[2]);
                            c_inner[2] = side_is_groove[2] ? makeBorderShade(c_inner[2]) : makeBorderLight(c_inner[2]);
                        }
                        if (side_is_groove[3] || side_is_ridge[3]) {
                            c_outer[3] = side_is_groove[3] ? makeBorderShade(c_outer[3]) : makeBorderLight(c_outer[3]);
                            c_inner[3] = side_is_groove[3] ? makeBorderLight(c_inner[3]) : makeBorderShade(c_inner[3]);
                        }
                        if (invert_colors) {
                            for (int i = 0; i < 4; i++) {
                                c_outer[i] = invertNonGrayscaleColor(c_outer[i]);
                                c_inner[i] = invertNonGrayscaleColor(c_inner[i]);
                            }
                        }

                        // Virtual half-split computed for every bordered side, regardless
                        // of its real style: this is what a neighbor's width contributes
                        // to *this* ring's corner geometry, as if that neighbor were
                        // itself groove/ridge with its own real declared width -- same
                        // rationale as DOUBLE just above.
                        int vouter_t = 0, vinner_t = 0;
                        if (hastopBorder)
                            splitGrooveRidgeBorderWidth(tbw, vouter_t, vinner_t);
                        int vouter_b = 0, vinner_b = 0;
                        if (hasbottomBorder)
                            splitGrooveRidgeBorderWidth(bbw, vouter_b, vinner_b);
                        int vouter_r = 0, vinner_r = 0;
                        if (hasrightBorder)
                            splitGrooveRidgeBorderWidth(rbw, vouter_r, vinner_r);
                        int vouter_l = 0, vinner_l = 0;
                        if (hasleftBorder)
                            splitGrooveRidgeBorderWidth(lbw, vouter_l, vinner_l);

                        bool draw_t = side_is_groove[0] || side_is_ridge[0];
                        bool draw_r = side_is_groove[1] || side_is_ridge[1];
                        bool draw_b = side_is_groove[2] || side_is_ridge[2];
                        bool draw_l = side_is_groove[3] || side_is_ridge[3];

                        fillRoundedRectBorder(drawbuf, X0, Y0, X1, Y1, rx, ry,
                                                  vouter_t, draw_t, vouter_r, draw_r,
                                                  vouter_b, draw_b, vouter_l, draw_l, c_outer);

                        int insetT = vouter_t, insetB = vouter_b, insetR = vouter_r, insetL = vouter_l;
                        int iX0 = X0 + insetL, iY0 = Y0 + insetT, iX1 = X1 - insetR, iY1 = Y1 - insetB;
                        if (iX0 < iX1 && iY0 < iY1) {
                            int irx[4] = {myMax(0, rx[0] - insetL), myMax(0, rx[1] - insetR), myMax(0, rx[2] - insetR), myMax(0, rx[3] - insetL)};
                            int iry[4] = {myMax(0, ry[0] - insetT), myMax(0, ry[1] - insetT), myMax(0, ry[2] - insetB), myMax(0, ry[3] - insetB)};
                            fillRoundedRectBorder(drawbuf, iX0, iY0, iX1, iY1, irx, iry,
                                                      vinner_t, draw_t, vinner_r, draw_r,
                                                      vinner_b, draw_b, vinner_l, draw_l, c_inner);
                        }
                    }

                    return;
                }
            }
        }

        // Own axis/sign follow drawBorderSideSquare()'s convention (see its
        // comment): horizontal picks this side's axis, sign picks which way
        // its ring grows into the box. a/b are this side's two neighbors, in
        // along-axis order (left-then-right for top/bottom, top-then-bottom
        // for left/right). outer_ring_inclusive preserves a pre-existing
        // per-side asymmetry in this legacy code -- see drawBorderSideSquare()'s
        // comment for what it means.
        if (hastopBorder) {
            drawBorderSideSquare(drawbuf, invert_colors, true, 1,
                                  x0+doc_x, x0+doc_x+fmt.getWidth()-1, doc_y+y0, tbw,
                                  style->border_style_top, topBordercolor,
                                  BorderSideNeighbor{hasleftBorder, style->border_style_left, lbw},
                                  BorderSideNeighbor{hasrightBorder, style->border_style_right, rbw},
                                  /*outer_ring_inclusive=*/true, /*top_left_shading=*/true);
        }
        if (hasrightBorder) {
            drawBorderSideSquare(drawbuf, invert_colors, false, 1,
                                  doc_y+y0, doc_y+y0+fmt.getHeight()-1, x0+doc_x+fmt.getWidth()-1, rbw,
                                  style->border_style_right, rightBordercolor,
                                  BorderSideNeighbor{hastopBorder, style->border_style_top, tbw},
                                  BorderSideNeighbor{hasbottomBorder, style->border_style_bottom, bbw},
                                  /*outer_ring_inclusive=*/false, /*top_left_shading=*/false);
        }
        if (hasbottomBorder) {
            drawBorderSideSquare(drawbuf, invert_colors, true, -1,
                                  x0+doc_x, x0+doc_x+fmt.getWidth()-1, doc_y+y0+fmt.getHeight()-1, bbw,
                                  style->border_style_bottom, bottomBordercolor,
                                  BorderSideNeighbor{hasleftBorder, style->border_style_left, lbw},
                                  BorderSideNeighbor{hasrightBorder, style->border_style_right, rbw},
                                  /*outer_ring_inclusive=*/true, /*top_left_shading=*/false);
        }
        if (hasleftBorder) {
            drawBorderSideSquare(drawbuf, invert_colors, false, -1,
                                  doc_y+y0, doc_y+y0+fmt.getHeight()-1, x0+doc_x, lbw,
                                  style->border_style_left, leftBordercolor,
                                  BorderSideNeighbor{hastopBorder, style->border_style_top, tbw},
                                  BorderSideNeighbor{hasbottomBorder, style->border_style_bottom, bbw},
                                  /*outer_ring_inclusive=*/true, /*top_left_shading=*/true);
        }
    }
}

// Fills enode's background-color rect (its border box), rounding the corners to
// match its border-radius when the box either has no border (nothing else will
// round it) or has one that DrawBorder() will itself paint as a rounded ring.
// Any other border is still drawn square by DrawBorder(), so rounding the fill
// underneath it would leave a square-cornered border with mismatched round
// background peeking out.
void FillBackgroundRect(LVDrawBuf & drawbuf, ldomNode * enode, css_style_ref_t style, RenderRectAccessor fmt, int abs_x0, int abs_y0, lUInt32 bg_color)
{
    int x1 = abs_x0 + fmt.getWidth();
    int y1 = abs_y0 + fmt.getHeight();
    int rx[4]={0,0,0,0}, ry[4]={0,0,0,0};
    bool has_rounded_bg = false;
    if ( styleHasBorderRadii(style.get()) && style->background_image.empty() ) {
        bool no_border = !styleHasAnyBorder(style.get());
        bool rounded_border = no_border || styleQualifiesForRoundedBorder(style.get());
        if (rounded_border)
            has_rounded_bg = computeBorderRadiiPx(enode, style.get(), fmt.getWidth(), fmt.getHeight(), rx, ry);
    }
    if (has_rounded_bg)
        fillRoundedRect(drawbuf, abs_x0, abs_y0, x1, y1, rx, ry, bg_color);
    else
        drawbuf.FillRect(abs_x0, abs_y0, x1, y1, bg_color);
}

void DrawBackgroundImage(ldomNode *enode,LVDrawBuf & drawbuf,int x0,int y0,int doc_x,int doc_y, int width, int height, bool clip_to_target)
{
    // The caller passes us the node's border box (fmt.getWidth()/getHeight()), for
    // background-color's default background-clip: border-box. But background-position/-size
    // resolve against the padding box (the default background-origin), so inset by the
    // border width below -- see https://www.w3.org/TR/css-backgrounds-3/#the-background-origin
    css_style_ref_t style=enode->getStyle();
    if (!style->background_image.empty()) {
        int leftBorderwidth = measureBorder(enode, 3);
        int topBorderwidth = measureBorder(enode, 0);
        int rightBorderwidth = measureBorder(enode, 1);
        int bottomBorderwidth = measureBorder(enode, 2);
        if (leftBorderwidth || topBorderwidth || rightBorderwidth || bottomBorderwidth) {
            x0 += leftBorderwidth;
            y0 += topBorderwidth;
            width -= leftBorderwidth + rightBorderwidth;
            height -= topBorderwidth + bottomBorderwidth;
        }
        lString32 filepath = lString32(style->background_image.c_str());
        LVImageSourceRef img = enode->getParentNode()->getDocument()->getObjectImageSource(filepath);
        if (img.isNull()) { // filepath may be url-encoded
            img = enode->getParentNode()->getDocument()->getObjectImageSource(DecodeHTMLUrlString(filepath));
        }
        if (!img.isNull() && width > 0 && height > 0) {
            // Raw, undecoded-transform pixel size of the image file
            int native_img_w = img->GetWidth();
            int native_img_h = img->GetHeight();
            // Native image size, scaled according to gRenderDPI like getStyledImageSize()
            // does for <img> elements.
            int img_w = scaleForRenderDPI(native_img_w);
            int img_h = scaleForRenderDPI(native_img_h);

            // See if background-size specified and we need to adjust image native size
            // (if both auto, use image native size)
            css_length_t bg_w = style->background_size[0];
            css_length_t bg_h = style->background_size[1];
            if ( bg_w.type != css_val_unspecified || bg_w.value != css_generic_auto ||
                 bg_h.type != css_val_unspecified || bg_h.value != css_generic_auto ) {
                int new_w = 0;
                int new_h = 0;
                // Use the (already border-inset) padding box as the basis for percentage
                // sizes and for cover/contain scaling.
                int container_w = width;
                int container_h = height;
                bool check_lengths = true;
                if ( bg_w.type == css_val_unspecified && bg_h.type == css_val_unspecified ) {
                    if ( bg_w.value == css_generic_contain && bg_h.value == css_generic_contain ) {
                        // Image should be fully contained in container (no crop)
                        int scale_w = 1024 * container_w / img_w;
                        int scale_h = 1024 * container_h / img_h;
                        if ( scale_w < scale_h ) {
                            new_w = container_w;
                            new_h = img_h * scale_w / 1024;
                        }
                        else {
                            new_h = container_h;
                            new_w = img_w * scale_h / 1024;
                        }
                        check_lengths = false;
                    }
                    else if ( bg_w.value == css_generic_cover && bg_h.value == css_generic_cover ) {
                        // Image should fully cover container (crop allowed)
                        int scale_w = 1024 * container_w / img_w;
                        int scale_h = 1024 * container_h / img_h;
                        if ( scale_w > scale_h ) {
                            new_w = container_w;
                            new_h = img_h * scale_w / 1024;
                        }
                        else {
                            new_h = container_h;
                            new_w = img_w * scale_h / 1024;
                        }
                        check_lengths = false;
                    }
                }
                if ( check_lengths ) {
                    // These will compute to 0 if (css_val_unspecified, css_generic_auto) when really not specified
                    new_w = lengthToPx(enode, style->background_size[0], container_w);
                    new_h = lengthToPx(enode, style->background_size[1], container_h);
                    if ( new_w == 0 ) {
                        if ( new_h == 0 ) { // keep image native size
                            new_h = img_h;
                            new_w = img_w;
                        }
                        else { // use style height, keep aspect ratio
                            new_w = img_w * new_h / img_h;
                        }
                    }
                    else if ( new_h == 0 ) { // use style width, keep aspect ratio
                        new_h = new_w * img_h / img_w;
                    }
                }
                if ( new_w == 0 || new_h == 0 ) {
                    // width or height computed to 0: nothing to draw
                    return;
                }
                img_w = new_w;
                img_h = new_h;
            }
            // Resize the decoded image to img_w x img_h if that doesn't match its
            // native pixel size, whether because of background-size, of gRenderDPI
            // scaling, or both (img_w/img_h above already account for either).
            // Honor the same "Image Scaling" (smooth vs nearest-neighbor) setting
            // used for normal <img> elements, so background-image scaling looks
            // consistent with the rest of the page.
            if ( img_w != native_img_w || img_h != native_img_h ) {
                img = LVCreateStretchFilledTransform(img, img_w, img_h, IMG_TRANSFORM_STRETCH, IMG_TRANSFORM_STRETCH, 0, 0,
                                                      drawbuf.getSmoothScalingImages());
            }

            // We can use some crengine facilities for background repetition and position,
            // which has the advantage that img will be decoded once even if tiling it many
            // times and if the target is many screen-heights long (like <BODY> could be).
            // Unfortunaly, it does not everything well when not using IMG_TRANSFORM_TILE,
            // as it would fill the not-drawn part of the target buffer with garbage,
            // instead of letting it as is.
            ImageTransform hori_transform = IMG_TRANSFORM_NONE;
            ImageTransform vert_transform = IMG_TRANSFORM_NONE;
            int transform_w = img_w;
            int transform_h = img_h;
            switch (style->background_repeat) {
                case css_background_no_repeat:
                case css_background_repeat_y:
                    break;
                case css_background_repeat_x:
                case css_background_repeat:
                default:
                    // No need to tile if image is larger than target
                    if ( width > img_w ) {
                        hori_transform = IMG_TRANSFORM_TILE;
                        transform_w = width;
                    }
                    break;
            }
            switch (style->background_repeat) {
                case css_background_no_repeat:
                case css_background_repeat_x:
                    break;
                case css_background_repeat_y:
                case css_background_repeat:
                default:
                    // No need to tile if image is larger than target
                    if ( height > img_h ) {
                        vert_transform = IMG_TRANSFORM_TILE;
                        transform_h = height;
                    }
                    break;
            }
            // Compute the position where to draw top left of image, as if
            // it was a single image when no-repeat.
            // Per spec, a <percentage> position is relative to the difference
            // between the container and (possibly background-size resized)
            // image sizes, while a <length> is a plain absolute offset.
            css_length_t bg_pos_x = style->background_position[0];
            css_length_t bg_pos_y = style->background_position[1];
            int draw_x;
            if ( bg_pos_x.type == css_val_percent )
                draw_x = (width - img_w) * bg_pos_x.value / (100 * 256);
            else
                draw_x = lengthToPx(enode, bg_pos_x, width);
            int draw_y;
            if ( bg_pos_y.type == css_val_percent )
                draw_y = (height - img_h) * bg_pos_y.value / (100 * 256);
            else
                draw_y = lengthToPx(enode, bg_pos_y, height);
            // If tiling, we need to adjust the transform x/y (the offset
            // in img, so, a value between 0 and img_w/h) to the point
            // inside image that should be at top left of target area
            int transform_x = 0;
            int transform_y = 0;
            if ( hori_transform == IMG_TRANSFORM_TILE && draw_x ) {
                transform_x = (draw_x % img_w);
                draw_x = 0;
            }
            if ( vert_transform == IMG_TRANSFORM_TILE && draw_y ) {
                // Strangely, using the following instead of what we did for x/w
                // gives the expected result (not investigated, might be
                // a bug in LVStretchImgSource::OnLineDecoded() )
                transform_y = img_h - (draw_y % img_h);
                draw_y = 0;
            }
            // Ready to have crengine do all the work.
            /* Looks like we don't need that:

                // (Inspired from LVDocView::drawPageBackground(),
                // we have to do it the complex way to avoid memory leaks
                LVRef<LVColorDrawBuf> buf = LVRef<LVColorDrawBuf>( new LVColorDrawBuf(img_w, img_h, 32) );
                buf->Draw(img, 0, 0, img_w, img_h, false); // (dither=false doesn't matter with a color buffer)
                LVImageSourceRef src = LVCreateDrawBufImageSource(buf.get(), false);
                LVImageSourceRef transformed = LVCreateStretchFilledTransform(src, transform_w, transform_h,

              We can just transform the original image, which will work in its original
              colorspace/depth, ensure alpha/transparency, and will be converted only
              at the end to the final drawbuf bit depth.
            */
            LVImageSourceRef transformed = LVCreateStretchFilledTransform(img, transform_w, transform_h,
                                               hori_transform, vert_transform, transform_x, transform_y);
            // We use the DrawBuf clip facility to ensure we don't draw outside this node fmt
            lvRect orig_clip;
            if (clip_to_target) {
                drawbuf.GetClipRect( &orig_clip ); // Backup the original one
                // Set a new one to the target area
                lvRect target_clip = lvRect(x0+doc_x, y0+doc_y, x0+doc_x+width, y0+doc_y+height);;
                // But don't overflow page top and bottom, in case target spans multiple pages
                if ( target_clip.top < orig_clip.top )
                    target_clip.top = orig_clip.top;
                if ( target_clip.bottom > orig_clip.bottom )
                    target_clip.bottom = orig_clip.bottom;
                drawbuf.SetClipRect( &target_clip );
            }
            // Draw
            drawbuf.Draw(transformed, x0+doc_x+draw_x, y0+doc_y+draw_y, transform_w, transform_h);
            if (clip_to_target) {
                drawbuf.SetClipRect( &orig_clip ); // Restore the original one
            }
        }
    }
}

void DrawBodyBackground( LVDrawBuf & drawbuf, bool draw_bg_color, bool draw_bg_image, ldomNode * enode, int x0, int y0, int dx, int dy, int doc_x, int doc_y)
{
    // https://www.w3.org/TR/CSS2/colors.html#background
    // <body> background does not obey margin rules, and it is to be drawn
    // instead on the whole canvas/viewport.
    // This is rather complex with EPUBs and DocFragment based documents,
    // as there are multiple BODYs that are usually split on new pages,
    // but could also meet on a page.
    // We don't draw on the fmt width, but on the drawbuf width.
    // Also, when in page mode, we'd rather have a fully fixed background,
    // (so, not respecting background-repeat and background-position)
    // to avoid ghosting and refreshes issues on eInk.
    // We try to do this right when there are multiple <BODY>, with possibly
    // different background colors/images, in the viewed page. This is a bit
    // harder to do right when in 2-pages mode, which can have a few issues.

    // We can draw on the whole buffer or clip area, unless some previous
    // or next body restrict these
    int bg_top = 0;
    int bg_bottom = drawbuf.GetHeight();
    int bg_left = 0;
    int bg_right = drawbuf.GetWidth();

    // Use the specific body background clip so the background is drawn
    // on the full canvas even on pages with shorter text.
    lvRect curclip;
    drawbuf.GetClipRect( &curclip );
    draw_extra_info_t * draw_extra_info = (draw_extra_info_t*)drawbuf.GetDrawExtraInfo();
    if ( draw_extra_info ) {
        // Set body background clip (we get one if in page mode)
        drawbuf.SetClipRect( &draw_extra_info->body_background_clip );
        // If there is a header or we are in 2-pages mode, the clip would ensure
        // we don't draw over them. But we want to position the drawing
        // adequately so the background-position can be ensured;
        // just use the provided clip as the area to paint
        bg_top = draw_extra_info->body_background_clip.top;
        bg_bottom = draw_extra_info->body_background_clip.bottom;
        bg_left = draw_extra_info->body_background_clip.left;
        bg_right = draw_extra_info->body_background_clip.right;
    }

    // If the current body we're dealing starts on ends inside this page/screen,
    // it does not necessarily mean there is a previous or next body that ends
    // or starts inside this page/screen: we may have inter body margins, or
    // some initial top margin above the first body.
    // We need to check there is really none to be able to draw on the whole buffer.
    const bool no_visible_previous_body = doc_y <= 0; // This body started before page top
    if ( !no_visible_previous_body ) {
        // Find previous body if any, to see if would have some part in this page
        // We expect either sibling BODY (FB2) or sibling DocFragement>BODY (EPUB)
        ldomNode * prevBody = NULL;
        ldomNode * n;
        n = enode->getUnboxedPrevSibling(true);
        if ( n && n->getNodeId() == el_body ) {
            prevBody = n;
        }
        else {
            n = enode->getUnboxedParent();
            if ( n && n->getNodeId() == el_DocFragment ) {
                n = n->getUnboxedPrevSibling(true);
                if ( n && n->getNodeId() == el_DocFragment ) {
                    n = n->getUnboxedLastChild(true);
                    if ( n && n->getNodeId() == el_body ) {
                        prevBody = n;
                    }
                }
            }
        }
        if ( prevBody ) {
            // Make out the doc_y this prev body would have
            lvRect prevrect;
            prevBody->getAbsRect(prevrect);
            lvRect thisrect;
            enode->getAbsRect(thisrect);
            int prev_bottom_doc_y = doc_y - thisrect.top + prevrect.bottom;
            if ( prev_bottom_doc_y > 0 ) {
                // Previous body does not end before this page: there may be unused
                // space between this prev body bottom and this body top, caused by
                // collapsed body top/bottom margins.
                // Make the boundary between backgrounds at the middle of this (round up)
                bg_top = y0 + doc_y - (thisrect.top - prevrect.bottom)/2;
            }
        }
    }
    // Same checks as above, but for a next body below this one
    RenderRectAccessor fmt( enode );
    const bool no_visible_next_body = doc_y + fmt.getHeight() >= dy; // this body ends after page bottom
    if ( !no_visible_next_body ) {
        // Find next body
        ldomNode * nextBody = NULL;
        ldomNode * n;
        n = enode->getUnboxedNextSibling(true);
        if ( n && n->getNodeId() == el_body ) {
            nextBody = n;
        }
        else {
            n = enode->getUnboxedParent();
            if ( n && n->getNodeId() == el_DocFragment ) {
                n = n->getUnboxedNextSibling(true);
                if ( n && n->getNodeId() == el_DocFragment ) {
                    n = n->getUnboxedLastChild(true); // body can be preceded by <styleSheet>
                    if ( n && n->getNodeId() == el_body ) {
                        nextBody = n;
                    }
                }
            }
        }
        if ( nextBody ) {
            lvRect nextrect;
            nextBody->getAbsRect(nextrect);
            lvRect thisrect;
            enode->getAbsRect(thisrect);
            int next_top_doc_y = doc_y - thisrect.top + nextrect.top;
            if ( next_top_doc_y < dy ) {
                // Next body starts on this page: There may be unused space
                // between this next body top and this body bottom, caused by
                // collapsed body top/bottom margins.
                // Make the boundary between backgrounds at the middle of this (round down)
                bg_bottom = y0 + doc_y + fmt.getHeight() + (nextrect.top - thisrect.bottom + 1)/2;
            }
        }
    }

    if ( draw_bg_color ) {
        css_style_ref_t style = enode->getStyle();
        // If not css_val_color, it must be (css_val_unspecified, css_generic_currentcolor)
        lUInt32 bg_color = style->background_color.type == css_val_color ? style->background_color.value : style->color.value;
        bg_color = drawbuf.getInvertColors() ? invertNonGrayscaleColor(bg_color) : bg_color;
        drawbuf.FillRect(bg_left, bg_top, bg_right, bg_bottom, bg_color);
    }
    if ( draw_bg_image ) {
        // We will provide clip_to_target=false to DrawBackgroundImage() for it to not
        // limit the clip to the body boundaries, which could give unexpected results
        // and is tricky to visualize how it would behave in all cases... It's easier
        // to just adjust the clip to limit the painted area.
        lvRect clip;
        drawbuf.GetClipRect( &clip );
        // We got either the orig fullscreen clip in scroll mode, or body_background_clip
        // in page mode, which have proper clip left and right.
        if ( clip.top < bg_top )
            clip.top = bg_top;
        if ( clip.bottom > bg_bottom )
            clip.bottom = bg_bottom;
        drawbuf.SetClipRect(&clip);
        // We provide x=0 w=screen width so that even if the clip crops out half of
        // this width, we get the background image positionned (background-position)
        // and repeated (backgroud-repeat) the same way whether we're drawing the
        // left of the right page: that way, drawings will coincide and look like a
        // single full page drawing, instead of having a cut in the middle.
        // We provide y=bg_top, so that when the body starts in the middle of the
        // page, the image have its top where it starts, as this might matter for
        // some images.
        DrawBackgroundImage(enode, drawbuf, 0, bg_top, 0, 0, drawbuf.GetWidth(), drawbuf.GetHeight()-bg_top, false);
    }

    drawbuf.SetClipRect(&curclip); // restore clip
}
