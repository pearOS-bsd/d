#!/bin/sh
# pearos-desktop.sh: the desktop of the Mac programs on pearOS: WindowServer (the window server), the stack of daemons (launchd, notifyd, cfprefsd, lsd, carender-stub, the IconServices pair ...)
# and the Dock + the Finder of the Mac, drawn by WindowServer's own compositor (CoreAnimation commits -> ca_scene).  Run as root on the VM after "kldload darwinator.ko".
#   sh pearos-desktop.sh [APP ...]        (APP: the path of the executable of another app to start at the end, e.g. /System/Applications/Stickies.app/Contents/MacOS/Stickies)
# The files: /tmp/WindowServer.log (WindowServer; WSD_TRACE=1 WSD_CA_DEBUG=1 give the details), /tmp/launchd.log (the daemons), /tmp/run26.log.  /root/killall.sh stops everything.
set -u
sysctl kern.darwinator.dyld=1 kern.darwinator.two_volumes=2 kern.darwinator.fast_sleep_ms=5 >/dev/null
rm -f /tmp/WindowServer.sock
(WSD_OPSET=26 WSD_APPEARANCE=dark nohup /root/misc-iso/wsbsd/WindowServer -t -s /tmp/WindowServer.sock >/tmp/WindowServer.log 2>&1 &)
(nohup sh /root/run26.sh >/tmp/run26.log 2>&1 &)
sleep 100
(cd /tmp; nohup /System/Library/CoreServices/Finder.app/Contents/MacOS/Finder </dev/null >/tmp/Finder.out 2>&1 &)
sleep 30
for a in "$@"; do (cd /tmp; nohup "$a" </dev/null >/tmp/app.out 2>&1 &); sleep 5; done
echo "desktop up: screenshot with  WS_SOCKET=/tmp/WindowServer.sock wsshot out.ppm"
