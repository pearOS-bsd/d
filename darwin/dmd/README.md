# dmd

The Screen Time policy service of pearOS, wire compatible with `com.apple.dmd.policy` of macOS 26.7.2 (`/usr/libexec/dmd`): DeviceManagement.framework's `DMFApplicationPolicyMonitor` (the Dock's `ECScreenTimeController`, which dims blocked applications) gets the answers of a Mac without Screen Time.  Objective-C on the runtime Foundation, no SDK; `DMFPolicyRegistration` comes from DeviceManagement.framework at run time.  Evidence: `research/docs/DOCK-SERVICES-2.md` (section 7).

- NSXPC protocol `DMFPolicyRemoteInterface` (7 methods, our declaration of Apple's selectors).  A client without the entitlement `com.apple.private.dmd.policy` is refused, as by Apple's daemon (the client gets NSCocoaErrorDomain 4097).
- `addRegistration:` and `requestPoliciesForTypes:` answer an empty dictionary (the framework then reports Screen Time as not enabled), the per-identifier requests answer `DMFPolicyOK` (0) for every identifier, the budgets an empty array.  Apple's answers to an entitled client could not be observed (AMFI does not let a test program carry the entitlement): these are inferred, see the research doc.
- `dmd [service-name]`; `DMD_LOG=1` logs, `DMD_NO_ENTITLEMENT_CHECK=1` accepts every client (tests).  Job: `launchd/com.apple.dmd.plist` (`@USER@` = the console user).

Build: `sh build.sh OUTDIR`.  Tested on the Mac VM: `research/tools/dock-services-2/dsprobe dmd dmd2` (the Dock's `createPolicyMonitorWithPolicyChangeHandler:completionHandler:` through Apple's framework) gets a monitor with Screen Time disabled from this daemon; without the test switch it gets the same refusal as from Apple's.
