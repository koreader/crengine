#ifndef CRSETUP_H_INCLUDED
#define CRSETUP_H_INCLUDED

/// Yes, even on macOS (Windows is unsupported)…
#define LINUX 1
#define _LINUX 1

/// Compression.
#define USE_ZLIB                             @USE_ZLIB@
#define USE_ZSTD                             @USE_ZSTD@

/// Documents cache.
#define DOCUMENT_CACHING_MIN_SIZE            0x10000   //  64.0 KiB
#define DOCUMENT_CACHING_SIZE_THRESHOLD      0x100000  //   1.0 MiB

/// Document formats.
#define CHM_SUPPORT_ENABLED                  @USE_CHMLIB@
#define ENABLE_ANTIWORD                      @USE_ANTIWORD@
#define USE_MD4C                             @USE_MD4C@

/// Images.
#define ARBITRARY_IMAGE_SCALE_ENABLED        1
#define MAX_IMAGE_SCALE_MUL                  2
#define USE_GIF                              @USE_GIF@
#define USE_LIBJPEG                          @USE_LIBJPEG@
#define USE_LIBPNG                           @USE_LIBPNG@
#define USE_LIBWEBP                          @USE_LIBWEBP@
#define USE_LUNASVG                          @USE_LUNASVG@
#define USE_NANOSVG                          @USE_NANOSVG@
#define USE_STB_IMAGE                        @USE_STB_IMAGE@ // only used with USE_NANOSVG==1

/// Miscellaneous.
#define MATHML_SUPPORT                       @USE_MATHML@
#define USE_SRELL_REGEX                      @USE_SRELL@

/// Output buffer.
#define COLOR_BACKBUFFER                     1
#define CR_INTERNAL_PAGE_ORIENTATION         1
#define GRAY_BACKBUFFER_BITS                 2
#define GRAY_INVERSE                         0

/// Streams.
#cmakedefine DISABLE_CLOEXEC                      1
#cmakedefine HAVE_OFF64_T                         1
#ifdef HAVE_OFF64_T
# define HAVE_STAT64                         1
# define _LARGEFILE64_SOURCE                 1
#endif
#define LVLONG_FILE_SUPPORT                  0
#define USE_ANSI_FILES                       1
#define FILE_STREAM_BUFFER_SIZE              0x20000   // 128.0 KiB
#define ZIP_STREAM_BUFFER_SIZE               0x40000   // 256.0 KiB

/// System.
#define CR_USE_THREADS                       0
#define LDOM_USE_OWN_MEM_MAN                 1

/// Text.
#define USE_LIMITED_FONT_SIZES_SET           0
#define USE_BITMAP_FONTS                     0
#define USE_WIN32_FONTS                      0
#define USE_GLYPHCACHE_HASHTABLE             0
#define GLYPH_CACHE_SIZE                     0x40000   // 256.0 KiB
#define ALLOW_KERNING                        1
#define USE_FONTCONFIG                       @USE_FONTCONFIG@
#define USE_FREETYPE                         @USE_FREETYPE@
#define USE_FRIBIDI                          @USE_FRIBIDI@
#define USE_HARFBUZZ                         @USE_HARFBUZZ@
#define USE_LIBUNIBREAK                      @USE_LIBUNIBREAK@
#define USE_UTF8PROC                         @USE_UTF8PROC@

/// Disable unused code.
#define CR_ENABLE_PAGE_IMAGE_CACHE           0

#endif//CRSETUP_H_INCLUDED

// vim: ft=cpp
