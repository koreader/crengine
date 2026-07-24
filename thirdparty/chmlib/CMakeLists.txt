set(CHMLIB_SRCS
    src/chm_lib.c
    src/lzx.c
)

add_library(chmlib OBJECT)
target_sources(chmlib PRIVATE ${CHMLIB_SRCS})
target_include_directories(chmlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(chmlib PROPERTIES C_VISIBILITY_PRESET hidden)
set_target_properties(chmlib PROPERTIES POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS})
