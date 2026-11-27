/* A stand-in for launchservicesd: it takes the service com.apple.coreservices.launchservicesd (the check-in through launchd, with the libxpc of the Mac), prints every message that a client sends
 * (the description of libxpc, to stderr) and answers with an empty dictionary.  It is for finding out what the real Dock asks LaunchServices; it is the first of the replacements. */
typedef void *xpc_object_t;
typedef void *dq;
static void od_reply(xpc_object_t m, xpc_object_t r);
extern int xpc_pipe_receive(unsigned int port, xpc_object_t *out, int flags);
extern int xpc_pipe_routine_reply(xpc_object_t reply);
extern int bootstrap_check_in(unsigned int bp, const char *name, unsigned int *port);
extern unsigned int bootstrap_port;
extern void dispatch_async(dq q, void (^b)(void));
extern xpc_object_t xpc_connection_create_mach_service(const char *name, dq q, unsigned long flags);
extern void xpc_connection_set_event_handler(xpc_object_t c, void (^h)(xpc_object_t));
extern void xpc_connection_resume(xpc_object_t c);
extern void xpc_connection_cancel(xpc_object_t c);
extern void xpc_release(xpc_object_t o);
extern void xpc_dictionary_set_string(xpc_object_t d, const char *k, const char *v);
extern void xpc_connection_send_message(xpc_object_t c, xpc_object_t m);
extern xpc_object_t xpc_dictionary_create_reply(xpc_object_t m);
extern xpc_object_t xpc_bool_create(int v);
extern xpc_object_t xpc_uint64_create(unsigned long v);
extern long xpc_dictionary_get_int64(xpc_object_t d, const char *k);
extern unsigned long xpc_dictionary_get_uint64(xpc_object_t d, const char *k);
extern void xpc_dictionary_set_uint64(xpc_object_t d, const char *k, unsigned long v);
extern void xpc_dictionary_set_int64(xpc_object_t d, const char *k, long v);
extern void xpc_dictionary_set_bool(xpc_object_t d, const char *k, int v);
extern void xpc_dictionary_set_data(xpc_object_t d, const char *k, const void *v, unsigned long n);
extern void xpc_dictionary_set_value(xpc_object_t d, const char *k, xpc_object_t v);
extern xpc_object_t xpc_dictionary_create(const char *const *keys, const xpc_object_t *values, unsigned long count);
extern xpc_object_t xpc_shmem_create(void *region, unsigned long length);
extern void *mmap(void *addr, unsigned long len, int prot, int flags, int fd, long off);
extern xpc_object_t xpc_dictionary_get_value(xpc_object_t d, const char *k);
extern unsigned long xpc_array_get_count(xpc_object_t a);
extern xpc_object_t xpc_array_get_value(xpc_object_t a, unsigned long i);
extern const char *xpc_string_get_string_ptr(xpc_object_t s);
extern xpc_object_t xpc_copy(xpc_object_t o);
extern const char *xpc_dictionary_get_string(xpc_object_t d, const char *k);
extern xpc_object_t xpc_string_create(const char *s);
extern xpc_object_t xpc_data_create(const void *b, unsigned long n);
extern xpc_object_t xpc_uuid_create(const unsigned char *u);
extern int xpc_connection_get_pid(xpc_object_t c);
extern int xpc_connection_get_asid(xpc_object_t c);
extern xpc_object_t xpc_array_create(const xpc_object_t *objects, unsigned long count);
extern void xpc_array_append_value(xpc_object_t a, xpc_object_t v);
extern xpc_object_t xpc_uint64_create(unsigned long v);
extern xpc_object_t xpc_string_create(const char *s);
extern char *xpc_copy_description(xpc_object_t o);
extern void *xpc_get_type(xpc_object_t o);
extern const struct _xpc_type_s _xpc_type_connection, _xpc_type_dictionary;
extern dq dispatch_queue_create(const char *label, void *attr);
extern void dispatch_main(void);
extern long write(int fd, const void *buf, unsigned long n);
extern void free(void *p);

static void out(const char *s) { unsigned long n = 0; while (s[n]) n++; write(2, s, n); }

static int
streq(const char *a, const char *b)
{
	while (*a && *a == *b) { a++; b++; }
	return *a == *b;
}

static void
lower(char *o, const char *s)
{
	unsigned long i;

	for (i = 0; s[i] && i < 254; i++) o[i] = s[i] >= 'A' && s[i] <= 'Z' ? s[i] + 32 : s[i];
	o[i] = 0;
}

