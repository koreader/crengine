cmake_minimum_required(VERSION 3.17.5)

# CMake options.
# NOTE: must be done before `project(…)` call.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "build type")
endif()
option(BUILD_SHARED_LIBS "build a shared library" ON)

project(koreader LANGUAGES C CXX)

set(CRE_LIBS)
set(CRE_SRCS
    crengine/qimagescale/qimagescale.cpp
    crengine/src/chmfmt.cpp
    crengine/src/cp_stats.cpp
    crengine/src/crtxtenc.cpp
    crengine/src/docxfmt.cpp
    crengine/src/epubfmt.cpp
    crengine/src/fb3fmt.cpp
    crengine/src/hist.cpp
    crengine/src/hyphman.cpp
    crengine/src/lstridmap.cpp
    crengine/src/lvdocview.cpp
    crengine/src/lvdrawbuf.cpp
    crengine/src/lvfnt.cpp
    crengine/src/lvfntman.cpp
    crengine/src/lvimg.cpp
    crengine/src/lvmemman.cpp
    crengine/src/lvopc.cpp
    crengine/src/lvpagesplitter.cpp
    crengine/src/lvrend.cpp
    crengine/src/lvstream.cpp
    crengine/src/lvstring.cpp
    crengine/src/lvstsheet.cpp
    crengine/src/lvstyles.cpp
    crengine/src/lvtextfm.cpp
    crengine/src/lvtinydom.cpp
    crengine/src/lvxml.cpp
    crengine/src/mathml.cpp
    crengine/src/mdfmt.cpp
    crengine/src/odtfmt.cpp
    crengine/src/odxutil.cpp
    crengine/src/pdbfmt.cpp
    crengine/src/props.cpp
    crengine/src/renderutil.cpp
    crengine/src/rtfimp.cpp
    crengine/src/textlang.cpp
    crengine/src/txtselector.cpp
    crengine/src/wordfmt.cpp
)

# helpers {{{

find_package(PkgConfig REQUIRED)

set(AUTO_RX [[^[Aa][Uu][Tt][Oo]$]])

macro(set01 VAR VALUE)
    if(${VALUE})
        set(${VAR} 1 ${ARGN})
    else()
        set(${VAR} 0 ${ARGN})
    endif()
endmacro()

function(featopt NAME DEFAULT)
    cmake_parse_arguments("" "" "PKG" "HDR" ${ARGN})
    if(_HDR AND _PKG)
        message(FATAL_ERROR "HDR and PKG keywords are mutually exclusive")
    endif()
    string(TOUPPER ${NAME} ID)
    set(${ID} ${DEFAULT} CACHE STRING "enable ${NAME} support (auto|off|on)")
    if(_HDR)
        set(${ID}_ROOT "" CACHE PATH "additional prefix where to look for ${NAME}")
    endif()
    set(VAL ${${ID}})
    if(VAL AND NOT VAL MATCHES "${AUTO_RX}")
        set(_REQUIRED REQUIRED)
    else()
        set(_REQUIRED)
    endif()
    set(${ID}_REQUIRED ${_REQUIRED} PARENT_SCOPE)
    set(FOUND ${VAL})
    if(VAL AND _HDR)
        if(NOT DEFINED CACHE{${ID}_FOUND})
            message(STATUS "Checking for header '${_HDR}'")
            if(DEFINED ${ID}_ROOT)
                set(PATHS ${${ID}_ROOT}/include)
            else()
                set(PATHS)
            endif()
            find_file(${ID}_FOUND NAMES ${_HDR} PATHS ${PATHS})
            if(${ID}_FOUND)
                message(STATUS "  Found ${${ID}_FOUND}")
            else()
                message(STATUS "  Header ${_HDR} not found")
                if(_REQUIRED)
                    message(FATAL_ERROR "The following required header was not found: ${_HDR}")
                endif()
            endif()
        endif()
        if(${ID}_FOUND)
            add_library(${ID} INTERFACE)
            add_library(PkgConfig::${ID} ALIAS ${ID})
            get_filename_component(DIR ${${ID}_FOUND} DIRECTORY)
            target_include_directories(${ID} INTERFACE ${DIR})
        else()
            set(FOUND FALSE)
        endif()
    endif()
    if(VAL AND _PKG)
        pkg_check_modules(${ID} ${_PKG} ${_REQUIRED} IMPORTED_TARGET)
        if(NOT ${ID}_FOUND)
            set(FOUND FALSE)
        endif()
    endif()
    if(FOUND AND (_HDR OR _PKG))
        set(CRE_LIBS ${CRE_LIBS} PkgConfig::${ID} PARENT_SCOPE)
    endif()
    set01(USE_${ID} ${FOUND} PARENT_SCOPE)
endfunction()

# }}}

# options {{{

# Boolean options.
option(DISABLE_CLOEXEC "force disable closexec support" OFF)
option(DISABLE_LFS     "force disable LFS support"      OFF)

