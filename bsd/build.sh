#!/bin/sh
# Builds the FreeBSD (GNUstep) daemons of pearOS:  WSBSD=path/of/wsbsd COMPAT=dir-of-libwsbsdcompat sh bsd/build.sh OUTDIR
#   OUTDIR/usr/libexec/{launchd,usernoted,airportd}, OUTDIR/usr/local/bin/wsnotify, OUTDIR/System/Library/{LaunchDaemons,LaunchAgents}/*.plist,
#   OUTDIR/System/Library/Frameworks/AppKit.framework/Versions/C/XPCServices/com.apple.appkit.xpc.openAndSavePanelService.xpc
# airportd links CoreWLAN/CoreWiFi from the same OUTDIR (wsbsd/sdk/build-corewlan.sh first).  usernoted needs COMPAT (libwsbsdcompat).
set -e
H="$(cd "$(dirname "$0")" && pwd)"; OUT="${1:?output directory}"; : "${WSBSD:?WSBSD: the wsbsd checkout}"
export WSBSD COMPAT
sh "$H/launchd/build.sh" "$OUT"
sh "$H/usernoted/build.sh" "$OUT" "${COMPAT:-/root/compat}"
sh "$H/airportd/build.sh" "$OUT"
sh "$H/xpcservices/build.sh" "$OUT/System/Library/Frameworks/AppKit.framework/Versions/C/XPCServices"
