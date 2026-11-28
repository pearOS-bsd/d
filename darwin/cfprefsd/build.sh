#!/bin/sh
# sh build.sh OUTDIR -> OUTDIR/cfprefsd   (FreeBSD: llvm21 for ld64.lld; elsewhere set LD64=path/to/ld64.lld, CC=clang;
# on a Mac: MAC=1 sh build.sh OUTDIR uses the system linker)
set -e
H="$(cd "$(dirname "$0")" && pwd)"; O="${1:-$H/out}"; mkdir -p "$O"
CC="${CC:-cc}"; LD64="${LD64:-/usr/local/llvm21/bin/ld64.lld}"
$CC --target=x86_64-apple-macosx12.0 -fblocks -O1 -ffreestanding -fno-builtin -fno-stack-protector -Wall -c "$H/cfprefsd.c" -o "$O/cfprefsd.o"
if [ -n "$MAC" ]; then
	$CC -arch x86_64 -mmacosx-version-min=12.0 -o "$O/cfprefsd" "$O/cfprefsd.o" -framework CoreFoundation
else
	"$LD64" -arch x86_64 -platform_version macos 12.0 12.0 -e _main -o "$O/cfprefsd" "$O/cfprefsd.o" -L"$H" -lSystem "$H/CoreFoundation.tbd"
fi
