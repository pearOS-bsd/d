/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Alexandru Balan
 *
 * usernoted for pearOS: the Dock's part of the user notification daemon, wire compatible with the usernoted of macOS 26.7.2
 * (/usr/sbin/usernoted, job com.apple.usernoted, its DockConnection.swift).  The Dock (DockNotificationCenterBadgeConnection) is the
 * client of the Mach service com.apple.usernoted.dock: raw XPC dictionaries with a "message_type" (uint64), no replies.
 *
 *   Dock -> usernoted   1 {}                                   "authentication": the Dock (re)connected
 *                       2 {bundle_identifier}                  an application was deleted (tellNCAboutDeletedApp:)
 *                       3 {bundle_identifier}                  an application was updated (tellNCAboutUpdatedApp:)
 *                       4 {bundle_identifier}                  the user reset the badge (tellNCAboutBadgeReset:)
 *   usernoted -> Dock   1 {bundle_identifier, badge_count}     the badge of an application (0 = no badge)
 *                       2 {bundle_identifiers: [string]}       the applications that may not badge (the Dock hides their status labels)
 *
 * What Apple's usernoted does and this one does too (decompiled and captured: research/docs/DOCK-SERVICES-2.md 2):
 *  - a peer is accepted only when the signing identifier of its code signature is "com.apple.dock" (case-insensitive compare);
 *    any other peer is ignored, its messages get nothing (not even a cancel);
 *  - the newest accepted Dock replaces the previous one;
 *  - on message 1 it sends message 2 (always, the array may be empty) and then message 1 for every application whose last badge
 *    count is > 0;
 *  - 4 clears the badge of the application (Apple's database: table app, column badge = NULL); 2 forgets it; 3 changes nothing here;
 *  - at start it posts the distributed notification com.apple.usernoted.isAlive (the Dock reconnects on it).
 * Apple's daemon stops with a Swift fatal error on an unknown message_type; this one logs it and goes on.
 * The badges are read from Apple's own store, ~/Library/Group Containers/group.com.apple.usernoted/db2/db (SQLite, table
 * app(identifier, badge)); pearOS has no notification clients yet, so nothing writes new badges there except this daemon's resets.
 * The set of applications that may not badge comes from the notification settings, which pearOS does not have yet: it is empty.
 *
 *   usernoted [-v] [dock=NAME]       (dock=NAME serves another name, for tests on a Mac; USERNOTED_ANY_PEER=1 accepts every peer, for tests;
 *                                     USERNOTED_DB=PATH another database file)
 * Our own code (BSD-2-Clause).
 */
#include "../common/csent.h"

typedef void *xpc_object_t;
typedef void *dq;
typedef unsigned long size_t;
typedef const void *CFTypeRef;

extern void xpc_connection_set_event_handler(xpc_object_t c, void (^h)(xpc_object_t));
extern xpc_object_t xpc_connection_create_mach_service(const char *name, dq q, unsigned long flags);
extern void xpc_connection_set_target_queue(xpc_object_t c, dq q);
extern void xpc_connection_resume(xpc_object_t c);
extern void xpc_connection_send_message(xpc_object_t c, xpc_object_t m);
extern int xpc_connection_get_pid(xpc_object_t c);
extern xpc_object_t xpc_retain(xpc_object_t o);
extern void xpc_release(xpc_object_t o);
extern xpc_object_t xpc_dictionary_create(const char *const *k, const xpc_object_t *v, size_t n);
extern const char *xpc_dictionary_get_string(xpc_object_t d, const char *k);
extern unsigned long xpc_dictionary_get_uint64(xpc_object_t d, const char *k);
extern void xpc_dictionary_set_value(xpc_object_t d, const char *k, xpc_object_t v);
extern void xpc_dictionary_set_string(xpc_object_t d, const char *k, const char *v);
extern void xpc_dictionary_set_uint64(xpc_object_t d, const char *k, unsigned long v);
extern xpc_object_t xpc_array_create(const xpc_object_t *o, size_t n);
extern void xpc_array_set_string(xpc_object_t a, size_t i, const char *s);
extern void *xpc_get_type(xpc_object_t o);
extern char _xpc_type_dictionary;
extern char _xpc_type_connection;
extern dq dispatch_queue_create(const char *label, void *attr);
extern void dispatch_main(void);
extern int dprintf(int fd, const char *fmt, ...);
extern char *getenv(const char *name);
extern size_t strlen(const char *s);
extern void *dlopen(const char *path, int mode);
extern void *dlsym(void *handle, const char *name);
extern int snprintf(char *s, size_t n, const char *fmt, ...);
/* CoreFoundation (the distributed center) */
extern CFTypeRef CFNotificationCenterGetDistributedCenter(void);
extern void CFNotificationCenterPostNotification(CFTypeRef center, CFTypeRef name, const void *object, CFTypeRef userInfo, unsigned char deliverImmediately);
extern CFTypeRef CFStringCreateWithCString(CFTypeRef alloc, const char *s, unsigned int encoding);

