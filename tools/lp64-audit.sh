#!/bin/bash
# What breaks if this build were 64-bit?
#
# The port is 32-bit by design: retail-shaped structs store pointers in four-byte
# fields, and the ordering table stores links in twenty-four bits. That is fine
# until a device has no 32-bit execution state at all, at which point the whole
# build is uninstallable rather than merely slow.
#
# This compiles the unity translation unit as LP64 and counts what falls over.
# Syntax-only, so nothing links and no 64-bit SDL has to exist. It changes
# nothing -- it is a measurement, and the number is the point: a conversion with
# no number attached has no way to report progress.
#
# Two counts, meaning different things:
#
#   static-assert failures   Retail layouts that stop holding once a pointer is
#                            eight bytes. Loud, enumerable, and safe -- the
#                            compiler finds every one of them for you.
#
#   pointer narrowing        A pointer squeezed through an int. These are the
#                            dangerous ones: a truncated pointer that still
#                            lands inside a valid allocation gives wrong
#                            behaviour with no diagnostic at all.
#
# Usage: tools/lp64-audit.sh [--list]
set -u

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
log=${TMPDIR:-/tmp}/ctr-lp64-audit.log

# The SDL revision header is generated into the build tree, so a configured
# desktop build has to exist before this can parse main.c at all.
sdl_revision="$root/build/externals/SDL/include-revision"
if [ ! -d "$sdl_revision" ]; then
    echo "Configure the desktop build first: cmake -S . -B build" >&2
    exit 1
fi

cc -fsyntax-only -m64 \
   -DBUILD=926 -DCTR_INTERNAL -DCTR_NATIVE \
   -DCTR_NATIVE_BUILD_ID='"lp64-audit"' \
   -DCTR_NATIVE_VERSION='"lp64-audit"' \
   -I"$root/include" \
   -I"$root/externals/enet/include" \
   -I"$sdl_revision" \
   -I"$root/externals/SDL/include" \
   -Wall -Wextra -Wno-error \
   -Wint-to-pointer-cast -Wpointer-to-int-cast \
   "$root/main.c" >"$log" 2>&1

asserts=$(grep -c 'static assertion failed' "$log")
narrow=$(grep -c 'cast from pointer to integer' "$log")
widen=$(grep -c 'cast to pointer from integer' "$log")
lines=$(grep -oE '^[^:]+:[0-9]+' "$log" | sort -u | wc -l)

printf 'static-assert failures ......... %s\n' "$asserts"
printf 'pointer-to-int narrowing ....... %s\n' "$narrow"
printf 'int-to-pointer widening ........ %s\n' "$widen"
printf 'distinct source lines .......... %s\n' "$lines"

if [ "${1:-}" = "--list" ]; then
    echo
    echo "--- most affected files ---"
    grep -E 'error:|warning:' "$log" \
        | grep -oE '^[^:]+' \
        | sed "s|$root/||" \
        | sort | uniq -c | sort -rn | head -20
    echo
    echo "full log: $log"
fi

# Zero on every count is the finish line for the conversion.
if [ "$asserts" -eq 0 ] && [ "$narrow" -eq 0 ] && [ "$widen" -eq 0 ]; then
    exit 0
fi
exit 1
