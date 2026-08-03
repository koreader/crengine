include_guard(GLOBAL)
include(linting)

# xmllint {{{

find_program(XMLLINT xmllint)

set(XMLLINT_FLAGS
    --output /dev/null
)

function(add_xmllint_target F COMPONENT)
    if(NOT XMLLINT)
        return()
    endif()
    get_filename_component(F ${F} ABSOLUTE)
    get_filename_component(TGT ${F} NAME)
    add_custom_target(xmllint-${TGT}
        COMMAND ${GA_GROUP} ${XMLLINT} ${XMLLINT_FLAGS} ${F}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        VERBATIM
    )
    add_chained_targets(lint lint-${COMPONENT} lint-${TGT} xmllint-${TGT})
    add_chained_targets(xmllint xmllint-${COMPONENT} xmllint-${TGT})
endfunction()

# }}}
