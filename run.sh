#!/usr/bin/env bash
# Arena2004 launcher - builds on first start if necessary.
set -e
cd "$(dirname "$0")"
if [ ! -x arena2004 ]; then
    make -j"$(nproc)"
fi
exec ./arena2004 "$@"
