/*******************************************************

   CoolReader Engine

   lvdrawbuf.cpp:  Gray bitmap buffer class

   (c) Vadim Lopatin, 2000-2006
   This source code is distributed under the terms of
   GNU General Public License

   See LICENSE file for details

*******************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../include/lvdrawbuf.h"

#define GUARD_BYTE 0xa5
#define CHECK_GUARD_BYTE \
	{ \
        if (_bpp != 1 && _bpp != 2 && _bpp !=3 && _bpp != 4 && _bpp != 8 && _bpp != 16 && _bpp != 32) crFatalError(-5, "wrong bpp"); \
        if (_ownData && _data && _data[_rowsize * _dy] != GUARD_BYTE) crFatalError(-5, "corrupted bitmap buffer"); \
    }

void LVDrawBuf::RoundRect( int x0, int y0, int x1, int y1, int borderWidth, int radius, lUInt32 color, int cornerFlags )
{
    FillRect( x0 + ((cornerFlags&1)?radius:0), y0, x1-1-((cornerFlags&2)?radius:0), y0+borderWidth, color );
    FillRect( x0, y0 + ((cornerFlags&1)?radius:0), x0+borderWidth, y1-1-((cornerFlags&4)?radius:0), color );
    FillRect( x1-borderWidth, y0 + ((cornerFlags&2)?radius:0), x1, y1-((cornerFlags&8)?radius:0), color );
    FillRect( x0 + ((cornerFlags&4)?radius:0), y1-borderWidth, x1-((cornerFlags&8)?radius:0), y1, color );
    // TODO: draw rounded corners
}

// NOTE: For more accurate (but slightly more costly) conversions, see:
//       stb does (lUInt8) (((r*77) + (g*150) + (b*29)) >> 8) (That's roughly the Rec601Luma algo)
//       Qt5 does (lUInt8) (((r*11) + (g*16) + (b*5)) >> 5) (That's closer to Rec601Luminance or Rec709Luminance IIRC)
static lUInt32 rgbToGray( lUInt32 color )
{
    const lUInt32 r = (0xFF0000 & color) >> 16;
    const lUInt32 g = (0x00FF00 & color) >> 8;
    const lUInt32 b = (0x0000FF & color) >> 0;
    return ((r + g + g + b)>>2) & 0xFF;
}

static lUInt8 rgbToGray( lUInt32 color, int bpp )
{
    const lUInt32 r = (0xFF0000 & color) >> 16;
    const lUInt32 g = (0x00FF00 & color) >> 8;
    const lUInt32 b = (0x0000FF & color) >> 0;
    return (lUInt8)(((r + g + g + b)>>2) & (((1<<bpp)-1)<<(8-bpp)));
}

#if 0

static lUInt16 rgb565(int r, int g, int b) {
    // rrrr rggg gggb bbbb
    return (lUInt16)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
}

#endif

static lUInt8 rgbToGrayMask( lUInt32 color, int bpp )
{
    switch ( bpp ) {
    case DRAW_BUF_1_BPP:
        color = rgbToGray(color) >> 7;
        color = (color&1) ? 0xFF : 0x00;
        break;
    case DRAW_BUF_2_BPP:
        color = rgbToGray(color) >> 6;
        color &= 3;
        color |= (color << 2) | (color << 4) | (color << 6);
        break;
    case DRAW_BUF_3_BPP: // 8 colors
    case DRAW_BUF_4_BPP: // 16 colors
    case DRAW_BUF_8_BPP: // 256 colors
        // return 8 bit as is
        color = rgbToGray(color);
        color &= ((1<<bpp)-1)<<(8-bpp);
        return (lUInt8)color;
    default:
        color = rgbToGray(color);
        return (lUInt8)color;
    }
    return (lUInt8)color;
}

static inline void ApplyAlphaRGB( lUInt32 &dst, lUInt32 src, lUInt8 alpha )
{
    if ( alpha == 0 ) {
        dst = src;
    } else if ( alpha < 255 ) {
        src &= 0xFFFFFF;
        const lUInt8 opaque = alpha ^ 0xFF;
        const lUInt32 n1 = (((dst & 0xFF00FF) * alpha + (src & 0xFF00FF) * opaque) >> 8) & 0xFF00FF;
        const lUInt32 n2 = (((dst & 0x00FF00) * alpha + (src & 0x00FF00) * opaque) >> 8) & 0x00FF00;
        dst = n1 | n2;
    }
}

static void ApplyAlphaRGB565( lUInt16 &dst, lUInt16 src, lUInt8 alpha )
{
    if ( alpha==0 ) {
        dst = src;
    } else if ( alpha < 255 ) {
        const lUInt8 opaque = alpha ^ 0xFF;
        const lUInt32 r = (((dst & 0xF800) * alpha + (src & 0xF800) * opaque) >> 8) & 0xF800;
        const lUInt32 g = (((dst & 0x07E0) * alpha + (src & 0x07E0) * opaque) >> 8) & 0x07E0;
        const lUInt32 b = (((dst & 0x001F) * alpha + (src & 0x001F) * opaque) >> 8) & 0x001F;
        dst = (lUInt16)(r | g | b);
    }
}

static void ApplyAlphaGray( lUInt8 &dst, lUInt8 src, lUInt8 alpha, int bpp )
{
    if ( alpha==0 ) {
        dst = src;
    } else if ( alpha < 255 ) {
        const int mask = ((1 << bpp) - 1) << (8 - bpp);
        src &= mask;
        const lUInt8 opaque = alpha ^ 0xFF;
        const lUInt8 n1 = ((dst * alpha + src * opaque) >> 8) & mask;
        dst = n1;
    }
}

static inline void ApplyAlphaGray8( lUInt8 &dst, lUInt8 src, lUInt8 alpha, lUInt8 opaque )
{
    if ( alpha==0 ) {
        dst = src;
    } else if ( alpha < 255 ) {
        const lUInt8 v = ((dst * alpha + src * opaque) >> 8);
        dst = v;
    }
}

// Combine two colors
// (Shortcuts if foreColor or backColor are fully transparent or fully opaque could be taken,
// see lvrend.cpp's setNodeStyle() and background_color; but keeping this function pure so
// we can check it gives the same result as these shortcuts.)
lUInt32 combineColors( lUInt32 foreColor, lUInt32 backColor ) {
    // Thanks to https://ciechanow.ski/alpha-compositing/ to make all that clearer,
    // especially sections "Combining Alphas" and "Combining Colors", from which
    // we take some wording for the following comments.
    lUInt8 falpha =  foreColor >> 24; // wrongly named "alpha" all around crengine, it's actually the transparency
    lUInt8 fopacity = falpha ^ 0xFF;
    lUInt8 balpha = backColor >> 24;
    lUInt8 bopacity = balpha ^ 0xFF;
    // The resulting transparency of two objects combined is equal to their product
    lUInt8 ralpha = DIV255(falpha*balpha);
    lUInt8 ropacity = ralpha ^ 0xFF;
    lUInt32 rcolor;
    if (ropacity == 0 ) {
        rcolor = 0xFF000000; // fully transparent
    }
    else {
        // foreColor's light contribution to the final color is its value * its opacity
        // backColor's light contribution to the final color is its value * its opacity, but some
        // of that light is blocked by the foremost object opacity (so the '* falpha')
        // The total light contribution is the sum of these "pre-multiplied-alpha" values.
        // But as these final values will be '* opacity' when later blended, we need
        // to un-pre-multiply them to get straigth-alpha (so the '/ ropacity').
        lUInt8 r = ( ((foreColor & 0xFF0000)>>16) * fopacity + DIV255( ((backColor & 0xFF0000)>>16) * bopacity * falpha ) ) / ropacity;
        lUInt8 g = ( ((foreColor & 0x00FF00)>>8 ) * fopacity + DIV255( ((backColor & 0x00FF00)>>8 ) * bopacity * falpha ) ) / ropacity;
        lUInt8 b = ( ((foreColor & 0x0000FF)    ) * fopacity + DIV255( ((backColor & 0x0000FF)    ) * bopacity * falpha ) ) / ropacity;
        rcolor = (ralpha<<24) | (r<<16) | (g<<8) | b;
    }
    return rcolor;
}

//static const short dither_2bpp_4x4[] = {
//    5, 13,  8,  16,
//    9,  1,  12,  4,
//    7, 15,  6,  14,
//    11, 3,  10,  2,
//};

static const short dither_2bpp_8x8[] = {
0, 32, 12, 44, 2, 34, 14, 46,
48, 16, 60, 28, 50, 18, 62, 30,
8, 40, 4, 36, 10, 42, 6, 38,
56, 24, 52, 20, 58, 26, 54, 22,
3, 35, 15, 47, 1, 33, 13, 45,
51, 19, 63, 31, 49, 17, 61, 29,
11, 43, 7, 39, 9, 41, 5, 37,
59, 27, 55, 23, 57, 25, 53, 21,
};

// returns byte with higher significant bits, lower bits are 0
lUInt32 DitherNBitColor( lUInt32 color, lUInt32 x, lUInt32 y, int bits )
{
    const int mask = ((1<<bits)-1)<<(8-bits);
    // gray = (r + 2*g + b)>>2
    //int cl = ((((color>>16) & 255) + ((color>>(8-1)) & (255<<1)) + ((color) & 255)) >> 2) & 255;
    int cl = ((((color>>16) & 255) + ((color>>(8-1)) & (255<<1)) + ((color) & 255)) >> 2) & 255;
    const int white = (1<<bits) - 1;
    const int precision = white;
    if (cl<precision)
        return 0;
    else if (cl>=255-precision)
        return mask;
    //int d = dither_2bpp_4x4[(x&3) | ( (y&3) << 2 )] - 1;
    // dither = 0..63
    const int d = dither_2bpp_8x8[(x&7) | ( (y&7) << 3 )] - 1;
    const int shift = bits-2;
    cl = ( (cl<<shift) + d - 32 ) >> shift;
    if ( cl>255 )
        cl = 255;
    if ( cl<0 )
        cl = 0;
    return cl & mask;
}

lUInt32 Dither2BitColor( lUInt32 color, lUInt32 x, lUInt32 y )
{
    int cl = ((((color>>16) & 255) + ((color>>8) & 255) + ((color) & 255)) * (256/3)) >> 8;
    if (cl<5)
        return 0;
    else if (cl>=250)
        return 3;
    //int d = dither_2bpp_4x4[(x&3) | ( (y&3) << 2 )] - 1;
    const int d = dither_2bpp_8x8[(x&7) | ( (y&7) << 3 )] - 1;

    cl = ( cl + d - 32 );
    if (cl<5)
        return 0;
    else if (cl>=250)
        return 3;
    return (cl >> 6) & 3;
}

lUInt32 Dither1BitColor( lUInt32 color, lUInt32 x, lUInt32 y )
{
    int cl = ((((color>>16) & 255) + ((color>>8) & 255) + ((color) & 255)) * (256/3)) >> 8;
    if (cl<16)
        return 0;
    else if (cl>=240)
        return 1;
    //int d = dither_2bpp_4x4[(x&3) | ( (y&3) << 2 )] - 1;
    const int d = dither_2bpp_8x8[(x&7) | ( (y&7) << 3 )] - 1;

    cl = ( cl + d - 32 );
    if (cl<5)
        return 0;
    else if (cl>=250)
        return 1;
    return (cl >> 7) & 1;
}

static lUInt8 revByteBits1( lUInt8 b )
{
    return ( (b&1)<<7 )
        |  ( (b&2)<<5 )
        |  ( (b&4)<<3 )
        |  ( (b&8)<<1 )
        |  ( (b&16)>>1 )
        |  ( (b&32)>>3 )
        |  ( (b&64)>>4 )
        |  ( (b&128)>>5 );
}

lUInt8 revByteBits2( lUInt8 b )
{
    return ( (b&0x03)<<6 )
        |  ( (b&0x0C)<<2 )
        |  ( (b&0x30)>>2 )
        |  ( (b&0xC0)>>6 );
}

/// rotates buffer contents by specified angle
void LVGrayDrawBuf::Rotate( cr_rotate_angle_t angle )
{
    if ( angle==CR_ROTATE_ANGLE_0 )
        return;
    int sz = (_rowsize * _dy);
    if ( angle==CR_ROTATE_ANGLE_180 ) {
        if ( _bpp==DRAW_BUF_1_BPP ) {
            for ( int i=sz/2-1; i>=0; i-- ) {
                lUInt8 tmp = revByteBits1( _data[i] );
                _data[i] = revByteBits1( _data[sz-i-1] );
                _data[sz-i-1] = tmp;
            }
        } else if ( _bpp==DRAW_BUF_2_BPP ) {
            for ( int i=sz/2-1; i>=0; i-- ) {
                lUInt8 tmp = revByteBits2( _data[i] );
                _data[i] = revByteBits2( _data[sz-i-1] );
                _data[sz-i-1] = tmp;
            }
        } else { // DRAW_BUF_3_BPP, DRAW_BUF_4_BPP, DRAW_BUF_8_BPP
            lUInt8 * buf = (lUInt8 *) _data;
            for ( int i=sz/2-1; i>=0; i-- ) {
                lUInt8 tmp = buf[i];
                buf[i] = buf[sz-i-1];
                buf[sz-i-1] = tmp;
            }
        }
        return;
    }
    const int newrowsize = _bpp<=2 ? (_dy * _bpp + 7) / 8 : _dy;
    sz = (newrowsize * _dx);
    lUInt8 * __restrict dst = (lUInt8 *)calloc(sz, sizeof(*dst));
    for ( int y=0; y<_dy; y++ ) {
        const lUInt8 * __restrict src = _data + _rowsize*y;
        int dstx, dsty;
        for ( int x=0; x<_dx; x++ ) {
            if ( angle==CR_ROTATE_ANGLE_90 ) {
                dstx = _dy-1-y;
                dsty = x;
            } else {
                dstx = y;
                dsty = _dx-1-x;
            }
            if ( _bpp==DRAW_BUF_1_BPP ) {
                const lUInt8 px = (src[ x >> 3 ] << (x&7)) & 0x80;
                lUInt8 * __restrict dstrow = dst + newrowsize * dsty;
                dstrow[ dstx >> 3 ] |= (px >> (dstx&7));
            } else if (_bpp==DRAW_BUF_2_BPP ) {
                const lUInt8 px = (src[ x >> 2 ] << ((x&3)<<1)) & 0xC0;
                lUInt8 * __restrict dstrow = dst + newrowsize * dsty;
                dstrow[ dstx >> 2 ] |= (px >> ((dstx&3)<<1));
            } else { // DRAW_BUF_3_BPP, DRAW_BUF_4_BPP, DRAW_BUF_8_BPP
                lUInt8 * __restrict dstrow = dst + newrowsize * dsty;
                dstrow[ dstx ] = src[ x ];
            }
        }
    }
    free( _data );
    _data = dst;
    int tmp = _dx;
    _dx = _dy;
    _dy = tmp;
    _rowsize = newrowsize;
}

/// rotates buffer contents by specified angle
void LVColorDrawBuf::Rotate( cr_rotate_angle_t angle )
{
    if ( angle==CR_ROTATE_ANGLE_0 )
        return;
    if ( _bpp==16 ) {
        int sz = (_dx * _dy);
        if ( angle==CR_ROTATE_ANGLE_180 ) {
            lUInt16 * buf = (lUInt16 *) _data;
            for ( int i=sz/2-1; i>=0; i-- ) {
                const lUInt16 tmp = buf[i];
                buf[i] = buf[sz-i-1];
                buf[sz-i-1] = tmp;
            }
            return;
        }
        const int newrowsize = _dy * 2;
        sz = (_dx * newrowsize);
        lUInt16 * __restrict dst = (lUInt16*) malloc( sz );
    #if !defined(__SYMBIAN32__) && defined(_WIN32) && !defined(QT_GL)
        const bool cw = angle!=CR_ROTATE_ANGLE_90;
    #else
        const bool cw = angle==CR_ROTATE_ANGLE_90;
    #endif
        for ( int y=0; y<_dy; y++ ) {
            const lUInt16 * __restrict src = (lUInt16*)_data + _dx*y;
            int nx, ny;
            if ( cw ) {
                nx = _dy - 1 - y;
            } else {
                nx = y;
            }
            for ( int x=0; x<_dx; x++ ) {
                if ( cw ) {
                    ny = x;
                } else {
                    ny = _dx - 1 - x;
                }
                dst[ _dy*ny + nx ] = src[ x ];
            }
        }
    #if !defined(__SYMBIAN32__) && defined(_WIN32) && !defined(QT_GL)
        memcpy( _data, dst, sz );
        free( dst );
    #else
        free( _data );
        _data = (lUInt8*)dst;
    #endif
        const int tmp = _dx;
        _dx = _dy;
        _dy = tmp;
        _rowsize = newrowsize;
    } else {
        int sz = (_dx * _dy);
        if ( angle==CR_ROTATE_ANGLE_180 ) {
            lUInt32 * buf = (lUInt32 *) _data;
            for ( int i=sz/2-1; i>=0; i-- ) {
                const lUInt32 tmp = buf[i];
                buf[i] = buf[sz-i-1];
                buf[sz-i-1] = tmp;
            }
            return;
        }
        const int newrowsize = _dy * 4;
        sz = (_dx * newrowsize);
        lUInt32 * __restrict dst = (lUInt32*) malloc( sz );
    #if !defined(__SYMBIAN32__) && defined(_WIN32) && !defined(QT_GL)
        bool cw = angle!=CR_ROTATE_ANGLE_90;
    #else
        bool cw = angle==CR_ROTATE_ANGLE_90;
    #endif
        for ( int y=0; y<_dy; y++ ) {
            const lUInt32 * __restrict src = (lUInt32*)_data + _dx*y;
            int nx, ny;
            if ( cw ) {
                nx = _dy - 1 - y;
            } else {
                nx = y;
            }
            for ( int x=0; x<_dx; x++ ) {
                if ( cw ) {
                    ny = x;
                } else {
                    ny = _dx - 1 - x;
                }
                dst[ _dy*ny + nx ] = src[ x ];
            }
        }
    #if !defined(__SYMBIAN32__) && defined(_WIN32) && !defined(QT_GL)
        memcpy( _data, dst, sz );
        free( dst );
    #else
        free( _data );
        _data = (lUInt8*)dst;
    #endif
        const int tmp = _dx;
        _dx = _dy;
        _dy = tmp;
        _rowsize = newrowsize;
    }
}

class LVImageScaledDrawCallback : public LVImageDecoderCallback
{
private:
    LVImageSourceRef src;
    LVBaseDrawBuf * __restrict dst;
    int dst_x;
    int dst_y;
    int dst_dx;
    int dst_dy;
    int src_dx;
    int src_dy;
    int * __restrict xmap;
    int * __restrict ymap;
    bool dither;
    bool invert;
    bool smoothscale;
    lUInt8 * __restrict decoded;
    bool isNinePatch;
public:
    static int * __restrict GenMap( int src_len, int dst_len )
    {
        int * __restrict map = new int[ dst_len ];
        for (int i=0; i<dst_len; i++)
        {
            map[ i ] = i * src_len / dst_len;
        }
        return map;
    }
    static int * __restrict GenNinePatchMap( int src_len, int dst_len, int frame1, int frame2)
    {
        int * __restrict map = new int[ dst_len ];
        if (frame1 + frame2 > dst_len) {
            const int total = frame1 + frame2;
            const int extra = total - dst_len;
            const int extra1 = frame1 * extra / total;
            const int extra2 = frame2 * extra / total;
            frame1 -= extra1;
            frame2 -= extra2;
        }
        int srcm = src_len - frame1 - frame2 - 2;
        const int dstm = dst_len - frame1 - frame2;
        if (srcm < 0)
            srcm = 0;
        for (int i=0; i<dst_len; i++)
        {
            if (i < frame1) {
                // start
                map[ i ] = i + 1;
            } else if (i >= dst_len - frame2) {
                // end
                const int rx = i - (dst_len - frame2);
                map[ i ] = src_len - frame2 + rx - 1;
            } else {
                // middle
                map[ i ] = 1 + frame1 + (i - frame1) * srcm / dstm;
            }
//            CRLog::trace("frame[%d, %d] src=%d dst=%d %d -> %d", frame1, frame2, src_len, dst_len, i, map[i]);
//            if (map[i] >= src_len) {
//                CRLog::error("Wrong coords");
//            }
        }
        return map;
    }
    LVImageScaledDrawCallback(LVBaseDrawBuf * dstbuf, LVImageSourceRef img, int x, int y, int width, int height, bool dith, bool inv, bool smooth )
    : src(img), dst(dstbuf), dst_x(x), dst_y(y), dst_dx(width), dst_dy(height), xmap(0), ymap(0), dither(dith), invert(inv), smoothscale(smooth), decoded(0)
    {
        src_dx = img->GetWidth();
        src_dy = img->GetHeight();
        if ( img->IsScalable() ) {
            // The image can scale itself to arbitrary sizes: we don't need to scale it here.
            // It will call our GetTargetSize() to draw itself to the requested size.
            src_dx = dst_dx;
            src_dy = dst_dy;
        }
        const CR9PatchInfo * __restrict np = img->GetNinePatchInfo();
        isNinePatch = false;
        lvRect ninePatch;
        if (np) {
            isNinePatch = true;
            ninePatch = np->frame;
        }
        // If smoothscaling was requested, but no scaling was needed, disable the post-processing pass
        if (smoothscale && src_dx == dst_dx && src_dy == dst_dy) {
            smoothscale = false;
            //fprintf( stderr, "Disabling smoothscale because no scaling was needed (%dx%d -> %dx%d)\n", src_dx, src_dy, dst_dx, dst_dy );
        }
        if ( src_dx != dst_dx || isNinePatch) {
            if (isNinePatch)
                xmap = GenNinePatchMap(src_dx, dst_dx, ninePatch.left, ninePatch.right);
            else if (!smoothscale)
                xmap = GenMap( src_dx, dst_dx );
        }
        if ( src_dy != dst_dy || isNinePatch) {
            if (isNinePatch)
                ymap = GenNinePatchMap(src_dy, dst_dy, ninePatch.top, ninePatch.bottom);
            else if (!smoothscale)
                ymap = GenMap( src_dy, dst_dy );
        }
        // If we have a smoothscale post-processing pass, we'll need to build a buffer of the *full* decoded image.
        if (smoothscale) {
            // Byte-sized buffer, we're 32bpp, so, 4 bytes per pixel.
            decoded = new lUInt8[src_dy * (src_dx * 4)];
        }
    }
    virtual bool GetTargetSize(int & width, int & height) const {
        width = dst_dx;
        height = dst_dy;
        return true;
    }
    virtual ~LVImageScaledDrawCallback()
    {
        if (xmap)
            delete[] xmap;
        if (ymap)
            delete[] ymap;
        if (decoded)
            delete[] decoded;
    }
    virtual void OnStartDecode( LVImageSource * )
    {
    }
    virtual bool OnLineDecoded( LVImageSource *, int y, lUInt32 * __restrict data )
    {
        //fprintf( stderr, "l_%d ", y );
        if (isNinePatch) {
            if (y == 0 || y == src_dy-1) // ignore first and last lines
                return true;
        }
        // Defer everything to the post-process pass for smooth scaling, we just have to store the line in our decoded buffer
        if (smoothscale) {
            //fprintf( stderr, "Smoothscale l_%d pass\n", y );
            memcpy(decoded + (y * (src_dx * 4)), data, (src_dx * 4));
            return true;
        }
        int yy = -1;
        int yy2 = -1;
        const lUInt32 rgba_invert = invert ? 0x00FFFFFF : 0;
        const lUInt8 gray_invert = invert ? 0xFF : 0;
        if (ymap) {
            for (int i = 0; i < dst_dy; i++) {
                if (ymap[i] == y) {
                    if (yy == -1)
                        yy = i;
                    yy2 = i + 1;
                }
            }
            if (yy == -1)
                return true;
        } else {
            yy = y;
            yy2 = y+1;
        }
//        if ( ymap )
//        {
//            int yy0 = (y - 1) * dst_dy / src_dy;
//            yy = y * dst_dy / src_dy;
//            yy2 = (y+1) * dst_dy / src_dy;
//            if ( yy == yy0 )
//            {
//                //fprintf( stderr, "skip_dup " );
//                //return true; // skip duplicate drawing
//            }
//            if ( yy2 > dst_dy )
//                yy2 = dst_dy;
//        }
        lvRect clip;
        dst->GetClipRect( &clip );
        for ( ;yy<yy2; yy++ )
        {
            if ( yy+dst_y<clip.top || yy+dst_y>=clip.bottom )
                continue;
            const int bpp = dst->GetBitsPerPixel();
            if ( bpp >= 24 )
            {
                lUInt32 * __restrict row = (lUInt32 *)dst->GetScanLine( yy + dst_y );
                row += dst_x;
                for (int x=0; x<dst_dx; x++)
                {
                    const int xx = x + dst_x;
                    if ( xx<clip.left || xx>=clip.right ) {
                        // OOB, don't plot it!
                        continue;
                    }

                    const lUInt32 cl = data[xmap ? xmap[x] : x];
                    const lUInt8 alpha = (cl >> 24)&0xFF;
                    // NOTE: Remember that for some mysterious reason, lvimg feeds us inverted alpha
                    //       (i.e., 0 is opaque, 0xFF is transparent)...
                    if ( alpha == 0xFF ) {
                        // Transparent, don't plot it...
                        if ( invert ) {
                            // ...unless we're doing night-mode shenanigans, in which case, we need to fake an inverted background
                            // (i.e., a *black* background, so it gets inverted back to white with NightMode, since white is our expected "standard" background color)
                            // c.f., https://github.com/koreader/koreader/issues/4986
                            row[ x ] = 0x00000000;
                        } else {
                            continue;
                        }
                    } else if ( alpha == 0 ) {
                        // Fully opaque, plot it as-is
                        row[ x ] = cl ^ rgba_invert;
                    } else {
                        if ((row[x] & 0xFF000000) == 0xFF000000) {
                            // Plot it as-is if *buffer* pixel is transparent
                            row[ x ] = cl ^ rgba_invert;
                        } else {
                            // NOTE: This *also* has a "fully opaque" shortcut... :/
                            ApplyAlphaRGB( row[x], cl, alpha );
                            // Invert post-blending to avoid potential stupidity...
                            row[ x ] ^= rgba_invert;
                        }
                    }
                }
            }
            else if ( bpp == 16 )
            {
                lUInt16 * __restrict row = (lUInt16 *)dst->GetScanLine( yy + dst_y );
                row += dst_x;
                for (int x=0; x<dst_dx; x++)
                {
                    const int xx = x + dst_x;
                    if ( xx<clip.left || xx>=clip.right ) {
                        // OOB, don't plot it!
                        continue;
                    }

                    const lUInt32 cl = data[xmap ? xmap[x] : x];
                    const lUInt8 alpha = (cl >> 24)&0xFF;
                    // NOTE: See final branch of the ladder. Not quite sure why some alpha ranges are treated differently...
                    if ( alpha >= 0xF0 ) {
                        // Transparent, don't plot it...
                        if ( invert ) {
                            // ...unless we're doing night-mode shenanigans, in which case, we need to fake an inverted background
                            // (i.e., a *black* background, so it gets inverted back to white with NightMode, since white is our expected "standard" background color)
                            // c.f., https://github.com/koreader/koreader/issues/4986
                            row[ x ] = 0x0000;
                        } else {
                            continue;
                        }
                    } else if ( alpha < 16 ) {
                        row[ x ] = rgb888to565( cl ^ rgba_invert );
                    } else if ( alpha < 0xF0 ) {
                        lUInt32 v = rgb565to888(row[x]);
                        ApplyAlphaRGB( v, cl, alpha );
                        row[ x ] = rgb888to565(v ^ rgba_invert);
                    }
                }
            }
            else if ( bpp > 2 ) // 3,4,8 bpp
            {
                lUInt8 * __restrict row = (lUInt8 *)dst->GetScanLine( yy + dst_y );
                row += dst_x;
                for (int x=0; x<dst_dx; x++)
                {
                    const int xx = x + dst_x;
                    if ( xx<clip.left || xx>=clip.right ) {
                        // OOB, don't plot it!
                        continue;
                    }

                    const int srcx = xmap ? xmap[x] : x;
                    lUInt32 cl = data[srcx];
                    const lUInt8 alpha = (cl >> 24)&0xFF;
                    if ( alpha == 0xFF ) {
                        // Transparent, don't plot it...
                        if ( invert ) {
                            // ...unless we're doing night-mode shenanigans, in which case, we need to fake a white background.
                            cl = 0x00FFFFFF;
                        } else {
                            continue;
                        }
                    } else if ( alpha != 0 ) {
                        lUInt8 origLuma = row[x];
                        // Expand lower bitdepths to Y8
                        if ( bpp == 3 ) {
                            origLuma = origLuma & 0xE0;
                            origLuma = origLuma | (origLuma>>3) | (origLuma>>6);
                        } else if ( bpp == 4 ) {
                            origLuma = origLuma & 0xF0;
                            origLuma = origLuma | (origLuma>>4);
                        }
                        // Expand Y8 to RGB32 (i.e., duplicate, R = G = B = Y)
                        lUInt32 bufColor = origLuma | (origLuma<<8) | (origLuma<<16);
                        ApplyAlphaRGB( bufColor, cl, alpha );
                        cl = bufColor;
                    }

                    lUInt8 dcl;
                    if ( dither && bpp < 8 ) {
#if (GRAY_INVERSE==1)
                        dcl = (lUInt8)DitherNBitColor( cl^0xFFFFFF, x, yy, bpp );
#else
                        dcl = (lUInt8)DitherNBitColor( cl, x, yy, bpp );
#endif
                    } else if ( dither && bpp == 8 ) {
                        dcl = rgbToGray( cl );
                        dcl = dither_o8x8( x, yy, dcl );
                    } else {
                        dcl = rgbToGray( cl, bpp );
                    }
                    row[ x ] = dcl ^ gray_invert;
                    // ApplyAlphaGray( row[x], dcl, alpha, bpp );
                }
            }
            else if ( bpp == 2 )
            {
                //fprintf( stderr, "." );
                lUInt8 * __restrict row = (lUInt8 *)dst->GetScanLine( yy+dst_y );
                //row += dst_x;
                for (int x=0; x<dst_dx; x++)
                {
                    const int xx = x + dst_x;
                    if ( xx<clip.left || xx>=clip.right ) {
                        // OOB, don't plot it!
                        continue;
                    }

                    const int byteindex = (xx >> 2);
                    const int bitindex = (3-(xx & 3))<<1;
                    const lUInt8 mask = 0xC0 >> (6 - bitindex);

                    lUInt32 cl = data[xmap ? xmap[x] : x];
                    const lUInt8 alpha = (cl >> 24)&0xFF;
                    if ( alpha == 0xFF ) {
                        // Transparent, don't plot it...
                        if ( invert ) {
                            // ...unless we're doing night-mode shenanigans, in which case, we need to fake a white background.
                            cl = 0x00FFFFFF;
                        } else {
                            continue;
                        }
                    } else if ( alpha != 0 ) {
                        lUInt8 origLuma = (row[ byteindex ] & mask)>>bitindex;
                        origLuma = origLuma | (origLuma<<2);
                        origLuma = origLuma | (origLuma<<4);
                        lUInt32 bufColor = origLuma | (origLuma<<8) | (origLuma<<16);
                        ApplyAlphaRGB( bufColor, cl, alpha );
                        cl = bufColor;
                    }

                    lUInt32 dcl = 0;
                    if ( dither ) {
#if (GRAY_INVERSE==1)
                        dcl = Dither2BitColor( cl ^ rgba_invert, x, yy ) ^ 3;
#else
                        dcl = Dither2BitColor( cl ^ rgba_invert, x, yy );
#endif
                    } else {
                        dcl = rgbToGrayMask( cl ^ rgba_invert, 2 ) & 3;
                    }
                    dcl = dcl << bitindex;
                    row[ byteindex ] = (lUInt8)((row[ byteindex ] & (~mask)) | dcl);
                }
            }
            else if ( bpp == 1 )
            {
                //fprintf( stderr, "." );
                lUInt8 * __restrict row = (lUInt8 *)dst->GetScanLine( yy+dst_y );
                //row += dst_x;
                for (int x=0; x<dst_dx; x++)
                {
                    const int xx = x + dst_x;
                    if ( xx<clip.left || xx>=clip.right ) {
                        // OOB, don't plot it!
                        continue;
                    }

                    lUInt32 cl = data[xmap ? xmap[x] : x];
                    const lUInt8 alpha = (cl >> 24)&0xFF;
                    if ( alpha & 0x80 ) {
                        // Transparent, don't plot it...
                        if ( invert ) {
                            // ...unless we're doing night-mode shenanigans, in which case, we need to fake a white background.
                            cl = 0x00FFFFFF;
                        } else {
                            continue;
                        }
                    }

                    lUInt32 dcl = 0;
                    if ( dither ) {
#if (GRAY_INVERSE==1)
                        dcl = Dither1BitColor( cl ^ rgba_invert, x, yy ) ^ 1;
#else
                        dcl = Dither1BitColor( cl ^ rgba_invert, x, yy ) ^ 0;
#endif
                    } else {
                        dcl = rgbToGrayMask( cl ^ rgba_invert, 1 ) & 1;
                    }
                    const int byteindex = (xx >> 3);
                    const int bitindex = ((xx & 7));
                    const lUInt8 mask = 0x80 >> (bitindex);
                    dcl = dcl << (7-bitindex);
                    row[ byteindex ] = (lUInt8)((row[ byteindex ] & (~mask)) | dcl);
                }
            }
            else
            {
                return false;
            }
        }
        return true;
    }
    virtual void OnEndDecode( LVImageSource * obj, bool )
    {
        // If we're not smooth scaling, we're done!
        if (!smoothscale) {
            return;
        }

        // Scale our decoded data...
        lUInt8 * __restrict sdata = nullptr;
        //fprintf( stderr, "Requesting smooth scaling (%dx%d -> %dx%d)\n", src_dx, src_dy, dst_dx, dst_dy );
        sdata = CRe::qSmoothScaleImage(decoded, src_dx, src_dy, false, dst_dx, dst_dy);
        if (sdata == nullptr) {
                // Hu oh... Scaling failed! Return *without* drawing anything!
                // We skipped map generation, so we can't easily fallback to nearest-neighbor...
                //fprintf( stderr, "Smooth scaling failed :(\n" );
                return;
        }

        // Process as usual, with a bit of a hack to avoid code duplication...
        smoothscale = false;
        for (int y=0; y < dst_dy; y++) {
            lUInt8 * __restrict row = sdata + (y * (dst_dx * 4));
            this->OnLineDecoded( obj, y, (lUInt32 *) row );
        }

        // This prints the unscaled decoded buffer, for debugging purposes ;).
        /*
        for (int y=0; y < src_dy; y++) {
            lUInt8 * row = decoded + (y * (src_dx * 4));
            this->OnLineDecoded( obj, y, (lUInt32 *) row );
        }
        */

        // And now that it's been rendered we can free the scaled buffer (it was allocated by CRe::qSmoothScaleImage).
        free(sdata);
    }
};


