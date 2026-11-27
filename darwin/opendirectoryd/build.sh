#!/bin/sh
# sh build.sh OUTDIR -> OUTDIR/opendirectoryd   (FreeBSD: llvm21 for ld64.lld; elsewhere set LD64=path/to/ld64.lld, CC=clang)
set -e
H="$(cd "$(dirname "$0")" && pwd)"; O="${1:-$H/out}"; mkdir -p "$O"
CC="${CC:-cc}"; LD64="${LD64:-/usr/local/llvm21/bin/ld64.lld}"
$CC --target=x86_64-apple-macosx12.0 -fblocks -O1 -ffreestanding -fno-builtin -fno-stack-protector -Wall -Wno-unused-function -c "$H/opendirectoryd.c" -o "$O/opendirectoryd.o"
"$LD64" -arch x86_64 -platform_version macos 12.0 12.0 -e _main -o "$O/opendirectoryd" "$O/opendirectoryd.o" -L"$H" -lSystem