#define XPC_CONNECTION_MACH_SERVICE_LISTENER 1
#define RTLD_NOW 2

static int verbose;
static xpc_object_t dock;		/* the accepted Dock (the newest one) */

static int
lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c + 32 : c;
}
static int
caseeq(const char *a, const char *b)
{
	while (*a && lower(*a) == lower(*b)) { a++; b++; }
	return lower(*a) == lower(*b);
}

/* ---- Apple's badge store: SQLite, table app (app_id, identifier, badge) ---- */
typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;
static struct {
	int (*open_v2)(const char *, sqlite3 **, int, const char *);
	int (*prepare_v2)(sqlite3 *, const char *, int, sqlite3_stmt **, const char **);
	int (*bind_text)(sqlite3_stmt *, int, const char *, int, void (*)(void *));
	int (*step)(sqlite3_stmt *);
	const unsigned char *(*column_text)(sqlite3_stmt *, int);
	long long (*column_int64)(sqlite3_stmt *, int);
	int (*column_type)(sqlite3_stmt *, int);
	int (*finalize)(sqlite3_stmt *);
	int (*close)(sqlite3 *);
} sq;
enum { SQLITE_OK = 0, SQLITE_ROW = 100, SQLITE_NULL = 5, SQLITE_OPEN_READWRITE = 2, SQLITE_OPEN_READONLY = 1 };

static void
sq_load(void)
{
	void *h = dlopen("/usr/lib/libsqlite3.dylib", RTLD_NOW);

	if (!h) return;
	sq.open_v2 = dlsym(h, "sqlite3_open_v2"); sq.prepare_v2 = dlsym(h, "sqlite3_prepare_v2"); sq.bind_text = dlsym(h, "sqlite3_bind_text");
	sq.step = dlsym(h, "sqlite3_step"); sq.column_text = dlsym(h, "sqlite3_column_text"); sq.column_int64 = dlsym(h, "sqlite3_column_int64");
	sq.column_type = dlsym(h, "sqlite3_column_type"); sq.finalize = dlsym(h, "sqlite3_finalize"); sq.close = dlsym(h, "sqlite3_close");
}
static sqlite3 *
db_open(int rw)
{
	char path[1024];
	const char *home = getenv("HOME");
	sqlite3 *db = 0;

	if (!sq.open_v2 || !sq.close || !sq.prepare_v2 || !sq.step || !sq.finalize) return 0;
	if (getenv("USERNOTED_DB")) snprintf(path, sizeof path, "%s", getenv("USERNOTED_DB"));
	else if (home) snprintf(path, sizeof path, "%s/Library/Group Containers/group.com.apple.usernoted/db2/db", home);
	else return 0;
	if (sq.open_v2(path, &db, rw ? SQLITE_OPEN_READWRITE : SQLITE_OPEN_READONLY, 0) != SQLITE_OK) { if (db) sq.close(db); return 0; }
	return db;
}

static void
send_badge(xpc_object_t c, const char *bundle, unsigned long count)
{
	xpc_object_t m = xpc_dictionary_create(0, 0, 0);

	xpc_dictionary_set_uint64(m, "message_type", 1);
	xpc_dictionary_set_string(m, "bundle_identifier", bundle);
	xpc_dictionary_set_uint64(m, "badge_count", count);
	xpc_connection_send_message(c, m);
	xpc_release(m);
	if (verbose) dprintf(2, "usernoted: -> Dock badge %s = %lu\n", bundle, count);
}

/* message 1 from the Dock: the applications that may not badge, then every badge > 0 (Apple: DockConnection, case 1) */
static void
send_state(xpc_object_t c)
{
	xpc_object_t m = xpc_dictionary_create(0, 0, 0), a = xpc_array_create(0, 0);
	sqlite3 *db;
	sqlite3_stmt *st = 0;

	xpc_dictionary_set_uint64(m, "message_type", 2);
	xpc_dictionary_set_value(m, "bundle_identifiers", a);
	xpc_connection_send_message(c, m);
	xpc_release(a);
	xpc_release(m);
	if (verbose) dprintf(2, "usernoted: -> Dock bundle_identifiers (0)\n");
	if ((db = db_open(0)) == 0) return;
	if (sq.prepare_v2(db, "SELECT identifier, badge FROM app", -1, &st, 0) == SQLITE_OK) {
		while (sq.step(st) == SQLITE_ROW) {
			const char *id = (const char *)sq.column_text(st, 0);
			long long n;

			if (!id || sq.column_type(st, 1) == SQLITE_NULL) continue;
			if ((n = sq.column_int64(st, 1)) > 0) send_badge(c, id, (unsigned long)n);
		}
	}
	if (st) sq.finalize(st);
	sq.close(db);
}

