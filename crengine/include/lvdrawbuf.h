/** \file lvdrawbuf.h
    \brief Drawing buffer, gray bitmap buffer

    CoolReader Engine

    (c) Vadim Lopatin, 2000-2006
    This source code is distributed under the terms of
    GNU General Public License.

    See LICENSE file for details.

*/

#ifndef __LVDRAWBUF_H_INCLUDED__
#define __LVDRAWBUF_H_INCLUDED__

#include "crsetup.h"

#if !defined(__SYMBIAN32__) && defined(_WIN32) && !defined(QT_GL)
extern "C" {
#include <windows.h>
}
#elif __SYMBIAN32__
#include <e32base.h>
#include <w32std.h>
#endif

#include "lvtypes.h"
#include "lvimg.h"

enum cr_rotate_angle_t {
    CR_ROTATE_ANGLE_0 = 0,
    CR_ROTATE_ANGLE_90,
    CR_ROTATE_ANGLE_180,
    CR_ROTATE_ANGLE_270
};

class LVFont;
class GLDrawBuf; // workaround for no-rtti builds

/// Abstract drawing buffer
class LVDrawBuf : public CacheableObject
{
public:
    // GL draw buffer support
    /// GL draw buffer compatibility - requires this call before any drawing
    virtual void beforeDrawing() {}
    /// GL draw buffer compatibility - requires this call after any drawing
    virtual void afterDrawing() {}

    // tiles support
    /// returns true if drawing buffer is tiled
    virtual bool isTiled() const { return false; }
    /// returns tile width (or just width if no tiles)
    virtual int tileWidth() const { return GetWidth(); }
    /// returns tile height (or just height if no tiles)
    virtual int tileHeight() const { return GetHeight(); }
    /// returns tile drawbuf for tiled image, returns this for non tiled draw buffer
    virtual LVDrawBuf * getTile(int x, int y) {
        CR_UNUSED2(x, y);
        return this;
    }
    /// returns number of tiles in row
    virtual int getXtiles() const {
        return 1;
    }
    /// returns number of tiles in column
    virtual int getYtiles() const {
        return 1;
    }

    /// returns tile rectangle
    virtual void getTileRect(lvRect & rc, int x, int y) const {
        CR_UNUSED2(x, y);
        rc.left = rc.top = 0;
        rc.right = GetWidth();
        rc.bottom = GetHeight();
    }

