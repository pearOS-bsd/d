# usernoted

The Dock's part of the user notification daemon of pearOS, wire compatible with the `usernoted` of macOS 26.7.2: the Mach service `com.apple.usernoted.dock`, through which the Dock (`DockNotificationCenterBadgeConnection`) gets the badges of the applications.  Plain C, no Apple header; built like `windowmanager` (`../common/build-objc.sh`).  Protocol and evidence: `research/docs/DOCK-SERVICES-2.md` (section 2).

- Raw XPC, no replies.  Dock to usernoted: `message_type` 1 (authentication, at every (re)connection), 2 deleted app, 3 updated app, 4 badge reset (`bundle_identifier`).  usernoted to Dock: 1 `{bundle_identifier, badge_count}`, 2 `{bundle_identifiers}` (the applications that may not badge).
- As Apple's daemon: only a peer whose code signature has the signing identifier `com.apple.dock` is served (others are ignored, not even cancelled); on 1 it sends 2 and then every badge > 0; 4 clears the badge in Apple's store; at start it posts the distributed notification `com.apple.usernoted.isAlive`, on which the Dock reconnects.
- Badges: Apple's own store `~/Library/Group Containers/group.com.apple.usernoted/db2/db` (SQLite, `app(identifier, badge)`), read through the system's libsqlite3.  Not yet: the services the applications use to set badges (`usernoted.client` & co), the notification settings (so the "may not badge" set is empty).
- `usernoted [-v] [dock=NAME]`; `USERNOTED_ANY_PEER=1` serves any peer, `USERNOTED_DB=PATH` another store (tests).  Job: `launchd/com.apple.usernoted.plist` (`@USER@` = the console user).

Build: `sh build.sh OUTDIR`.  Tested on the Mac VM: a probe signed with the identifier `com.apple.dock` gets from this daemon what it gets from Apple's (`research/tools/dock-services-2`, job `com.pearos.t.un`), an unsigned probe gets nothing from either.
