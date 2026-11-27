# WallpaperAgent

The server of `com.apple.wallpaper` for pearOS, wire compatible with WallpaperAgent.app of macOS 26.7.2: the Dock's Wallpaper.framework sends
it plain XPC dictionaries `{ AgentXPCMessage: <JSON data> }`.  Objective-C with no SDK (`../common/objc-min.h`); Foundation, QuartzCore,
CoreGraphics and ImageIO of the Mac at run time.  Protocol and evidence: `research/docs/DOCK-WINDOWMANAGER-WALLPAPER.md`.

- `setDisplaySpacesInfo` (no reply) is kept; `takeAssertion` creates a CoreAnimation remote context with the picture and replies
  `AssertionReply {"id":N,"contextID":C}` (the Dock hosts it with a CALayerHost in its desktop window); `releaseAssertion` -> `{}`; any other
  or undecodable message -> `{}` when a reply is asked for, as Apple's agent answers.  A client's assertions go with its connection.
- The picture: `WP_IMAGE=<file>`, else a plain colour (`WP_COLOR="r,g,b"`).  The Mac's wallpaper store is not read yet.
- `WallpaperAgent [name]` (default `com.apple.wallpaper`), `WP_LOG=1` logs every message on stderr.
- Job for the launchd of d: `launchd/com.apple.wallpaper.agent.plist` (the Mac's label; our program `/usr/libexec/pearos/WallpaperAgent`, as
  `@USER@`, started at the first look-up).  The job's other names (CacheDelete, debug.service) are not served.

Build: `sh build.sh OUTDIR` (FreeBSD: llvm21; elsewhere `CC=clang LD64=ld64.lld sh build.sh OUTDIR`; CI: `.github/workflows/dock-services.yml`).

Tested: Mac VM, Apple's Wallpaper.framework against this agent (name redirected): the Dock's three messages, the reply and a CALayerHost with
our context id.  FreeBSD lab VM: started on demand by our launchd, the replies as on the Mac (context id 0 there: no CARenderServer on the lab VM).
Not yet: with the real Dock on pearOS, the picture drawn on screen.
