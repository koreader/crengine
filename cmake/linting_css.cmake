include_guard(GLOBAL)
include(linting)

# stylelint {{{

find_program(NPX npx)

set(STYLELINT_FLAGS
    --config ${CMAKE_SOURCE_DIR}/.stylelintrc.json
)
if(CMAKE_COLOR_DIAGNOSTICS)
    list(APPEND STYLELINT_FLAGS --color)
endif()
if(GITHUB_ACTIONS)
    list(APPEND STYLELINT_FLAGS --custom-formatter=@csstools/stylelint-formatter-github)
else()
    list(APPEND STYLELINT_FLAGS --formatter=unix)
endif()

function(add_stylelint_target F COMPONENT)
    if(NOT NPX OR NOT EXISTS ${CMAKE_SOURCE_DIR}/package-lock.json)
        return()
    endif()
    get_filename_component(F ${F} ABSOLUTE)
    get_filename_component(TGT ${F} NAME)
    add_custom_target(stylelint-${TGT}
        COMMAND ${GA_GROUP} ${NPX} stylelint ${STYLELINT_FLAGS} ${F}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        VERBATIM
    )
    add_chained_targets(lint lint-${COMPONENT} lint-${TGT} stylelint-${TGT})
    add_chained_targets(stylelint stylelint-${COMPONENT} stylelint-${TGT})
endfunction()

# }}}
