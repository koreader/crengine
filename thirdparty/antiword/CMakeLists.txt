set(ANTIWORD_SRCS
    asc85enc.c
    blocklist.c
    chartrans.c
    datalist.c
    depot.c
    dib2eps.c
    doclist.c
    fail.c
    finddata.c
    findtext.c
    fontlist.c
    fonts.c
    fonts_u.c
    hdrftrlist.c
    imgexam.c
    listlist.c
    misc.c
    notes.c
    options.c
    out2window.c
    pdf.c
    pictlist.c
    prop0.c
    prop2.c
    prop6.c
    prop8.c
    properties.c
    propmod.c
    rowlist.c
    sectlist.c
    stylelist.c
    stylesheet.c
    summary.c
    tabstop.c
    unix.c
    utf8.c
    word2text.c
    worddos.c
    wordlib.c
    wordmac.c
    wordole.c
    wordwin.c
    xmalloc.c
)

add_library(antiword OBJECT)
target_sources(antiword PRIVATE ${ANTIWORD_SRCS})
target_include_directories(antiword PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(antiword PROPERTIES C_VISIBILITY_PRESET hidden)
set_target_properties(antiword PROPERTIES POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS})