    /// rotates buffer contents by specified angle
    virtual void Rotate( cr_rotate_angle_t angle ) = 0;
    /// returns white pixel value
    virtual lUInt32 GetWhiteColor() const = 0;
    /// returns black pixel value
    virtual lUInt32 GetBlackColor() const = 0;
    /// returns current background color
    virtual lUInt32 GetBackgroundColor() const = 0;
    /// sets current background color
    virtual void SetBackgroundColor( lUInt32 cl ) = 0;
    /// returns current text color
    virtual lUInt32 GetTextColor() const = 0;
    /// sets current text color
    virtual void SetTextColor( lUInt32 cl ) = 0;
    /// gets clip rect
    virtual void GetClipRect( lvRect * clipRect ) const = 0;
    /// sets clip rect
    virtual void SetClipRect( const lvRect * clipRect ) = 0;
    /// gets draw extra info object
    virtual void * GetDrawExtraInfo() = 0;
    /// sets draw extra info object
    virtual void SetDrawExtraInfo( void * ) = 0;
    /// wants to be fed hidden content (only LVInkMeasurementDrawBuf may return true)
    virtual bool WantsHiddenContent() const { return false; }
    /// set to true for drawing in Paged mode, false for Scroll mode
    virtual void setHidePartialGlyphs( bool hide ) = 0;
    /// set to true to invert images only (so they get inverted back to normal by nightmode)
    virtual void setInvertImages( bool invert ) = 0;
    virtual bool getInvertImages() const = 0;
    /// set to true to enforce dithering (only relevant for 8bpp Gray drawBuf)
    virtual void setDitherImages( bool dither ) = 0;
    /// set to true to switch to a more costly smooth scaler instead of nearest neighbor
    virtual void setSmoothScalingImages( bool smooth ) = 0;
    /// invert image
    virtual void Invert() = 0;
    /// get buffer width, pixels
    virtual int GetWidth() const = 0;
    /// get buffer height, pixels
    virtual int GetHeight() const = 0;
    /// get buffer bits per pixel
    virtual int GetBitsPerPixel() const = 0;
    /// get row size (bytes)
    virtual int GetRowSize() const = 0;
    /// fills buffer with specified color
    virtual void Clear( lUInt32 color ) = 0;
    /// get pixel value
    virtual lUInt32 GetPixel( int x, int y ) const = 0;
    /// get average pixel value for area (coordinates are fixed floating points *16)
    virtual lUInt32 GetAvgColor(lvRect & rc16) const = 0;
    /// get linearly interpolated pixel value (coordinates are fixed floating points *16)
    virtual lUInt32 GetInterpolatedColor(int x16, int y16) const = 0;
    /// draw gradient filled rectangle with colors for top-left, top-right, bottom-right, bottom-left
    virtual void GradientRect(int x0, int y0, int x1, int y1, lUInt32 color1, lUInt32 color2, lUInt32 color3, lUInt32 color4) {
        CR_UNUSED8(x0, x1, y0, y1, color1, color2, color3, color4);
    }
    /// fills rectangle with specified color
    virtual void FillRect( int x0, int y0, int x1, int y1, lUInt32 color ) = 0;
    /// draw frame
    inline void DrawFrame(const lvRect & rc, lUInt32 color, int width = 1)
    {
        FillRect( rc.left, rc.top, rc.right, rc.top + width, color );
        FillRect( rc.left, rc.bottom - width, rc.right, rc.bottom, color );
        FillRect( rc.left, rc.top + width, rc.left + width, rc.bottom - width, color );
        FillRect( rc.right - width, rc.top + width, rc.right, rc.bottom - width, color );
    }
    /// fills rectangle with specified color
    inline void FillRect( const lvRect & rc, lUInt32 color )
    {
        FillRect( rc.left, rc.top, rc.right, rc.bottom, color );
    }
    /// draws rectangle with specified color
    inline void Rect( int x0, int y0, int x1, int y1, lUInt32 color )
    {
        FillRect( x0, y0, x1-1, y0+1, color );
        FillRect( x0, y0, x0+1, y1-1, color );
        FillRect( x1-1, y0, x1, y1, color );
        FillRect( x0, y1-1, x1, y1, color );
    }
    /// draws rectangle with specified width and color
    inline void Rect( int x0, int y0, int x1, int y1, int borderWidth, lUInt32 color )
    {
        FillRect( x0, y0, x1-1, y0+borderWidth, color );
        FillRect( x0, y0, x0+borderWidth, y1-1, color );
        FillRect( x1-borderWidth, y0, x1, y1, color );
        FillRect( x0, y1-borderWidth, x1, y1, color );
    }
    /// draws rounded rectangle with specified line width, rounding radius, and color
    void RoundRect( int x0, int y0, int x1, int y1, int borderWidth, int radius, lUInt32 color, int cornerFlags=0x0F  );
    /// draws rectangle with specified color
    inline void Rect( const lvRect & rc, lUInt32 color )
    {
        Rect( rc.left, rc.top, rc.right, rc.bottom, color );
    }
    /// draws rectangle with specified color
    inline void Rect( const lvRect & rc, int borderWidth, lUInt32 color )
    {
        Rect( rc.left, rc.top, rc.right, rc.bottom, borderWidth, color );
    }
    /// fills rectangle with pattern
    virtual void FillRectPattern( int x0, int y0, int x1, int y1, lUInt32 color0, lUInt32 color1, const lUInt8 * __restrict pattern ) = 0;
    /// inverts image in specified rectangle
    virtual void InvertRect(int x0, int y0, int x1, int y1) = 0;
    /// sets new size
    virtual void Resize( int dx, int dy ) = 0;
    /// draws bitmap (1 byte per pixel) using specified palette
    virtual void Draw( int x, int y, const lUInt8 * bitmap, int width, int height, const lUInt32 * __restrict palette ) = 0;
    /// draws image
    virtual void Draw( LVImageSourceRef img, int x, int y, int width, int height, bool dither=true ) = 0;
    /// draws part of source image, possible rescaled
    virtual void Draw( LVImageSourceRef img, int x, int y, int width, int height, int srcx, int srcy, int srcwidth, int srcheight, bool dither=true ) { CR_UNUSED10(img, x, y, width, height, srcx, srcy, srcwidth, srcheight, dither); }
    /// for GL buf only - rotated drawing
    virtual void DrawRotated( LVImageSourceRef img, int x, int y, int width, int height, int rotationAngle) { Draw(img, x, y, width, height); CR_UNUSED(rotationAngle); }
    /// draws buffer content to another buffer doing color conversion if necessary
    virtual void DrawTo( LVDrawBuf * __restrict buf, int x, int y, int options, const lUInt32 * __restrict palette ) = 0;
    // draws buffer on top of another buffer to implement background
    virtual void DrawOnTop( LVDrawBuf * __restrict buf, int x, int y) = 0;
    /// draws rescaled buffer content to another buffer doing color conversion if necessary
    virtual void DrawRescaled(const LVDrawBuf * __restrict src, int x, int y, int dx, int dy, int options) = 0;
    /// draws rescaled buffer content to another buffer doing color conversion if necessary
    virtual void DrawFragment(const LVDrawBuf * __restrict src, int srcx, int srcy, int srcdx, int srcdy, int x, int y, int dx, int dy, int options) {
        CR_UNUSED10(src, srcx, srcy, srcdx, srcdy, x, y, dx, dy, options);
    }
    /// draw lines
    virtual void DrawLine(int x0, int y0, int x1, int y1, lUInt32 color0, int length1, int length2, int direction) = 0;
#if !defined(__SYMBIAN32__) && defined(_WIN32) && !defined(QT_GL)
    /// draws buffer content to another buffer doing color conversion if necessary
    virtual void DrawTo( HDC dc, int x, int y, int options, const lUInt32 * __restrict palette ) = 0;
#endif
    /// draws text string
    /*
    virtual void DrawTextString( int x, int y, LVFont * pfont,
                       const lChar32 * text, int len,
                       lChar32 def_char, lUInt32 * palette, bool addHyphen=false ) = 0;
    */

/*
    /// draws formatted text
    virtual void DrawFormattedText( formatted_text_fragment_t * text, int x, int y ) = 0;
*/
    /// returns scanline pointer
    virtual lUInt8 * GetScanLine( int y ) const = 0;


