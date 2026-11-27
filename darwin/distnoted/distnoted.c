/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Alexandru Balan
 *
 * distnoted for pearOS: the distributed notification server, wire compatible with the distnoted of macOS 26.7.2.  Its client
 * is CoreFoundation's distributed center (CFNotificationCenterGetDistributedCenter, NSDistributedNotificationCenter), which
 * sends one-way XPC messages, "version" 1 and a "method":
 *   register   name, object, token, options, pn   (name "kCFNotificationAnyName" / object "kCFNotificationAnyObject" = any;
 *                                                  options: the suspension behavior in the low 4 bits: 1 deliver immediately,
 *                                                  2 drop, 4 coalesce, 8 hold; the higher bits say CF or Foundation)
 *   unregister tokens (array of uint64)
 *   post       name, object, userinfo (binary plist data, optional), options (1 DeliverImmediately, 2 PostToAllSessions)
 *   suspend / unsuspend
 * and gets, for every registration that matches a post, {method "post_token", version 1, token, name, object[, userinfo]}.
 * What Apple's distnoted does and this one does too (decompiled, research/docs/OPENDIRECTORY-DISTNOTED-WIRE.md):
 *  - a registration for any name and any object is dropped (it is allowed only to sandboxed clients; pearOS has no sandbox);
 *  - a suspended client: a post that is not "deliver immediately" for a registration that is not "deliver immediately" is held
 *    (hold), replaces an older queued post of the same name and object (coalesce) or is dropped (drop); unsuspend sends the queue;
 *    a "deliver immediately" post first sends the queue;
 *  - PostToAllSessions in a user's agent goes to the system daemon (the copy of the message with method "post_all"), which delivers
 *    it to every session; the seven locale names and two preference names also post their notify(3) names.
 * No message of this protocol has a reply.
 *
 * distnoted agent      a user's instance: com.apple.distributed_notifications@Uv3 (its job: launchd/com.apple.distnoted.xpc.agent.plist)
 * distnoted daemon     the system instance: com.apple.distributed_notifications@1v3 for system processes and @0v3 for the agents
 * Tests: agent=NAME super=NAME0, daemon=NAME1 agents=NAME0 serve other names; -v logs every message on stderr.
 * How the agents and the daemon talk to each other (both ours) is inferred: an agent sends "post_all" to @0v3 (Apple's), the
 * daemon sends the post back to every agent as a "post" (options without 2) and an agent introduces itself with "unsuspend".
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
extern xpc_object_t xpc_dictionary_create(const char *const *k, const xpc_object_t *v, size_t n);
extern xpc_object_t xpc_dictionary_get_value(xpc_object_t d, const char *k);
extern const char *xpc_dictionary_get_string(xpc_object_t d, const char *k);
extern unsigned long xpc_dictionary_get_uint64(xpc_object_t d, const char *k);
extern void xpc_dictionary_set_value(xpc_object_t d, const char *k, xpc_object_t v);
extern void xpc_dictionary_set_string(xpc_object_t d, const char *k, const char *v);
extern void xpc_dictionary_set_uint64(xpc_object_t d, const char *k, unsigned long v);
extern xpc_object_t xpc_array_create(const xpc_object_t *o, size_t n);
extern void xpc_array_append_value(xpc_object_t a, xpc_object_t v);
extern size_t xpc_array_get_count(xpc_object_t a);
extern xpc_object_t xpc_array_get_value(xpc_object_t a, size_t i);
extern unsigned long xpc_array_get_uint64(xpc_object_t a, size_t i);
extern xpc_object_t xpc_copy(xpc_object_t o);
extern xpc_object_t xpc_retain(xpc_object_t o);
extern void xpc_release(xpc_object_t o);
extern void *xpc_get_type(xpc_object_t o);
extern const struct _xpc_type_s _xpc_type_dictionary, _xpc_type_array;
extern dq dispatch_queue_create(const char *label, void *attr);
extern void dispatch_main(void);
extern unsigned int notify_post(const char *name);
extern long write(int fd, const void *buf, size_t n);
extern void *malloc(size_t n);
extern void free(void *p);

static size_t slen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static void out(const char *s) { write(2, s, slen(s)); }
static int streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int prefix(const char *s, const char *p) { while (*p && *s == *p) { s++; p++; } return *p == 0; }
static void
outn(unsigned long v)
{
	char b[24]; int i = 23;

	b[i] = 0;
	do { b[--i] = '0' + v % 10; v /= 10; } while (v);
	out(b + i);
}
static char *
sdup(const char *s)
{
	size_t n = slen(s), i;
	char *d = malloc(n + 1);

	if (d) for (i = 0; i <= n; i++) d[i] = s[i];
	return (d);
}
static int seq(const char *a, const char *b) { return a && b && streq(a, b); }

static const char ANY_NAME[] = "kCFNotificationAnyName", ANY_OBJECT[] = "kCFNotificationAnyObject";
enum { SB_IMMEDIATE = 1, SB_DROP = 2, SB_COALESCE = 4, SB_HOLD = 8 };
enum { POST_IMMEDIATE = 1, POST_ALL_SESSIONS = 2 };

struct reg { struct reg *next; unsigned long token; char *name, *object; unsigned int opt; };	/* name / object 0 = any */
struct client {
	struct client *next;
	xpc_object_t peer;
	int pid, suspended, agent;		/* agent: a peer of @0v3 (an agent of a session, at the daemon) */
	struct reg *regs;
	xpc_object_t queue;			/* the posts held while suspended (post_token messages) */
};

static struct client *clients;
static dq q;
static int verbose, daemon_mode;
static xpc_object_t super_conn;		/* agent: the connection to the daemon (@0v3); 0 when there is none */
static int super_dead;

/* ---- delivery ---- */

static void
send_msg(struct client *c, xpc_object_t m)
{
	xpc_connection_send_message(c->peer, m);
}
static void
flush_queue(struct client *c)
{
	size_t i, n = xpc_array_get_count(c->queue);

	for (i = 0; i < n; i++) send_msg(c, xpc_array_get_value(c->queue, i));
	xpc_release(c->queue);
	c->queue = xpc_array_create(0, 0);
}

/* ClientOfLocalNoteServer postNotification:object:token:options:immediate:userInfo: of Apple's distnoted */
static void
deliver(struct client *c, struct reg *r, const char *name, const char *object, xpc_object_t ui, int immediate)
{
	xpc_object_t m = xpc_dictionary_create(0, 0, 0);

	xpc_dictionary_set_string(m, "method", "post_token");
	xpc_dictionary_set_uint64(m, "version", 1);
	xpc_dictionary_set_uint64(m, "token", r->token);
	xpc_dictionary_set_string(m, "name", name);
	xpc_dictionary_set_string(m, "object", object);
	if (ui) xpc_dictionary_set_value(m, "userinfo", ui);
	if (!immediate && !(r->opt & SB_IMMEDIATE) && c->suspended) {
		if (r->opt == SB_HOLD) xpc_array_append_value(c->queue, m);
		else if (r->opt != SB_DROP && (r->opt & SB_COALESCE)) {	/* the older post of the same name and object goes */
			xpc_object_t nq = xpc_array_create(0, 0);
			size_t i, n = xpc_array_get_count(c->queue);

			for (i = 0; i < n; i++) {
				xpc_object_t o = xpc_array_get_value(c->queue, i);

				if (!(seq(xpc_dictionary_get_string(o, "name"), name) && seq(xpc_dictionary_get_string(o, "object"), object)))
					xpc_array_append_value(nq, o);
			}
			xpc_array_append_value(nq, m);
			xpc_release(c->queue);
			c->queue = nq;
		}
		/* drop (or no behavior): nothing */
	} else {
		if (immediate || (r->opt & SB_IMMEDIATE)) flush_queue(c);
		send_msg(c, m);
	}
	xpc_release(m);
}

/* a post for this instance's clients (never to the agents at the daemon: they get a "post" each) */
static void
post_local(const char *name, const char *object, xpc_object_t ui, unsigned long options)
{
	struct client *c;
	struct reg *r;
	int n = 0;

	for (c = clients; c; c = c->next) {
		if (c->agent) continue;
		for (r = c->regs; r; r = r->next) {
			if (r->name && !streq(r->name, name)) continue;
			if (r->object && !streq(r->object, object)) continue;
			deliver(c, r, name, object, ui, (options & POST_IMMEDIATE) != 0);
			n++;
		}
	}
	if (verbose) { out("distnoted: post "); out(name); out(" -> "); outn(n); out(" registrations\n"); }
}

static const char *const locale_names[] = {
	"AppleNumberPreferencesChangedNotification", "AppleTimePreferencesChangedNotification", "AppleDatePreferencesChangedNotification",
	"AppleLanguagePreferencesChangedNotification", "AppleTextBehaviorPreferencesChangedNotification",
	"AppleTemperatureUnitPreferencesChangedNotification", "AppleMeasurementSystemPreferencesChangedNotification", 0
};

/* daemon: a post for every session: its own clients and every agent */
static void
post_all_sessions(xpc_object_t m, const char *name, const char *object, xpc_object_t ui, unsigned long options)
{
	struct client *c;

	post_local(name, object, ui, options & ~(unsigned long)POST_ALL_SESSIONS);
	for (c = clients; c; c = c->next) {
		xpc_object_t f;

		if (!c->agent) continue;
		f = xpc_copy(m);
		xpc_dictionary_set_string(f, "method", "post");
		xpc_dictionary_set_uint64(f, "options", options & ~(unsigned long)POST_ALL_SESSIONS);
		send_msg(c, f);
		xpc_release(f);
	}
}

static void super_connect(void);

static void
handle_post(xpc_object_t m)
{
	const char *name = xpc_dictionary_get_string(m, "name"), *object = xpc_dictionary_get_string(m, "object");
	xpc_object_t ui = xpc_dictionary_get_value(m, "userinfo");
	unsigned long options = xpc_dictionary_get_uint64(m, "options");
	int i;

	if (name == 0) return;
	if (object == 0) object = ANY_OBJECT;
	if (options & POST_ALL_SESSIONS) {
		if (daemon_mode) { post_all_sessions(m, name, object, ui, options); return; }
		super_connect();
		if (super_conn && !super_dead) {
			xpc_object_t f = xpc_copy(m);

			xpc_dictionary_set_string(f, "method", "post_all");
			xpc_connection_send_message(super_conn, f);
			xpc_release(f);
			return;
		}
		/* no system daemon: this session is every session there is */
	}
	for (i = 0; locale_names[i]; i++) if (streq(name, locale_names[i])) notify_post("com.apple.distnote.locale_changed");
	if (streq(name, "com.apple.MCX._managementStatusChangedForDomains")) notify_post("com.apple.MCX._managementStatusChangedForDomains");
	if (streq(name, "com.apple.CFPreferences._domainsChangedExternally")) notify_post("com.apple.CFPreferences._domainsChangedExternally");
	post_local(name, object, ui, options);
}

static void
reg_free(struct reg *r)
{
	free(r->name); free(r->object); free(r);
}

static void
handle(struct client *c, xpc_object_t m)
{
	const char *method = xpc_dictionary_get_string(m, "method");

	if (method == 0 || xpc_dictionary_get_uint64(m, "version") != 1) return;
	if (verbose) { out("distnoted: pid "); outn(c->pid); out(" "); out(method); out("\n"); }
	if (streq(method, "post")) handle_post(m);
	else if (streq(method, "post_all")) {		/* daemon: from an agent */
		if (daemon_mode) {
			const char *name = xpc_dictionary_get_string(m, "name"), *object = xpc_dictionary_get_string(m, "object");

			if (name) post_all_sessions(m, name, object ? object : ANY_OBJECT, xpc_dictionary_get_value(m, "userinfo"), xpc_dictionary_get_uint64(m, "options"));
		}
	} else if (streq(method, "register")) {
		const char *name = xpc_dictionary_get_string(m, "name"), *object = xpc_dictionary_get_string(m, "object");
		struct reg *r;

		if (name == 0 || object == 0) return;
		if (streq(name, ANY_NAME) && streq(object, ANY_OBJECT)) { if (verbose) out("distnoted: register-all dropped\n"); return; }
		if ((r = malloc(sizeof(*r))) == 0) return;
		r->token = xpc_dictionary_get_uint64(m, "token");
		r->name = streq(name, ANY_NAME) ? 0 : sdup(name);
		r->object = streq(object, ANY_OBJECT) ? 0 : sdup(object);
		r->opt = (unsigned int)(xpc_dictionary_get_uint64(m, "options") & 0xf);
		r->next = c->regs;
		c->regs = r;
		if (verbose) { out("distnoted: register "); out(name); out(" "); out(object); out(" token "); outn(r->token); out("\n"); }
	} else if (streq(method, "unregister")) {
		xpc_object_t t = xpc_dictionary_get_value(m, "tokens");
		size_t i, n;

		if (t == 0 || xpc_get_type(t) != (void *)&_xpc_type_array) return;
		n = xpc_array_get_count(t);
		for (i = 0; i < n; i++) {
			unsigned long tok = xpc_array_get_uint64(t, i);
			struct reg **pp = &c->regs, *r;

			while ((r = *pp) != 0) { if (r->token == tok) { *pp = r->next; reg_free(r); } else pp = &r->next; }
		}
	} else if (streq(method, "suspend")) c->suspended = 1;
	else if (streq(method, "unsuspend")) { c->suspended = 0; flush_queue(c); }
	/* monitor, dump: Apple's diagnostics, nothing here */
}

static void
client_drop(struct client *c)
{
	struct client **pp = &clients;
	struct reg *r;

	while (*pp && *pp != c) pp = &(*pp)->next;
	if (*pp) *pp = c->next;
	while ((r = c->regs) != 0) { c->regs = r->next; reg_free(r); }
	xpc_release(c->queue);
	if (verbose) { out("distnoted: pid "); outn(c->pid); out(" gone\n"); }
	free(c);
}

static void
serve(const char *name, int agents)
{
	xpc_object_t svc = xpc_connection_create_mach_service(name, q, 1 /* XPC_CONNECTION_MACH_SERVICE_LISTENER */);

	xpc_connection_set_event_handler(svc, ^(xpc_object_t peer) {
		struct client *c;

		if (xpc_get_type(peer) == (void *)&_xpc_type_dictionary) return;	/* an error of the listener */
		if ((c = malloc(sizeof(*c))) == 0) { xpc_connection_cancel(peer); return; }
		c->peer = peer; c->pid = xpc_connection_get_pid(peer); c->suspended = 0; c->agent = agents; c->regs = 0;
		c->queue = xpc_array_create(0, 0);
		c->next = clients; clients = c;
		xpc_connection_set_target_queue(peer, q);
		xpc_connection_set_event_handler(peer, ^(xpc_object_t m) {
			if (xpc_get_type(m) == (void *)&_xpc_type_dictionary) handle(c, m);
			else client_drop(c);					/* invalid / interrupted: the client is gone */
		});
		xpc_connection_resume(peer);
	});
	xpc_connection_resume(svc);
	out("distnoted: serving "); out(name); out("\n");
}

/* agent: the connection to the daemon; the posts of other sessions arrive on it as "post" */
static const char *super_name = "com.apple.distributed_notifications@0v3";
static void
super_connect(void)
{
	xpc_object_t hello;

	if (super_conn || daemon_mode) return;
	super_conn = xpc_connection_create_mach_service(super_name, q, 0);
	if (super_conn == 0) { super_dead = 1; return; }
	xpc_connection_set_event_handler(super_conn, ^(xpc_object_t m) {
		const char *method;

		if (xpc_get_type(m) != (void *)&_xpc_type_dictionary) { super_dead = 1; return; }
		method = xpc_dictionary_get_string(m, "method");
		if (method && streq(method, "post") && xpc_dictionary_get_uint64(m, "version") == 1) {
			const char *name = xpc_dictionary_get_string(m, "name"), *object = xpc_dictionary_get_string(m, "object");

			if (name) post_local(name, object ? object : ANY_OBJECT, xpc_dictionary_get_value(m, "userinfo"), xpc_dictionary_get_uint64(m, "options"));
		}
	});
	xpc_connection_resume(super_conn);
	hello = xpc_dictionary_create(0, 0, 0);			/* so that the daemon knows this agent before the first post_all */
	xpc_dictionary_set_string(hello, "method", "unsuspend");
	xpc_dictionary_set_uint64(hello, "version", 1);
	xpc_connection_send_message(super_conn, hello);
	xpc_release(hello);
}

int
main(int argc, char **argv)
{
	const char *uname = "com.apple.distributed_notifications@Uv3", *sname = "com.apple.distributed_notifications@1v3";
	const char *aname = "com.apple.distributed_notifications@0v3";
	int i;

	q = dispatch_queue_create("com.apple.distnoted", 0);
	for (i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (streq(a, "-v")) verbose = 1;
		else if (streq(a, "daemon")) daemon_mode = 1;
		else if (streq(a, "agent")) daemon_mode = 0;
		else if (prefix(a, "agent=")) { daemon_mode = 0; uname = a + 6; }
		else if (prefix(a, "super=")) super_name = a + 6;
		else if (prefix(a, "daemon=")) { daemon_mode = 1; sname = a + 7; }
		else if (prefix(a, "agents=")) aname = a + 7;
	}
	if (daemon_mode) { serve(sname, 0); serve(aname, 1); }
	else { serve(uname, 0); super_connect(); }
	dispatch_main();
	return (0);
}
