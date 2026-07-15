#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Enforce the freestanding symbol contract (docs, freestanding/UEFI): built with
# COPAL_HOSTED=OFF and the freestanding flags, the core must reference no
# external symbol beyond the residual mem* family that every target provides.
# Any new hosted dependency (a stray malloc/fopen/pthread/libm call) shows up
# here and fails the build.
set -eu

build=${1:-build-freestanding}
flags="-ffreestanding -ffunction-sections -fdata-sections -fno-math-errno \
-D_FORTIFY_SOURCE=0 -fno-stack-protector"
allowed="memcpy memmove memset"

cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release -DCOPAL_HOSTED=OFF \
    -DCOPAL_BUILD_TESTS=OFF -DCOPAL_BUILD_EXAMPLES=OFF \
    -DCMAKE_C_FLAGS="$flags" >/dev/null
cmake --build "$build" --target copal -j >/dev/null

lib=$(find "$build" -name 'libcopal.a' | head -n1)
[ -n "$lib" ] || {
    echo "libcopal.a not found in $build" >&2
    exit 2
}

# External surface = undefined references minus anything the archive defines.
nm "$lib" | awk '$2 ~ /^[A-TV-Za-tv-z]$/ {print $3}' | sort -u >"$build/.defined"
nm "$lib" | awk '$1 == "U" {print $2}' | sort -u |
    comm -23 - "$build/.defined" | grep -v '_GLOBAL_OFFSET_TABLE_' \
    >"$build/.external" || true

echo "Freestanding external symbols:"
sed 's/^/  /' "$build/.external"

bad=
while IFS= read -r s; do
    [ -z "$s" ] && continue
    case " $allowed " in
        *" $s "*) ;;
        *) bad="$bad $s" ;;
    esac
done <"$build/.external"

if [ -n "$bad" ]; then
    echo "FAIL: unexpected freestanding symbol(s):$bad" >&2
    echo "The freestanding core may reference only: $allowed" >&2
    exit 1
fi
echo "OK: freestanding surface is within { $allowed }"