/* the keyed archive that the runningboardd of the Mac sends with a process handle (a BSAuditToken under the key "35770"); the token is at offset 126 */
static const unsigned char rbs_archive[278] = {0x62,0x70,0x6c,0x69,0x73,0x74,0x30,0x30,0xd4,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x0a,0x58,0x24,0x76,0x65,0x72,0x73,0x69,0x6f,0x6e,0x59,0x24,0x61,0x72,0x63,0x68,0x69,0x76,0x65,0x72,0x54,0x24,0x74,0x6f,0x70,0x58,0x24,0x6f,0x62,0x6a,0x65,0x63,0x74,0x73,0x12,0x00,0x01,0x86,0xa0,0x5f,0x10,0x0f,0x4e,0x53,0x4b,0x65,0x79,0x65,0x64,0x41,0x72,0x63,0x68,0x69,0x76,0x65,0x72,0xd1,0x08,0x09,0x55,0x33,0x35,0x37,0x37,0x30,0x80,0x01,0xa4,0x0b,0x0c,0x11,0x12,0x55,0x24,0x6e,0x75,0x6c,0x6c,0xd2,0x0d,0x0e,0x0f,0x10,0x56,0x24,0x63,0x6c,0x61,0x73,0x73,0x5b,0x5f,0x61,0x75,0x64,0x69,0x74,0x54,0x6f,0x6b,0x65,0x6e,0x80,0x03,0x80,0x02,0x4f,0x10,0x20,0xf5,0x01,0x00,0x00,0xf5,0x01,0x00,0x00,0x14,0x00,0x00,0x00,0xf5,0x01,0x00,0x00,0x14,0x00,0x00,0x00,0x2e,0x4e,0x00,0x00,0xc0,0x87,0x01,0x00,0x61,0xcb,0x00,0x00,0xd2,0x13,0x14,0x15,0x16,0x5a,0x24,0x63,0x6c,0x61,0x73,0x73,0x6e,0x61,0x6d,0x65,0x58,0x24,0x63,0x6c,0x61,0x73,0x73,0x65,0x73,0x5c,0x42,0x53,0x41,0x75,0x64,0x69,0x74,0x54,0x6f,0x6b,0x65,0x6e,0xa2,0x17,0x18,0x5c,0x42,0x53,0x41,0x75,0x64,0x69,0x74,0x54,0x6f,0x6b,0x65,0x6e,0x58,0x4e,0x53,0x4f,0x62,0x6a,0x65,0x63,0x74,0x08,0x11,0x1a,0x24,0x29,0x32,0x37,0x49,0x4c,0x52,0x54,0x59,0x5f,0x64,0x6b,0x77,0x79,0x7b,0x9e,0xa3,0xae,0xb7,0xc4,0xc7,0xd4,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x19,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xdd};

/* the error that the runningboardd of the Mac answers to a refused call (an NSError in a keyed archive under the key "36190"): used for the calls that are not served */
static const unsigned char rbs_error[492] = {0x62,0x70,0x6c,0x69,0x73,0x74,0x30,0x30,0xd4,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x0a,0x58,0x24,0x76,0x65,0x72,0x73,0x69,0x6f,0x6e,0x59,0x24,0x61,0x72,0x63,0x68,0x69,0x76,0x65,0x72,0x54,0x24,0x74,0x6f,0x70,0x58,0x24,0x6f,0x62,0x6a,0x65,0x63,0x74,0x73,0x12,0x00,0x01,0x86,0xa0,0x5f,0x10,0x0f,0x4e,0x53,0x4b,0x65,0x79,0x65,0x64,0x41,0x72,0x63,0x68,0x69,0x76,0x65,0x72,0xd1,0x08,0x09,0x55,0x33,0x36,0x31,0x39,0x30,0x80,0x01,0xa8,0x0b,0x0c,0x15,0x16,0x1e,0x1f,0x20,0x26,0x55,0x24,0x6e,0x75,0x6c,0x6c,0xd4,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x56,0x4e,0x53,0x43,0x6f,0x64,0x65,0x5a,0x4e,0x53,0x55,0x73,0x65,0x72,0x49,0x6e,0x66,0x6f,0x58,0x4e,0x53,0x44,0x6f,0x6d,0x61,0x69,0x6e,0x56,0x24,0x63,0x6c,0x61,0x73,0x73,0x10,0x01,0x80,0x03,0x80,0x02,0x80,0x07,0x5f,0x10,0x15,0x52,0x42,0x53,0x53,0x65,0x72,0x76,0x69,0x63,0x65,0x45,0x72,0x72,0x6f,0x72,0x44,0x6f,0x6d,0x61,0x69,0x6e,0xd3,0x17,0x18,0x10,0x19,0x1b,0x1d,0x57,0x4e,0x53,0x2e,0x6b,0x65,0x79,0x73,0x5a,0x4e,0x53,0x2e,0x6f,0x62,0x6a,0x65,0x63,0x74,0x73,0xa1,0x1a,0x80,0x04,0xa1,0x1c,0x80,0x05,0x80,0x06,0x5f,0x10,0x18,0x4e,0x53,0x4c,0x6f,0x63,0x61,0x6c,0x69,0x7a,0x65,0x64,0x46,0x61,0x69,0x6c,0x75,0x72,0x65,0x52,0x65,0x61,0x73,0x6f,0x6e,0x5f,0x10,0x48,0x6f,0x72,0x69,0x67,0x69,0x6e,0x61,0x74,0x6f,0x72,0x20,0x64,0x6f,0x65,0x73,0x6e,0x27,0x74,0x20,0x68,0x61,0x76,0x65,0x20,0x65,0x6e,0x74,0x69,0x74,0x6c,0x65,0x6d,0x65,0x6e,0x74,0x20,0x63,0x6f,0x6d,0x2e,0x61,0x70,0x70,0x6c,0x65,0x2e,0x70,0x72,0x69,0x76,0x61,0x74,0x65,0x2e,0x72,0x75,0x6e,0x6e,0x69,0x6e,0x67,0x62,0x6f,0x61,0x72,0x64,0x2e,0x72,0x6f,0x6c,0x65,0x73,0xd2,0x21,0x22,0x23,0x24,0x5a,0x24,0x63,0x6c,0x61,0x73,0x73,0x6e,0x61,0x6d,0x65,0x58,0x24,0x63,0x6c,0x61,0x73,0x73,0x65,0x73,0x5c,0x4e,0x53,0x44,0x69,0x63,0x74,0x69,0x6f,0x6e,0x61,0x72,0x79,0xa2,0x23,0x25,0x58,0x4e,0x53,0x4f,0x62,0x6a,0x65,0x63,0x74,0xd2,0x21,0x22,0x27,0x28,0x57,0x4e,0x53,0x45,0x72,0x72,0x6f,0x72,0xa2,0x27,0x25,0x00,0x08,0x00,0x11,0x00,0x1a,0x00,0x24,0x00,0x29,0x00,0x32,0x00,0x37,0x00,0x49,0x00,0x4c,0x00,0x52,0x00,0x54,0x00,0x5d,0x00,0x63,0x00,0x6c,0x00,0x73,0x00,0x7e,0x00,0x87,0x00,0x8e,0x00,0x90,0x00,0x92,0x00,0x94,0x00,0x96,0x00,0xae,0x00,0xb5,0x00,0xbd,0x00,0xc8,0x00,0xca,0x00,0xcc,0x00,0xce,0x00,0xd0,0x00,0xd2,0x00,0xed,0x01,0x38,0x01,0x3d,0x01,0x48,0x01,0x51,0x01,0x5e,0x01,0x61,0x01,0x6a,0x01,0x6f,0x01,0x77,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x29,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x7a};