    virtual int getAlpha() const { return 0; }
    virtual void setAlpha(int alpha) { CR_UNUSED(alpha); }
    virtual lUInt32 applyAlpha(lUInt32 cl) { return cl; }

    /// virtual destructor
    virtual ~LVDrawBuf() { }
    virtual GLDrawBuf * asGLDrawBuf() { return NULL; }
};

/// LVDrawBufferBase
class LVBaseDrawBuf : public LVDrawBuf
{
protected:
    int _dx;
    int _dy;
    int _rowsize;
    lvRect _clip;
    unsigned char * _data;
    void * _drawExtraInfo;
    lUInt32 _backgroundColor;
    lUInt32 _textColor;
    bool _hidePartialGlyphs;
    bool _invertImages;
    bool _ditherImages;
    bool _smoothImages;
    int _drawnImagesCount;
    int _drawnImagesSurface;
public:
    /// set to true for drawing in Paged mode, false for Scroll mode
    virtual void setHidePartialGlyphs( bool hide ) { _hidePartialGlyphs = hide; }
    /// set to true to invert images only (so they get inverted back to normal by nightmode)
    virtual void setInvertImages( bool invert ) { _invertImages = invert; }
    virtual bool getInvertImages() const { return _invertImages; }
    /// set to true to enforce dithering (only relevant for 8bpp Gray drawBuf)
    virtual void setDitherImages( bool dither ) { _ditherImages = dither; }
    /// set to true to switch to a more costly smooth scaler instead of nearest neighbor
    virtual void setSmoothScalingImages( bool smooth ) { _smoothImages = smooth; }
    /// returns current background color
    virtual lUInt32 GetBackgroundColor() const { return _backgroundColor; }
    /// sets current background color
    virtual void SetBackgroundColor( lUInt32 cl ) { _backgroundColor=cl; }
    /// returns current text color
    virtual lUInt32 GetTextColor() const { return _textColor; }
    /// sets current text color
    virtual void SetTextColor( lUInt32 cl ) { _textColor = cl; }
    /// gets clip rect
    virtual void GetClipRect( lvRect * clipRect ) const { *clipRect = _clip; }
    /// sets clip rect
    virtual void SetClipRect( const lvRect * clipRect );
    /// gets draw extra info oject
    virtual void * GetDrawExtraInfo() { return _drawExtraInfo; }
    /// sets draw extra info oject
    virtual void SetDrawExtraInfo( void * draw_extra_info ) { _drawExtraInfo = draw_extra_info; }
    /// get average pixel value for area (coordinates are fixed floating points *16)
    virtual lUInt32 GetAvgColor(lvRect & rc16) const;
    /// get linearly interpolated pixel value (coordinates are fixed floating points *16)
    virtual lUInt32 GetInterpolatedColor(int x16, int y16) const;
    /// get buffer width, pixels
    virtual int GetWidth() const;
    /// get buffer height, pixels
    virtual int GetHeight() const;
    /// get row size (bytes)
    virtual int GetRowSize() const { return _rowsize; }
    virtual void DrawLine(int x0, int y0, int x1, int y1, lUInt32 color0, int length1, int length2, int direction) = 0;
    /// draws text string
    /*
    virtual void DrawTextString( int x, int y, LVFont * pfont,
                       const lChar32 * text, int len,
                       lChar32 def_char,
                       lUInt32 * palette, bool addHyphen=false );
    */
    /// draws formatted text
    //virtual void DrawFormattedText( formatted_text_fragment_t * text, int x, int y );

