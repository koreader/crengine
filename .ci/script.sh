#!/usr/bin/env bash

# Protect against forgetting to add or remove hyphenation patterns in languages.json
search_dir=./cr3gui/data/hyph
file_list=""
for entry in "$search_dir"/*; do
    if [ -f "$entry" ] && [ ! "$entry" = "$search_dir/languages.json" ]; then
        entry=${entry#*${search_dir}/}
        [ ! "$file_list" = "" ] && file_list="$file_list"$'\n'"$entry" || file_list="$entry"
    fi
done

file_list_jq=$(jq -r '.[].filename' "$search_dir/languages.json" | grep -v '@none' | grep -v '@algorithm')

if [ ! "$file_list" = "$file_list_jq" ]; then
    echo "Warning, json should reflect hyphenation patterns. Diff:"
    diff <(echo "$file_list") <(echo "$file_list_jq")
fi