static unsigned long front_pid;

/* the application records of the daemon (found with a program on the Mac: SwiftUI/docs/EVIDENCE.md): the check-in (500) stores the information of the application, 450 replaces keys of it
 * (TransformProcessType / -[NSApplication setActivationPolicy:] sends the whole record with "ApplicationType" = "Foreground"), 360 reads keys (restrictto) or the whole record, 130 compares two ASNs */
static struct { unsigned long pid; xpc_object_t rec; } recs[64];

/* the info page (command 110, "lsinfopage"): one shared page for the session, written in place; the clients map it once (vm_remap, shared) and read
 * it without asking again.  Layout (research/docs/LAUNCHSERVICES-WIRE.md 6.3, decompiled from LaunchServices LSSharedMemoryPage): a client drops a page
 * whose version (+0) is not 0x104 or whose size (+4) is below 0xe460, and then asks for it again at every use.  Header as LSSharedMemoryPage(LSSessionID)
 * makes it; one record of 0x48 bytes at 0x90 + i * 0x48 per checked-in application, at most 0x32a. */
static unsigned int *infopage;

static unsigned int *
page_get(void)
{
	if (infopage == 0) {
		void *p = mmap(0, 61440, 3, 0x1001, -1, 0);	/* MAP_ANON | MAP_SHARED */

		if ((long)p == -1) return (0);
		infopage = p;
		infopage[0] = 0x104;		/* version */
		infopage[1] = 61440;		/* size */
		infopage[2] = 100001;		/* the session */
		infopage[0x20 / 4] = infopage[0x24 / 4] = infopage[0x28 / 4] = 100;	/* the seeds start at 100 */
		infopage[0x2c / 4] = infopage[0x30 / 4] = infopage[0x34 / 4] = 100;
		infopage[0x68 / 4] = 257;	/* the session of the graphics server (the same as the answer to 200) */
		infopage[0x8c / 4] = 0x32a;	/* the number of records that fit */
	}
	return (infopage);
}

/* a checked-in application: its record in the page (the ASN low half, the ChangeCount of its information, flags 0x80 = checked in, the pid) */
static void
page_add(unsigned int asnlo, unsigned int chg, unsigned int pid)
{
	unsigned int *pg = page_get(), n, i, *r;

	if (pg == 0) return;
	n = pg[0x88 / 4];
	for (i = 0; i < n; i++)
		if (pg[(0x90 + i * 0x48) / 4 + 4] == pid) break;
	if (i == n) {
		if (n >= 0x32a) return;
		pg[0x88 / 4] = n + 1;
		pg[0x28 / 4]++;			/* applicationListSeed */
	}
	r = pg + (0x90 + i * 0x48) / 4;
	r[1] = 0;				/* +4 first to 0 and last to its value, as the daemon writes a record (LSSharedMemoryPage::SetApplicationInformation) */
	r[0] = asnlo;
	r[2] = 0x80;
	r[4] = pid;
	r[1] = chg;
	pg[0x34 / 4]++;				/* applicationInformationSeed */
}

static xpc_object_t
rec_by_asn(unsigned long asn)
{
	unsigned long lo = asn & 0xffffffffUL, pid = lo >= 100000 ? lo - 100000 : lo;
	int i;

	for (i = 0; i < 64; i++)
		if (recs[i].rec != 0 && recs[i].pid == pid) return (recs[i].rec);
	return (0);
}

static void
rec_store(unsigned long pid, xpc_object_t rec)
{
	int i, free_i = -1;

	for (i = 0; i < 64; i++) {
		if (recs[i].rec != 0 && recs[i].pid == pid) { recs[i].rec = rec; return; }
		if (recs[i].rec == 0 && free_i < 0) free_i = i;
	}
	if (free_i < 0) free_i = 0;
	recs[free_i].pid = pid; recs[free_i].rec = rec;
}