    /// Get nb of images drawn on buffer
    int getDrawnImagesCount() const { return _drawnImagesCount; }
    /// Get surface of images drawn on buffer
    int getDrawnImagesSurface() const { return _drawnImagesSurface; }

    LVBaseDrawBuf() : _dx(0), _dy(0), _rowsize(0), _data(NULL), _drawExtraInfo(NULL), _hidePartialGlyphs(true),
                        _invertImages(false), _ditherImages(false), _smoothImages(false),
                        _drawnImagesCount(0), _drawnImagesSurface(0) { }
    virtual ~LVBaseDrawBuf() { }
};

/// use to simplify saving draw buffer state
class LVDrawStateSaver
{
    LVDrawBuf & _buf;
    lUInt32 _textColor;
    lUInt32 _backgroundColor;
    int _alpha;
    lvRect _clipRect;
	LVDrawStateSaver & operator = (LVDrawStateSaver &) {
		// no assignment
        return *this;
	}
public:
    /// save settings
    LVDrawStateSaver( LVDrawBuf & buf )
    : _buf( buf )
    , _textColor( buf.GetTextColor() )
    , _backgroundColor( buf.GetBackgroundColor() )
    , _alpha(buf.getAlpha())
    {
        _buf.GetClipRect( &_clipRect );
    }
    void restore()
    {
        _buf.SetTextColor( _textColor );
        _buf.SetBackgroundColor( _backgroundColor );
        _buf.setAlpha(_alpha);
        _buf.SetClipRect( &_clipRect );
    }
    /// restore settings on destroy
    ~LVDrawStateSaver()
    {
        restore();
    }
};

#define SAVE_DRAW_STATE( buf ) LVDrawStateSaver drawBufSaver( buf )

enum DrawBufPixelFormat
{
    DRAW_BUF_1_BPP = 1, /// 1 bpp, 8 pixels per byte packed
    DRAW_BUF_2_BPP = 2, /// 2 bpp, 4 pixels per byte packed
    DRAW_BUF_3_BPP = 3, /// 3 bpp, 1 pixel per byte, higher 3 bits are significant
    DRAW_BUF_4_BPP = 4, /// 4 bpp, 1 pixel per byte, higher 4 bits are significant
    DRAW_BUF_8_BPP = 8, /// 8 bpp, 1 pixel per byte, all 8 bits are significant
    DRAW_BUF_16_BPP = 16, /// color 16bit RGB 565
    DRAW_BUF_32_BPP = 32  /// color 32bit RGB 888
};

/**
 * 2-bit gray bitmap buffer, partial support for 1-bit buffer
 * Supported pixel formats for LVGrayDrawBuf :
 *    1 bpp, 8 pixels per byte packed
 *    2 bpp, 4 pixels per byte packed
 *    3 bpp, 1 pixel per byte, higher 3 bits are significant
 *    4 bpp, 1 pixel per byte, higher 4 bits are significant
 *    8 bpp, 1 pixel per byte, all 8 bits are significant
 *
 */
class LVGrayDrawBuf : public LVBaseDrawBuf
{
private:
    int _bpp;
    bool _ownData;
public:
    /// rotates buffer contents by specified angle
    virtual void Rotate( cr_rotate_angle_t angle );
    /// returns white pixel value
    virtual lUInt32 GetWhiteColor() const;
    /// returns black pixel value
    virtual lUInt32 GetBlackColor() const;
    /// draws buffer content to another buffer doing color conversion if necessary
    virtual void DrawTo( LVDrawBuf * __restrict buf, int x, int y, int options, const lUInt32 * __restrict palette );
    // draws buffer on top of another buffer to implement background
    virtual void DrawOnTop( LVDrawBuf * __restrict buf, int x, int y);
    /// draws rescaled buffer content to another buffer doing color conversion if necessary
    virtual void DrawRescaled(const LVDrawBuf * __restrict src, int x, int y, int dx, int dy, int options);
#if !defined(__SYMBIAN32__) && defined(_WIN32) && !defined(QT_GL)
    /// draws buffer content to another buffer doing color conversion if necessary
    virtual void DrawTo( HDC dc, int x, int y, int options, const lUInt32 * __restrict palette );
#endif
    /// invert image
    virtual void Invert();
    /// get buffer bits per pixel
    virtual int GetBitsPerPixel() const;
    /// returns scanline pointer
    virtual lUInt8 * GetScanLine( int y ) const;
    /// fills buffer with specified color
    virtual void Clear( lUInt32 color );
    /// get pixel value
    virtual lUInt32 GetPixel( int x, int y ) const;
    /// fills rectangle with specified color
    virtual void FillRect( int x0, int y0, int x1, int y1, lUInt32 color );
    /// inverts image in specified rectangle
    virtual void InvertRect( int x0, int y0, int x1, int y1 );
    /// fills rectangle with pattern
    virtual void FillRectPattern( int x0, int y0, int x1, int y1, lUInt32 color0, lUInt32 color1, const lUInt8 * __restrict pattern );
    /// sets new size
    virtual void Resize( int dx, int dy );
    /// draws image
    virtual void Draw( LVImageSourceRef img, int x, int y, int width, int height, bool dither );
    /// draws bitmap (1 byte per pixel) using specified palette
    virtual void Draw( int x, int y, const lUInt8 * bitmap, int width, int height, const lUInt32 * __restrict palette );
    /// constructor
    LVGrayDrawBuf(int dx, int dy, int bpp=2, void * auxdata = NULL );
    /// destructor
    virtual ~LVGrayDrawBuf();
    /// convert to 1-bit bitmap
    void ConvertToBitmap(bool flgDither);
    virtual void DrawLine(int x0, int y0, int x1, int y1, lUInt32 color0, int length1, int length2, int direction=0);
};

