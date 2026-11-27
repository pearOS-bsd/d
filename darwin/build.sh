#!/bin/sh
# Builds the Mach-O daemons of pearOS (they run on darwinator): sh darwin/build.sh [OUTDIR]   (needs llvm21 with ld64.lld, see each Makefile / build.sh)
# OUTDIR (default darwin/out): every daemon in OUTDIR/<name>/.  lsd-objc and wm-stub are built on a Mac (clang, Foundation): see their README / header comment.
set -e
H="$(cd "$(dirname "$0")" && pwd)"; O="${1:-$H/out}"; mkdir -p "$O"
for d in launchd notifyd wsbridge; do mkdir -p "$O/$d"; cp -R "$H/$d/." "$O/$d/"; (cd "$O/$d" && make "$d" >/dev/null) && echo "built $O/$d/$d"; done
for d in lsd-stub svc-stub carender-stub tccd; do sh "$H/$d/build.sh" "$O/$d/out" >/dev/null && echo "built $O/$d/out/$d"; done
echo "not built here: lsd-objc (Mac: clang -framework Foundation -framework CoreServices lsd.m), wm-stub (Mac)"
