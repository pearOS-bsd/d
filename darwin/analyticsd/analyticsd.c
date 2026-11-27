/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Alexandru Balan
 *
 * analyticsd for pearOS: the CoreAnalytics event server com.apple.analyticsd of macOS 26.7.2
 * (/System/Library/PrivateFrameworks/CoreAnalytics.framework/Support/analyticsd).  Its client is CoreAnalytics.framework
 * (analytics_send_event, analytics_send_event_lazy, analytics_is_event_used: the Dock sends its "com.apple.dock.*" events through them):
 * raw XPC dictionaries with a string "command".
 *
 * What Apple's daemon answers (captured on the Mac, research/docs/DOCK-SERVICES-2.md 5):
 *   {command "send", event, json, timestamp, format, priority}      -> {}  (the event is taken; nothing is collected on pearOS)
 *   a message without "command" from a client without the daemon's entitlements -> {error "not entitled"}
 *   any other command (from such a client)                           -> the connection is cancelled
 * Apple's daemon also pushes {notification "configuration-change", configuration <shmem>, configuration-size} to a new client: the
 * configuration (which events are used, their transforms) is Apple's own binary format and pearOS has none, so nothing is pushed here
 * (CoreAnalytics then answers analytics_is_event_used from what it has, and the Dock sends or skips its events; nothing waits for this).
 *
 *   analyticsd [-v] [name=NAME]      (name=NAME serves another name, for tests on a Mac)
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

extern xpc_object_t xpc_dictionary_create(const char *const *k, const xpc_object_t *v, size_t n);
extern void xpc_dictionary_set_string(xpc_object_t d, const char *k, const char *v);
extern int strcmp(const char *a, const char *b);

#define XPC_CONNECTION_MACH_SERVICE_LISTENER 1

static int verbose;

static void
message(xpc_object_t c, xpc_object_t m)
{
	const char *cmd = xpc_dictionary_get_string(m, "command");
	xpc_object_t r;

	if (cmd && !strcmp(cmd, "send")) {
		if (verbose) { const char *e = xpc_dictionary_get_string(m, "event"), *j = xpc_dictionary_get_string(m, "json"); dprintf(2, "analyticsd: pid %d event %s %s\n", xpc_connection_get_pid(c), e ? e : "?", j ? j : ""); }
		if ((r = xpc_dictionary_create_reply(m)) != 0) { xpc_connection_send_message(c, r); xpc_release(r); }
		return;
	}
	if (!cmd) {
		if ((r = xpc_dictionary_create_reply(m)) != 0) { xpc_dictionary_set_string(r, "error", "not entitled"); xpc_connection_send_message(c, r); xpc_release(r); }
		return;
	}
	if (verbose) dprintf(2, "analyticsd: pid %d command %s: not served, connection cancelled\n", xpc_connection_get_pid(c), cmd);
	xpc_connection_cancel(c);
}

int
main(int argc, char **argv)
{
	const char *name = "com.apple.analyticsd";
	dq q = dispatch_queue_create("com.apple.analyticsd", 0);
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
		xpc_connection_set_event_handler(c, ^(xpc_object_t m) { if (xpc_get_type(m) == &_xpc_type_dictionary) message(c, m); });
		xpc_connection_resume(c);
	});
	xpc_connection_resume(l);
	if (verbose) dprintf(2, "analyticsd: serving %s\n", name);
	dispatch_main();
	return 0;
}