inline lUInt32 RevRGB( lUInt32 cl ) {
    return ((cl<<16)&0xFF0000) | ((cl>>16)&0x0000FF) | (cl&0x00FF00);
}

inline lUInt32 rgb565to888( lUInt32 cl ) {
    return ((cl & 0xF800)<<8) | ((cl & 0x07E0)<<5) | ((cl & 0x001F)<<3);
}

inline lUInt16 rgb888to565( lUInt32 cl ) {
    return (lUInt16)(((cl>>8)& 0xF800) | ((cl>>5 )& 0x07E0) | ((cl>>3 )& 0x001F));
}

// Combine two colors
lUInt32 combineColors( lUInt32 foreColor, lUInt32 backColor );

#define DIV255(V)                                                                                        \
({                                                                                                       \
	auto _v = (V) + 128;                                                                             \
	(((_v >> 8U) + _v) >> 8U);                                                                       \
})

// Because of course we're not using <stdint.h> -_-".
#ifndef UINT8_MAX
	#define UINT8_MAX (255U)
#endif

// Quantize an 8-bit color value down to a palette of 16 evenly spaced colors, using an ordered 8x8 dithering pattern.
// With a grayscale input, this happens to match the eInk palette perfectly ;).
// If the input is not grayscale, and the output fb is not grayscale either,
// this usually still happens to match the eInk palette after the EPDC's own quantization pass.
// c.f., https://en.wikipedia.org/wiki/Ordered_dithering
// & https://github.com/ImageMagick/ImageMagick/blob/ecfeac404e75f304004f0566557848c53030bad6/MagickCore/threshold.c#L1627
// NOTE: As the references imply, this is straight from ImageMagick,
//       with only minor simplifications to enforce Q8 & avoid fp maths.
static inline lUInt8 dither_o8x8(int x, int y, lUInt8 v)
{
	// c.f., https://github.com/ImageMagick/ImageMagick/blob/ecfeac404e75f304004f0566557848c53030bad6/config/thresholds.xml#L107
	static const lUInt8 threshold_map_o8x8[] = { 1,  49, 13, 61, 4,  52, 16, 64, 33, 17, 45, 29, 36, 20, 48, 32,
						      9,  57, 5,  53, 12, 60, 8,  56, 41, 25, 37, 21, 44, 28, 40, 24,
						      3,  51, 15, 63, 2,  50, 14, 62, 35, 19, 47, 31, 34, 18, 46, 30,
						      11, 59, 7,  55, 10, 58, 6,  54, 43, 27, 39, 23, 42, 26, 38, 22 };

	// Constants:
	// Quantum = 8; Levels = 16; map Divisor = 65
	// QuantumRange = 0xFF
	// QuantumScale = 1.0 / QuantumRange
	//
	// threshold = QuantumScale * v * ((L-1) * (D-1) + 1)
	// NOTE: The initial computation of t (specifically, what we pass to DIV255) would overflow an uint8_t.
	//       With a Q8 input value, we're at no risk of ever underflowing, so, keep to unsigned maths.
	//       Technically, an uint16_t would be wide enough, but it gains us nothing,
	//       and requires a few explicit casts to make GCC happy ;).
	lUInt32 t = DIV255(v * ((15U << 6) + 1U));
	// level = t / (D-1);
	const lUInt32 l = (t >> 6U);
	// t -= l * (D-1);
	t = (t - (l << 6U));

	// map width & height = 8
	// c = ClampToQuantum((l+(t >= map[(x % mw) + mw * (y % mh)])) * QuantumRange / (L-1));
	const lUInt32 q = ((l + (t >= threshold_map_o8x8[(x & 7U) + 8U * (y & 7U)])) * 17U);
	// NOTE: We're doing unsigned maths, so, clamping is basically MIN(q, UINT8_MAX) ;).
	//       The only overflow we should ever catch should be for a few white (v = 0xFF) input pixels
	//       that get shifted to the next step (i.e., q = 272 (0xFF + 17)).
	return (q > UINT8_MAX ? UINT8_MAX : static_cast<lUInt8>(q));
}