int LVBaseDrawBuf::GetWidth() const
{
    return _dx;
}

int  LVBaseDrawBuf::GetHeight() const
{
    return _dy;
}

int  LVGrayDrawBuf::GetBitsPerPixel() const
{
    return _bpp;
}

void LVGrayDrawBuf::Draw( LVImageSourceRef img, int x, int y, int width, int height, bool dither )
{
    //fprintf( stderr, "LVGrayDrawBuf::Draw( img(%d, %d), %d, %d, %d, %d\n", img->GetWidth(), img->GetHeight(), x, y, width, height );
    if ( width<=0 || height<=0 )
        return;
    LVImageScaledDrawCallback drawcb( this, img, x, y, width, height, _ditherImages, _invertImages, _smoothImages );
    img->Decode( &drawcb );

    _drawnImagesCount++;
    _drawnImagesSurface += width*height;
}


/// get pixel value
lUInt32 LVGrayDrawBuf::GetPixel( int x, int y ) const
{
    if (x<0 || y<0 || x>=_dx || y>=_dy)
        return 0;
    const lUInt8 * __restrict line = GetScanLine(y);
    if (_bpp==1) {
        // 1bpp
        if ( line[x>>3] & (0x80>>(x&7)) )
            return 1;
        else
            return 0;
    } else if (_bpp==2) {
        return (line[x>>2] >> (6-((x&3)<<1))) & 3;
    } else { // 3, 4, 8
        return line[x];
    }
}

