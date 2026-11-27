#!/bin/sh
# sh build.sh OUTDIR -> OUTDIR/appleeventsd   (FreeBSD: llvm21 for ld64.lld; elsewhere set LD64=path/to/ld64.lld, CC=clang)
set -e
H="$(cd "$(dirname "$0")" && pwd)"; O="${1:-$H/out}"
sh "$H/../common/build-objc.sh" "$O/appleeventsd" "$H/appleeventsd.c"