// Declare our bit of scaler ripped from Qt5...
namespace CRe {
lUInt8* qSmoothScaleImage(const lUInt8* __restrict src, int sw, int sh, bool ignore_alpha, int dw, int dh);
}

/// 32-bit RGB buffer
class LVColorDrawBuf : public LVBaseDrawBuf
{
private:
#if !defined(__SYMBIAN32__) && defined(_WIN32) && !defined(QT_GL)
    HDC _drawdc;
    HBITMAP _drawbmp;
#endif
    int _bpp;
    bool _ownData;
public:
    /// rotates buffer contents by specified angle
    virtual void Rotate( cr_rotate_angle_t angle );
    /// returns white pixel value
    virtual lUInt32 GetWhiteColor() const;
    /// returns black pixel value
    virtual lUInt32 GetBlackColor() const;
    /// draws buffer content to another buffer doing color conversion if necessary
    virtual void DrawTo( LVDrawBuf * __restrict buf, int x, int y, int options, const lUInt32 * __restrict palette );
    // draws buffer on top of another buffer to implement background
    virtual void DrawOnTop( LVDrawBuf * __restrict buf, int x, int y);
    /// draws rescaled buffer content to another buffer doing color conversion if necessary
    virtual void DrawRescaled(const LVDrawBuf * __restrict src, int x, int y, int dx, int dy, int options);
#if !defined(__SYMBIAN32__) && defined(_WIN32) && !defined(QT_GL)
    /// draws buffer content to another buffer doing color conversion if necessary
    virtual void DrawTo( HDC dc, int x, int y, int options, const lUInt32 * __restrict palette );
#endif
    /// invert image
    virtual void Invert();
    /// get buffer bits per pixel
    virtual int GetBitsPerPixel() const;
    /// fills buffer with specified color
    virtual void Clear( lUInt32 color );
    /// get pixel value
    virtual lUInt32 GetPixel( int x, int y ) const;
    /// fills rectangle with specified color
    virtual void FillRect( int x0, int y0, int x1, int y1, lUInt32 color );
    /// fills rectangle with pattern
    virtual void FillRectPattern( int x0, int y0, int x1, int y1, lUInt32 color0, lUInt32 color1, const lUInt8 * __restrict pattern );
    /// inverts specified rectangle
	virtual void InvertRect( int x0, int y0, int x1, int y1 );
    /// sets new size
    virtual void Resize( int dx, int dy );
    /// draws image
    virtual void Draw( LVImageSourceRef img, int x, int y, int width, int height, bool dither );
    /// draws bitmap (1 byte per pixel) using specified palette
    virtual void Draw( int x, int y, const lUInt8 * bitmap, int width, int height, const lUInt32 * __restrict palette );
    /// returns scanline pointer
    virtual lUInt8 * GetScanLine( int y ) const;

    /// create own draw buffer
    LVColorDrawBuf(int dx, int dy, int bpp=32);
    /// creates wrapper around external RGBA buffer
    LVColorDrawBuf(int dx, int dy, lUInt8 * externalBuffer, int bpp=32 );
    /// destructor
    virtual ~LVColorDrawBuf();
    /// convert to 1-bit bitmap
    void ConvertToBitmap(bool flgDither);
    /// draw line
    virtual void DrawLine(int x0, int y0, int x1, int y1, lUInt32 color0, int length1=1, int length2=0, int direction=0);
#if !defined(__SYMBIAN32__) && defined(_WIN32) && !defined(QT_GL)
    /// returns device context for bitmap buffer
    HDC GetDC() const { return _drawdc; }
#endif
};

/// Ink measurement buffer
class LVInkMeasurementDrawBuf : public LVBaseDrawBuf
{
private:
    int ink_top_y;
    int ink_bottom_y;
    int ink_left_x;
    int ink_right_x;
    bool has_ink;
    bool measure_hidden_content;
    bool ignore_decorations; // ignore borders and background
public:
    /// get buffer bits per pixel
    virtual int  GetBitsPerPixel() const { return 8; }

    /// wants to be fed hidden content (only LVInkMeasurementDrawBuf may return true)
    virtual bool WantsHiddenContent() const { return measure_hidden_content; }

    /// fills buffer with specified color
    virtual void Clear( lUInt32 color ) {
        has_ink = false;
    }