void LVGrayDrawBuf::Clear( lUInt32 color )
{
    if (!_data)
        return;
    color = rgbToGrayMask( color, _bpp );
#if (GRAY_INVERSE==1)
    color ^= 0xFF;
#endif
    memset( _data, color, _rowsize * _dy );
//    for (int i = _rowsize * _dy - 1; i>0; i--)
//    {
//        _data[i] = (lUInt8)color;
//    }
    SetClipRect( NULL );
}

void LVGrayDrawBuf::FillRect( int x0, int y0, int x1, int y1, lUInt32 color32 )
{
    if (x0<_clip.left)
        x0 = _clip.left;
    if (y0<_clip.top)
        y0 = _clip.top;
    if (x1>_clip.right)
        x1 = _clip.right;
    if (y1>_clip.bottom)
        y1 = _clip.bottom;
    if (x0>=x1 || y0>=y1)
        return;
    lUInt8 color = rgbToGrayMask( color32, _bpp );
    const lUInt8 alpha = (color32 >> 24) & 0xFF;
    if (alpha == 0xFF) // Fully transparent color
        return;
    const lUInt8 opacity = alpha ^ 0xFF;
#if (GRAY_INVERSE==1)
    color ^= 0xFF;
#endif
    lUInt8 * __restrict line = GetScanLine(y0);
    for (int y=y0; y<y1; y++)
    {
        if (_bpp==1) {
            for (int x=x0; x<x1; x++)
            {
                const lUInt8 mask = 0x80 >> (x&7);
                const int index = x >> 3;
                line[index] = (lUInt8)((line[index] & ~mask) | (color & mask));
            }
        } else if (_bpp==2) {
            for (int x=x0; x<x1; x++)
            {
                const lUInt8 mask = 0xC0 >> ((x&3)<<1);
                const int index = x >> 2;
                line[index] = (lUInt8)((line[index] & ~mask) | (color & mask));
            }
        } else { // 3, 4, 8
            if ( opacity == 0xFF ) {
                for (int x=x0; x<x1; x++)
                    line[x] = color;
            }
            else {
                for (int x=x0; x<x1; x++)
                    ApplyAlphaGray8( line[x], color, alpha, opacity );
            }
        }
        line += _rowsize;
    }
}

#if 0

void LVGrayDrawBuf::FillRectPattern( int x0, int y0, int x1, int y1, lUInt32 color032, lUInt32 color132, const lUInt8 * __restrict pattern )
{
    if (x0<_clip.left)
        x0 = _clip.left;
    if (y0<_clip.top)
        y0 = _clip.top;
    if (x1>_clip.right)
        x1 = _clip.right;
    if (y1>_clip.bottom)
        y1 = _clip.bottom;
    if (x0>=x1 || y0>=y1)
        return;
    lUInt8 color0 = rgbToGrayMask( color032, _bpp );
    lUInt8 color1 = rgbToGrayMask( color132, _bpp );
    lUInt8 * __restrict line = GetScanLine(y0);
    for (int y=y0; y<y1; y++)
    {
        const lUInt8 patternMask = pattern[y & 3];
        if (_bpp==1) {
            for (int x=x0; x<x1; x++)
            {
                const lUInt8 patternBit = (patternMask << (x&7)) & 0x80;
                const lUInt8 mask = 0x80 >> (x&7);
                const int index = x >> 3;
                line[index] = (lUInt8)((line[index] & ~mask) | ((patternBit?color1:color0) & mask));
            }
        } else if (_bpp==2) {
            for (int x=x0; x<x1; x++)
            {
                const lUInt8 patternBit = (patternMask << (x&7)) & 0x80;
                const lUInt8 mask = 0xC0 >> ((x&3)<<1);
                const int index = x >> 2;
                line[index] = (lUInt8)((line[index] & ~mask) | ((patternBit?color1:color0) & mask));
            }
        } else {
            for (int x=x0; x<x1; x++)
            {
                const lUInt8 patternBit = (patternMask << (x&7)) & 0x80;
                line[x] = patternBit ? color1 : color0;
            }
        }
        line += _rowsize;
    }
}

#endif

static const lUInt8 fill_masks1[5] = {0x00, 0x3, 0x0f, 0x3f, 0xff};
static const lUInt8 fill_masks2[4] = {0x00, 0xc0, 0xf0, 0xfc};

#define INVERT_PRSERVE_GRAYS

