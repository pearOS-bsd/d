#!/bin/sh
# Regenerates libSystem.tbd and CoreFoundation.tbd from the undefined symbols of cfprefsd.o (run after adding a call):
# sh gen-tbd.sh   (needs clang and llvm-nm)
set -e
H="$(cd "$(dirname "$0")" && pwd)"; T="${TMPDIR:-/tmp}/cfprefsd-tbd.$$.o"
clang --target=x86_64-apple-macosx12.0 -fblocks -O1 -ffreestanding -fno-builtin -fno-stack-protector -c "$H/cfprefsd.c" -o "$T"
syms=$(llvm-nm -u "$T"); rm -f "$T"
list() { echo "$syms" | grep -E "$1" | tr '\n' ',' | sed 's/,$//;s/,/, /g'; }
cat > "$H/libSystem.tbd" <<E
--- !tapi-tbd
tbd-version:     4
targets:         [ x86_64-macos ]
install-name:    '/usr/lib/libSystem.B.dylib'
current-version: 1351
exports:
  - targets:         [ x86_64-macos ]
    symbols:         [ dyld_stub_binder, $(list '^_[^Ck]|^_C[^F]|^_k[^C]|^__') ]
...
E
cat > "$H/CoreFoundation.tbd" <<E
--- !tapi-tbd
tbd-version:     4
targets:         [ x86_64-macos ]
install-name:    '/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation'
current-version: 5026.6.7
compatibility-version: 150
exports:
  - targets:         [ x86_64-macos ]
    symbols:         [ $(list '^_(CF|kCF)') ]
...
E