    /// fills rectangle with specified color
    void updateInkBounds( int x0, int y0, int x1, int y1 ) {
        if ( has_ink ) {
            if ( x0 < ink_left_x ) ink_left_x = x0;
            if ( x1 < ink_left_x ) ink_left_x = x1;
            if ( x1 > ink_right_x ) ink_right_x = x1;
            if ( x0 > ink_right_x ) ink_right_x = x0;
            if ( y0 < ink_top_y ) ink_top_y = y0;
            if ( y1 < ink_top_y ) ink_top_y = y1;
            if ( y1 > ink_bottom_y ) ink_bottom_y = y1;
            if ( y0 > ink_bottom_y ) ink_bottom_y = y0;
        }
        else {
            ink_left_x = x0 < x1 ? x0 : x1;
            ink_right_x = x0 > x1 ? x0 : x1;
            ink_top_y = y0 < y1 ? y0 : y1;
            ink_bottom_y = y0 > y1 ? y0 : y1;
            has_ink = true;
        }
    }
    /// fills rectangle with specified color
    virtual void FillRect( int x0, int y0, int x1, int y1, lUInt32 color ) {
        if ( ignore_decorations )
            return;
        // printf("  ink FillRect %d %d %d %d\n", x0, y0, x1, y1);
        // Don't do this check, as backgroundcolor may not be initialized
        // (initializing it to white would be a random choice):
        // if ( color != GetBackgroundColor() )
        updateInkBounds(x0, y0, x1, y1);
    }
    virtual void FillRectPattern( int x0, int y0, int x1, int y1, lUInt32 color0, lUInt32 color1, const lUInt8 * __restrict pattern ) {
        if ( ignore_decorations )
            return;
        FillRect( x0, y0, x1, y1, color0);
    }
    /// draws image
    virtual void Draw( LVImageSourceRef img, int x, int y, int width, int height, bool dither ) {
        // An image (even if empty) sets the ink area
        // printf("  ink Draw image %d %d %d %d\n", x, y, width, height);
        updateInkBounds(x, y, x+width, y+height);
    }
    /// draws bitmap (1 byte per pixel) using specified palette
    virtual void Draw( int x, int y, const lUInt8 * bitmap, int width, int height, const lUInt32 * __restrict palette ) {
        // printf("  ink Draw %d %d %d %d\n", x, y, width, height);
        // Used to draw glyph. Trust the font that the bitmap is the glyph
        // bounding box ("blackbox" in cre), so its ink area
        updateInkBounds(x, y, x+width, y+height);
    }
    /// draw line
    virtual void DrawLine( int x0, int y0, int x1, int y1, lUInt32 color0, int length1=1, int length2=0, int direction=0 ) {
        if ( ignore_decorations )
            return;
        // printf("  ink DrawLine %d %d %d %d\n", x0, y0, x1, y1);
        updateInkBounds(x0, y0, x1, y1);
    }

    virtual bool getInkArea( lvRect &rect ) {
        if ( has_ink ) {
            rect.top = ink_top_y;
            rect.bottom = ink_bottom_y;
            rect.left = ink_left_x;
            rect.right = ink_right_x;
            return true;
        }
        return false;
    }

    // Drawing code might request a clip, but we don't want to impose any.
    // So, have a large dynamic one around the ink area met until then
    virtual void GetClipRect( lvRect * clipRect ) const {
        clipRect->top = ink_top_y - 1000;
        clipRect->bottom = ink_bottom_y + 1000;
        clipRect->left = ink_left_x - 1000;
        clipRect->right = ink_right_x + 1000;
    }

    /// create own draw buffer
    explicit LVInkMeasurementDrawBuf( bool measurehiddencontent=false, bool ignoredecorations=false)
        : ink_top_y(0), ink_bottom_y(0), ink_left_x(0), ink_right_x(0) , has_ink(false)
        , measure_hidden_content(measurehiddencontent) , ignore_decorations(ignoredecorations)
        {}
    /// destructor
    virtual ~LVInkMeasurementDrawBuf() {}

    // Unused methods in the context of lvrend that we need to have defined
    virtual void Rotate( cr_rotate_angle_t angle ) {}
    virtual lUInt32 GetWhiteColor() const { return 0; }
    virtual lUInt32 GetBlackColor() const { return 0; }
    virtual void DrawTo( LVDrawBuf * __restrict buf, int x, int y, int options, const lUInt32 * __restrict palette ) {}
    virtual void DrawOnTop( LVDrawBuf * __restrict buf, int x, int y) {}
    virtual void DrawRescaled(const LVDrawBuf * __restrict src, int x, int y, int dx, int dy, int options) {}
#if !defined(__SYMBIAN32__) && defined(_WIN32) && !defined(QT_GL)
    virtual void DrawTo( HDC dc, int x, int y, int options, const lUInt32 * __restrict palette ) {}
#endif
    virtual void Invert() {}
    virtual lUInt32 GetPixel( int x, int y ) const { return 0; }
    virtual void InvertRect( int x0, int y0, int x1, int y1 ) {}
    virtual void Resize( int dx, int dy ) {}
    virtual lUInt8 * GetScanLine( int y ) const { return 0; }
};

