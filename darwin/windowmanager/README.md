# WindowManager

The server of `com.apple.windowmanager.external` for pearOS, wire compatible with WindowManager.app of macOS 26.7.2: the Dock's
WindowManager.framework (`AgentXPCProxy`) talks to it with NSXPC.  Objective-C with no SDK (`../common/objc-min.h`), Foundation of the Mac at run time.
Protocol and evidence: `research/docs/DOCK-WINDOWMANAGER-WALLPAPER.md`.

- Same protocol as Apple's (`AgentXPCProxyServerInterface`, 46 selectors, Apple's type encodings), the 20 argument classes with Apple's names
  and coder keys (one NSData of JSON each), the client's 8 callbacks.
- Same check: a client needs `com.apple.private.windowmanager` (from `valueForEntitlement:`, or on pearOS from the code signature of its
  executable: `../common/csent.c`).  `WM_NO_ENTITLEMENT_CHECK=1` turns it off (tests with an unsigned client on a Mac).
- What the Dock asks at start: `xpcBeginDesktopPeekMonitor:completion:` -> completion(nil) and a `desktopPeekMonitorStateDidChange:` with no
  spaces peeked.  `End...PeekMonitor`, `Begin/EndExposeCoordinator`, `ProvideVisibilityHintForSpace` succeed; the rest (Stage Manager, Mission
  Control, window picking, tiling, remote transitions) answer at once with NSCocoaErrorDomain 3328, NO or 0: no reply is left pending.
- `WindowManager [name]` (default `com.apple.windowmanager.external`), `WM_LOG=1` logs every call on stderr.
- Job for the launchd of d: `launchd/com.apple.WindowManager.agent.plist` (the Mac's label; our program `/usr/libexec/pearos/WindowManager`,
  as `@USER@`, started at the first look-up).  `com.apple.windowmanager.server` is `../wm-stub`; `.dragserver` is not served.

Build: `sh build.sh OUTDIR` (FreeBSD: llvm21; elsewhere `CC=clang LD64=ld64.lld sh build.sh OUTDIR`; CI: `.github/workflows/dock-services.yml`).

Tested: Mac VM, Apple's WindowManager.framework against this server (name redirected): the begin call, its completion and the callback
arrive (NSXPC captures); without the entitlement the client sees `Connection interrupted`, as with Apple's.  FreeBSD lab VM: our launchd starts
it on demand, an unsigned client is refused, a client signed with the entitlement is served.  Not yet: with the real Dock on pearOS.