static void
handle(xpc_object_t peer, xpc_object_t m, int nsxpc, const char *svc)
{
	char *d = xpc_copy_description(m);
	xpc_object_t r;

	out("lsd-stub: message ("); out(svc); out("): "); out(d); out("\n");
	free(d);
	if ((streq(svc, "com.apple.talagent") || streq(svc, "com.apple.ViewBridgeAuxiliary")) && xpc_get_type(m) == (void *)&_xpc_type_dictionary) {
		/* talagent does not run in the session of an ssh login on the Mac: the look-up fails there at once and AppKit then has no persistent-UI manager (NSPersistentUIManager sharedManager is nil).  An answer makes AppKit wait for the end of the restoring of windows, and until then it orders windows without animation and without a transaction.  The connection is cancelled: the caller gets an error at once. */
		xpc_connection_cancel(peer);
		return;
	}
	if (xpc_get_type(m) == (void *)&_xpc_type_dictionary && xpc_dictionary_get_value(m, "replysig")) {
		xpc_object_t nr;

		/* a service whose clients wait for the answer (the fonts): an empty NSXPC answer (the client reports an error and goes on) */
		if (nsxpc && (nr = xpc_dictionary_create_reply(m)) != 0) {
			/* the reply of NSXPC: only "root", the archive of the arguments of the reply block (found with a real NSXPC service: this is (nil, nil)) */
			static const unsigned char nilnil[36] = { 0x62,0x70,0x6c,0x69,0x73,0x74,0x31,0x37, 0xa0,0x23,0,0,0,0,0,0, 0,0xe0,0x76,0x76,0x40,0x3f,0x40,0x40, 0,0xa0,0x23,0,0,0,0,0, 0,0,0xe0,0xe0 };

			static const unsigned char boolno[35] = { 0x62,0x70,0x6c,0x69,0x73,0x74,0x31,0x37, 0xa0,0x22,0,0,0,0,0,0, 0,0xe0,0x75,0x76,0x40,0x3f,0x63,0, 0xa0,0x22,0,0,0,0,0,0, 0,0x11,0 };	/* a reply block with one BOOL: NO */
			const char *sig = xpc_dictionary_get_string(m, "replysig");

			if (sig != 0 && streq(sig, "v12@?0c8"))
				xpc_dictionary_set_value(nr, "root", xpc_data_create(boolno, sizeof(boolno)));
			else
				xpc_dictionary_set_value(nr, "root", xpc_data_create(nilnil, sizeof(nilnil)));
			xpc_dictionary_set_uint64(nr, "sequence", xpc_dictionary_get_uint64(m, "sequence"));
			xpc_connection_send_message(peer, nr);
			{ out("lsd-stub: empty NSXPC answer sent for "); out(svc); out("\n"); }
		} else
			{ out("lsd-stub: NO answer to an NSXPC message of "); out(svc); out(" (not in the NSXPC list)\n"); }
		return;
	}	/* the answers of an NSXPC message (no "replysig" reply) crash libxpc of the Mac: not answering is the only thing that works here */
	if (xpc_get_type(m) == (void *)&_xpc_type_dictionary && (r = xpc_dictionary_create_reply(m)) != 0) {
		long cmd = xpc_dictionary_get_int64(m, "command");

		if (streq(svc, "com.apple.system.opendirectoryd.libinfo") && xpc_dictionary_get_string(m, "rpc_name") != 0) {
			od_reply(m, r);
			{ int pr = xpc_pipe_routine_reply(r); out(pr == 0 ? "lsd-stub: od pipe reply sent\n" : "lsd-stub: od pipe reply FAILED\n"); }
			return;
		}
		if (streq(svc, "com.apple.pasteboard.1") && xpc_dictionary_get_string(m, "com.apple.pboard.message") != 0) {
			/* the pasteboard server: the answer of the one of the Mac to "create" (found with a program): the change count, the generation, whether we own it, a uuid */
			static const unsigned char u[16] = { 0x4c, 0xfb, 0x29, 0x07, 0x91, 0x27, 0x41, 0xd7, 0x85, 0x41, 0x3f, 0x3a, 0x41, 0x7b, 0x0b, 0x19 };

			xpc_dictionary_set_uint64(r, "com.apple.pboard.change", 1);
			xpc_dictionary_set_uint64(r, "com.apple.pboard.generation", 1);
			xpc_dictionary_set_bool(r, "com.apple.pboard.isOwner", 0);
			xpc_dictionary_set_value(r, "com.apple.pboard.uuid", xpc_uuid_create(u));
			xpc_connection_send_message(peer, r);
			return;
		}
		if (streq(svc, "com.apple.uiintelligencesupport.agent") && xpc_dictionary_get_value(m, "_CodableBody") != 0) {
			/* IntelligenceSupport's Codable XPC (macOS 26): AppKit sends "initialize" before it commits the first window and goes on with the reply.  Reply in the same shape: an empty body, version 1, not sync. The real body of the reply is not known yet. */
			xpc_dictionary_set_data(r, "_CodableBody", "", 0);
			xpc_dictionary_set_value(r, "_CodableOutOfLine4CodableObject", xpc_array_create(0, 0));
			xpc_dictionary_set_value(r, "_CodableOutOfLine", xpc_array_create(0, 0));
			xpc_dictionary_set_int64(r, "_CodableCoderVersion", 1);
			xpc_dictionary_set_bool(r, "_CodableIsSync", 0);
			xpc_connection_send_message(peer, r);
			out("lsd-stub: uiintelligencesupport reply sent\n");
			return;
		}
		if (streq(svc, "com.apple.pluginkit.pkd") && xpc_dictionary_get_string(m, "request") != 0) {
			/* PlugInKit: {"matches": {key: data}}; the data is a bplist15 of an empty list of plug-ins (the answer of the pkd of the Mac to a match with no result, found with a program) */
			static const unsigned char bp[23] = { 'b','p','l','i','s','t','1','5', 0x13, 0x17, 0,0,0,0,0,0, 0x80, 0x12, 0,0,0,0, 0xa0 };
			xpc_object_t mt = xpc_dictionary_create(0, 0, 0);

			xpc_dictionary_set_data(mt, "ECF19A18-7AA6-4141-B4DC-A2E5123B2B5C", bp, sizeof(bp));
			xpc_dictionary_set_value(r, "matches", mt);
			xpc_connection_send_message(peer, r);
			return;
		}

		if (streq(svc, "com.apple.runningboard") && xpc_dictionary_get_string(m, "rbs_selector") != 0) {
			/* RunningBoard: the answer to the handshake (the shape found by asking the runningboardd of the Mac with a program): the response with the handle of the calling process.  Anything else: no answer, and the connection ends (the client is told) */
			const char *sel = xpc_dictionary_get_string(m, "rbs_selector");

			if (streq(sel, "handshakeWithRequest:")) {
				long pid = xpc_connection_get_pid(peer);
				xpc_object_t rv = xpc_dictionary_create(0, 0, 0), h = xpc_dictionary_create(0, 0, 0), ch = xpc_dictionary_create(0, 0, 0), id = xpc_dictionary_create(0, 0, 0);
				xpc_object_t at = xpc_dictionary_create(0, 0, 0), bu = xpc_dictionary_create(0, 0, 0);

				xpc_dictionary_set_string(rv, "bsx_class", "RBSHandshakeResponse");
				xpc_dictionary_set_value(rv, "success-keys", xpc_array_create(0, 0));
				xpc_dictionary_set_value(rv, "success-values", xpc_array_create(0, 0));
				xpc_dictionary_set_value(rv, "fail-keys", xpc_array_create(0, 0));
				xpc_dictionary_set_value(rv, "fail-values", xpc_array_create(0, 0));
				xpc_dictionary_set_string(id, "bsx_class", "RBSOpaqueProcessIdentity");
				xpc_dictionary_set_int64(id, "_pid", pid);
				xpc_dictionary_set_int64(id, "_auid", 0);
				xpc_dictionary_set_string(id, "_description", "anon<lsd>(0)");
				xpc_dictionary_set_value(ch, "_identity", id);
				xpc_dictionary_set_uint64(ch, "_pid", pid);
				{
					unsigned char ar[278];
					unsigned long q;
					unsigned int w[8] = { 0, 0, 0, 0, 0, (unsigned int)pid, 100001, 1 };

					for (q = 0; q < 278; q++) ar[q] = rbs_archive[q];
					for (q = 0; q < 32; q++) ar[126 + q] = ((unsigned char *)w)[q];
					xpc_dictionary_set_value(ch, "bsx_archive", xpc_data_create(ar, 278));
				}
				xpc_dictionary_set_string(at, "bsx_index", "35770");
				xpc_dictionary_set_value(ch, "_bsAuditToken", at);
				xpc_dictionary_set_uint64(ch, "_data", 0);
				xpc_dictionary_set_string(bu, "bsx_class", "RBSProcessBundle");
				xpc_dictionary_set_value(ch, "_bundle", bu);
				xpc_dictionary_set_value(h, "_codedHandle", ch);
				xpc_dictionary_set_string(h, "bsx_class", "RBSProcessHandle");
				xpc_dictionary_set_value(rv, "_handle", h);
				xpc_dictionary_set_value(r, "return-value", rv);
				xpc_connection_send_message(peer, r);
				return;
			}
			if (sel[0] == 'v' && sel[1] == 'o' && sel[2] == 'i' && sel[3] == 'd' && sel[4] == '_') {	/* a call without a return value: an empty reply */
				xpc_connection_send_message(peer, r);
				return;
			}
			if (streq(sel, "acquireAssertionWithDescriptor:error:")) {	/* refused: the error of the Mac for a client without the entitlement */
				xpc_object_t er = xpc_dictionary_create(0, 0, 0);

				xpc_dictionary_set_value(r, "bsx_archive", xpc_data_create(rbs_error, 492));
				xpc_dictionary_set_string(er, "bsx_index", "36190");
				xpc_dictionary_set_value(r, "__RBSXPCError__", er);
				xpc_connection_send_message(peer, r);
				return;
			}
			xpc_release(r);					/* not sent: the destruction of the reply drops the right to reply, and the client is told that no answer will come */
			xpc_connection_cancel(peer);
			return;
		}
		/* the answers of the launchservicesd of the Mac (found by asking it with a program on the Mac): 100 check-in, 110 the info page (a shared memory of 15 pages), 200 the meta information
		 * (the session of the graphics server), 505 and 360 (an application that is not known): "success": false */
		if (cmd == 100) {
			xpc_dictionary_set_uint64(r, "serverversion", xpc_dictionary_get_uint64(m, "clientversion"));
			xpc_dictionary_set_bool(r, "success", 1);
			xpc_dictionary_set_int64(r, "session", 100001);
		} else if (cmd == 110) {
			void *pg = page_get();

			if (pg != 0) xpc_dictionary_set_value(r, "lsinfopage", xpc_shmem_create(pg, 61440));
			xpc_dictionary_set_bool(r, "success", 1);
		} else if (cmd == 200) {		/* the meta information of the session: only the keys in "wanted" (the Mac: {wanted: [LSSessionID]} -> {metainfo: {LSSessionID: 100001}}, the audit session of the
						 * client; HIServices' _RegisterApplication gives it to the window server in CreateApplication, which refuses a session id of 0); keys it does not know are left out */
			xpc_object_t meta = xpc_dictionary_create(0, 0, 0), wanted = xpc_dictionary_get_value(m, "wanted");
			unsigned long wi;

			for (wi = 0; wanted && wi < xpc_array_get_count(wanted); wi++) {
				const char *k = xpc_string_get_string_ptr(xpc_array_get_value(wanted, wi));

				if (k && streq(k, "LSSessionID")) xpc_dictionary_set_int64(meta, k, xpc_connection_get_asid(peer));
				else if (k && streq(k, "LSCGSSessionID")) xpc_dictionary_set_int64(meta, k, 257);
			}
			xpc_dictionary_set_value(r, "metainfo", meta);
			xpc_dictionary_set_bool(r, "success", 1);
		} else if (cmd == 300) {			/* the front application (_LSCopyFrontApplication when the shared page has none): the ASN as a number: 1 << 32 | the low part (100000 + pid, what the check-in below gives); the application that checked in last is in front */
			/* the Mac answers "success": false while nothing is in front (checked for the first program of a run): TransformProcessType then takes the path of an application that is not in front (no 320) and sends 450 */
			xpc_dictionary_set_bool(r, "success", 0);
		} else if (cmd == 360) {			/* the value of keys for an application (restrictto: the keys, none: the whole record); an application that did not check in: "success": false */
			xpc_object_t keys = xpc_dictionary_get_value(m, "restrictto");
			xpc_object_t rec = rec_by_asn(xpc_dictionary_get_uint64(m, "asn"));
			xpc_object_t res = xpc_dictionary_create(0, 0, 0);

			if (rec == 0) {
				xpc_dictionary_set_bool(r, "success", 0);
			} else {
				if (keys && xpc_array_get_count(keys) > 0) {
					unsigned long ki;

					for (ki = 0; ki < xpc_array_get_count(keys); ki++) {
						const char *k = xpc_string_get_string_ptr(xpc_array_get_value(keys, ki));
						xpc_object_t v = k ? xpc_dictionary_get_value(rec, k) : 0;

						if (v) xpc_dictionary_set_value(res, k, v);
					}
				} else
					res = xpc_copy(rec);
				xpc_dictionary_set_value(r, "result", res);
				xpc_dictionary_set_bool(r, "cacheable", keys == 0);
				xpc_dictionary_set_bool(r, "success", 1);
			}
		} else if (cmd == 510) {			/* set keys of the record of an application (TransformProcessType sends {"ApplicationType": "Foreground"}); the answer is the whole record */
			xpc_object_t data = xpc_dictionary_get_value(m, "info");
			xpc_object_t rec = rec_by_asn(xpc_dictionary_get_uint64(m, "asn"));

			if (rec != 0) {
				if (data != 0) xpc_dictionary_apply(data, ^_Bool(const char *k, xpc_object_t v) { xpc_dictionary_set_value(rec, k, v); return (1); });
				xpc_dictionary_set_int64(rec, "ChangeCount", xpc_dictionary_get_int64(rec, "ChangeCount") + 2);
				xpc_dictionary_set_value(r, "info", xpc_copy(rec));
			}
			xpc_dictionary_set_bool(r, "success", rec != 0);
		} else if (cmd == 450) {			/* the application sets keys of its own record (the new ChangeCount goes with them) */
			xpc_object_t data = xpc_dictionary_get_value(m, "data");
			xpc_object_t rec = rec_by_asn(xpc_dictionary_get_uint64(m, "asn"));

			if (rec != 0 && data != 0) {
				xpc_dictionary_apply(data, ^_Bool(const char *k, xpc_object_t v) { xpc_dictionary_set_value(rec, k, v); return (1); });
				xpc_dictionary_set_int64(rec, "ChangeCount", xpc_dictionary_get_int64(rec, "ChangeCount") + 2);
			}
			xpc_dictionary_set_bool(r, "success", rec != 0);
		} else if (cmd == 130) {			/* two ASNs: the same? */
			xpc_dictionary_set_bool(r, "success", xpc_dictionary_get_uint64(m, "asn1") == xpc_dictionary_get_uint64(m, "asn2"));
		} else if (cmd == 410) {			/* the registration of a listener for the notifications about applications: an id, the applications that it was asked for, the session */
			xpc_object_t add = xpc_dictionary_get_value(m, "addasn");
			xpc_object_t aff = xpc_array_create(0, 0);
			unsigned long i;

			for (i = 0; add && i < xpc_array_get_count(add); i++)
				xpc_array_append_value(aff, xpc_array_get_value(add, i));
			xpc_dictionary_set_uint64(r, "id", 1);
			xpc_dictionary_set_value(r, "affectedasns", aff);
			xpc_dictionary_set_bool(r, "success", 1);
			xpc_dictionary_set_int64(r, "session", 100001);
		} else if (cmd == 500) {
			front_pid = (unsigned long)xpc_connection_get_pid(peer);
			/* the check-in of an application: the answer is its own information plus what the daemon adds (found by asking the daemon of the Mac with a program) */
			xpc_object_t info = xpc_dictionary_get_value(m, "info");
			xpc_object_t res = info ? xpc_copy(info) : xpc_dictionary_create(0, 0, 0);
			char low[256];
			const char *s;
			unsigned char tok[32];
			static const unsigned char asnid[16] = { 0xc9, 0x61, 0x72, 0xa0, 0xce, 0x74, 0x4d, 0x62, 0xaf, 0xef, 0xec, 0xfe, 0x4d, 0x04, 0xd8, 0x27 };	/* the marker that makes LaunchServices read the dictionary as an application serial number (seen in the answer of the Mac) */
			unsigned long i;
			xpc_object_t asn = xpc_dictionary_create(0, 0, 0);

			for (i = 0; i < 32; i++) tok[i] = 0;
			tok[0] = 0xf5; tok[1] = 1; tok[8] = 0xf5; tok[9] = 1;
			xpc_dictionary_set_value(res, "LSAuditToken", xpc_data_create(tok, 32));
			if ((s = xpc_dictionary_get_string(res, "CFBundleName")) != 0) { lower(low, s); xpc_dictionary_set_value(res, "CFBundleNameLowerCase", xpc_string_create(low)); xpc_dictionary_set_value(res, "LSDisplayName", xpc_string_create(s)); }
			if ((s = xpc_dictionary_get_string(res, "CFBundleIdentifier")) != 0) { lower(low, s); xpc_dictionary_set_value(res, "BundleIdentifierLowerCase", xpc_string_create(low)); }
			if ((s = xpc_dictionary_get_string(res, "CFBundleExecutable")) != 0) {
				lower(low, s);
				xpc_dictionary_set_value(res, "CFBundleExecutablePath", xpc_string_create(s));
				xpc_dictionary_set_value(res, "LSExecutableFilenameLowerCaseKey", xpc_string_create(low));
			}
			xpc_dictionary_set_int64(res, "LSApplicationMemberCoalitionIDKey", 1);
			xpc_dictionary_set_int64(res, "pid", xpc_connection_get_pid(peer));
			xpc_dictionary_set_uint64(asn, "com.apple.coreservices.asn.lo", 100000 + xpc_connection_get_pid(peer));
			xpc_dictionary_set_uint64(asn, "com.apple.coreservices.asn.hi", 0);
			xpc_dictionary_set_value(asn, "com.apple.coreservices.asn", xpc_uuid_create(asnid));
			xpc_dictionary_set_value(res, "LSASN", asn);
			xpc_dictionary_set_bool(res, "LSApplicationHasRegistered", 1);
			xpc_dictionary_set_int64(res, "ChangeCount", 1);
			if (xpc_dictionary_get_value(res, "ApplicationType") == 0) xpc_dictionary_set_value(res, "ApplicationType", xpc_string_create("Foreground"));	/* an application whose information has none: the bundles of the lab programs */
			rec_store((unsigned long)xpc_connection_get_pid(peer), xpc_copy(res));
			page_add(100000 + xpc_connection_get_pid(peer), 1, xpc_connection_get_pid(peer));
			xpc_dictionary_set_value(r, "result", res);
			xpc_dictionary_set_bool(r, "success", 1);
		} else if (cmd == 505) {
			xpc_dictionary_set_bool(r, "success", 0);
		} else
			xpc_dictionary_set_bool(r, "success", 1);
		if (cmd == 500) { char *rd = xpc_copy_description(r); out("lsd-stub: 500 reply: "); out(rd); out("\n"); free(rd); }
		xpc_connection_send_message(peer, r);
	}
}

