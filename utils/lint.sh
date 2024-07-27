#!/bin/sh
set -e
JOBS="$(getconf _NPROCESSORS_ONLN)"
exec make --file="${0%/*}/lint.mk" --jobs="${JOBS}" --load="${JOBS}" --keep-going --output-sync=line --no-builtin-rules --no-builtin-variables "$@"
