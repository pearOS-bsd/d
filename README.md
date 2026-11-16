# d — the daemons of pearOS

All the services that run in the background (and the scripts that start them together) in one place; the pearOS build (wsbsd/iso/build.sh) takes them from here.

| folder | what | built with |
|---|---|---|
| `darwin/` | Mach-O daemons for darwinator (they run unmodified Mac programs): `launchd` (Mach bootstrap, services by name), `notifyd`, `wsbridge` (the Mac client library <-> wsd), `lsd-stub`/`xpc-stub`, `lsd-objc` (LaunchServices of our own), `svc-stub`, `carender-stub` (CARenderServer), `hid-stub`, `wm-stub` (WindowManager) | `sh darwin/build.sh` (llvm21 + ld64.lld; lsd-objc and wm-stub on a Mac) |
| `bsd/` | FreeBSD/GNUstep daemons: `launchd` (+ launch agents of the desktop), `usernoted` (+ `wsnotify`), `airportd`, `xpcservices/` (Open and Save Panel Service) | `WSBSD=.. COMPAT=.. sh bsd/build.sh OUT` |
| `session/` | the scripts that bring the daemons up together (stack-dock*.sh, pearos-desktop.sh, pearos-killall.sh): they expect the binaries under `/root/dnew/<name>/` (sync `darwin/` there) | on the VM, as root, after `kldload darwinator.ko` |

Related repositories: `pearOS-bsd/darwinator` (the kernel module), `pearOS-bsd/misc` (wsbsd: the window server wsd, the AppKit "open", the desktop programs; its `sdk/` is used by `bsd/`).
History: the sources were moved from `darwinator` (launchd, notifyd, wsbridge, lsd-stub, lsd-objc, svc-stub, carender-stub, hid-stub, wm-stub, tools/stack-*.sh, tools/pearos-*.sh) and from `misc/wsbsd` (launchd, usernoted, airportd, appkit/xpcservices, sdk/build-{usernoted,airportd}.sh); their older history stays in those repositories.
