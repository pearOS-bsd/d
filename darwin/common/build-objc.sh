#!/bin/sh
# Builds one Objective-C daemon of darwin/ as a Mach-O x86_64 executable without an SDK (objc-min.h, text stubs in tbd/: install names only,
# the symbols bind at run time to the libraries dyld finds).  Used by windowmanager/build.sh and wallpaperagent/build.sh:
#   sh common/build-objc.sh OUTFILE source.m [more sources .m/.c]      (CC=clang, LD64=ld64.lld; default llvm21 of FreeBSD)
set -e
C="$(cd "$(dirname "$0")" && pwd)"; OUT="$1"; shift
# the compiler's own headers (stddef.h, stdarg.h) are needed: FreeBSD's base clang keeps them in /usr/include, so prefer the llvm21 package
[ -z "$CC" ] && { [ -x /usr/local/llvm21/bin/clang ] && CC=/usr/local/llvm21/bin/clang || CC=cc; }
LD64="${LD64:-/usr/local/llvm21/bin/ld64.lld}"
mkdir -p "$(dirname "$OUT")"
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
"$LD64" -arch x86_64 -platform_version macos 12.0 12.0 -undefined dynamic_lookup -o "$OUT" $OBJS -L"$C/tbd" -F"$C/tbd" -lSystem -lobjc -framework Foundation -framework CoreFoundation
rm -f $OBJS
