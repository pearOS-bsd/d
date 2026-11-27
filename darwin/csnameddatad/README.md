# csnameddatad

CarbonCore's named data store for pearOS, wire compatible with the XPC service `com.apple.carboncore.csnameddata` of macOS 26.7.2 (`csnameddatad.xpc` in CarbonCore.framework, one instance per user): `_CSSetNamedData`, `_CSCopyNamedData`, `_CSGetNamedData` and `_CSFlushNamedData` of CoreServices keep small named blobs here (the Dock writes `com.apple.dock,anwrite.<uid>`, SystemUIServer and screencapture `...mtwrite.<uid>`: CFAbsoluteTime stamps).  Objective-C on the runtime Foundation, no SDK.  Evidence: `research/docs/DOCK-SERVICES-2.md` (section 6).

- NSXPC protocol `_CSNamedDataReadWriteProtocol` (8 methods: set with and without reply, get, flush with and without reply, all keys, all items, terminate), with the answers Apple's service gives: a missing name is `(nil, nil)`, flush answers YES also for a missing name, nil data removes a name.  The store is in memory, as Apple's.
- `csnameddatad [service-name]`, `ND_LOG=1` logs.  Job: `launchd/com.apple.carboncore.csnameddata.plist` (the user's job: the launchd of d has no XPC service domain).

Build: `sh build.sh OUTDIR`.  Tested on the Mac VM: CarbonCore's own client (`research/tools/dock-services-2/dsprobe named`, `xpc_connection_create` redirected by `dcap.dylib`) and the whole protocol over NSXPC (`dsprobe namedx`) give the same answers from this service as from Apple's.