#ifdef INVERT_PRSERVE_GRAYS
static const lUInt8 inverted_bytes[] = {
    0xff, 0xfd, 0xfe, 0xfc, 0xf7, 0xf5, 0xf6, 0xf4, 0xfb, 0xf9, 0xfa, 0xf8, 0xf3, 0xf1,
    0xf2, 0xf0, 0xdf, 0xdd, 0xde, 0xdc, 0xd7, 0xd5, 0xd6, 0xd4, 0xdb, 0xd9, 0xda, 0xd8,
    0xd3, 0xd1, 0xd2, 0xd0, 0xef, 0xed, 0xee, 0xec, 0xe7, 0xe5, 0xe6, 0xe4, 0xeb, 0xe9,
    0xea, 0xe8, 0xe3, 0xe1, 0xe2, 0xe0, 0xcf, 0xcd, 0xce, 0xcc, 0xc7, 0xc5, 0xc6, 0xc4,
    0xcb, 0xc9, 0xca, 0xc8, 0xc3, 0xc1, 0xc2, 0xc0, 0x7f, 0x7d, 0x7e, 0x7c, 0x77, 0x75,
    0x76, 0x74, 0x7b, 0x79, 0x7a, 0x78, 0x73, 0x71, 0x72, 0x70, 0x5f, 0x5d, 0x5e, 0x5c,
    0x57, 0x55, 0x56, 0x54, 0x5b, 0x59, 0x5a, 0x58, 0x53, 0x51, 0x52, 0x50, 0x6f, 0x6d,
    0x6e, 0x6c, 0x67, 0x65, 0x66, 0x64, 0x6b, 0x69, 0x6a, 0x68, 0x63, 0x61, 0x62, 0x60,
    0x4f, 0x4d, 0x4e, 0x4c, 0x47, 0x45, 0x46, 0x44, 0x4b, 0x49, 0x4a, 0x48, 0x43, 0x41,
    0x42, 0x40, 0xbf, 0xbd, 0xbe, 0xbc, 0xb7, 0xb5, 0xb6, 0xb4, 0xbb, 0xb9, 0xba, 0xb8,
    0xb3, 0xb1, 0xb2, 0xb0, 0x9f, 0x9d, 0x9e, 0x9c, 0x97, 0x95, 0x96, 0x94, 0x9b, 0x99,
    0x9a, 0x98, 0x93, 0x91, 0x92, 0x90, 0xaf, 0xad, 0xae, 0xac, 0xa7, 0xa5, 0xa6, 0xa4,
    0xab, 0xa9, 0xaa, 0xa8, 0xa3, 0xa1, 0xa2, 0xa0, 0x8f, 0x8d, 0x8e, 0x8c, 0x87, 0x85,
    0x86, 0x84, 0x8b, 0x89, 0x8a, 0x88, 0x83, 0x81, 0x82, 0x80, 0x3f, 0x3d, 0x3e, 0x3c,
    0x37, 0x35, 0x36, 0x34, 0x3b, 0x39, 0x3a, 0x38, 0x33, 0x31, 0x32, 0x30, 0x1f, 0x1d,
    0x1e, 0x1c, 0x17, 0x15, 0x16, 0x14, 0x1b, 0x19, 0x1a, 0x18, 0x13, 0x11, 0x12, 0x10,
    0x2f, 0x2d, 0x2e, 0x2c, 0x27, 0x25, 0x26, 0x24, 0x2b, 0x29, 0x2a, 0x28, 0x23, 0x21,
    0x22, 0x20, 0xf, 0xd, 0xe, 0xc, 0x7, 0x5, 0x6, 0x4, 0xb, 0x9, 0xa, 0x8,
    0x3, 0x1, 0x2, 0x0
};
#define GET_INVERTED_BYTE(x) inverted_bytes[x]
#else
#define GET_INVERTED_BYTE(x) ~(x)
#endif
void LVGrayDrawBuf::InvertRect(int x0, int y0, int x1, int y1)
{
    if (x0<_clip.left)
        x0 = _clip.left;
    if (y0<_clip.top)
        y0 = _clip.top;
    if (x1>_clip.right)
        x1 = _clip.right;
    if (y1>_clip.bottom)
        y1 = _clip.bottom;
    if (x0>=x1 || y0>=y1)
        return;

	if (_bpp==1) {
		; //TODO: implement for 1 bit
	} else if (_bpp==2) {
		lUInt8 * line = GetScanLine(y0) + (x0 >> 2);
		lUInt16 before = 4 - (x0 & 3); // number of pixels before byte boundary
		if (before == 4)
			before = 0;
		lUInt16 w = (x1 - x0 - before);
		lUInt16 after  = (w & 3); // number of pixels after byte boundary
		w >>= 2;
		before = fill_masks1[before];
		after = fill_masks2[after];
		for (int y = y0; y < y1; y++) {
			lUInt8 *dst = line;
			if (before) {
				const lUInt8 color = GET_INVERTED_BYTE(dst[0]);
				dst[0] = ((dst[0] & ~before) | (color & before));
				dst++;
			}
			for (int x = 0; x < w; x++) {
				dst[x] = GET_INVERTED_BYTE(dst[x]);
			}
			dst += w;
			if (after) {
				const lUInt8 color = GET_INVERTED_BYTE(dst[0]);
				dst[0] = ((dst[0] & ~after) | (color & after));
			}
			line += _rowsize;
		}
        }
#if 0
        else if (_bpp == 4) { // 3, 4, 8
            lUInt8 * line = GetScanLine(y0);
            for (int y=y0; y<y1; y++) {
                for (int x=x0; x<x1; x++) {
                    lUInt8 value = line[x];
                    if (value == 0 || value == 0xF0)
                        line[x] = ~value;
                }
                line += _rowsize;
            }
        }
#endif
        else { // 3, 4, 8
            lUInt8 * __restrict line = GetScanLine(y0);
            for (int y=y0; y<y1; y++) {
                for (int x=x0; x<x1; x++)
                    line[x] ^= 0xFF;
                line += _rowsize;
            }
        }
	CHECK_GUARD_BYTE;
}
void LVGrayDrawBuf::Resize( int dx, int dy )
{
    if (!_ownData) {
        _data = NULL;
        _ownData = false;
    } else if (_data) {
    	CHECK_GUARD_BYTE;
        free(_data);
        _data = NULL;
	}
    _dx = dx;
    _dy = dy;
    _rowsize = _bpp<=2 ? (_dx * _bpp + 7) / 8 : _dx;
    if (dx > 0 && dy > 0) {
        _data = (unsigned char *)calloc(_rowsize * _dy + 1, sizeof(*_data));
        _data[_rowsize * _dy] = GUARD_BYTE;
    } else {
        Clear(0);
    }
    SetClipRect( NULL );
}

/// returns white pixel value
lUInt32 LVGrayDrawBuf::GetWhiteColor() const
{
    return 0xFFFFFF;
    /*
#if (GRAY_INVERSE==1)
    return 0;
#else
    return (1<<_bpp) - 1;
#endif
    */
}
/// returns black pixel value
lUInt32 LVGrayDrawBuf::GetBlackColor() const
{
    return 0;
    /*
#if (GRAY_INVERSE==1)
    return (1<<_bpp) - 1;
#else
    return 0;
#endif
    */
}

LVGrayDrawBuf::LVGrayDrawBuf(int dx, int dy, int bpp, void * auxdata )
    : LVBaseDrawBuf(), _bpp(bpp), _ownData(true)
{
    _dx = dx;
    _dy = dy;
    _bpp = bpp;
    _rowsize = (bpp<=2) ? (_dx * _bpp + 7) / 8 : _dx;

    _backgroundColor = GetWhiteColor(); // NOLINT: Call to virtual function during construction
    _textColor = GetBlackColor();       // NOLINT

    if ( auxdata ) {
        _data = (lUInt8 *) auxdata;
        _ownData = false;
    } else if (_dx && _dy) {
        _data = (lUInt8 *) calloc(_rowsize * _dy + 1, sizeof(*_data));
        _data[_rowsize * _dy] = GUARD_BYTE;
    }
    SetClipRect( NULL );
    CHECK_GUARD_BYTE;
}

LVGrayDrawBuf::~LVGrayDrawBuf()
{
    if (_data && _ownData ) {
    	CHECK_GUARD_BYTE;
    	free( _data );
    }
}
void LVGrayDrawBuf::DrawLine(int x0, int y0, int x1, int y1, lUInt32 color0, int length1, int length2, int direction)
{
    if (x0<_clip.left)
        x0 = _clip.left;
    if (y0<_clip.top)
        y0 = _clip.top;
    if (x1>_clip.right)
        x1 = _clip.right;
    if (y1>_clip.bottom)
        y1 = _clip.bottom;
    if (x0>=x1 || y0>=y1)
        return;
    lUInt8 color = rgbToGrayMask( color0, _bpp );
    const lUInt8 alpha = (color0 >> 24) & 0xFF;
    if (alpha == 0xFF) // Fully transparent color
        return;
    const lUInt8 opacity = alpha ^ 0xFF;
#if (GRAY_INVERSE==1)
    color ^= 0xFF;
#endif

    for (int y=y0; y<y1; y++)
    {
        if (_bpp==1) {
            for (int x=x0; x<x1; x++)
            {
                lUInt8 * __restrict line = GetScanLine(y);
                if (direction==0 &&x%(length1+length2)<length1)line[x] = color;
                if (direction==1 &&y%(length1+length2)<length1)line[x] = color;
            }
        } else if (_bpp==2) {
            for (int x=x0; x<x1; x++)
            {
                lUInt8 * __restrict line = GetScanLine(y);
                if (direction==0 &&x%(length1+length2)<length1)line[x] = color;
                if (direction==1 &&y%(length1+length2)<length1)line[x] = color;
            }
        } else { // 3, 4, 8
            for (int x=x0; x<x1; x++)
            {
                lUInt8 * __restrict line = GetScanLine(y);
                if ( (direction==0 &&x%(length1+length2)<length1) ||
                     (direction==1 &&y%(length1+length2)<length1) ) {
                    if ( opacity == 0xFF )
                        line[x] = color;
                    else
                        ApplyAlphaGray8( line[x], color, alpha, opacity );
                }
            }
        }
    }
}
void LVGrayDrawBuf::Draw( int x, int y, const lUInt8 * bitmap, int width, int height, const lUInt32 * __restrict /*palette*/)
{
    // NOTE: LVColorDrawBuf's variant does a _data NULL check?
    //int buf_width = _dx; /* 2bpp */
    const int initial_height = height;
    int bx = 0;
    int by = 0;
    const int bmp_width = width;

    if (x<_clip.left)
    {
        width += x-_clip.left;
        bx -= x-_clip.left;
        x = _clip.left;
        if (width<=0)
            return;
    }
    if (y<_clip.top)
    {
        height += y-_clip.top;
        by -= y-_clip.top;
        y = _clip.top;
        if (_hidePartialGlyphs && height<=initial_height/2) // HIDE PARTIAL VISIBLE GLYPHS
            return;
        if (height<=0)
            return;
    }
    if (x + width > _clip.right)
    {
        width = _clip.right - x;
    }
    if (width<=0)
        return;
    if (y + height > _clip.bottom)
    {
        if (_hidePartialGlyphs && height<=initial_height/2) // HIDE PARTIAL VISIBLE GLYPHS
            return;
        int clip_bottom = _clip.bottom;
        if ( _hidePartialGlyphs )
            clip_bottom = this->_dy;
        if ( y+height > clip_bottom)
            height = clip_bottom - y;
    }
    if (height<=0)
        return;

    bitmap += bx + by*bmp_width;

//    bool white = (color & 0x80) ?
//#if (GRAY_INVERSE==1)
//            false : true;
//#else
//            true : false;
//#endif

    // Note: support for TextColor opacity/alpha was added only in the _bpp==8 branch
    if ( _bpp==8 ) {
        lUInt8 * dstline = _data + _rowsize*y + x;
        const lUInt8 color = rgbToGrayMask(GetTextColor(), 8);
        const lUInt8 bopacity = ~GetTextColor() >> 24;
        if (bopacity == 0) // Fully transparent color
            return;
        while (height--)
        {
            const lUInt8 * __restrict src = bitmap;
            lUInt8 * __restrict dst = dstline;
            size_t px_count = width;
            while (px_count--)
            {
                lUInt8 opacity = *(src++); // glyph opacity
                if ( opacity == 0 ) {
                    // Background pixel, NOP
                }
                else if (opacity == 0xFF && bopacity == 0xFF) { // fully opaque pixel and color
                    *dst = color;
                }
                else {
                    opacity = (opacity*bopacity)>>8;
                    const lUInt8 alpha = opacity ^ 0xFF;
                    ApplyAlphaGray8( *dst, color, alpha, opacity );
                }
                dst++;
            }
            /* next line, accounting for clipping in src and padding in dst */
            bitmap += bmp_width;
            dstline += _rowsize;
        }
    } else if ( _bpp==2 ) {
        lUInt8 * dstline = _data + _rowsize*y + (x >> 2);
        const int shift0 = (x & 3);
        // foreground color
        const lUInt8 cl = (lUInt8)(rgbToGray(GetTextColor()) >> 6); // 0..3
        //cl ^= 0x03;
        while (height--)
        {
            const lUInt8 * __restrict src = bitmap;
            lUInt8 * __restrict dst = dstline;
            int shift = shift0;
            size_t px_count = width;
            while (px_count--)
            {
                const lUInt8 opaque = (*src >> 4) & 0x0F; // 0..15
                if ( opaque>0x3 ) {
                    const int shift2 = shift<<1;
                    const int shift2i = 6-shift2;
                    const lUInt8 mask = 0xC0 >> shift2;
                    lUInt8 dstcolor;
                    if ( opaque>=0xC ) {
                        dstcolor = cl;
                    } else {
                        const lUInt8 bgcolor = ((*dst)>>shift2i)&3; // 0..3
                        dstcolor = ((opaque*cl + (15-opaque)*bgcolor)>>4)&3;
                    }
                    *dst = (*dst & ~mask) | (dstcolor<<shift2i);
                }
                src++;
                /* next pixel */
                if (!(++shift & 3))
                {
                    shift = 0;
                    dst++;
                }
            }
            /* next line, accounting for clipping in src and funky bitdepths in dst */
            bitmap += bmp_width;
            dstline += _rowsize;
        }
    } else if ( _bpp==1 ) {
        lUInt8 * dstline = _data + _rowsize*y + (x >> 3);
        const int shift0 = (x & 7);
        while (height--)
        {
            const lUInt8 * __restrict src = bitmap;
            lUInt8 * __restrict dst = dstline;
            int shift = shift0;
            size_t px_count = width;
            while (px_count--)
            {
    #if (GRAY_INVERSE==1)
                *dst |= (( (*src++) & 0x80 ) >> ( shift ));
    #else
                *dst &= ~(( ((*src++) & 0x80) ) >> ( shift ));
    #endif
                /* next pixel */
                if (!(++shift & 7))
                {
                    shift = 0;
                    dst++;
                }
            }
            /* next line, accounting for clipping in src and funky bitdepths in dst */
            bitmap += bmp_width;
            dstline += _rowsize;
        }
    } else { // 3,4
        lUInt8 * dstline = _data + _rowsize*y + x;
        const lUInt8 color = rgbToGrayMask(GetTextColor(), _bpp);
        const int mask = ((1<<_bpp)-1)<<(8-_bpp);
        while (height--)
        {
            const lUInt8 * __restrict src = bitmap;
            lUInt8 * __restrict dst = dstline;
            size_t px_count = width;
            while (px_count--)
            {
                const lUInt8 opaque = (*src++);
                if ( opaque ) {
                    if ( opaque>=mask )
                        *dst = color;
                    else {
                        const lUInt8 alpha = opaque ^ 0xFF;
                        ApplyAlphaGray( *dst, color, alpha, _bpp );
                    }
                }
                dst++;
            }
            /* next line, accounting for clipping in src and funky bitdepths in dst */
            bitmap += bmp_width;
            dstline += _rowsize;
        }
    }
    CHECK_GUARD_BYTE;
}

void LVBaseDrawBuf::SetClipRect( const lvRect * clipRect )
{
    if (clipRect)
    {
        _clip = *clipRect;
        if (_clip.left<0)
            _clip.left = 0;
        if (_clip.top<0)
            _clip.top = 0;
        if (_clip.right>_dx)
            _clip.right = _dx;
        if (_clip.bottom > _dy)
            _clip.bottom = _dy;
    }
    else
    {
        _clip.top = 0;
        _clip.left = 0;
        _clip.right = _dx;
        _clip.bottom = _dy;
    }
}

lUInt8 * LVGrayDrawBuf::GetScanLine( int y ) const
{
    return _data + _rowsize*y;
}

void LVGrayDrawBuf::Invert()
{
    unsigned char * __restrict p = _data;
    size_t px_count = _rowsize * _dy;
    while (px_count--) {
        *p++ ^= 0xFF;
    }
}

#if 0

void LVGrayDrawBuf::ConvertToBitmap(bool flgDither)
{
    if (_bpp==1)
        return;
    // TODO: implement for byte per pixel mode
    const size_t sz = GetRowSize();
    lUInt8 * bitmap = (lUInt8*) calloc(sz, sizeof(*bitmap));
    if (flgDither)
    {
        static const lUInt8 cmap[4][4] = {
            { 0, 0, 0, 0},
            { 0, 0, 1, 0},
            { 0, 1, 0, 1},
            { 1, 1, 1, 1},
        };
        for (int y=0; y<_dy; y++)
        {
            const lUInt8 * __restrict src = GetScanLine(y);
            lUInt8 * __restrict dst = bitmap + ((_dx+7)/8)*y;
            for (int x=0; x<_dx; x++) {
                int cl = (src[x>>2] >> (6-((x&3)*2)))&3;
                cl = cmap[cl][ (x&1) + ((y&1)<<1) ];
                if (cmap[cl][ (x&1) + ((y&1)<<1) ])
                    dst[x>>3] |= 0x80>>(x&7);
            }
        }
    }
    else
    {
        for (int y=0; y<_dy; y++)
        {
            const lUInt8 * __restrict src = GetScanLine(y);
            lUInt8 * __restrict dst = bitmap + ((_dx+7)/8)*y;
            for (int x=0; x<_dx; x++) {
                const int cl = (src[x>>2] >> (7-((x&3)*2)))&1;
                if (cl)
                    dst[x>>3] |= 0x80>>(x&7);
            }
        }
    }
    free( _data );
    _data = bitmap;
    _bpp = 1;
    _rowsize = (_dx+7)/8;
	CHECK_GUARD_BYTE;
}

