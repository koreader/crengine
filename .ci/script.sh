#!/usr/bin/env bash

SHELLSCRIPT_ERROR=0

# Protect against forgetting to add or remove hyphenation patterns in languages.json
search_dir=./cr3gui/data/hyph
file_list=""
for entry in "$search_dir"/*; do
    if [ -f "$entry" ] && [ ! "$entry" = "$search_dir/languages.json" ]; then
        entry=${entry#*${search_dir}/}
        [ ! "$file_list" = "" ] && file_list="$file_list"$'\n'"$entry" || file_list="$entry"
    fi
done

file_list_jq=$(jq -r '.[].filename' "$search_dir/languages.json" | grep -v '@none' | grep -v '@algorithm' | grep -v '@softhyphens')

# Sort to ensure they're in exactly the same order.
file_list=$(echo "$file_list"|tr " " "\n"|sort -u|tr "\n" " ")
file_list_jq=$(echo "$file_list_jq"|tr " " "\n"|sort -u|tr "\n" " ")

if [ ! "$file_list" = "$file_list_jq" ]; then
    echo "Warning, json should reflect hyphenation patterns. Diff:"
    diff <(echo "$file_list") <(echo "$file_list_jq")
    SHELLSCRIPT_ERROR=1
fi

mapfile -t pattern_files < <(git ls-files cr3gui/data/hyph/*.pattern)
for pattern in "${pattern_files[@]}"; do
    echo "Running xmllint on ${pattern}"
    xmllint "$pattern" >/dev/null || SHELLSCRIPT_ERROR=1
done

changed_files="$(git diff --name-only origin/master HEAD | grep -E '\.([CcHh]|[ch]pp)$')"

if [ -n "${changed_files}" ]; then
    echo "Running cppcheck on ${changed_files}"
    # Set some configuration/define to speed up cppcheck by limiting
    # the combinations of configurations it will check.
    # We force the values set with add_definition() in kpvcrlib/CMakeLists.txt
    # and some from crengine/include/crsetup.h.
    # shellcheck disable=SC2086
    cppcheck -j 4 --error-exitcode=2 --language=c++ \
        -DUSE_FONTCONFIG=0 \
        -DUSE_FREETYPE=1 \
        -DUSE_HARFBUZZ=1 \
        -DUSE_FRIBIDI=1 \
        -DUSE_LIBUNIBREAK=1 \
        -DUSE_UTF8PROC=1 \
        -DUSE_NANOSVG=1 \
        -DALLOW_KERNING=1 \
        -DCR3_PATCH=1 \
        -DLINUX=1 \
        -D_LINUX=1 \
        -DCR_RENDER_32BPP_RGB_PXFMT \
        -DCR_EMULATE_GETTEXT \
        -DBUILD_LITE=0 \
        -DLBOOK=0 \
        -UANDROID \
        -UCR_POCKETBOOK \
        ${changed_files}

    # ignore header files in clang-tidy for now
    # @TODO rename to *.hpp (or *.hxx)?
    # see https://github.com/koreader/crengine/pull/130#issuecomment-373823848
    changed_files="$(git diff --name-only origin/master HEAD | grep -E '\.([Cc]|[c]pp)$')"
    # To check them all, uncomment this:
    # changed_files="$(find crengine/src | grep -E '\.([Cc]|[c]pp)$')"
    echo "Running clang-tidy on ${changed_files}"
    # shellcheck disable=SC2086
    clang-tidy ${changed_files} -- -Icrengine/include
fi

exit "${SHELLSCRIPT_ERROR}"