// This is to be used as the buffer provided to font->DrawTextString(). We based it
// on LVInkMeasurementDrawBuf just so that we don't have to redefine all the methods,
// even if none of them will be used (FillRect might be called when drawing underlines,
// but we're explicitely not handling it).
class LVHorizontalOverlapMeasurementDrawBuf : public LVInkMeasurementDrawBuf
{
private:
    bool drawing_right;
    bool by_line;
    lUInt8  min_opacity;
    int  buf_height;
    int  vertical_spread;
    int  whole_left_max_x;
    int  whole_right_min_x;
    int * left_max_x;
    int * right_min_x;
public:
    virtual void Draw( int x0, int y0, const lUInt8 * bitmap, int width, int height, const lUInt32 * __restrict palette ) {
        if ( width == 0 || height == 0)
            return;
        int y1 = y0 + height;
        int x1 = x0 + width;
        if (drawing_right) {
            for ( int y=y0; y<y1; y++ ) {
                if ( y >= 0 && y < buf_height ) {
                    int * const bucket = by_line ? &right_min_x[y] : &whole_right_min_x;
                    // Drawing a right word glyph: we want to catch its left edge:
                    // scan from the left to limit the amount of loops
                    const lUInt8 * __restrict tmp = bitmap + (y-y0)*width;
                    for ( int x=x0; x<x1; x++ ) {
                        if (*tmp >= min_opacity) { // (0 = blank pixel)
                            if ( by_line && vertical_spread > 0 ) {
                                for (int i=1; i<=vertical_spread; i++) {
                                    if (y+i < buf_height && right_min_x[y+i] > x)
                                        right_min_x[y+i] = x;
                                    if (y-i >= 0 && right_min_x[y-i] > x)
                                        right_min_x[y-i] = x;
                                }
                            }
                            if (*bucket > x) {
                                *bucket = x;
                                break; // No need to scan more of this line
                            }
                        }
                        tmp++;
                    }
                }
            }
        }
        else {
            for ( int y=y0; y<y1; y++ ) {
                if ( y >= 0 && y < buf_height ) {
                    int * const bucket = by_line ? &left_max_x[y] : &whole_left_max_x;
                    // Drawing a left word glyph: we want to catch its right edge:
                    // scan from the right to limit the amount of loops
                    const lUInt8 * __restrict tmp = bitmap + (y-y0+1)*width - 1;
                    for ( int x=x1-1; x>=x0; x-- ) {
                        if (*tmp >= min_opacity) {
                            if ( by_line && vertical_spread > 0 ) {
                                for (int i=1; i<=vertical_spread; i++) {
                                    if (y+i < buf_height && left_max_x[y+i] < x)
                                        left_max_x[y+i] = x;
                                    if (y-i >= 0 && left_max_x[y-i] < x)
                                        left_max_x[y-i] = x;
                                }
                            }
                            if (*bucket < x) {
                                *bucket = x;
                                break; // No need to scan more of this line
                            }
                        }
                        tmp--;
                    }
                }
            }
        }
    }
    int getDistance() {
        int min_distance = 0x7FFFFFFF;
        if (by_line) {
            for (int i=0; i<buf_height; i++) {
                // if right_min_x = left_max_x, they overlap, so this -1
                int distance = right_min_x[i] - left_max_x[i] - 1;
                if (min_distance > distance) {
                    min_distance = distance;
                }
            }
        }
        else {
            min_distance = whole_right_min_x - whole_left_max_x;
        }
        return min_distance;
    }
    void DrawingRight(bool right=true) {
        drawing_right = right;
    }
    /// create own draw buffer
    explicit LVHorizontalOverlapMeasurementDrawBuf( int h, bool byline, int vertspread=0, lUInt8 minopacity=1 )
        : LVInkMeasurementDrawBuf(false, false), drawing_right(false), by_line(byline)
        , min_opacity(minopacity), buf_height(h), vertical_spread(vertspread)
        {
            if ( by_line ) {
                left_max_x = (int*)malloc( sizeof(int) * buf_height );
                right_min_x = (int*)malloc( sizeof(int) * buf_height );
                for (int i=0; i<buf_height; i++) {
                    left_max_x[i] = - 0x0FFFFFFF; // -infinity
                    right_min_x[i] = 0x0FFFFFFF;  // +infinity
                }
            }
            else {
                whole_left_max_x = - 0x0FFFFFFF;
                whole_right_min_x = 0x0FFFFFFF;
            }
        }
    /// destructor
    virtual ~LVHorizontalOverlapMeasurementDrawBuf() {
        if ( by_line ) {
            free(left_max_x);
            free(right_min_x);
        }
    }
};

#endif

