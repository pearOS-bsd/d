/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Alexandru Balan
 *
 * tccd for pearOS: the daemon of Transparency, Consent and Control, wire compatible with the tccd of macOS 26.7.2.
 * `tccd system` takes com.apple.tccd.system (the system instance, root), `tccd` (or `tccd user`) takes com.apple.tccd (one
 * per user), `tccd both` takes both in one process (the lab); `system=NAME` / `user=NAME` serve an instance under another
 * name (the test against Apple's client on a Mac, through a job com.pearos.t.tccd and the IconServices redirect).
 * It answers what Apple's libTCC sends (TCCAccessPreflight, TCCAccessRequest, TCCAccessCheckAuditToken,
 * TCCAccessPreflightWithAuditToken, the tcc_server SPI): every message carries "function" = "TCCAccessRequest",
 * "service", "preflight", "target_token" (null or the 32-byte audit token of another process), "TCCD_MSG_ID".
 * The reply has Apple's keys: result, auth_value, auth_reason, auth_version, prompt_type, preflight_unknown,
 * auth_error_code, auth_error_string.  Every other function gets the empty dictionary that Apple's tccd gives to a
 * client it does not serve.  No message that asks for a reply is left without one.
 * Protocol, evidence and the policy: research/docs/TCC-WIRE.md.  No Apple header or file is used (plain C, the
 * functions of libSystem are declared here).
 *
 * The policy (in the order of Apple's tccd):
 *  1. a service that macOS does not know: auth_error_code 2, "Invalid Service Name: <name>";
 *  2. the code signature of the subject has the entitlement com.apple.private.tcc.allow with the service: allowed,
 *     auth_reason 11 (Entitled);
 *  3. a row of the access table of TCC.db (Apple's SQLite file, the system one for the system instance, the user's
 *     one for the user instance) for the subject's signing identifier (client_type 0) or path (client_type 1);
 *  4. the default of the service on a Mac for a client with no row: "unknown" for a preflight (auth_value 1), denied
 *     for a request (there is no consent prompt on pearOS yet), or the fixed answer of the services that have a
 *     service policy (auth_reason 5).
 */
typedef void *xpc_object_t;
typedef void *dq;
typedef unsigned long size_t;

extern void xpc_connection_set_event_handler(xpc_object_t c, void (^h)(xpc_object_t));
extern xpc_object_t xpc_connection_create_mach_service(const char *name, dq q, unsigned long flags);
extern void xpc_connection_resume(xpc_object_t c);
extern void xpc_connection_send_message(xpc_object_t c, xpc_object_t m);
extern int xpc_connection_get_pid(xpc_object_t c);
extern xpc_object_t xpc_dictionary_create_reply(xpc_object_t m);
extern const char *xpc_dictionary_get_string(xpc_object_t d, const char *k);
extern int xpc_dictionary_get_bool(xpc_object_t d, const char *k);
extern const void *xpc_dictionary_get_data(xpc_object_t d, const char *k, size_t *len);
extern unsigned long xpc_dictionary_get_uint64(xpc_object_t d, const char *k);
extern void xpc_dictionary_set_bool(xpc_object_t d, const char *k, int v);
extern void xpc_dictionary_set_uint64(xpc_object_t d, const char *k, unsigned long v);
extern void xpc_dictionary_set_string(xpc_object_t d, const char *k, const char *v);
extern void xpc_release(xpc_object_t o);
extern void *xpc_get_type(xpc_object_t o);
extern const struct _xpc_type_s _xpc_type_dictionary;
extern dq dispatch_queue_create(const char *label, void *attr);
extern void dispatch_main(void);
extern long write(int fd, const void *buf, size_t n);
extern int open(const char *path, int flags, ...);
extern long pread(int fd, void *buf, size_t n, long off);
extern int close(int fd);
extern void *malloc(size_t n);
extern void free(void *p);
extern char *getenv(const char *name);
extern int proc_pidpath(int pid, void *buf, unsigned int size);

/* libsqlite3 (in the dyld cache of the Mac that pearOS runs on): linked weakly, a system without it answers from rules 1, 2 and 4 */
typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;
extern int sqlite3_open_v2(const char *path, sqlite3 **db, int flags, const char *vfs) __attribute__((weak_import));
extern int sqlite3_prepare_v2(sqlite3 *db, const char *sql, int n, sqlite3_stmt **st, const char **tail) __attribute__((weak_import));
extern int sqlite3_bind_text(sqlite3_stmt *st, int i, const char *v, int n, void (*d)(void *)) __attribute__((weak_import));
extern int sqlite3_step(sqlite3_stmt *st) __attribute__((weak_import));
extern int sqlite3_column_int(sqlite3_stmt *st, int i) __attribute__((weak_import));
extern int sqlite3_finalize(sqlite3_stmt *st) __attribute__((weak_import));
extern int sqlite3_close(sqlite3 *db) __attribute__((weak_import));
extern const char *sqlite3_errmsg(sqlite3 *db) __attribute__((weak_import));

/* the authorization values and reasons of TCC (the auth_value / auth_reason columns of TCC.db and the reply keys) */
enum { AUTH_DENIED = 0, AUTH_UNKNOWN = 1, AUTH_ALLOWED = 2 };
enum { REASON_NONE = 0, REASON_ERROR = 1, REASON_SYSTEM_SET = 4, REASON_SERVICE_POLICY = 5, REASON_PREFLIGHT_UNKNOWN = 10, REASON_ENTITLED = 11 };

static size_t slen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static void out(const char *s) { write(2, s, slen(s)); }
static int streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void
outn(long v)
{
	char b[24]; int i = 23;

	b[i] = 0;
	if (v < 0) { out("-"); v = -v; }
	do { b[--i] = '0' + v % 10; v /= 10; } while (v);
	out(b + i);
}

/* The services that tccd of macOS 26.7.2 accepts (every other name: "Invalid Service Name"), with the instance that libTCC sends them to and what the Mac answers to a
 * client that has no row and no entitlement (measured one by one with TCCAccessPreflight, research/docs/TCC-WIRE.md 4): 'U' unknown (the prompt decides), 'D' denied by
 * service policy, 'A' allowed by service policy.  The first character is 's' for the system instance, 'u' for the user instance. */
static const char *const services[] = {
	"sUAccessibility", "sDDeveloperTool", "sUListenEvent", "sUPostEvent", "sURemoteDesktop", "sUScreenCapture", "sDSystemPolicyAllFiles",
	"uUAddressBook", "uUAll", "uUAppleEvents", "uUAudioCapture", "uUBluetoothAlways", "uUCalendar", "uUCamera", "uUContactsFull", "uUContactsLimited",
	"uUEnergyKitGuidance", "uUExternalCameraMedia", "uDFacebook", "uUFileProviderDomain", "uUFileProviderPresence", "uUFocusStatus", "uUGameCenterFriends",
	"uDLinkedIn", "uALiverpool", "uUMediaLibrary", "uUMicrophone", "uUMotion", "uDPasteboard", "uUPhotos", "uUPhotosAdd", "uUPrototype3Rights",
	"uUPrototype4Rights", "uUReminders", "uDShareKit", "uDSinaWeibo", "uUSiri", "uUSpeechRecognition", "uUSystemPolicyAppBundles", "uUSystemPolicyAppData",
	"uUSystemPolicyDesktopFolder", "uUSystemPolicyDeveloperFiles", "uUSystemPolicyDocumentsFolder", "uUSystemPolicyDownloadsFolder",
	"uUSystemPolicyNetworkVolumes", "uUSystemPolicyRemovableVolumes", "uUSystemPolicySysAdminFiles", "uDTencentWeibo", "uDTwitter", "uAUbiquity",
	"uAUserTracking", "uUVirtualMachineNetworking", "uUVoiceBanking", "uUWebBrowserPublicKeyCredential", "uAWebKitIntelligentTrackingPrevention", "uUWillow",
	0
};

static const char *
service_find(const char *name)
{
	int i;

	if (name == 0) return (0);
	for (i = 0; i < 11; i++) if (name[i] != "kTCCService"[i]) return (0);
	for (i = 0; services[i]; i++) if (streq(services[i] + 2, name + 11)) return (services[i]);
	return (0);
}

/* ---- the subject: its executable, the signing identifier and the entitlements of its code signature ---- */

struct subject { int pid; char path[1024]; char ident[256]; char *ents; };

static unsigned int be32(const unsigned char *p) { return (unsigned int)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static unsigned int le32(const unsigned char *p) { return (unsigned int)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]; }

/* Reads the SuperBlob of LC_CODE_SIGNATURE of the x86_64 slice: the identifier of the CodeDirectory (slot 0) and the XML entitlements (slot 5, magic 0xfade7171) */
static void
subject_signature(struct subject *s)
{
	enum { HSZ = 65536 };				/* the header and every load command (the Dock's take 11856 bytes) */
	unsigned char *h = malloc(HSZ), *sb = 0;
	long base = 0;
	unsigned int i, n, off, sz;
	int fd = open(s->path, 0);

	if (fd < 0 || h == 0) { if (fd >= 0) close(fd); free(h); return; }
	if (pread(fd, h, HSZ, 0) < 32) goto done;
	if (be32(h) == 0xcafebabe) {			/* fat: the x86_64 slice */
		n = be32(h + 4);
		for (i = 0; i < n && 8 + i * 20 + 20 <= HSZ; i++)
			if (be32(h + 8 + i * 20) == 0x01000007) { base = be32(h + 8 + i * 20 + 8); break; }
		if (base == 0 || pread(fd, h, HSZ, base) < 32) goto done;
	}
	if (le32(h) != 0xfeedfacf) goto done;
	n = le32(h + 16); off = 32; sz = 0;
	for (i = 0; i < n && off + 16 <= HSZ; i++) {
		if (le32(h + off) == 0x1d) { sz = le32(h + off + 12); off = le32(h + off + 8); break; }	/* LC_CODE_SIGNATURE: dataoff, datasize */
		off += le32(h + off + 4);
	}
	if (sz < 12 || sz > (1 << 22) || (sb = malloc(sz)) == 0) goto done;
	if (pread(fd, sb, sz, base + off) != (long)sz || be32(sb) != 0xfade0cc0) goto done;
	n = be32(sb + 8);
	for (i = 0; i < n && 12 + i * 8 + 8 <= sz; i++) {
		unsigned int ty = be32(sb + 12 + i * 8), bo = be32(sb + 12 + i * 8 + 4), bl;

		if (bo + 8 > sz) continue;
		bl = be32(sb + bo + 4);
		if (bl > sz - bo) continue;
		if (ty == 0 && be32(sb + bo) == 0xfade0c02 && bl > 24) {		/* CodeDirectory: identOffset at +20 */
			unsigned int io = be32(sb + bo + 20), k;

			for (k = 0; io + k < bl && k + 1 < sizeof(s->ident) && sb[bo + io + k]; k++) s->ident[k] = sb[bo + io + k];
			s->ident[k] = 0;
		} else if (ty == 5 && be32(sb + bo) == 0xfade7171 && s->ents == 0 && (s->ents = malloc(bl - 8 + 1)) != 0) {
			unsigned int k;

			for (k = 0; k < bl - 8; k++) s->ents[k] = sb[bo + 8 + k];
			s->ents[bl - 8] = 0;
		}
	}
done:
	free(sb);
	free(h);
	close(fd);
}

static void
subject_get(struct subject *s, int pid)
{
	s->pid = pid; s->path[0] = 0; s->ident[0] = 0; s->ents = 0;
	if (pid <= 0 || proc_pidpath(pid, s->path, sizeof(s->path)) <= 0) { s->path[0] = 0; return; }
	subject_signature(s);
}

static const char *
find(const char *h, const char *n)
{
	size_t k = slen(n);

	for (; *h; h++) {
		size_t i;

		for (i = 0; i < k && h[i] == n[i]; i++) ;
		if (i == k) return (h);
	}
	return (0);
}

/* The entitlement `key` (an array of service names in the XML plist of the signature) lists `service` */
static int
entitled(const struct subject *s, const char *key, const char *service)
{
	char k[160], v[160];
	const char *p, *e;
	size_t i, j;

	if (s->ents == 0) return (0);
	for (i = 0, j = 0; "<key>"[j]; j++) k[i++] = "<key>"[j];
	for (j = 0; key[j] && i < 140; j++) k[i++] = key[j];
	for (j = 0; "</key>"[j]; j++) k[i++] = "</key>"[j];
	k[i] = 0;
	for (i = 0, j = 0; "<string>"[j]; j++) v[i++] = "<string>"[j];
	for (j = 0; service[j] && i < 140; j++) v[i++] = service[j];
	for (j = 0; "</string>"[j]; j++) v[i++] = "</string>"[j];
	v[i] = 0;
	if ((p = find(s->ents, k)) == 0) return (0);
	p += slen(k);
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
	if (find(p, "<array>") != p) return (0);
	if ((e = find(p, "</array>")) == 0) return (0);
	return ((p = find(p, v)) != 0 && p < e);
}

/* ---- TCC.db: Apple's file (SQLite, table access) ---- */

static const char *dbpath;	/* -check ... DB: a database file instead of Apple's place */

static int
db_lookup(const struct subject *s, const char *service, int sysinst, int *value, int *reason)
{
	char path[1100];
	const char *home, *rel = "/Library/Application Support/com.apple.TCC/TCC.db";
	sqlite3 *db = 0;
	sqlite3_stmt *st = 0;
	size_t i = 0, j;
	int found = 0;

	if (sqlite3_open_v2 == 0) return (0);
	if (!sysinst) {
		if ((home = getenv("HOME")) == 0 || slen(home) > 1000) return (0);
		for (j = 0; home[j]; j++) path[i++] = home[j];
	}
	for (j = 0; rel[j]; j++) path[i++] = rel[j];
	path[i] = 0;
	if (dbpath) { for (i = 0; dbpath[i] && i < 1099; i++) path[i] = dbpath[i]; path[i] = 0; }
	if (sqlite3_open_v2(path, &db, 1 /* SQLITE_OPEN_READONLY */, 0) != 0) { if (db) sqlite3_close(db); return (0); }
	if (sqlite3_prepare_v2(db, "SELECT auth_value, auth_reason FROM access WHERE service = ?1 AND indirect_object_identifier = 'UNUSED' AND "
	    "((client_type = 0 AND client = ?2) OR (client_type = 1 AND client = ?3)) ORDER BY client_type LIMIT 1", -1, &st, 0) != 0) {
		out("tccd: "); out(path); out(": "); out(sqlite3_errmsg(db)); out("\n");
	} else {
		sqlite3_bind_text(st, 1, service, -1, 0);
		sqlite3_bind_text(st, 2, s->ident, -1, 0);
		sqlite3_bind_text(st, 3, s->path, -1, 0);
		int rc = sqlite3_step(st);

		if (rc == 100 /* SQLITE_ROW */) { *value = sqlite3_column_int(st, 0); *reason = sqlite3_column_int(st, 1); found = 1; }
		else if (rc != 101 /* SQLITE_DONE */) { out("tccd: "); out(path); out(": sqlite error "); outn(rc); out("\n"); }
		sqlite3_finalize(st);
	}
	sqlite3_close(db);
	return (found);
}

/* ---- the answers ---- */

static void
reply_auth(xpc_object_t r, int value, int reason, int preflight)
{
	if (value == AUTH_UNKNOWN && preflight) {
		xpc_dictionary_set_bool(r, "preflight_unknown", 1);
	} else {
		xpc_dictionary_set_bool(r, "result", value == AUTH_ALLOWED || value == 3 /* limited */);
	}
	xpc_dictionary_set_uint64(r, "auth_value", (unsigned long)value);
	xpc_dictionary_set_uint64(r, "auth_reason", (unsigned long)reason);
	xpc_dictionary_set_uint64(r, "auth_version", 1);
	xpc_dictionary_set_uint64(r, "prompt_type", 1);
}

static void
reply_error(xpc_object_t r, int code, const char *text1, const char *text2)
{
	char b[512];
	size_t i = 0, j;

	for (j = 0; text1[j] && i < 250; j++) b[i++] = text1[j];
	for (j = 0; text2 && text2[j] && i < 500; j++) b[i++] = text2[j];
	b[i] = 0;
	xpc_dictionary_set_string(r, "auth_error_string", b);
	xpc_dictionary_set_uint64(r, "auth_error_code", (unsigned long)code);
	xpc_dictionary_set_bool(r, "result", 0);
	xpc_dictionary_set_uint64(r, "auth_value", AUTH_DENIED);
	xpc_dictionary_set_uint64(r, "auth_reason", REASON_ERROR);
	if (code != 2) { xpc_dictionary_set_uint64(r, "auth_version", 1); xpc_dictionary_set_uint64(r, "prompt_type", 1); }
}

/* The decision for one subject (rules 2-4 of the header); promptpolicy: TCCD_MSG_MESSAGE_OPTION_REQUEST_PROMPT_POLICY_KEY of the tcc_server SPI (1: do not prompt) */
static void
decide(const struct subject *sub, const char *service, const char *ent, int sysinst, int preflight, unsigned long promptpolicy, int *value, int *reason)
{
	if (entitled(sub, "com.apple.private.tcc.allow", service)) { *value = AUTH_ALLOWED; *reason = REASON_ENTITLED; }
	else if (db_lookup(sub, service, sysinst, value, reason)) ;
	else if (ent[1] == 'A') { *value = AUTH_ALLOWED; *reason = REASON_SERVICE_POLICY; }
	else if (ent[1] == 'D') { *value = AUTH_DENIED; *reason = REASON_SERVICE_POLICY; }
	else if (preflight) { *value = AUTH_UNKNOWN; *reason = sysinst ? REASON_SERVICE_POLICY : REASON_NONE; }
	else { *value = AUTH_DENIED; *reason = promptpolicy == 1 ? REASON_PREFLIGHT_UNKNOWN : REASON_SERVICE_POLICY; }	/* no consent prompt on pearOS yet */
}

static void
access_request(xpc_object_t peer, xpc_object_t m, xpc_object_t r, int sysinst)
{
	const char *service = xpc_dictionary_get_string(m, "service"), *ent;
	int preflight = xpc_dictionary_get_bool(m, "preflight"), value, reason, rpid = xpc_connection_get_pid(peer), spid = rpid;
	size_t tl = 0;
	const unsigned char *tok = xpc_dictionary_get_data(m, "target_token", &tl);
	struct subject req, sub;

	if ((ent = service_find(service)) == 0) {
		reply_error(r, 2, "Invalid Service Name: ", service ? service : "(null)");
		out("tccd: pid "); outn(rpid); out(" "); out(service ? service : "(no service)"); out(": invalid service\n");
		return;
	}
	subject_get(&req, rpid);
	sub = req;
	if (tok != 0 && tl == 32) spid = (int)le32(tok + 20);			/* audit_token_t val[5]: the pid */
	if (spid != rpid) {
		/* checking another process needs com.apple.private.tcc.manager.check-by-audit-token for the service (WindowServer has it for the event and capture services) */
		if (!entitled(&req, "com.apple.private.tcc.manager.check-by-audit-token", service)) {
			reply_error(r, 3, "Requestor is not entitled to check access for accessor ", req.path);
			out("tccd: pid "); outn(rpid); out(" not entitled to check pid "); outn(spid); out("\n");
			free(req.ents);
			return;
		}
		subject_get(&sub, spid);
	}
	decide(&sub, service, ent, sysinst, preflight, xpc_dictionary_get_uint64(m, "TCCD_MSG_MESSAGE_OPTION_REQUEST_PROMPT_POLICY_KEY"), &value, &reason);
	reply_auth(r, value, reason, preflight);
	out("tccd: pid "); outn(spid); out(" "); out(sub.path[0] ? sub.path : "?"); out(" ("); out(sub.ident[0] ? sub.ident : "no identifier"); out(") ");
	out(service); out(preflight ? " preflight" : " request"); out(": auth_value "); outn(value); out(" auth_reason "); outn(reason); out("\n");
	if (sub.ents != req.ents) free(sub.ents);
	free(req.ents);
}

static void
handle(xpc_object_t peer, xpc_object_t m, int sysinst)
{
	xpc_object_t r;
	const char *fn;

	if (xpc_get_type(m) != (void *)&_xpc_type_dictionary) return;		/* the end of a connection */
	if ((r = xpc_dictionary_create_reply(m)) == 0) return;			/* a message that wants no answer */
	fn = xpc_dictionary_get_string(m, "function");
	if (fn != 0 && streq(fn, "TCCAccessRequest")) access_request(peer, m, r, sysinst);
	else { out("tccd: function "); out(fn ? fn : "(none)"); out(": empty answer\n"); }	/* Apple's answer to a function that the client may not call */
	xpc_connection_send_message(peer, r);
	xpc_release(r);
}

/* sysinst: the instance of com.apple.tccd.system (the system TCC.db, auth_reason 5 for "unknown"); else a user's instance */
static void
serve(const char *name, dq q, int sysinst)
{
	xpc_object_t svc = xpc_connection_create_mach_service(name, q, 1 /* XPC_CONNECTION_MACH_SERVICE_LISTENER */);

	xpc_connection_set_event_handler(svc, ^(xpc_object_t peer) {
		if (xpc_get_type(peer) == (void *)&_xpc_type_dictionary) return;	/* an error of the listener */
		xpc_connection_set_event_handler(peer, ^(xpc_object_t m) { handle(peer, m, sysinst); });
		xpc_connection_resume(peer);
	});
	xpc_connection_resume(svc);
	out("tccd: serving "); out(name); out("\n");
}

int
main(int argc, char **argv)
{
	dq q = dispatch_queue_create("com.apple.tccd", 0);
	int i;

	if (argc >= 4 && streq(argv[1], "-check")) {	/* tccd -check PATH SERVICE [DB]: the answer for an executable, without a client (tests) */
		struct subject s;
		const char *ent = service_find(argv[3]);
		int v, r;

		s.pid = 0; s.ident[0] = 0; s.ents = 0;
		for (i = 0; argv[2][i] && i < 1023; i++) s.path[i] = argv[2][i];
		s.path[i] = 0;
		subject_signature(&s);
		if (argc > 4) dbpath = argv[4];
		out("sqlite: "); out(sqlite3_open_v2 != 0 ? "loaded\n" : "absent\n");
		out("identifier: "); out(s.ident[0] ? s.ident : "(none)"); out("\nentitlements: "); outn(s.ents ? (long)slen(s.ents) : 0); out(" bytes\n");
		if (ent == 0) { out("invalid service\n"); return (1); }
		decide(&s, argv[3], ent, ent[0] == 's', 1, 0, &v, &r);
		out("preflight: auth_value "); outn(v); out(" auth_reason "); outn(r); out("\n");
		return (0);
	}
	if (argc < 2) serve("com.apple.tccd", q, 0);
	for (i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (streq(a, "system") || streq(a, "both")) serve("com.apple.tccd.system", q, 1);
		if (streq(a, "user") || streq(a, "both")) serve("com.apple.tccd", q, 0);
		if (find(a, "system=") == a) serve(a + 7, q, 1);
		if (find(a, "user=") == a) serve(a + 5, q, 0);
	}
	dispatch_main();
	return (0);
}