#endif

//=======================================================
// 32-bit RGB buffer
//=======================================================

/// invert image
void  LVColorDrawBuf::Invert()
{
}

/// get buffer bits per pixel
int  LVColorDrawBuf::GetBitsPerPixel() const
{
    return _bpp;
}

void LVColorDrawBuf::Draw( LVImageSourceRef img, int x, int y, int width, int height, bool dither )
{
    //fprintf( stderr, "LVColorDrawBuf::Draw( img(%d, %d), %d, %d, %d, %d\n", img->GetWidth(), img->GetHeight(), x, y, width, height );
    LVImageScaledDrawCallback drawcb( this, img, x, y, width, height, dither, _invertImages, _smoothImages );
    img->Decode( &drawcb );
    _drawnImagesCount++;
    _drawnImagesSurface += width*height;
}

/// fills buffer with specified color
void LVColorDrawBuf::Clear( lUInt32 color )
{
    // NOTE: Guard against _dx <= 0?
    if ( _bpp==16 ) {
        const lUInt16 cl16 = rgb888to565(color);
        for (int y=0; y<_dy; y++)
        {
            lUInt16 * __restrict dst = (lUInt16 *)GetScanLine(y);
            size_t px_count = _dx;
            while (px_count--)
            {
                *dst++ = cl16;
            }
        }
    } else {
        const lUInt32 cl32 = color;
        for (int y=0; y<_dy; y++)
        {
            lUInt32 * __restrict dst = (lUInt32 *)GetScanLine(y);
            size_t px_count = _dx;
            while (px_count--)
            {
                *dst++ = cl32;
            }
        }
    }
}


/// get pixel value
lUInt32 LVColorDrawBuf::GetPixel( int x, int y ) const
{
    if (!_data || y<0 || x<0 || y>=_dy || x>=_dx)
        return 0;
    if ( _bpp==16 )
        return rgb565to888(((lUInt16*)GetScanLine(y))[x]);
    return ((lUInt32*)GetScanLine(y))[x];
}

inline static lUInt32 AA(lUInt32 color) {
    return (color >> 24) & 0xFF;
}

inline static lUInt32 RR(lUInt32 color) {
	return (color >> 16) & 0xFF;
}

inline static lUInt32 GG(lUInt32 color) {
	return (color >> 8) & 0xFF;
}

inline static lUInt32 BB(lUInt32 color) {
	return color & 0xFF;
}

#if 0
inline static lUInt32 RRGGBB(lUInt32 r, lUInt32 g, lUInt32 b) {
	return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}
#endif

