# analyticsd

The CoreAnalytics event server of pearOS, wire compatible with `com.apple.analyticsd` of macOS 26.7.2: CoreAnalytics.framework (`analytics_send_event`, used by the Dock for its `com.apple.dock.*` events) gets the answers of Apple's daemon, and nothing is collected.  Plain C, no Apple header (`../common/build-objc.sh`).  Evidence: `research/docs/DOCK-SERVICES-2.md` (section 5).

- Raw XPC with a string `command`.  `send` (event, json, timestamp, format, priority) gets `{}`; a message without a command gets `{error "not entitled"}`; any other command cancels the connection (what Apple's daemon does to a client without its entitlements).
- Apple's daemon pushes its configuration (shared memory, Apple's own format) to new clients; pearOS has none, so nothing is pushed.
- `analyticsd [-v] [name=NAME]`.  Job: `launchd/com.apple.analyticsd.plist` (root; the Mac uses `_analyticsd`).  Not here: `com.apple.analyticsd.messagetracer`.

Build: `sh build.sh OUTDIR`.  Tested on the Mac VM: Apple's CoreAnalytics (`research/tools/dock-services-2/dsprobe analytics`, `xraw`) against this daemon (job `com.pearos.t.and`, a LaunchDaemon) gets the replies Apple's daemon gives.
