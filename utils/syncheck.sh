#!/bin/sh

set -e

utilsdir="$(readlink -m "${0%/*}")"
srcdir="${utilsdir%/*}"
builddb="$1/compile_commands.json"
shift

if [ $# -eq 0 ] || ! [ -r "${builddb}" ]; then
    echo "USAGE: $0 CMAKE_BUILDDIR SOURCE_FILES+" 1>&2
    exit 1
fi

for arg in "$@"; do
    arg="$(readlink -m "${arg}")"
    set -- "$@" "${arg}"
    shift
done

jq --raw-output --arg srcdir "${srcdir}" --from-file "${utilsdir}/syncheck.jq" "${builddb}" --args "$@" | parallel --group -v