/* libinfo (getpwuid, getpwnam, getgrgid, getgrnam, getgrouplist: libsystem_info asks opendirectoryd through an xpc pipe, which has its own receive and reply calls; found in libsystem_info: __od_rpc_call, __extract_user): the reply is a dictionary with the fields of struct passwd / struct group (the names are the strings of libsystem_info: pw_name pw_passwd pw_uid pw_gid pw_change pw_expire pw_class pw_gecos pw_dir pw_shell, gr_name gr_passwd gr_gid gr_mem, groups, error).
 * The users and groups are those of this machine: /etc/passwd and /etc/group (the FreeBSD files: name:*:uid:gid:gecos:home:shell and name:*:gid:members), so that a user of the lab has his home directory (cfprefsd looks it up for the preferences of the user).  Without a file the only user is root. */
extern int open(const char *path, int flags, ...);
extern long read(int fd, void *buf, unsigned long n);
extern int close(int fd);
extern xpc_object_t xpc_array_create(const xpc_object_t *objects, unsigned long count);
extern void xpc_array_append_value(xpc_object_t a, xpc_object_t v);
extern xpc_object_t xpc_int64_create(long v);

static char od_buf[8192];
static int
od_load(const char *path)
{
	int fd = open(path, 0), n = 0;
	long r;

	if (fd < 0) return 0;
	while (n < (int)sizeof(od_buf) - 1 && (r = read(fd, od_buf + n, sizeof(od_buf) - 1 - n)) > 0) n += (int)r;
	close(fd);
	od_buf[n] = 0;
	return n;
}
/* the next line of od_buf from *pos: the fields (up to 8) are cut at ':' in place; returns the number of fields, 0 at the end */
static int
od_line(int *pos, char *f[8])
{
	int nf = 0;
	char *p = od_buf + *pos;

	while (*p == '\n' || *p == '\r') p++;
	if (*p == 0) { *pos = (int)(p - od_buf); return 0; }
	if (*p == '#') { while (*p && *p != '\n') p++; *pos = (int)(p - od_buf); return od_line(pos, f); }
	f[nf++] = p;
	while (*p && *p != '\n') { if (*p == ':' && nf < 8) { *p = 0; f[nf++] = p + 1; } p++; }
	if (*p == '\n') *p++ = 0;
	*pos = (int)(p - od_buf);
	return nf;
}
static long
od_num(const char *s)
{
	long v = 0; int neg = 0;

	if (*s == '-') { neg = 1; s++; }
	while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
	return neg ? -v : v;
}
/* the home directory that libinfo gets: the real one of /etc/passwd would make cfprefsd (the daemon) read and watch the preferences of the user, and it dies of a null call (`FSEventStreamCreate`, SIGSEGV at address 0, measured 2026-11-23: with /Users/alex the daemon is gone after the first client, with /var/empty it never touches the user's files).  Until FSEvents works the user's home is /var/empty (set LSD_OD_REAL_HOME=1 in the environment of lsd-stub to give the real one). */
extern char *getenv(const char *name);
static const char *
od_home(const char *real)
{
	return getenv("LSD_OD_REAL_HOME") != 0 ? real : "/var/empty";
}
static void
od_reply(xpc_object_t m, xpc_object_t r)
{
	const char *rn = xpc_dictionary_get_string(m, "rpc_name"), *nm = xpc_dictionary_get_string(m, "name");
	char *f[8];
	int pos = 0, nf;

	if (rn == 0) { xpc_dictionary_set_int64(r, "error", 1); return; }
	if (streq(rn, "getpwuid") || streq(rn, "getpwnam")) {
		long uid = streq(rn, "getpwuid") ? xpc_dictionary_get_int64(m, "uid") : 0;

		od_load("/etc/passwd");
		while ((nf = od_line(&pos, f)) > 0) {
			if (nf < 7) continue;
			if (streq(rn, "getpwuid") ? od_num(f[2]) == uid : (nm != 0 && streq(f[0], nm))) {
				xpc_dictionary_set_string(r, "pw_name", f[0]);
				xpc_dictionary_set_string(r, "pw_passwd", "*");
				xpc_dictionary_set_int64(r, "pw_uid", od_num(f[2]));
				xpc_dictionary_set_int64(r, "pw_gid", od_num(f[3]));
				xpc_dictionary_set_int64(r, "pw_change", 0);
				xpc_dictionary_set_int64(r, "pw_expire", 0);
				xpc_dictionary_set_string(r, "pw_class", "");
				xpc_dictionary_set_string(r, "pw_gecos", f[4]);
				xpc_dictionary_set_string(r, "pw_dir", od_home(f[5]));
				xpc_dictionary_set_string(r, "pw_shell", f[6]);
				return;
			}
		}
		xpc_dictionary_set_int64(r, "error", 1);
	} else if (streq(rn, "getgrgid") || streq(rn, "getgrnam")) {
		long gid = streq(rn, "getgrgid") ? xpc_dictionary_get_int64(m, "gid") : 0;

		od_load("/etc/group");
		while ((nf = od_line(&pos, f)) > 0) {
			if (nf < 3) continue;
			if (streq(rn, "getgrgid") ? od_num(f[2]) == gid : (nm != 0 && streq(f[0], nm))) {
				xpc_object_t mem = xpc_array_create(0, 0);

				xpc_dictionary_set_string(r, "gr_name", f[0]);
				xpc_dictionary_set_string(r, "gr_passwd", "*");
				xpc_dictionary_set_int64(r, "gr_gid", od_num(f[2]));
				if (nf > 3) {		/* the members, separated by commas */
					char *p = f[3];

					while (*p) {
						char *q = p;
						while (*q && *q != ',') q++;
						if (*q == ',') *q++ = 0;
						if (*p) xpc_array_append_value(mem, xpc_string_create(p));
						p = q;
					}
				}
				xpc_dictionary_set_value(r, "gr_mem", mem);
				return;
			}
		}
		xpc_dictionary_set_int64(r, "error", 1);
	} else if (streq(rn, "getgrouplist") && nm != 0) {		/* the groups of a user: his primary group and every group that lists him */
		xpc_object_t gl = xpc_array_create(0, 0);
		long pg = -1;
		int n = 0;

		od_load("/etc/passwd");
		while ((nf = od_line(&pos, f)) > 0) if (nf >= 7 && streq(f[0], nm)) { pg = od_num(f[3]); break; }
		if (pg >= 0) { xpc_array_append_value(gl, xpc_int64_create(pg)); n++; }
		od_load("/etc/group"); pos = 0;
		while ((nf = od_line(&pos, f)) > 0) {
			if (nf < 4 || od_num(f[2]) == pg) continue;
			{
				char *p = f[3];

				while (*p) {
					char *q = p;
					while (*q && *q != ',') q++;
					if (*q == ',') *q++ = 0;
					if (streq(p, nm)) { xpc_array_append_value(gl, xpc_int64_create(od_num(f[2]))); n++; break; }
					p = q;
				}
			}
		}
		xpc_dictionary_set_value(r, "groups", gl);
	} else
		xpc_dictionary_set_int64(r, "error", 1);
}

