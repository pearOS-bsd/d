# uninstalld

The application uninstall service of pearOS, wire compatible with `com.apple.uninstall.service` of macOS 26.7.2 (`Uninstall.framework/Resources/uninstalld`): Uninstall.framework's `UNUninstallClient` (the Dock makes the connection at start; it sends requests when an application is deleted from Launchpad / the Apps folder) gets an answer to every request.  Objective-C on the runtime Foundation, no SDK; `UNUninstallClientRequest` comes from Uninstall.framework at run time.  Evidence: `research/docs/DOCK-SERVICES-2.md` (section 9).

- NSXPC protocol `UNUninstallServiceProtocol` (`queueRequest:withPriority:reply:`, `abortRequest:reply:`), client callbacks `UNUninstallClientProtocol`.  A client without `com.apple.private.uninstalld.client` is refused.
- Not yet: removing applications.  A request is answered NO with NSCocoaErrorDomain 3328 (ours), an abort NO.  The framework sends requests asynchronously (`queueRequest:error:` returns YES at once), so nothing waits.
- `uninstalld [service-name]`; `UN_LOG=1` logs, `UN_NO_ENTITLEMENT_CHECK=1` accepts every client (tests).  Job: `launchd/com.apple.uninstalld.plist` (root, as on the Mac).

Build: `sh build.sh OUTDIR`.  Tested on the Mac VM: `research/tools/dock-services-2/dsprobe uninstall` (Apple's framework, job `com.pearos.t.und`, a LaunchDaemon) connects, queues and aborts against this daemon.