# Feature options.
set(AUTO AUTO CACHE STRING "override value of all 'auto' features")
# lint_cmake: -whitespace/mismatch
featopt(Antiword    ${AUTO}                                  )
featopt(CHMLib      ${AUTO}                                  )
featopt(FreeType    ${AUTO} PKG "freetype2"                  )
featopt(Fontconfig  OFF     PKG "fontconfig"                 )
featopt(FriBidi     ${AUTO} PKG "fribidi"                    )
featopt(GIF         ${AUTO}                                  )
featopt(HarfBuzz    ${AUTO} PKG "harfbuzz >= 8.0"            )
featopt(libjpeg     ${AUTO} PKG "libjpeg"                    )
featopt(libpng      ${AUTO} PKG "libpng"                     )
featopt(libunibreak ${AUTO} PKG "libunibreak >= 6.0"         )
featopt(LibwebP     ${AUTO} PKG "libwebpdecoder libwebpdemux")
featopt(LunaSVG     ${AUTO} PKG "lunasvg"                    )
featopt(MathML      ${AUTO}                                  )
featopt(MD4C        ${AUTO} PKG "md4c-html"                  )
featopt(NanoSVG     ${AUTO} HDR "nanosvg.h;nanosvg/nanosvg.h")
featopt(SRELL       ${AUTO} HDR "srell.hpp"                  )
featopt(stb_image   OFF     HDR "stb_image.h;stb/stb_image.h")
featopt(utf8proc    ${AUTO} PKG "libutf8proc"                )
featopt(zlib        ${AUTO} PKG "zlib"                       )
featopt(zstd        ${AUTO} PKG "libzstd"                    )
# lint_cmake: +whitespace/mismatch

# Mandatory dependencies.
pkg_check_modules(XXHASH libxxhash REQUIRED IMPORTED_TARGET)
list(APPEND CRE_LIBS PkgConfig::XXHASH)

# Handle conflicting feature options.

if(USE_FONTCONFIG AND NOT USE_FREETYPE)
    if(FONTCONFIG_REQUIRED)
        message(FATAL_ERROR "cannot enable FONTCONFIG without FREETYPE")
    endif()
    set(USE_FONTCONFIG 0)
endif()

# NOTE: prefer LunaSVG (better SVG support).
if(USE_NANOSVG AND USE_LUNASVG)
    if(NANOSVG_REQUIRED AND LUNASVG_REQUIRED)
        message(FATAL_ERROR "cannot enable both NANOSVG and LUNASVG")
    endif()
    if(NANOSVG_REQUIRED)
        set(DNR LUNASVG)
    else()
        set(DNR NANOSVG)
    endif()
    list(REMOVE_ITEM CRE_LIBS PkgConfig::${DNR})
    set(USE_${DNR} 0)
endif()

# }}}

# crsetup.h {{{

include(CheckTypeSize)

if(DISABLE_CLOEXEC)
    set(DISABLE_CLOEXEC 1)
endif()

if(NOT DISABLE_LFS)
    # Test and Enable LFS Support in glibc, based on zlib
    set(CMAKE_REQUIRED_DEFINITIONS -D_LARGEFILE64_SOURCE=1)
    check_type_size(off64_t OFF64_T)
    set(CMAKE_REQUIRED_DEFINITIONS)
endif()

configure_file(crengine/include/crsetup.h.cmake crsetup.h @ONLY)

# }}}

add_compile_definitions($<$<CONFIG:Debug>:_DEBUG=1> $<$<CONFIG:Debug>:DEBUG=1>)

# sub-libraries {{{

if(ANTIWORD)
    add_compile_definitions(CR3_ANTIWORD_PATCH=1)
    add_subdirectory(thirdparty/antiword)
    list(APPEND CRE_LIBS antiword)
endif()

if(CHMLIB)
    add_subdirectory(thirdparty/chmlib)
    list(APPEND CRE_LIBS chmlib)
endif()

# }}}

# libcrengine {{{

add_library(crengine)
target_sources(crengine PRIVATE ${CRE_SRCS})
# Make sure we get full `constexpr` support.
target_compile_features(crengine PRIVATE cxx_std_17)
target_compile_options(crengine PRIVATE -ftabstop=4 -Wall)
if(NOT BUILD_SHARED_LIBS)
    # Enable `-fvisibility=hidden`.
    set_target_properties(crengine PROPERTIES CXX_VISIBILITY_PRESET hidden)
    # Enable `-fvisibility-inlines-hidden`.
    set_target_properties(crengine PROPERTIES VISIBILITY_INLINES_HIDDEN TRUE)
endif()
target_include_directories(crengine PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(crengine PUBLIC crengine/include)
target_link_libraries(crengine PRIVATE ${CRE_LIBS})

# }}}

# vim: foldmethod=marker foldlevel=0
