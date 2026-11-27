/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Alexandru Balan
 *
 * appleeventsd for pearOS: the Apple Event session server com.apple.coreservices.appleevents of macOS 26.7.2
 * (/System/Library/CoreServices/appleeventsd --server, job com.apple.coreservices.appleevents).  Its client is AE.framework
 * (CoreServices): raw XPC dictionaries with an int64 "command", sent with a reply.
 *
 * What a process sends when it starts taking Apple Events (aeInitializeFromHIToolbox: the Dock, every AppKit application) and what
 * Apple's daemon answers (captured on the Mac, research/docs/DOCK-SERVICES-2.md 4):
 *   {command 100, appName, signature (int64), needsProcessAssertions (bool), port (the process's Apple Event receive right, send right)}
 *     -> {success true, supressOAPP false, session 0}          (Apple's spelling "supressOAPP")
 *   any other command (or none): the connection is cancelled (the client sees XPC_ERROR_CONNECTION_INVALID / INTERRUPTED)
 * At start it posts the notify(3) name com.apple.coreservices.appleevents.startup (AE.framework reconnects on it), as Apple's does.
 * The check-ins are kept (pid, name, signature, port) for the routing of events between processes, which is not here yet: AESend to
 * another process does not go through appleeventsd in this version.
 *
 *   appleeventsd [-v] [name=NAME]      (name=NAME serves another name, for tests on a Mac)
 * Our own code (BSD-2-Clause).
 */
typedef void *xpc_object_t;
typedef void *dq;
typedef unsigned long size_t;

extern void xpc_connection_set_event_handler(xpc_object_t c, void (^h)(xpc_object_t));
extern xpc_object_t xpc_connection_create_mach_service(const char *name, dq q, unsigned long flags);
extern void xpc_connection_set_target_queue(xpc_object_t c, dq q);
extern void xpc_connection_resume(xpc_object_t c);
extern void xpc_connection_cancel(xpc_object_t c);
extern void xpc_connection_send_message(xpc_object_t c, xpc_object_t m);
extern int xpc_connection_get_pid(xpc_object_t c);
extern xpc_object_t xpc_dictionary_create_reply(xpc_object_t m);
extern xpc_object_t xpc_dictionary_get_value(xpc_object_t d, const char *k);
extern const char *xpc_dictionary_get_string(xpc_object_t d, const char *k);
extern long long xpc_dictionary_get_int64(xpc_object_t d, const char *k);
extern _Bool xpc_dictionary_get_bool(xpc_object_t d, const char *k);
extern void xpc_dictionary_set_bool(xpc_object_t d, const char *k, _Bool v);
extern void xpc_dictionary_set_int64(xpc_object_t d, const char *k, long long v);
extern xpc_object_t xpc_retain(xpc_object_t o);
extern void xpc_release(xpc_object_t o);
extern void *xpc_get_type(xpc_object_t o);
extern char _xpc_type_dictionary;
extern char _xpc_type_connection;
extern dq dispatch_queue_create(const char *label, void *attr);
extern void dispatch_main(void);
extern int dprintf(int fd, const char *fmt, ...);
extern int notify_post(const char *name);
extern void *malloc(size_t n);
extern void free(void *p);

#define XPC_CONNECTION_MACH_SERVICE_LISTENER 1
#define AE_CHECK_IN 100

static int verbose;

/* the processes that checked in: the message is kept (it holds the send right of the process's Apple Event port) */
struct client { struct client *next; xpc_object_t conn, checkin; int pid; };
static struct client *clients;

static void
forget(xpc_object_t c)
{
	struct client **p, *k;

	for (p = &clients; (k = *p) != 0; p = &k->next)
		if (k->conn == c) { *p = k->next; if (k->checkin) xpc_release(k->checkin); xpc_release(k->conn); free(k); return; }
}

static void
message(xpc_object_t c, xpc_object_t m)
{
	long long cmd = xpc_dictionary_get_value(m, "command") ? xpc_dictionary_get_int64(m, "command") : -1;
	xpc_object_t r;

	if (cmd != AE_CHECK_IN) {
		if (verbose) dprintf(2, "appleeventsd: pid %d command %lld: not served, connection cancelled (Apple's answer)\n", xpc_connection_get_pid(c), cmd);
		xpc_connection_cancel(c);
		return;
	}
	{
		struct client *k = malloc(sizeof *k);
		const char *n = xpc_dictionary_get_string(m, "appName");

		if (k) { forget(c); k->conn = xpc_retain(c); k->checkin = xpc_retain(m); k->pid = xpc_connection_get_pid(c); k->next = clients; clients = k; }
		if (verbose) dprintf(2, "appleeventsd: pid %d checks in: %s signature %lld%s\n", xpc_connection_get_pid(c), n ? n : "?",
		    xpc_dictionary_get_int64(m, "signature"), xpc_dictionary_get_bool(m, "needsProcessAssertions") ? " needsProcessAssertions" : "");
	}
	if ((r = xpc_dictionary_create_reply(m)) == 0) return;
	xpc_dictionary_set_bool(r, "success", 1);
	xpc_dictionary_set_bool(r, "supressOAPP", 0);
	xpc_dictionary_set_int64(r, "session", 0);
	xpc_connection_send_message(c, r);
	xpc_release(r);
}

int
main(int argc, char **argv)
{
	const char *name = "com.apple.coreservices.appleevents";
	dq q = dispatch_queue_create("com.apple.coreservices.appleevents", 0);
	xpc_object_t l;
	int i;

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (a[0] == '-' && a[1] == 'v') verbose = 1;
		else if (a[0] == 'n' && a[1] == 'a' && a[2] == 'm' && a[3] == 'e' && a[4] == '=') name = a + 5;
	}
	l = xpc_connection_create_mach_service(name, q, XPC_CONNECTION_MACH_SERVICE_LISTENER);
	xpc_connection_set_event_handler(l, ^(xpc_object_t c) {
		if (xpc_get_type(c) != &_xpc_type_connection) return;
		xpc_connection_set_target_queue(c, q);
		xpc_connection_set_event_handler(c, ^(xpc_object_t m) {
			if (xpc_get_type(m) == &_xpc_type_dictionary) message(c, m);
			else forget(c);
		});
		xpc_connection_resume(c);
	});
	xpc_connection_resume(l);
	notify_post("com.apple.coreservices.appleevents.startup");
	if (verbose) dprintf(2, "appleeventsd: serving %s\n", name);
	dispatch_main();
	return 0;
}