/* libinfo (getpwuid, getpwnam) asks opendirectoryd over an xpc pipe, which has its own receive and reply calls: the first message of every client is the "w00t" of libxpc (xpc_pipe_receive refuses it with 5: skipped),
 * the request follows */
static void
od_serve(const char *name)
{
	unsigned int port = 0;
	int kr = bootstrap_check_in(bootstrap_port, name, &port);

	out("lsd-stub: pipe service "); out(name); out(kr == 0 ? " ready\n" : " check-in failed\n");
	if (kr != 0) return;
	for (;;) {
		xpc_object_t m = 0, r;
		int rc = xpc_pipe_receive(port, &m, 0);

		if (rc != 0 || m == 0) { if (rc != 5) { out("lsd-stub: pipe receive rc "); out(rc == 0x23 ? "35" : "other"); out("\n"); } continue; }
		{ char *dd = xpc_copy_description(m); out("lsd-stub: pipe request: "); out(dd); out("\n"); free(dd); }
		if ((r = xpc_dictionary_create_reply(m)) == 0) { out("lsd-stub: libinfo: no reply\n"); continue; }
		od_reply(m, r);
		{ int pr = xpc_pipe_routine_reply(r); out(pr == 0 ? "lsd-stub: pipe reply sent\n" : "lsd-stub: pipe reply FAILED\n"); }
	}
}

