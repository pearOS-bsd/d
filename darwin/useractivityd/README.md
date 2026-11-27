# useractivityd

The Handoff best-app service of pearOS, wire compatible with `com.apple.coreservices.lsbestappsuggestionmanager.xpc` of macOS 26.7.2 (`useractivityd`): UserActivity.framework's `UABestAppSuggestionManager` (the Dock's Handoff tile) connects, registers and asks, and is told there is no suggestion.  Objective-C on the runtime Foundation, no SDK.  Evidence: `research/docs/DOCK-SERVICES-2.md` (section 8).

- NSXPC protocol `UABestAppSuggestionManagerProtocol` (10 methods, our declaration of Apple's selectors), client callbacks `UABestAppSuggestionManagerResponseProtocol`.  A client without `com.apple.private.coreservices.lsuseractivityd.bestappsuggestion` is refused, as by Apple's daemon.
- pearOS has no Continuity: every question gets "none" (empty arrays, no date, confidence 0, NO), `notifyBestAppsChanged:` is never sent, a payload fetch answers NSCocoaErrorDomain 3328 (ours).  The synchronous `-bestAppSuggestion` of the framework gets its answer at once.
- `useractivityd [service-name]`; `UA_LOG=1` logs, `UA_NO_ENTITLEMENT_CHECK=1` accepts every client (tests).  Job: `launchd/com.apple.coreservices.useractivityd.plist`.

Build: `sh build.sh OUTDIR`.  Tested on the Mac VM: `research/tools/dock-services-2/dsprobe ua` (the Dock's calls through Apple's framework, job `com.pearos.t.ua`): registration accepted, `bestAppSuggestion` nil, no reconnection loop (against Apple's daemon the unentitled probe is refused and reconnects for ever).