inline static lUInt32 AARRGGBB(lUInt32 a, lUInt32 r, lUInt32 g, lUInt32 b) {
    return ((a & 0xFF) << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}



/// get linearly interpolated pixel value (coordinates are fixed floating points *16)
lUInt32 LVBaseDrawBuf::GetInterpolatedColor(int x16, int y16) const
{
	const int shx = x16 & 0x0F;
	const int shy = y16 & 0x0F;
	const int nshx = 16 - shx;
	const int nshy = 16 - shy;
	const int x = x16 >> 4;
	const int y = y16 >> 4;
	int x1 = x + 1;
	int y1 = y + 1;
	if (x1 >= _dx)
		x1 = x;
	if (y1 >= _dy)
		y1 = y;
    lUInt32 cl00 = GetPixel(x, y);
    lUInt32 cl01 = GetPixel(x1, y);
    lUInt32 cl10 = GetPixel(x, y1);
    lUInt32 cl11 = GetPixel(x1, y1);
    lUInt32 a = (((AA(cl00) * nshx + AA(cl01) * shx) * nshy +
                  (AA(cl10) * nshx + AA(cl11) * shx) * shy) >> 8) & 0xFF;
    lUInt32 r = (((RR(cl00) * nshx + RR(cl01) * shx) * nshy +
                  (RR(cl10) * nshx + RR(cl11) * shx) * shy) >> 8) & 0xFF;
	lUInt32 g = (((GG(cl00) * nshx + GG(cl01) * shx) * nshy +
                  (GG(cl10) * nshx + GG(cl11) * shx) * shy) >> 8) & 0xFF;
	lUInt32 b = (((BB(cl00) * nshx + BB(cl01) * shx) * nshy +
                  (BB(cl10) * nshx + BB(cl11) * shx) * shy) >> 8) & 0xFF;
    return AARRGGBB(a, r, g, b);
}

/// get average pixel value for area (coordinates are fixed floating points *16)
lUInt32 LVBaseDrawBuf::GetAvgColor(lvRect & rc16) const
{
    if (!_data)
        return 0;
    int x0 = rc16.left;
    int y0 = rc16.top;
    int x1 = rc16.right;
    int y1 = rc16.bottom;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    const int maxxx = _dx << 4;
    const int maxyy = _dy << 4;
    if (x1 > maxxx)
        x1 = maxxx;
    if (y1 > maxyy)
        y1 = maxyy;
    if (x0 > x1 || y0 > y1)
        return 0; // invalid rectangle
    int rs = 0;
    int gs = 0;
    int bs = 0;
    int s = 0;
    const int maxy = ((y1 - 1) >> 4);
    const int maxx = ((x1 - 1) >> 4);
    for (int y = (y0 >> 4); y <= maxy; y++ ) {
        int yy0 = y << 4;
        int yy1 = (y + 1) << 4;
        if (yy0 < y0)
            yy0 = y0;
        if (yy1 > y1)
            yy1 = y1;
        const int ys = yy1 - yy0; // 0..16
        if (ys < 1)
            continue;
        for (int x = (x0 >> 4); x <= maxx; x++ ) {

            int xx0 = x << 4;
            int xx1 = (x + 1) << 4;
            if (xx0 < x0)
                xx0 = x0;
            if (xx1 > x1)
                xx1 = x1;
            const int xs = xx1 - xx0; // 0..16
            if (xs < 1)
                continue;

            const int mult = xs * ys;

            const lUInt32 pixel = GetPixel(x, y);
            const int r = (pixel >> 16) & 0xFF;
            const int g = (pixel >> 8) & 0xFF;
            const int b = pixel & 0xFF;

            rs += r * mult;
            gs += g * mult;
            bs += b * mult;
            s += mult;
        }
    }

    if (s == 0)
        return 0;
    rs = (rs / s) & 0xFF;
    gs = (gs / s) & 0xFF;
    bs = (bs / s) & 0xFF;
    return (rs << 16) | (gs << 8) | bs;
}

/// fills rectangle with specified color
void LVColorDrawBuf::FillRect( int x0, int y0, int x1, int y1, lUInt32 color )
{
    if (x0<_clip.left)
        x0 = _clip.left;
    if (y0<_clip.top)
        y0 = _clip.top;
    if (x1>_clip.right)
        x1 = _clip.right;
    if (y1>_clip.bottom)
        y1 = _clip.bottom;
    if (x0>=x1 || y0>=y1)
        return;
    const lUInt8 alpha = (color >> 24) & 0xFF;
    if (alpha == 0xFF) // Fully transparent color
        return;
    if ( _bpp==16 ) {
        const lUInt16 cl16 = rgb888to565(color);
        for (int y=y0; y<y1; y++)
        {
            lUInt16 * __restrict line = (lUInt16 *)GetScanLine(y);
            for (int x=x0; x<x1; x++)
            {
                if (alpha)
                    ApplyAlphaRGB565(line[x], cl16, alpha);
                else
                    line[x] = cl16;
            }
        }
    } else {
        for (int y=y0; y<y1; y++)
        {
            lUInt32 * __restrict line = (lUInt32 *)GetScanLine(y);
            for (int x=x0; x<x1; x++)
            {
                if (alpha)
                    ApplyAlphaRGB(line[x], color, alpha);
                else
                    line[x] = color;
            }
        }
    }
}

void LVColorDrawBuf::DrawLine(int x0, int y0, int x1, int y1, lUInt32 color0, int length1, int length2, int direction)
{
    if (x0<_clip.left)
        x0 = _clip.left;
    if (y0<_clip.top)
        y0 = _clip.top;
    if (x1>_clip.right)
        x1 = _clip.right;
    if (y1>_clip.bottom)
        y1 = _clip.bottom;
    if (x0>=x1 || y0>=y1)
        return;
    const lUInt8 alpha = (color0 >> 24) & 0xFF;
    if (alpha == 0xFF) // Fully transparent color
        return;
    if ( _bpp==16 ) {
        const lUInt16 cl16 = rgb888to565(color0);
        for (int y=y0; y<y1; y++)
        {
            lUInt16 * __restrict line = (lUInt16 *)GetScanLine(y);
            for (int x=x0; x<x1; x++)
            {
                if ( (direction==0 &&x%(length1+length2)<length1) ||
                     (direction==1 &&y%(length1+length2)<length1) ) {
                    if (alpha)
                        ApplyAlphaRGB565(line[x], cl16, alpha);
                    else
                        line[x] = cl16;
                }
            }
        }
    } else {
        const lUInt32 cl32 = color0;
        for (int y=y0; y<y1; y++)
        {
            lUInt32 * __restrict line = (lUInt32 *)GetScanLine(y);
            for (int x=x0; x<x1; x++)
            {
                if ( (direction==0 &&x%(length1+length2)<length1) ||
                     (direction==1 &&y%(length1+length2)<length1) ) {
                    if (alpha)
                        ApplyAlphaRGB(line[x], cl32, alpha);
                    else
                        line[x] = cl32;
                }
            }
        }
    }
}

#if 0

/// fills rectangle with specified color
void LVColorDrawBuf::FillRectPattern( int x0, int y0, int x1, int y1, lUInt32 color0, lUInt32 color1, const lUInt8 * __restrict pattern )
{
    if (x0<_clip.left)
        x0 = _clip.left;
    if (y0<_clip.top)
        y0 = _clip.top;
    if (x1>_clip.right)
        x1 = _clip.right;
    if (y1>_clip.bottom)
        y1 = _clip.bottom;
    if (x0>=x1 || y0>=y1)
        return;
    if ( _bpp==16 ) {
        const lUInt16 cl16_0 = rgb888to565(color0);
        const lUInt16 cl16_1 = rgb888to565(color1);
        for (int y=y0; y<y1; y++)
        {
            const lUInt8 patternMask = pattern[y & 3];
            lUInt16 * __restrict line = (lUInt16 *)GetScanLine(y);
            for (int x=x0; x<x1; x++)
            {
                const lUInt8 patternBit = (patternMask << (x&7)) & 0x80;
                line[x] = patternBit ? cl16_1 : cl16_0;
            }
        }
    } else {
        for (int y=y0; y<y1; y++)
        {
            const lUInt8 patternMask = pattern[y & 3];
            lUInt32 * __restrict line = (lUInt32 *)GetScanLine(y);
            for (int x=x0; x<x1; x++)
            {
                const lUInt8 patternBit = (patternMask << (x&7)) & 0x80;
                line[x] = patternBit ? color1 : color0;
            }
        }
    }
}

#endif

/// sets new size
void LVColorDrawBuf::Resize( int dx, int dy )
{
    if ( dx==_dx && dy==_dy ) {
    	//CRLog::trace("LVColorDrawBuf::Resize : no resize, not changed");
    	return;
    }
    if ( !_ownData ) {
    	//CRLog::trace("LVColorDrawBuf::Resize : no resize, own data");
        return;
    }
    //CRLog::trace("LVColorDrawBuf::Resize : resizing %d x %d to %d x %d", _dx, _dy, dx, dy);
    // delete old bitmap
    if ( _dx>0 && _dy>0 && _data )
    {
#if !defined(__SYMBIAN32__) && defined(_WIN32) && !defined(QT_GL)
        if (_drawbmp)
            DeleteObject( _drawbmp );
        if (_drawdc)
            DeleteObject( _drawdc );
        _drawbmp = NULL;
        _drawdc = NULL;
#else
        free(_data);
#endif
        _data = NULL;
        _rowsize = 0;
        _dx = 0;
        _dy = 0;
    }

    if (dx>0 && dy>0)
    {
        // create new bitmap
        _dx = dx;
        _dy = dy;
        _rowsize = dx*(_bpp>>3);
#if !defined(__SYMBIAN32__) && defined(_WIN32) && !defined(QT_GL)
        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
        bmi.bmiHeader.biWidth = _dx;
        bmi.bmiHeader.biHeight = _dy;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        bmi.bmiHeader.biSizeImage = 0;
        bmi.bmiHeader.biXPelsPerMeter = 1024;
        bmi.bmiHeader.biYPelsPerMeter = 1024;
        bmi.bmiHeader.biClrUsed = 0;
        bmi.bmiHeader.biClrImportant = 0;

        _drawbmp = CreateDIBSection( NULL, &bmi, DIB_RGB_COLORS, (void**)(&_data), NULL, 0 );
        _drawdc = CreateCompatibleDC(NULL);
        SelectObject(_drawdc, _drawbmp);
        memset( _data, 0, _rowsize * _dy );
#else
        _data = (lUInt8 *)calloc((_bpp>>3) * _dx * _dy, sizeof(*_data));
#endif
    }
    SetClipRect( NULL );
}

void LVColorDrawBuf::InvertRect(int x0, int y0, int x1, int y1)
{
    if (x0<_clip.left)
        x0 = _clip.left;
    if (y0<_clip.top)
        y0 = _clip.top;
    if (x1>_clip.right)
        x1 = _clip.right;
    if (y1>_clip.bottom)
        y1 = _clip.bottom;
    if (x0>=x1 || y0>=y1)
        return;

    if (_bpp==16) {
        for (int y=y0; y<y1; y++) {
            lUInt16 * __restrict line = (lUInt16 *)GetScanLine(y);
            for (int x=x0; x<x1; x++) {
                line[x] ^= 0xFFFF;
            }
        }
    }
    else {
        for (int y=y0; y<y1; y++) {
            lUInt32 * __restrict line = (lUInt32 *)GetScanLine(y);
            for (int x=x0; x<x1; x++) {
                line[x] ^= 0x00FFFFFF;
            }
        }
    }
}

/// draws bitmap (1 byte per pixel) using specified palette
void LVColorDrawBuf::Draw( int x, int y, const lUInt8 * bitmap, int width, int height, const lUInt32 * __restrict palette )
{
    if ( !_data )
        return;
    //int buf_width = _dx; /* 2bpp */
    const int initial_height = height;
    int bx = 0;
    int by = 0;
    const int bmp_width = width;
    const lUInt32 bmpcl = palette?palette[0]:GetTextColor();

    if (x<_clip.left)
    {
        width += x-_clip.left;
        bx -= x-_clip.left;
        x = _clip.left;
        if (width<=0)
            return;
    }
    if (y<_clip.top)
    {
        height += y-_clip.top;
        by -= y-_clip.top;
        y = _clip.top;
        if (_hidePartialGlyphs && height<=initial_height/2) // HIDE PARTIAL VISIBLE GLYPHS
            return;
        if (height<=0)
            return;
    }
    if (x + width > _clip.right)
    {
        width = _clip.right - x;
    }
    if (width<=0)
        return;
    if (y + height > _clip.bottom)
    {
        if (_hidePartialGlyphs && height<=initial_height/2) // HIDE PARTIAL VISIBLE GLYPHS
            return;
        int clip_bottom = _clip.bottom;
        if (_hidePartialGlyphs )
            clip_bottom = this->_dy;
        if ( y+height > clip_bottom)
            height = clip_bottom - y;
    }
    if (height<=0)
        return;

    bitmap += bx + by*bmp_width;

    if ( _bpp==16 ) {
        const lUInt16 bmpcl16 = rgb888to565(bmpcl);
        const lUInt8 bopacity = ~bmpcl >> 24; // alpha=0x00 => opacity=0xFF
        if (bopacity == 0) // Fully transparent color
            return;

        while (height--)
        {
            const lUInt8 * __restrict src = bitmap;
            lUInt16 * __restrict dst = ((lUInt16*)GetScanLine(y++)) + x;

            size_t px_count = width;
            while (px_count--)
            {
                // Note: former code was considering pixel opacity >= 0xF0 as fully opaque (0xFF),
                // not sure why (it would save on the blending computation by 5% to 15%).
                lUInt8 opacity = *(src++); // glyph pixel opacity
                if ( opacity == 0 ) {
                    // Background pixel, NOP
                }
                else if (opacity == 0xFF && bopacity == 0xFF) { // fully opaque pixel and color
                    *dst = bmpcl16;
                }
                else {
                    opacity = (opacity*bopacity)>>8;
                    const lUInt8 alpha = opacity ^ 0xFF;
                    const lUInt32 r = ((((*dst) & 0xF800) * alpha + (bmpcl16 & 0xF800) * opacity) >> 8) & 0xF800;
                    const lUInt32 g = ((((*dst) & 0x07E0) * alpha + (bmpcl16 & 0x07E0) * opacity) >> 8) & 0x07E0;
                    const lUInt32 b = ((((*dst) & 0x001F) * alpha + (bmpcl16 & 0x001F) * opacity) >> 8) & 0x001F;
                    *dst = (lUInt16)(r | g | b);
                }
                dst++;
            }
            /* new src line, to account for clipping */
            bitmap += bmp_width;
        }
    } else {
        const lUInt32 bmpcl32 = bmpcl & 0x00FFFFFF;
        const lUInt8 bopacity = ~bmpcl >> 24; // alpha=0x00 => opacity=0xFF
        if (bopacity == 0) // Fully transparent color
            return;

        while (height--)
        {
            const lUInt8 * __restrict src = bitmap;
            lUInt32 * __restrict dst = ((lUInt32*)GetScanLine(y++)) + x;

            size_t px_count = width;
            while (px_count--)
            {
                // Note: former code was considering pixel opacity >= 0xF0 as fully opaque (0xFF),
                // not sure why (it would save on the blending computation by 5% to 15%).
                lUInt8 opacity = *(src++); // glyph pixel opacity
                if ( opacity == 0 ) {
                    // Background pixel, NOP
                }
                else if (opacity == 0xFF && bopacity == 0xFF) { // fully opaque pixel and color
                    *dst = bmpcl32;
                }
                else {
                    opacity = (opacity*bopacity)>>8;
                    const lUInt8 alpha = opacity ^ 0xFF;
                    const lUInt32 n1 = ((((*dst) & 0xFF00FF) * alpha + (bmpcl32 & 0xFF00FF) * opacity) >> 8) & 0xFF00FF;
                    const lUInt32 n2 = ((((*dst) & 0x00FF00) * alpha + (bmpcl32 & 0x00FF00) * opacity) >> 8) & 0x00FF00;
                    *dst = n1 | n2;
                }

                dst++;
            }
            bitmap += bmp_width;
        }
    }
}

#if 0

#if !defined(__SYMBIAN32__) && defined(_WIN32) && !defined(QT_GL)
/// draws buffer content to DC doing color conversion if necessary
void LVGrayDrawBuf::DrawTo( HDC dc, int x, int y, int options, const lUInt32 * __restrict palette )
{
    if (!dc || !_data)
        return;
    LVColorDrawBuf buf( _dx, 1 );
    lUInt32 * __restrict dst = (lUInt32 *)buf.GetScanLine(0);
#if (GRAY_INVERSE==1)
    static lUInt32 def_pal_1bpp[2] = {0xFFFFFF, 0x000000};
    static lUInt32 def_pal_2bpp[4] = {0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000};
#else
    static lUInt32 def_pal_1bpp[2] = {0x000000, 0xFFFFFF};
    static lUInt32 def_pal_2bpp[4] = {0x000000, 0x555555, 0xAAAAAA, 0xFFFFFF};
#endif
	lUInt32 pal[256];
	if ( _bpp<=8 ) {
		const int n = 1<<_bpp;
		for ( int i=0; i<n; i++ ) {
			int c = 255 * i / (n-1);
			pal[i] = c | (c<<8) | (c<<16);
		}
	}
    if (!palette)
        palette = (_bpp==1) ? def_pal_1bpp : def_pal_2bpp;
    for (int yy=0; yy<_dy; yy++)
    {
        lUInt8 * __restrict src = GetScanLine(yy);
        for (int xx=0; xx<_dx; xx++)
        {
            //
            if (_bpp==1)
            {
                const int shift = 7-(xx&7);
                const int x0 = xx >> 3;
                dst[xx] = palette[ (src[x0]>>shift) & 1];
            }
            else if (_bpp==2)
            {
                const int shift = 6-((xx&3)<<1);
                const int x0 = xx >> 2;
                dst[xx] = palette[ (src[x0]>>shift) & 3];
            }
            else // 3,4,8
            {
                const int index = (src[xx] >> (8-_bpp)) & ((1<<_bpp)-1);
                dst[xx] = pal[ index ];
            }
        }
        BitBlt( dc, x, y+yy, _dx, 1, buf.GetDC(), 0, 0, SRCCOPY );
    }
}


/// draws buffer content to DC doing color conversion if necessary
void LVColorDrawBuf::DrawTo( HDC dc, int x, int y, int options, const lUInt32 * __restrict palette )
{
    if (dc!=NULL && _drawdc!=NULL)
        BitBlt( dc, x, y, _dx, _dy, _drawdc, 0, 0, SRCCOPY );
}
#endif

/// draws buffer content to another buffer doing color conversion if necessary
void LVGrayDrawBuf::DrawTo( LVDrawBuf * __restrict buf, int x, int y, int options, const lUInt32 * __restrict palette )
{
    CR_UNUSED2(options, palette);
    lvRect clip;
    buf->GetClipRect(&clip);

	if ( !(!clip.isEmpty() || buf->GetBitsPerPixel()!=GetBitsPerPixel() || GetWidth()!=buf->GetWidth() || GetHeight()!=buf->GetHeight()) ) {
		// simple copy
        memcpy( buf->GetScanLine(0), GetScanLine(0), GetHeight() * GetRowSize() );
		return;
	}
    const int bpp = GetBitsPerPixel();
    if (buf->GetBitsPerPixel() != bpp)
		return; // not supported yet

	if (buf->GetBitsPerPixel() == 32) {
		// support for 32bpp to Gray drawing
	    for (int yy=0; yy<_dy; yy++)
	    {
	        if (y+yy >= clip.top && y+yy < clip.bottom)
	        {
	            const lUInt8 * __restrict src = (lUInt8 *)GetScanLine(yy);
                lUInt32 * __restrict dst = ((lUInt32 *)buf->GetScanLine(y+yy)) + x;
	            if (bpp==1)
	            {
	                int shift = x & 7;
	                for (int xx=0; xx<_dx; xx++)
	                {
	                    if ( x+xx >= clip.left && x+xx < clip.right )
	                    {
	                        const lUInt8 cl = (*src << shift) & 0x80;
	                        *dst = cl ? 0xFFFFFF : 0x000000;
	                    }
	                    dst++;
	                    if (++shift >= 8) {
	                    	shift = 0;
		                    src++;
	                    }

	                }
	            }
	            else if (bpp==2)
	            {
	                int shift = x & 3;
	                for (int xx=0; xx<_dx; xx++)
	                {
	                    if ( x+xx >= clip.left && x+xx < clip.right )
	                    {
	                        lUInt32 cl = (*src << (shift<<1)) & 0xC0;
	                        cl = cl | (cl >> 2) | (cl>>4) | (cl>>6);
	                        *dst = cl | (cl << 8) | (cl << 16);
	                    }
	                    dst++;
	                    if (++shift >= 4) {
	                    	shift = 0;
		                    src++;
	                    }

	                }
	            }
	            else
	            {
	            	// byte per pixel
	                for (int xx=0; xx<_dx; xx++)
	                {
	                    if ( x+xx >= clip.left && x+xx < clip.right )
	                    {
	                        lUInt32 cl = *src;
	                        if (bpp == 3) {
	                        	cl &= 0xE0;
	                        	cl = cl | (cl>>3) | (cl>>6);
	                        } else if (bpp == 4) {
	                        	cl &= 0xF0;
	                        	cl = cl | (cl>>4);
	                        }
	                        *dst = cl | (cl << 8) | (cl << 16);
	                    }
	                    dst++;
	                    src++;
	                }
	            }
	        }
	    }
	    return;
	} else if (buf->GetBitsPerPixel() == 16) {
		// support for 32bpp to Gray drawing
	    for (int yy=0; yy<_dy; yy++)
	    {
	        if (y+yy >= clip.top && y+yy < clip.bottom)
	        {
	            const lUInt8 * __restrict src = (lUInt8 *)GetScanLine(yy);
                lUInt16 * __restrict dst = ((lUInt16 *)buf->GetScanLine(y+yy)) + x;
	            if (bpp==1)
	            {
	                int shift = x & 7;
	                for (int xx=0; xx<_dx; xx++)
	                {
	                    if ( x+xx >= clip.left && x+xx < clip.right )
	                    {
	                        const lUInt8 cl = (*src << shift) & 0x80;
	                        *dst = cl ? 0xFFFF : 0x0000;
	                    }
	                    dst++;
	                    if (++shift >= 8) {
	                    	shift = 0;
		                    src++;
	                    }

	                }
	            }
	            else if (bpp==2)
	            {
	                int shift = x & 3;
	                for (int xx=0; xx<_dx; xx++)
	                {
	                    if ( x+xx >= clip.left && x+xx < clip.right )
	                    {
	                        lUInt16 cl = (*src << (shift<<1)) & 0xC0;
	                        cl = cl | (cl >> 2) | (cl>>4) | (cl>>6);
	                        *dst = rgb565(cl, cl, cl);
	                    }
	                    dst++;
	                    if (++shift >= 4) {
	                    	shift = 0;
		                    src++;
	                    }

	                }
	            }
	            else
	            {
	            	// byte per pixel
	                for (int xx=0; xx<_dx; xx++)
	                {
	                    if ( x+xx >= clip.left && x+xx < clip.right )
	                    {
	                        lUInt16 cl = *src;
	                        if (bpp == 3) {
	                        	cl &= 0xE0;
	                        	cl = cl | (cl>>3) | (cl>>6);
	                        } else if (bpp == 4) {
	                        	cl &= 0xF0;
	                        	cl = cl | (cl>>4);
	                        }
	                        *dst = rgb565(cl, cl, cl);
	                    }
	                    dst++;
	                    src++;
	                }
	            }
	        }
	    }
	    return;
	}
    for (int yy=0; yy<_dy; yy++)
    {
        if (y+yy >= clip.top && y+yy < clip.bottom)
        {
            const lUInt8 * __restrict src = (lUInt8 *)GetScanLine(yy);
            if (bpp==1)
            {
                const int shift = x & 7;
                lUInt8 * __restrict dst = buf->GetScanLine(y+yy) + (x>>3);
                for (int xx=0; xx<_dx; xx+=8)
                {
                    if ( x+xx >= clip.left && x+xx < clip.right )
                    {
                        //lUInt8 mask = ~((lUInt8)0xC0>>shift);
                        const lUInt16 cl = (*src << 8)>>shift;
                        const lUInt16 mask = (0xFF00)>>shift;
						lUInt8 c = *dst;
						c &= ~(mask>>8);
						c |= (cl>>8);
                        *dst = c;
                        if (mask & 0xFF) {
                            c = *(dst+1);
                            c &= ~(mask&0xFF);
                            c |= (cl&0xFF);
                            *(dst+1) = c;
                        }
                    }
                    dst++;
                    src++;
                }
            }
            else if (bpp==2)
            {
                const int shift = (x & 3) * 2;
                lUInt8 * __restrict dst = buf->GetScanLine(y+yy) + (x>>2);
                for (int xx=0; xx<_dx; xx+=4)
                {
                    if ( x+xx >= clip.left && x+xx < clip.right )
                    {
                        //lUInt8 mask = ~((lUInt8)0xC0>>shift);
                        const lUInt16 cl = (*src << 8)>>shift;
                        const lUInt16 mask = (0xFF00)>>shift;
						lUInt8 c = *dst;
						c &= ~(mask>>8);
						c |= (cl>>8);
                        *dst = c;
                        if (mask & 0xFF) {
                            c = *(dst+1);
                            c &= ~(mask&0xFF);
                            c |= (cl&0xFF);
                            *(dst+1) = c;
                        }
                    }
                    dst++;
                    src++;
                }
            }
            else
            {
                lUInt8 * __restrict dst = buf->GetScanLine(y+yy) + x;
                for (int xx=0; xx<_dx; xx++)
                {
                    if ( x+xx >= clip.left && x+xx < clip.right )
                    {
                        *dst = *src;
                    }
                    dst++;
                    src++;
                }
            }
        }
    }
	CHECK_GUARD_BYTE;
}

/// draws buffer content to another buffer doing color conversion if necessary
void LVGrayDrawBuf::DrawOnTop( LVDrawBuf * __restrict buf, int x, int y )
{
    lvRect clip;
    buf->GetClipRect(&clip);

    if ( !(!clip.isEmpty() || buf->GetBitsPerPixel()!=GetBitsPerPixel() || GetWidth()!=buf->GetWidth() || GetHeight()!=buf->GetHeight()) ) {
        // simple copy
        memcpy( buf->GetScanLine(0), GetScanLine(0), GetHeight() * GetRowSize() );
        return;
    }
    const int bpp = GetBitsPerPixel();
    if (buf->GetBitsPerPixel() == 32) {
        // support for 32bpp to Gray drawing
        for (int yy=0; yy<_dy; yy++)
        {
            if (y+yy >= clip.top && y+yy < clip.bottom)
            {
                const lUInt8 * __restrict src = (lUInt8 *)GetScanLine(yy);
                lUInt32 * __restrict dst = ((lUInt32 *)buf->GetScanLine(y+yy)) + x;
                if (bpp==1)
                {
                    int shift = x & 7;
                    for (int xx=0; xx<_dx; xx++)
                    {
                        if ( x+xx >= clip.left && x+xx < clip.right )
                        {
                            const lUInt8 cl = (*src << shift) & 0x80;
                            if(src!=0) *dst = cl ? 0xFFFFFF : 0x000000;
                        }
                        dst++;
                        if (++shift >= 8) {
                            shift = 0;
                            src++;
                        }

                    }
                }
                else if (bpp==2)
                {
                    int shift = x & 3;
                    for (int xx=0; xx<_dx; xx++)
                    {
                        if ( x+xx >= clip.left && x+xx < clip.right )
                        {
                            lUInt32 cl = (*src << (shift<<1)) & 0xC0;
                            cl = cl | (cl >> 2) | (cl>>4) | (cl>>6);
                            if(src!=0) *dst = cl | (cl << 8) | (cl << 16);
                        }
                        dst++;
                        if (++shift >= 4) {
                            shift = 0;
                            src++;
                        }

                    }
                }
                else
                {
                    // byte per pixel
                    for (int xx=0; xx<_dx; xx++)
                    {
                        if ( x+xx >= clip.left && x+xx < clip.right )
                        {
                            lUInt32 cl = *src;
                            if (bpp == 3) {
                                cl &= 0xE0;
                                cl = cl | (cl>>3) | (cl>>6);
                            } else if (bpp == 4) {
                                cl &= 0xF0;
                                cl = cl | (cl>>4);
                            }
                            if(src!=0) *dst = cl | (cl << 8) | (cl << 16);
                        }
                        dst++;
                        src++;
                    }
                }
            }
        }
        return;
    }
    if (buf->GetBitsPerPixel() == 16) {
        // support for 32bpp to Gray drawing
        for (int yy=0; yy<_dy; yy++)
        {
            if (y+yy >= clip.top && y+yy < clip.bottom)
            {
                const lUInt8 * __restrict src = (lUInt8 *)GetScanLine(yy);
                lUInt16 * __restrict dst = ((lUInt16 *)buf->GetScanLine(y+yy)) + x;
                if (bpp==1)
                {
                    int shift = x & 7;
                    for (int xx=0; xx<_dx; xx++)
                    {
                        if ( x+xx >= clip.left && x+xx < clip.right )
                        {
                            const lUInt8 cl = (*src << shift) & 0x80;
                            if(*src!=0) *dst = cl ? 0xFFFF : 0x0000;
                        }
                        dst++;
                        if (++shift >= 8) {
                            shift = 0;
                            src++;
                        }

                    }
                }
                else if (bpp==2)
                {
                    int shift = x & 3;
                    for (int xx=0; xx<_dx; xx++)
                    {
                        if ( x+xx >= clip.left && x+xx < clip.right )
                        {
                            lUInt16 cl = (*src << (shift<<1)) & 0xC0;
                            cl = cl | (cl >> 2) | (cl>>4) | (cl>>6);
                            if(*src!=0) *dst = rgb565(cl, cl, cl);
                        }
                        dst++;
                        if (++shift >= 4) {
                            shift = 0;
                            src++;
                        }

                    }
                }
                else
                {
                    // byte per pixel
                    for (int xx=0; xx<_dx; xx++)
                    {
                        if ( x+xx >= clip.left && x+xx < clip.right )
                        {
                            lUInt16 cl = *src;
                            if (bpp == 3) {
                                cl &= 0xE0;
                                cl = cl | (cl>>3) | (cl>>6);
                            } else if (bpp == 4) {
                                cl &= 0xF0;
                                cl = cl | (cl>>4);
                            }
                            if(*src!=0) *dst = rgb565(cl, cl, cl);
                        }
                        dst++;
                        src++;
                    }
                }
            }
        }
        return;
    }
    if (buf->GetBitsPerPixel() != bpp)
        return; // not supported yet
    for (int yy=0; yy<_dy; yy++)
    {
        if (y+yy >= clip.top && y+yy < clip.bottom)
        {
            const lUInt8 * __restrict src = (lUInt8 *)GetScanLine(yy);
            if (bpp==1)
            {
                const int shift = x & 7;
                lUInt8 * __restrict dst = buf->GetScanLine(y+yy) + (x>>3);
                for (int xx=0; xx<_dx; xx+=8)
                {
                    if ( x+xx >= clip.left && x+xx < clip.right )
                    {
                        //lUInt8 mask = ~((lUInt8)0xC0>>shift);
                        const lUInt16 cl = (*src << 8)>>shift;
                        const lUInt16 mask = (0xFF00)>>shift;
                        lUInt8 c = *dst;
                        c &= ~(mask>>8);
                        c |= (cl>>8);
                        if(*src!=0) *dst = c;
                        if (mask & 0xFF) {
                            c = *(dst+1);
                            c &= ~(mask&0xFF);
                            c |= (cl&0xFF);
                            if(*src!=0) *(dst+1) = c;
                        }
                    }
                    dst++;
                    src++;
                }
            }
            else if (bpp==2)
            {
                const int shift = (x & 3) * 2;
                lUInt8 * __restrict dst = buf->GetScanLine(y+yy) + (x>>2);
                for (int xx=0; xx<_dx; xx+=4)
                {
                    if ( x+xx >= clip.left && x+xx < clip.right )
                    {
                        //lUInt8 mask = ~((lUInt8)0xC0>>shift);
                        const lUInt16 cl = (*src << 8)>>shift;
                        const lUInt16 mask = (0xFF00)>>shift;
                        lUInt8 c = *dst;
                        c &= ~(mask>>8);
                        c |= (cl>>8);
                        if(*src!=0) *dst = c;
                        if (mask & 0xFF) {
                            c = *(dst+1);
                            c &= ~(mask&0xFF);
                            c |= (cl&0xFF);
                            if(*src!=0) *(dst+1) = c;
                        }
                    }
                    dst++;
                    src++;
                }
            }
            else
            {
                lUInt8 * __restrict dst = buf->GetScanLine(y+yy) + x;
                for (int xx=0; xx<_dx; xx++)
                {
                    if ( x+xx >= clip.left && x+xx < clip.right )
                    {
                        if(*src!=0) *dst = *src;
                    }
                    dst++;
                    src++;
                }
            }
        }
    }
    CHECK_GUARD_BYTE;
}

/// draws buffer content to another buffer doing color conversion if necessary
void LVColorDrawBuf::DrawTo( LVDrawBuf * __restrict buf, int x, int y, int options, const lUInt32 * __restrict palette )
{
    CR_UNUSED(options);
    CR_UNUSED(palette);
    //
    if ( !_data )
        return;
    lvRect clip;
    buf->GetClipRect(&clip);
    const int bpp = buf->GetBitsPerPixel();
    for (int yy=0; yy<_dy; yy++) {
        if (y+yy >= clip.top && y+yy < clip.bottom) {
            if ( _bpp==16 ) {
                const lUInt16 * __restrict src = (lUInt16 *)GetScanLine(yy);
                if (bpp == 1) {
                    int shift = x & 7;
                    lUInt8 * __restrict dst = buf->GetScanLine(y+yy) + (x>>3);
                    for (int xx=0; xx<_dx; xx++) {
                        if (x + xx >= clip.left && x + xx < clip.right) {
                            //lUInt8 mask = ~((lUInt8)0xC0>>shift);
    #if (GRAY_INVERSE==1)
                            const lUInt8 cl = (((lUInt8)(*src)&0x8000)^0x8000) >> (shift+8);
    #else
                            const lUInt8 cl = (((lUInt8)(*src)&0x8000)) >> (shift+8);
    #endif
                            *dst |= cl;
                        }
                        if (!((shift = (shift + 1) & 7)))
                            dst++;
                        src++;
                    }
                } else if (bpp == 2) {
                    int shift = x & 3;
                    lUInt8 * __restrict dst = buf->GetScanLine(y+yy) + (x>>2);
                    for (int xx=0; xx < _dx; xx++) {
                        if ( x+xx >= clip.left && x+xx < clip.right ) {
                            //lUInt8 mask = ~((lUInt8)0xC0>>shift);
    #if (GRAY_INVERSE==1)
                            const lUInt8 cl = (((lUInt8)(*src)&0xC000)^0xC000) >> ((shift<<1) + 8);
    #else
                            const lUInt8 cl = (((lUInt8)(*src)&0xC000)) >> ((shift<<1) + 8);
    #endif
                            *dst |= cl;
                        }
                        if (!((shift = ((shift + 1) & 3))))
                            dst++;
                        src++;
                    }
                } else if (bpp<=8) {
                    lUInt8 * __restrict dst = buf->GetScanLine(y+yy) + x;
                    for (int xx=0; xx<_dx; xx++) {
                        if ( x+xx >= clip.left && x+xx < clip.right ) {
                            //lUInt8 mask = ~((lUInt8)0xC0>>shift);
                            *dst = (lUInt8)(*src >> 8);
                        }
                        dst++;
                        src++;
                    }
                } else if (bpp == 16) {
                    lUInt16 * __restrict dst = ((lUInt16 *)buf->GetScanLine(y + yy)) + x;
                    for (int xx=0; xx < _dx; xx++) {
                        if (x + xx >= clip.left && x + xx < clip.right) {
                            *dst = *src;
                        }
                        dst++;
                        src++;
                    }
                } else if (bpp == 32) {
                    lUInt32 * __restrict dst = ((lUInt32 *)buf->GetScanLine(y + yy)) + x;
                    for (int xx=0; xx<_dx; xx++) {
                        if ( x+xx >= clip.left && x+xx < clip.right ) {
                            *dst = rgb565to888( *src );
                        }
                        dst++;
                        src++;
                    }
                }
            } else {
                const lUInt32 * __restrict src = (lUInt32 *)GetScanLine(yy);
                if (bpp==1) {
                    int shift = x & 7;
                    lUInt8 * __restrict dst = buf->GetScanLine(y+yy) + (x>>3);
                    for (int xx=0; xx<_dx; xx++) {
                        if ( x+xx >= clip.left && x+xx < clip.right ) {
                            //lUInt8 mask = ~((lUInt8)0xC0>>shift);
    #if (GRAY_INVERSE==1)
                            const lUInt8 cl = (((lUInt8)(*src)&0x80)^0x80) >> (shift);
    #else
                            const lUInt8 cl = (((lUInt8)(*src)&0x80)) >> (shift);
    #endif
                            *dst |= cl;
                        }
                        if (!((shift = (shift + 1) & 7)))
                            dst++;
                        src++;
                    }
                } else if (bpp==2) {
                    int shift = x & 3;
                    lUInt8 * __restrict dst = buf->GetScanLine(y+yy) + (x>>2);
                    for (int xx=0; xx<_dx; xx++) {
                        if ( x+xx >= clip.left && x+xx < clip.right ) {
                            //lUInt8 mask = ~((lUInt8)0xC0>>shift);
    #if (GRAY_INVERSE==1)
                            const lUInt8 cl = (((lUInt8)(*src)&0xC0)^0xC0) >> (shift<<1);
    #else
                            const lUInt8 cl = (((lUInt8)(*src)&0xC0)) >> (shift<<1);
    #endif
                            *dst |= cl;
                        }
                        if (!((shift = (shift + 1) & 3)))
                            dst++;
                        src++;
                    }
                } else if (bpp<=8) {
                    lUInt8 * __restrict dst = buf->GetScanLine(y + yy) + x;
                    for (int xx=0; xx<_dx; xx++) {
                        if (x + xx >= clip.left && x + xx < clip.right) {
                            //lUInt8 mask = ~((lUInt8)0xC0>>shift);
                            *dst = (lUInt8)*src;
                        }
                        dst++;
                        src++;
                    }
                } else if (bpp == 32) {
                    lUInt32 * __restrict dst = ((lUInt32 *)buf->GetScanLine(y + yy)) + x;
                    for (int xx = 0; xx < _dx; xx++) {
                        if (x+xx >= clip.left && x + xx < clip.right) {
                            *dst = *src;
                        }
                        dst++;
                        src++;
                    }
                }
            }
        }
    }
}
/// draws buffer content on top of another buffer doing color conversion if necessary
void LVColorDrawBuf::DrawOnTop( LVDrawBuf * __restrict buf, int x, int y)
{
    //
    if ( !_data )
        return;
    lvRect clip;
    buf->GetClipRect(&clip);
    const int bpp = buf->GetBitsPerPixel();
    for (int yy=0; yy<_dy; yy++) {
        if (y+yy >= clip.top && y+yy < clip.bottom) {
            if ( _bpp==16 ) {
                const lUInt16 * __restrict src = (lUInt16 *)GetScanLine(yy);
                if (bpp == 1) {
                    int shift = x & 7;
                    lUInt8 * __restrict dst = buf->GetScanLine(y+yy) + (x>>3);
                    for (int xx=0; xx<_dx; xx++) {
                        if (x + xx >= clip.left && x + xx < clip.right) {
                            //lUInt8 mask = ~((lUInt8)0xC0>>shift);
#if (GRAY_INVERSE==1)
                            const lUInt8 cl = (((lUInt8)(*src)&0x8000)^0x8000) >> (shift+8);
    #else
                            const lUInt8 cl = (((lUInt8)(*src)&0x8000)) >> (shift+8);
#endif
                            if(cl!=0) *dst |= cl;
                        }
                        if (!((shift = (shift + 1) & 7)))
                            dst++;
                        src++;
                    }
                } else if (bpp == 2) {
                    int shift = x & 3;
                    lUInt8 * __restrict dst = buf->GetScanLine(y+yy) + (x>>2);
                    for (int xx=0; xx < _dx; xx++) {
                        if ( x+xx >= clip.left && x+xx < clip.right ) {
                            //lUInt8 mask = ~((lUInt8)0xC0>>shift);
#if (GRAY_INVERSE==1)
                            const lUInt8 cl = (((lUInt8)(*src)&0xC000)^0xC000) >> ((shift<<1) + 8);
    #else
                            const lUInt8 cl = (((lUInt8)(*src)&0xC000)) >> ((shift<<1) + 8);
#endif
                            if(cl!=0) *dst |= cl;
                        }
                        if (!((shift = ((shift + 1) & 3))))
                            dst++;
                        src++;
                    }
                } else if (bpp<=8) {
                    lUInt8 * __restrict dst = buf->GetScanLine(y+yy) + x;
                    for (int xx=0; xx<_dx; xx++) {
                        if ( x+xx >= clip.left && x+xx < clip.right ) {
                            //lUInt8 mask = ~((lUInt8)0xC0>>shift);
                            if(src!=0) *dst = (lUInt8)(*src >> 8);
                        }
                        dst++;
                        src++;
                    }
                } else if (bpp == 16) {
                    lUInt16 * __restrict dst = ((lUInt16 *)buf->GetScanLine(y + yy)) + x;
                    for (int xx=0; xx < _dx; xx++) {
                        if (x + xx >= clip.left && x + xx < clip.right) {
                            if(src!=0) *dst = *src;
                        }
                        dst++;
                        src++;
                    }
                } else if (bpp == 32) {
                    lUInt32 * __restrict dst = ((lUInt32 *)buf->GetScanLine(y + yy)) + x;
                    for (int xx=0; xx<_dx; xx++) {
                        if ( x+xx >= clip.left && x+xx < clip.right ) {
                            if(src!=0) *dst = rgb565to888( *src );
                        }
                        dst++;
                        src++;
                    }
                }
            } else {
                const lUInt32 * __restrict src = (lUInt32 *)GetScanLine(yy);
                if (bpp==1) {
                    int shift = x & 7;
                    lUInt8 * __restrict dst = buf->GetScanLine(y+yy) + (x>>3);
                    for (int xx=0; xx<_dx; xx++) {
                        if ( x+xx >= clip.left && x+xx < clip.right ) {
                            //lUInt8 mask = ~((lUInt8)0xC0>>shift);
#if (GRAY_INVERSE==1)
                            const lUInt8 cl = (((lUInt8)(*src)&0x80)^0x80) >> (shift);
    #else
                            const lUInt8 cl = (((lUInt8)(*src)&0x80)) >> (shift);
#endif
                            if(*src!=0) *dst |= cl;
                        }
                        if (!((shift = (shift + 1) & 7)))
                            dst++;
                        src++;
                    }
                } else if (bpp==2) {
                    int shift = x & 3;
                    lUInt8 * __restrict dst = buf->GetScanLine(y+yy) + (x>>2);
                    for (int xx=0; xx<_dx; xx++) {
                        if ( x+xx >= clip.left && x+xx < clip.right ) {
                            //lUInt8 mask = ~((lUInt8)0xC0>>shift);
#if (GRAY_INVERSE==1)
                            const lUInt8 cl = (((lUInt8)(*src)&0xC0)^0xC0) >> (shift<<1);
    #else
                            const lUInt8 cl = (((lUInt8)(*src)&0xC0)) >> (shift<<1);
#endif
                            if(*src!=0) *dst |= cl;
                        }
                        if (!((shift = (shift + 1) & 3)))
                            dst++;
                        src++;
                    }
                } else if (bpp<=8) {
                    lUInt8 * __restrict dst = buf->GetScanLine(y + yy) + x;
                    for (int xx=0; xx<_dx; xx++) {
                        if (x + xx >= clip.left && x + xx < clip.right) {
                            //lUInt8 mask = ~((lUInt8)0xC0>>shift);
                            if(*src!=0) *dst = (lUInt8)*src;
                        }
                        dst++;
                        src++;
                    }
                } else if (bpp == 16) {
                    lUInt16 * __restrict dst = ((lUInt16 *)buf->GetScanLine(y + yy)) + x;
                    for (int xx=0; xx < _dx; xx++) {
                        if (x + xx >= clip.left && x + xx < clip.right) {
                            if(src!=0) *dst = rgb888to565(*src);
                        }
                        dst++;
                        src++;
                    }
                } else if (bpp == 32) {
                    lUInt32 * __restrict dst = ((lUInt32 *)buf->GetScanLine(y + yy)) + x;
                    for (int xx = 0; xx < _dx; xx++) {
                        if (x+xx >= clip.left && x + xx < clip.right) {
                            if(*src!=0) *dst = *src;
                        }
                        dst++;
                        src++;
                    }
                }
            }
        }
    }
}
/// draws rescaled buffer content to another buffer doing color conversion if necessary
void LVGrayDrawBuf::DrawRescaled(const LVDrawBuf * __restrict src, int x, int y, int dx, int dy, int options)
{
    CR_UNUSED(options);
    if (dx < 1 || dy < 1)
        return;
    lvRect clip;
    GetClipRect(&clip);
    const int srcdx = src->GetWidth();
    const int srcdy = src->GetHeight();
    const bool linearInterpolation = (srcdx <= dx || srcdy <= dy);
    //CRLog::trace("LVGrayDrawBuf::DrawRescaled bpp=%d %dx%d srcbpp=%d (%d,%d) (%d,%d)", _bpp, GetWidth(), GetHeight(), src->GetBitsPerPixel(), x, y, dx, dy);
	CHECK_GUARD_BYTE;
    for (int yy=0; yy<dy; yy++)
    {
        if (y+yy >= clip.top && y+yy < clip.bottom)
        {
            lUInt8 * __restrict dst0 = (lUInt8 *)GetScanLine(y + yy);
            if (linearInterpolation) {
                // linear interpolation
                const int srcy16 = srcdy * yy * 16 / dy;
                for (int xx=0; xx<dx; xx++)	{
                    if ( x+xx >= clip.left && x+xx < clip.right ) {
                        const int srcx16 = srcdx * xx * 16 / dx;
                        const lUInt32 cl = src->GetInterpolatedColor(srcx16, srcy16);
                        const lUInt32 alpha = (cl >> 24) & 0xFF;
                        if (_bpp==1)
                        {
                            if (alpha >= 128)
                                continue;
                            const int shift = (xx + x) & 7;
                            lUInt8 * __restrict dst = dst0 + ((x + xx) >> 3);
                            const lUInt32 dithered = Dither1BitColor(cl, xx, yy);
                            if (dithered)
                                *dst = (*dst) | (0x80 >> shift);
                            else
                                *dst = (*dst) & ~(0x80 >> shift);
                        }
                        else if (_bpp==2)
                        {
                            if (alpha >= 128)
                                continue;
                            lUInt8 * __restrict dst = dst0 + ((x + xx) >> 2);
                            const int shift = ((x+xx) & 3) * 2;
                            const lUInt32 dithered = Dither2BitColor(cl, xx, yy) << 6;
                            const lUInt8 b = *dst & ~(0xC0 >> shift);
                            *dst = (lUInt8)(b | (dithered >> shift));
                        }
                        else
                        {
                            lUInt8 * __restrict dst = dst0 + x + xx;
                            lUInt32 dithered;
                            if (_bpp<8)
                                dithered = DitherNBitColor(cl, xx, yy, _bpp); // << (8 - _bpp);
                            else
                                dithered = cl;
                            if (alpha < 16)
                                *dst = (lUInt8)dithered;
                            else if (alpha < 240) {
                                const lUInt32 nalpha = alpha ^ 0xFF;
                                lUInt32 pixel = *dst;
                                if (_bpp == 4)
                                    pixel = ((pixel * alpha + dithered * nalpha) >> 8) & 0xF0;
                                else
                                    pixel = ((pixel * alpha + dithered * nalpha) >> 8) & 0xFF;
                                *dst = (lUInt8)pixel;
                            }
                        }
                    }
                }
#if 1
            	{
            		if (_ownData && _data[_rowsize * _dy] != GUARD_BYTE) {
            			CRLog::error("lin interpolation, corrupted buffer, yy=%d of %d", yy, dy);
            			crFatalError(-5, "corrupted bitmap buffer");
            		}
            	}
#endif
            } else {
                // area average
                lvRect srcRect;
                srcRect.top = srcdy * yy * 16 / dy;
                srcRect.bottom = srcdy * (yy + 1) * 16 / dy;
                for (int xx=0; xx<dx; xx++)
                {
                    if ( x+xx >= clip.left && x+xx < clip.right )
                    {
                        srcRect.left = srcdx * xx * 16 / dx;
                        srcRect.right = srcdx * (xx + 1) * 16 / dx;
                        const lUInt32 cl = src->GetAvgColor(srcRect);
                        if (_bpp==1)
                        {
                            int shift = (x + xx) & 7;
                            lUInt8 * __restrict dst = dst0 + ((x + xx) >> 3);
                            const lUInt32 dithered = Dither1BitColor(cl, xx, yy);
                            if (dithered)
                                *dst = (*dst) | (0x80 >> shift);
                            else
                                *dst = (*dst) & ~(0x80 >> shift);
                        }
                        else if (_bpp==2)
                        {
                            lUInt8 * __restrict dst = dst0 + ((x + xx) >> 2);
                            const int shift = x & 3;
                            const lUInt32 dithered = Dither2BitColor(cl, xx, yy) << 6;
                            const lUInt8 b = *dst & ~(0xC0 >> shift);
                            *dst = (lUInt8)(b | (dithered >> (shift * 2)));
                        }
                        else
                        {
                            lUInt8 * __restrict dst = dst0 + x + xx;
                            lUInt32 dithered;
                            if (_bpp < 8)
                                dithered = DitherNBitColor(cl, xx, yy, _bpp);// << (8 - _bpp);
                            else
                                dithered = cl;
                            *dst = (lUInt8)dithered;
                        }
                    }
                }
#if 1
                {
            		if (_ownData && _data[_rowsize * _dy] != GUARD_BYTE) {
            			CRLog::error("area avg, corrupted buffer, yy=%d of %d", yy, dy);
            			crFatalError(-5, "corrupted bitmap buffer");
            		}
            	}
#endif
            }
        }
    }
	CHECK_GUARD_BYTE;
	_drawnImagesCount += ((LVBaseDrawBuf*)src)->getDrawnImagesCount();
	_drawnImagesSurface += dx*dy;
}



/// draws rescaled buffer content to another buffer doing color conversion if necessary
void LVColorDrawBuf::DrawRescaled(const LVDrawBuf * __restrict src, int x, int y, int dx, int dy, int options)
{
    CR_UNUSED(options);
    if (dx < 1 || dy < 1)
        return;
    lvRect clip;
    GetClipRect(&clip);
    const int srcdx = src->GetWidth();
    const int srcdy = src->GetHeight();
    const bool linearInterpolation = (srcdx <= dx || srcdy <= dy);
	for (int yy=0; yy<dy; yy++) {
		if (y+yy >= clip.top && y+yy < clip.bottom)	{
			if (linearInterpolation) {
				// linear interpolation
				const int srcy16 = srcdy * yy * 16 / dy;
				for (int xx=0; xx<dx; xx++)	{
					if ( x+xx >= clip.left && x+xx < clip.right ) {
						const int srcx16 = srcdx * xx * 16 / dx;
						const lUInt32 cl = src->GetInterpolatedColor(srcx16, srcy16);
                        if (_bpp == 16) {
							lUInt16 * __restrict dst = (lUInt16 *)GetScanLine(y + yy);
							dst[x + xx] = rgb888to565(cl);
						} else {
							lUInt32 * __restrict dst = (lUInt32 *)GetScanLine(y + yy);
							dst[x + xx] = cl;
						}
					}
				}
			} else {
				// area average
				lvRect srcRect;
				srcRect.top = srcdy * yy * 16 / dy;
				srcRect.bottom = srcdy * (yy + 1) * 16 / dy;
				for (int xx=0; xx<dx; xx++)	{
					if ( x+xx >= clip.left && x+xx < clip.right ) {
						srcRect.left = srcdx * xx * 16 / dx;
						srcRect.right = srcdx * (xx + 1) * 16 / dx;
						const lUInt32 cl = src->GetAvgColor(srcRect);
                        if (_bpp == 16) {
							lUInt16 * __restrict dst = (lUInt16 *)GetScanLine(y + yy);
							dst[x + xx] = rgb888to565(cl);
						} else {
							lUInt32 * __restrict dst = (lUInt32 *)GetScanLine(y + yy);
							dst[x + xx] = cl;
						}
					}
				}
			}
		}
	}
	_drawnImagesCount += ((LVBaseDrawBuf*)src)->getDrawnImagesCount();
	_drawnImagesSurface += dx*dy;
}

#endif

/// returns scanline pointer
lUInt8 * LVColorDrawBuf::GetScanLine( int y ) const
{
#if !defined(__SYMBIAN32__) && defined(_WIN32) && !defined(QT_GL)
    return _data + _rowsize * (_dy-1-y);
#else
    return _data + _rowsize * y;
#endif
}

/// returns white pixel value
lUInt32 LVColorDrawBuf::GetWhiteColor() const
{
    return 0xFFFFFF;
}
/// returns black pixel value
lUInt32 LVColorDrawBuf::GetBlackColor() const
{
    return 0x000000;
}


/// constructor
LVColorDrawBuf::LVColorDrawBuf(int dx, int dy, int bpp)
:     LVBaseDrawBuf()
#if !defined(__SYMBIAN32__) && defined(_WIN32) && !defined(QT_GL)
    ,_drawdc(NULL)
    ,_drawbmp(NULL)
#endif
    ,_bpp(bpp)
    ,_ownData(true)
{
    _rowsize = dx*(_bpp>>3);
    Resize( dx, dy ); // NOLINT: Call to virtual function during construction
}

/// creates wrapper around external RGBA buffer
LVColorDrawBuf::LVColorDrawBuf(int dx, int dy, lUInt8 * externalBuffer, int bpp )
:     LVBaseDrawBuf()
#if !defined(__SYMBIAN32__) && defined(_WIN32) && !defined(QT_GL)
    ,_drawdc(NULL)
    ,_drawbmp(NULL)
#endif
    ,_bpp(bpp)
    ,_ownData(false)
{
    _dx = dx;
    _dy = dy;
    _rowsize = dx*(_bpp>>3);
    _data = externalBuffer;
    SetClipRect( NULL );
}

/// destructor
LVColorDrawBuf::~LVColorDrawBuf()
{
	if ( !_ownData )
		return;
#if !defined(__SYMBIAN32__) && defined(_WIN32) && !defined(QT_GL)
    if (_drawdc)
        DeleteDC(_drawdc);
    if (_drawbmp)
        DeleteObject(_drawbmp);
#else
    if (_data)
        free( _data );
#endif
}

#if 0

/// convert to 1-bit bitmap
void LVColorDrawBuf::ConvertToBitmap(bool flgDither)
{
    // not implemented
    CR_UNUSED(flgDither);
}

#endif
