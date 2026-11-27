# lsd-stub

**Replaced for `com.apple.coreservices.launchservicesd`** by the real daemon of LaunchServices (`LaunchServices/launchservicesd`, wire-compatible with the Mac's, installed as `/usr/libexec/pearos/launchservicesd` with the job `LaunchServices/launchd/com.apple.coreservices.launchservicesd.plist`).  Started without names, the stub leaves that name alone when `/usr/libexec/pearos/launchservicesd` exists (`LSD_STUB_LAUNCHSERVICESD=1` takes it anyway); started with names (`xpc-stub NAME ...`) it still serves the other stand-ins (RunningBoard, opendirectoryd libinfo, pasteboard, pkd, ...).  The notes below describe the stub's own launchservicesd part, kept for history.

A stand-in for `launchservicesd` (the first of the replacements for what comes from the Mac).  A Mach-O program built on FreeBSD (`sh build.sh OUT`, clang + ld64.lld, no Apple header) that takes the service `com.apple.coreservices.launchservicesd` through launchd and answers the XPC messages of LaunchServices clients; it prints every message (libxpc's description) to stderr.

What the Dock sends (found with it): `command` 100 (check-in, `clientversion`: the answer needs `serverversion`), 110, 360 (`restrictto`, `asn`), 410 (`listener` endpoint, `addasn`), 200, 500, 505; all with `session` (100000 for the user, -2 for the session of the process).  Only 100 has an answer with content for now.

The real `launchservicesd` of the Mac (run through the module) crashes in libxpc when a message that it sent cannot be answered (`_xpc_connection_call_reply_async` with no context); the stub does not need what it needs.

## The info page (command 110)

The answer to 110 is one shared page per session (`mmap` MAP_ANON|MAP_SHARED, 61440 bytes, made at the first 110 and sent again every time), with the header that LaunchServices checks (version 0x104 at +0, size at +4, session at +8, the seeds at 100, the CGS session 257 at +0x68, the capacity 0x32a at +0x8c) and one 72-byte record per check-in (ASN low half, ChangeCount, flags 0x80, pid).  An all-zero page is rejected by the client (it asks for 110 again at every use; that alone does not stop the check-in).  Layout and evidence: research/docs/LAUNCHSERVICES-WIRE.md 6.3.  Not done yet: records are never removed (no death notification), the front application, the front-to-back order and the hashes/inodes of a record are not written.

The Dock abort "unable to get application ASN from launchservicesd ... requires an ASN" happens when the client's connection to this service becomes **invalid** (`XPC_ERROR_CONNECTION_INVALID`): LaunchServices then never connects again and sends no 500 (research/docs/LAUNCHSERVICES-WIRE.md 6.4, reproduced on the Mac VM with probes/fk.c).  If the Dock stops after 100, 110, 110, look for the stub dying or its service going away.

Tested on the Mac VM: Apple's AppKit client (research probes/a.m) against this stub through `__LSXPCSERVICENAME` (LaunchAgent with MachServices) checks in, and the page it gets decodes as above (`probes/ip.c` + `infopage.py`).  Built on the FreeBSD lab VM with build.sh; not run on pearOS in this change.

## Check-in (command 500) and NSXPC

- 500 (check-in of an application): the answer is `{success, result: <the info of the application + LSAuditToken, LSDisplayName, CFBundleExecutablePath, LSExecutableFilenameLowerCaseKey, BundleIdentifierLowerCase, CFBundleNameLowerCase, LSApplicationMemberCoalitionIDKey, pid, LSApplicationHasRegistered, ChangeCount, LSASN>}` (shape found by asking the daemon of the Mac with a program). `LSASN` must be a dictionary of three entries (`...asn.lo`, `...asn.hi` and `com.apple.coreservices.asn` = the marker uuid C96172A0-CE74-4D62-AFEF-ECFE4D04D827): LaunchServices reads only that form as a serial number; with another uuid the conversion returns NULL and the caller crashes in `CFDictionaryGetValue(NULL)`.
- Messages that carry `replysig` (NSXPC): they are not answered. An answer without the NSXPC fields takes `_xpc_connection_call_reply_async` of libxpc to a NULL context (SIGSEGV in ControlCenter, Dock, ...).
- Other things the apps need (not here): `/System/Library/CoreServices/SystemVersion.plist` (without it `GetMacOSXSystemVersionInformation failed` aborts HIServices `_RegisterApplication`); it is a fact file of the version, copied to the VM only.

## RunningBoard (com.apple.runningboard) as a stub

The real `launchservicesd` (tools/stack-dock-reallsd.sh) waits for RunningBoard at start-up.  The stub answers
`handshakeWithRequest:` with the shape that the runningboardd of the Mac gives (found by asking it with a program under
lldb): `{"return-value": {bsx_class: RBSHandshakeResponse, success-keys/values, fail-keys/values: [], _handle: {bsx_class:
RBSProcessHandle, _codedHandle: {_identity {RBSOpaqueProcessIdentity, _pid, _auid, _description}, _pid, _bsAuditToken
{bsx_index}, _data, _bundle}}}}`.  Any other selector: the reply is released (so the right to reply is destroyed and the
kernel sends MACH_NOTIFY_SEND_ONCE to the waiter) and the connection is cancelled.

Do not put `com.apple.logd` among the names of the stubs: Dock then finds a logd that does not speak the protocol and
`os_trace_metadata_init` crashes (the firehose buffer is NULL).

The handshake reply must also carry `bsx_archive` (a keyed archive with a `BSAuditToken` under the key named by
`bsx_index`): without it RunningBoardServices throws while holding an `os_unfair_lock` and `launchservicesd` then
waits on that lock for ever.  The stub embeds the archive of the Mac (278 bytes) and patches the token (offset 126).
With it, the real `launchservicesd` + Dock run: Dock sends its layers (49 messages in /tmp/ca.bin).

RunningBoard, more selectors (seen from lsd and Dock): `void_subscribeToProcessStateChangesWithConfiguration:error:` gets an
empty reply; `acquireAssertionWithDescriptor:error:` gets the NSError archive of the Mac for a client without the entitlement
(`RBSServiceErrorDomain` 1).  With the real `launchservicesd` this is still not enough: a thread of lsd sits in
`-[RBSAssertion acquireWithError:]` (it sleeps and retries on Cocoa error 4101) and a client waits for lsd for ever.
The format of a successful assertion reply is not known (the Mac refuses the call without the entitlement).
