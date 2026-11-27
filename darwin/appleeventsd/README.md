# appleeventsd

The Apple Event session server of pearOS, wire compatible with `com.apple.coreservices.appleevents` of macOS 26.7.2 (`appleeventsd --server`): every process that takes Apple Events (the Dock's `aeInitializeFromHIToolbox`, every AppKit application) checks in here synchronously at start.  Plain C, no Apple header (`../common/build-objc.sh`).  Evidence: `research/docs/DOCK-SERVICES-2.md` (section 4).

- Raw XPC with a reply.  `{command 100, appName, signature, needsProcessAssertions, port}` (check-in, also sent again with `reconnect` true) gets Apple's `{success true, supressOAPP false, session 0}`; any other command, or none, cancels the connection.
- At start it posts the notify(3) name `com.apple.coreservices.appleevents.startup`, on which AE.framework checks in again (as with Apple's daemon).
- The check-ins are kept for routing, which is not here yet: events between processes do not go through this daemon in this version.
- `appleeventsd [-v] [name=NAME]` (the `--server` argument of the Mac's job is accepted and ignored).  Job: `launchd/com.apple.coreservices.appleevents.plist` (root; the Mac uses `_appleevents`).

Build: `sh build.sh OUTDIR`.  Tested on the Mac VM: Apple's AE.framework (`aeInitializeFromHIToolbox`, `research/tools/dock-services-2/dsprobe ae`) against this daemon (job `com.pearos.t.aed`, a LaunchDaemon: AE looks the service up in the system domain) gets the reply Apple's daemon gives.  Known difference: for an unknown command Apple's client sees "Connection invalid", ours "Connection interrupted".
