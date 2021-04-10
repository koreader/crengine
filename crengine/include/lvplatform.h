/** \file lvplatform.h
    \brief CREngine platform-specific logic

    (c) Vadim Lopatin, 2000-2006
    (c) NiLuJe <ninuje@gmail.com>, 2021
    This source code is distributed under the terms of
    GNU General Public License.
    See LICENSE file for details.
*/

#ifndef LVPLATFORM_H_INCLUDED
#define LVPLATFORM_H_INCLUDED

#include <fcntl.h>

/// platform-dependent path separator
#if defined(_WIN32) && !defined(__WINE__)
#  define PATH_SEPARATOR_CHAR '\\'
#elif __SYMBIAN32__
#  define PATH_SEPARATOR_CHAR '\\'
#else
#  define PATH_SEPARATOR_CHAR '/'
#endif

/// CLOEXEC handling
#if defined(_WIN32)
#  define STDIO_CLOEXEC "N"  // MSVC 14 supposedly supports "e", too
#  if !defined(O_CLOEXEC)
#    define O_CLOEXEC _O_NOINHERIT
#  endif
#else
#  if defined(__GLIBC__)
// NOTE: stdio support requires glibc 2.7, open support requires Linux 2.6.23.
//       open harmlessly ignores unsupported flags, but fopen *will* throw EINVAL on unsupported modes,
//       so checking for the glibc version should cover everything.
#    if __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 7
#      define STDIO_CLOEXEC "e"
#    else
#      define DISABLE_CLOEXEC
#    endif
#  elif defined(__ANDROID__)
// NOTE: On Android, this is available since Android 4.4 (API 19),
//       via https://android.googlesource.com/platform/bionic/+/6b05c8e28017518fae04a3a601d0d245916561d2
#    include <android/api-level.h>
#    if __ANDROID_API__ >= 19
#      define STDIO_CLOEXEC "e"
#    else
#      define DISABLE_CLOEXEC
#    endif
#  elif defined(O_CLOEXEC)
// NOTE: Naive autodetection on other POSIX systems. (BSDs have supported it in stdio for nearly ten years).
#    define STDIO_CLOEXEC "e"
#  else
#    define DISABLE_CLOEXEC
#  endif
#endif

// In case the build TC is newer than the target, the buildsystem can request disabling this.
#if defined(DISABLE_CLOEXEC)
#  if defined(O_CLOEXEC)
#    undef O_CLOEXEC
#  endif
#  define O_CLOEXEC 0
#  if defined(STDIO_CLOEXEC)
#    undef STDIO_CLOEXEC
#  endif
#  define STDIO_CLOEXEC
#endif

#endif // LVPLATFORM_H_INCLUDED
