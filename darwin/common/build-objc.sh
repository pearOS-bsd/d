#!/bin/sh
# Builds one Objective-C daemon of darwin/ as a Mach-O x86_64 executable without an SDK (objc-min.h, text stubs written here: install names only,
# the symbols bind at run time to the libraries dyld finds).  Used by windowmanager/build.sh and wallpaperagent/build.sh:
#   sh common/build-objc.sh OUTFILE source.m [more sources .m/.c]      (CC=clang, LD64=ld64.lld; default llvm21 of FreeBSD)
set -e
C="$(cd "$(dirname "$0")" && pwd)"; OUT="$1"; shift
# the compiler's own headers (stddef.h, stdarg.h) are needed: FreeBSD's base clang keeps them in /usr/include, so prefer the llvm21 package
[ -z "$CC" ] && { [ -x /usr/local/llvm21/bin/clang ] && CC=/usr/local/llvm21/bin/clang || CC=cc; }
LD64="${LD64:-/usr/local/llvm21/bin/ld64.lld}"
mkdir -p "$(dirname "$OUT")"
T="$OUT.stubs"; rm -rf "$T"; mkdir -p "$T/Foundation.framework" "$T/CoreFoundation.framework"
stub() { printf -- "--- !tapi-tbd\ntbd-version:     4\ntargets:         [ x86_64-macos ]\ninstall-name:    '%s'\ncurrent-version: 1\nexports:\n  - targets:         [ x86_64-macos ]\n    symbols:         [ ]\n...\n" "$2" > "$1"; }
stub "$T/libSystem.tbd" /usr/lib/libSystem.B.dylib
stub "$T/libobjc.tbd" /usr/lib/libobjc.A.dylib
stub "$T/Foundation.framework/Foundation.tbd" /System/Library/Frameworks/Foundation.framework/Versions/C/Foundation
stub "$T/CoreFoundation.framework/CoreFoundation.tbd" /System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation
F="--target=x86_64-apple-macosx12.0 -nostdlibinc -ffreestanding -fno-builtin -fno-stack-protector -fblocks -O1 -Wall -Wno-unused-function -I$C"
OBJS=""
for s in "$@"; do
	o="$OUT.$(basename "$s").o"
	case "$s" in
	*.m) $CC $F -x objective-c -fobjc-arc -fobjc-runtime=macosx-12.0 -c "$s" -o "$o" ;;
	*) $CC $F -c "$s" -o "$o" ;;
	esac
	OBJS="$OBJS $o"
done
"$LD64" -arch x86_64 -platform_version macos 12.0 12.0 -undefined dynamic_lookup -o "$OUT" $OBJS -L"$T" -F"$T" -lSystem -lobjc -framework Foundation -framework CoreFoundation
rm -rf $OBJS "$T"