/* message 4: the user reset the badge of an application (Apple: "Reset badge count for %s", UPDATE app SET badge=NULL WHERE identifier=?) */
static void
reset_badge(const char *bundle)
{
	char low[512];
	size_t i;
	sqlite3 *db;
	sqlite3_stmt *st = 0;

	for (i = 0; bundle[i] && i + 1 < sizeof low; i++) low[i] = (char)lower(bundle[i]);
	low[i] = 0;
	if (verbose) dprintf(2, "usernoted: Reset badge count for %s\n", low);
	if ((db = db_open(1)) == 0) return;
	if (sq.prepare_v2(db, "UPDATE app SET badge=NULL WHERE identifier=?", -1, &st, 0) == SQLITE_OK && sq.bind_text(st, 1, low, -1, 0) == SQLITE_OK) sq.step(st);
	if (st) sq.finalize(st);
	sq.close(db);
}

static void
dock_message(xpc_object_t c, xpc_object_t m)
{
	unsigned long t = xpc_dictionary_get_uint64(m, "message_type");
	const char *b = xpc_dictionary_get_string(m, "bundle_identifier");

	if (verbose) dprintf(2, "usernoted: Dock message %lu %s\n", t, b ? b : "");
	switch (t) {
	case 1: send_state(c); break;
	case 2: case 3: break;		/* deleted / updated: no notification records here to drop or refresh */
	case 4: if (b) reset_badge(b); break;
	default: dprintf(2, "usernoted: unknown Dock message_type %lu (Apple's usernoted stops here)\n", t); break;
	}
}

static void
peer(xpc_object_t c, dq q)
{
	char ident[256];
	int pid = xpc_connection_get_pid(c);

	csent_identifier(pid, ident, sizeof ident);
	if (!caseeq(ident, "com.apple.dock") && !getenv("USERNOTED_ANY_PEER")) {
		if (verbose) dprintf(2, "usernoted: peer pid %d (%s) is not the Dock: ignored\n", pid, ident[0] ? ident : "unsigned");
		return;			/* Apple: DockConnection(...) is nil, the connection is never resumed */
	}
	xpc_retain(c);
	if (dock) xpc_release(dock);	/* the newest Dock replaces the previous one */
	dock = c;
	xpc_connection_set_target_queue(c, q);
	xpc_connection_set_event_handler(c, ^(xpc_object_t m) {
		if (xpc_get_type(m) == &_xpc_type_dictionary) dock_message(c, m);
		else if (c == dock) { xpc_release(dock); dock = 0; if (verbose) dprintf(2, "usernoted: the Dock went away\n"); }
	});
	xpc_connection_resume(c);
	if (verbose) dprintf(2, "usernoted: Dock pid %d accepted\n", pid);
}

int
main(int argc, char **argv)
{
	const char *name = "com.apple.usernoted.dock";
	dq q = dispatch_queue_create("com.apple.usernoted.dock", 0);
	xpc_object_t l;
	int i;

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (a[0] == '-' && a[1] == 'v') verbose = 1;
		else if (a[0] == 'd' && a[1] == 'o' && a[2] == 'c' && a[3] == 'k' && a[4] == '=') name = a + 5;
	}
	sq_load();
	l = xpc_connection_create_mach_service(name, q, XPC_CONNECTION_MACH_SERVICE_LISTENER);
	xpc_connection_set_event_handler(l, ^(xpc_object_t c) { if (xpc_get_type(c) == &_xpc_type_connection) peer(c, q); });
	xpc_connection_resume(l);
	/* Apple's usernoted posts this when it starts: the Dock connects again (DockNotificationCenterBadgeConnection _got_server_keep_alive:) */
	CFNotificationCenterPostNotification(CFNotificationCenterGetDistributedCenter(), CFStringCreateWithCString(0, "com.apple.usernoted.isAlive", 0x08000100), 0, 0, 1);
	if (verbose) dprintf(2, "usernoted: serving %s\n", name);
	dispatch_main();
	return 0;
}
