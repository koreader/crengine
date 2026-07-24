#!/bin/bash

set -eo pipefail

HYPH_DIR='cr3gui/data/hyph'
HYPH_LANGUAGES="${HYPH_DIR}/languages.json"

filesystem_patterns="$(find "${HYPH_DIR}" -name '*.pattern' -printf '%f\n' | sort)"
languages_patterns="$(jq --raw-output '.[] | .filename | select(startswith("@") == false)' "${HYPH_LANGUAGES}" | sort)"

if ! diff="$(diff --unified --color=always --label="files in ${HYPH_DIR}" <(printf '%s\n' "${filesystem_patterns}") --label="files in ${HYPH_LANGUAGES}" <(printf '%s\n' "${languages_patterns}"))"; then
    printf '%s\n' "${diff}" 2>&1
    exit 1
fi