int
main(int argc, char **argv)
{
	dq q = dispatch_queue_create("lsd-stub", 0);
	int i;

	/* one listener for each name on the command line (several services in one process: every process of the Mac costs memory here); no name: the LaunchServices daemon */
	for (i = 1; i < (argc > 1 ? argc : 2); i++) {
		xpc_object_t svc = xpc_connection_create_mach_service(argc > 1 ? argv[i] : "com.apple.coreservices.launchservicesd", q, 1);
		const char *nm = argc > 1 ? argv[i] : "";
		int nsx = streq(nm, "com.apple.fonts") || streq(nm, "com.apple.carboncore.csnameddata") || streq(nm, "com.apple.lsd.mapdb") || streq(nm, "com.apple.analyticsd") || streq(nm, "com.apple.bird") || streq(nm, "com.apple.bird.token") || streq(nm, "com.apple.metadata.mds") || streq(nm, "com.apple.quicklook.ThumbnailsAgent") || streq(nm, "com.apple.quicklook.QuickLookUIService") || streq(nm, "com.apple.widget-observation");	/* the services whose clients wait for an NSXPC answer */

		xpc_connection_set_event_handler(svc, ^(xpc_object_t peer) {
			out("lsd-stub: a connection\n");
			xpc_connection_set_event_handler(peer, ^(xpc_object_t m) { handle(peer, m, nsx, nm); });
			xpc_connection_resume(peer);
		});
		xpc_connection_resume(svc);
	}
	out("lsd-stub: serving\n");
	dispatch_main();
	return (0);
}
