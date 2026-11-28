/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Alexandru Balan
 *
 * cfprefsd for pearOS: the preferences server, wire compatible with the cfprefsd of macOS 26.7.2 (CoreFoundation-5026.6.7.5).
 * Its clients are CoreFoundation's CFPreferences and Foundation's NSUserDefaults of every Mac program; they talk to two services:
 *   com.apple.cfprefsd.agent    one per user: the user's domains (~/Library/Preferences, ByHost, containers)
 *   com.apple.cfprefsd.daemon   the system: kCFPreferencesAnyUser (/Library/Preferences), root and role accounts, other users
 *                               when root asks, managed preferences (/Library/Managed Preferences)
 * Every message is an XPC dictionary with "CFPreferencesOperation" (int64):
 *   1 read a source (reply: CFPreferencesPropertyList, a binary plist) or write it (Key + Value, or MultiKeySet + a dictionary)
 *   2 an agent introduces itself to the daemon (CFPreferencesUser, CFPreferencesEndpoint), no reply
 *   3 flush managed sources (the daemon forwards it to every agent)       4 flush a domain: re-read it from disk
 *   5 multi-message (CFPreferencesMessages: an array, the reply has the same positions)
 *   6 a user was deleted (root only)        7 / 8 start / stop observing a source        9 the path of a source
 *   999 write every changed source to disk now, no reply
 * Every reply of 1/7/8 carries CFPreferencesShmemName / ShmemIndex / ShmemState: the clients map a POSIX shared memory page of
 * 4096 uint32 counters ("apple.cfprefs.<euid>v1" for the agent, "apple.cfprefs.daemonv1" for the daemon), one slot per source,
 * bumped at every change; a client reads a source again when its slot moved.  A change is also pushed to the other observers
 * of the source as {CFPreferencesDomain, CFPreferencesUser, CFPreferencesIsByHost, CFPreferencesPID} (one in flight per
 * client).  Values stay in memory; the files (binary plists, written to a temporary file and renamed) are written by a 10 s
 * timer, at once when the client asks (CFPreferencesShouldWriteSynchronously), when the file is new, at op 999 and at SIGTERM.
 * The protocol and the evidence (decompiled CoreFoundation, traffic recorded on the Mac): research/docs/CFPREFSD-WIRE.md.
 *
 * cfprefsd agent | daemon [-v]     the role (Apple's takes it from the launchd session; the job files pass the same word)
 * Environment: __CFPreferencesTestDaemon=1 serves com.apple.cfprefsd.{agent,daemon}.test with the shared memory
 * "test.cfprefs..." (Apple's test mode; Apple's clients use the same names when they have the variable).
 */
#include "darwin.h"

#define MAXPATH 1024
#define SHMEM_SIZE 0x4000
#define SHMEM_SLOTS 4096

enum { ROLE_AGENT = 1, ROLE_DAEMON = 2 };
static int role, verbose, testmode;
static dispatch_queue_t q;
static xpc_connection_t listener;
static volatile uint32_t *shmem;
static char shmem_name[64];
static char my_user[256];
static uid_t my_euid;
static char hostid[64];

/* ------------------------------------------------------------------ small helpers */
static void
logf_(const char *fmt, const char *a, const char *b)
{
	if (verbose) dprintf(2, fmt, a ? a : "", b ? b : "");
}

static int
streq(const char *a, const char *b)
{
	return a && b && strcmp(a, b) == 0;
}

static CFStringRef
cfstr(const char *s)
{
	return CFStringCreateWithCString(NULL, s, kCFStringEncodingUTF8);
}

static char *
cstr(CFStringRef s)		/* malloc'd UTF-8 copy */
{
	CFIndex n = CFStringGetMaximumSizeForEncoding(CFStringGetLength(s), kCFStringEncodingUTF8) + 1;
	char *b = malloc(n);
	if (b && !CFStringGetCString(s, b, n, kCFStringEncodingUTF8)) b[0] = 0;
	return b;
}

static int
ends_with_ci(const char *s, const char *suf)
{
	size_t a = strlen(s), b = strlen(suf);
	return a >= b && strcasecmp(s + a - b, suf) == 0;
}

static int
is_dir(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

static void
dirname_of(const char *path, char *out)
{
	size_t n = strlen(path);
	while (n > 0 && path[n - 1] != '/') n--;
	if (n > 1) n--;			/* drop the slash, keep "/" for the root */
	memcpy(out, path, n);
	out[n] = 0;
}

/* ------------------------------------------------------------------ shared memory (CFPrefsDaemon _initializeShmemPage:) */
static void
shmem_init(void)
{
	if (role == ROLE_AGENT) snprintf(shmem_name, sizeof shmem_name, "%scfprefs.%dv%d", testmode ? "test." : "apple.", (int)my_euid, 1);
	else snprintf(shmem_name, sizeof shmem_name, "%scfprefs.daemonv%d", testmode ? "test." : "apple.", 1);
	int fd = shm_open(shmem_name, O_RDWR | O_CREAT, 0644);
	void *p = MAP_FAILED;
	if (fd >= 0) {
		ftruncate(fd, SHMEM_SIZE);
		p = mmap(NULL, SHMEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		close(fd);
	}
	if (p == MAP_FAILED) {
		dprintf(2, "cfprefsd: shm_open %s failed (errno %d): cross-process preferences will not work correctly\n", shmem_name, errno);
		p = calloc(1, SHMEM_SIZE);
	}
	shmem = p;
}

static void
shmem_bump(int idx)		/* updateShmemIndex: atomic add, 0 is never used again */
{
	if (idx <= 0 || idx >= SHMEM_SLOTS) return;
	uint32_t v = __atomic_add_fetch(&shmem[idx], 1, __ATOMIC_SEQ_CST);
	if (v == 0) __atomic_store_n(&shmem[idx], 1, __ATOMIC_SEQ_CST);
}

static int
domain_base(const char *domain)	/* the first of the seven slots of a domain */
{
	if (!domain) return 0;
	CFStringRef s = cfstr(domain);
	if (!s) return 1;
	long len = CFStringGetLength(s);
	unsigned long base = (CFHash(s) % 585) * 7 + (len % 7);
	CFRelease(s);
	if (base > 0xff8 || base == 0) base = (len % 7) + 1;
	return (int)base;
}

/* ------------------------------------------------------------------ clients and sources */
struct source;
struct client {
	xpc_connection_t conn;
	pid_t pid;
	uid_t euid;
	gid_t egid;
	int inflight;			/* a change notification waits for its reply */
	xpc_object_t pending;		/* notifications held while one is in flight (an xpc array) */
	struct source **observed;
	int nobserved;
	int dead;
};

struct source {
	struct source *next;
	char *path;			/* the uncanonicalized path: the key of the cache */
	char *domain;			/* fixed-up domain ("kCFPreferencesAnyApplication" for the global domain) */
	char *user;			/* user name or "kCFPreferencesAnyUser" */
	char *container;
	int byhost, managed, anyuser;
	int shmidx;
	CFMutableDictionaryRef plist;	/* NULL until loaded */
	int loaded, on_disk, dirty;
	long disk_size;
	uid_t owner_uid;
	gid_t owner_gid;
	int have_owner, restricted;
	struct client **observers;
	int nobservers;
	struct source *mirror;		/* the current user's global domain mirrors AppleLanguages and AppleLocale (CFPDMirroredSource) */
};

#define NBUCKET 1024
static struct source *buckets[NBUCKET];
static dispatch_source_t timer;
static int timer_running;

static unsigned
hash_path(const char *s)
{
	unsigned h = 2166136261u;
	while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
	return h % NBUCKET;
}

static struct source *
source_find(const char *path)
{
	for (struct source *s = buckets[hash_path(path)]; s; s = s->next) if (streq(s->path, path)) return s;
	return NULL;
}

static void
list_add(void ***arr, int *n, void *p)
{
	for (int i = 0; i < *n; i++) if ((*arr)[i] == p) return;
	*arr = realloc(*arr, sizeof(void *) * (*n + 1));
	(*arr)[(*n)++] = p;
}

static void
list_remove(void ***arr, int *n, void *p)
{
	for (int i = 0; i < *n; i++) if ((*arr)[i] == p) { (*arr)[i] = (*arr)[--*n]; return; }
}

/* ------------------------------------------------------------------ paths (+[CFPrefsDaemon _getUncanonicalizedSourcePath:...]) */
static void
hostid_init(void)
{
	unsigned char u[16];
	struct timespec w = { 30, 0 };
	if (gethostuuid(u, &w) != 0) { snprintf(hostid, sizeof hostid, "00000000-0000-0000-0000-000000000000"); return; }
	snprintf(hostid, sizeof hostid, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
	    u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
	if (strncmp(hostid, "00000000-0000-1000-8000-", 24) == 0) {	/* legacy form: the MAC address, lower case */
		char t[16];
		int i;
		for (i = 0; i < 12; i++) { char c = hostid[24 + i]; t[i] = (c >= 'A' && c <= 'Z') ? c + 32 : c; }
		t[12] = 0;
		memcpy(hostid, t, 13);
	}
}

/* fills path; 0 = no such source ("Domain or user not found") */
static int
source_path(const char *domain, const char *user, int byhost, int managed, const char *container, char *path)
{
	char name[512], dir[MAXPATH];
	int anyuser = streq(user, "kCFPreferencesAnyUser");

	snprintf(name, sizeof name, "%s", streq(domain, "kCFPreferencesAnyApplication") ? ".GlobalPreferences" : domain);
	if (ends_with_ci(name, ".plist")) name[strlen(name) - 6] = 0;
	if (managed) {
		if (anyuser) snprintf(path, MAXPATH, "/Library/Managed Preferences/%s.plist", name);
		else snprintf(path, MAXPATH, "/Library/Managed Preferences/%s/%s.plist", user, name);
		return 1;
	}
	if (anyuser) {
		if (my_euid != 0) return 0;	/* only a root cfprefsd serves kCFPreferencesAnyUser */
		snprintf(dir, sizeof dir, "/Library/Preferences/");
	} else if (container && *container) {
		snprintf(dir, sizeof dir, "%s/Library/Preferences/%s", container, byhost ? "ByHost/" : "");
	} else {
		struct passwd *pw = getpwnam(user);
		const char *home = pw && pw->pw_dir ? pw->pw_dir : "/var/empty";
		snprintf(dir, sizeof dir, "%s%sLibrary/Preferences/%s", home, ends_with_ci(home, "/") ? "" : "/", byhost ? "ByHost/" : "");
	}
	if (byhost && !anyuser) snprintf(path, MAXPATH, "%s%s.%s.plist", dir, name, hostid);
	else snprintf(path, MAXPATH, "%s%s.plist", dir, name);
	if (strncmp(path, "/private/var/empty", 18) == 0 || strncmp(path, "/var/empty", 10) == 0) return 0;
	if (strncmp(path, "/private/", 9) == 0) { size_t i, n = strlen(path); for (i = 0; i + 8 <= n; i++) path[i] = path[i + 8]; }
	return 1;
}

/* ------------------------------------------------------------------ files */
static CFDataRef
read_file(const char *path, struct stat *st)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) return NULL;
	if (fstat(fd, st) != 0 || (st->st_mode & S_IFMT) == S_IFDIR) { close(fd); return NULL; }
	size_t n = (size_t)st->st_size, got = 0;
	uint8_t *b = malloc(n ? n : 1);
	while (b && got < n) {
		ssize_t r = read(fd, b + got, n - got);
		if (r < 0 && errno == EINTR) continue;
		if (r <= 0) break;
		got += (size_t)r;
	}
	close(fd);
	CFDataRef d = b ? CFDataCreate(NULL, b, (CFIndex)got) : NULL;
	free(b);
	return d;
}

static void
source_load(struct source *s)
{
	struct stat st;
	if (s->loaded) return;
	s->loaded = 1;
	s->on_disk = 0;
	s->plist = NULL;
	CFDataRef d = read_file(s->path, &st);
	if (!d) return;
	s->on_disk = 1;
	s->disk_size = (long)st.st_size;
	if (!s->have_owner) { s->owner_uid = st.st_uid; s->owner_gid = st.st_gid; }
	if (CFDataGetLength(d) > 0) {
		CFTypeRef pl = CFPropertyListCreateWithData(NULL, d, kCFPropertyListMutableContainersAndLeaves, NULL, NULL);
		if (pl && CFGetTypeID(pl) == CFDictionaryGetTypeID()) s->plist = (CFMutableDictionaryRef)pl;
		else if (pl) CFRelease(pl);
	}
	CFRelease(d);
}

static void
source_unload(struct source *s)	/* respondToFileWrittenToBehindOurBack: the next read goes to the disk */
{
	if (s->plist) CFRelease(s->plist);
	s->plist = NULL;
	s->loaded = 0;
	s->dirty = 0;
}

static CFDataRef
source_data(struct source *s)	/* the binary plist of the current contents (an empty dictionary when there is none) */
{
	CFDataRef d;
	if (s->plist) return CFPropertyListCreateData(NULL, s->plist, kCFPropertyListBinaryFormat_v1_0, 0, NULL);
	CFMutableDictionaryRef e = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	d = CFPropertyListCreateData(NULL, e, kCFPropertyListBinaryFormat_v1_0, 0, NULL);
	CFRelease(e);
	return d;
}

/* _CFPrefsTemporaryFDToWriteTo + _CFPrefsWritePlistToFDThenClose: temporary file next to the plist, owner, mode, fsync, rename */
static int
source_write(struct source *s)
{
	char dir[MAXPATH], tmp[MAXPATH + 16];
	CFDataRef d = source_data(s);
	if (!d) return -1;
	const uint8_t *p = CFDataGetBytePtr(d);
	size_t n = (size_t)CFDataGetLength(d), off = 0;
	mode_t mode = (s->anyuser && !s->restricted) ? 0644 : 0600;

	dirname_of(s->path, dir);
	if (!is_dir(dir) && streq(dir, "/Library/Preferences")) mkdir(dir, 0755);
	snprintf(tmp, sizeof tmp, "%s.XXXXXXX", s->path);
	int fd = mkstemp(tmp);
	if (fd < 0) { CFRelease(d); logf_("cfprefsd: cannot create a temporary file for %s%s\n", s->path, ""); return -1; }
	if (s->have_owner && my_euid == 0) fchown(fd, s->owner_uid, s->owner_gid);
	fchmod(fd, mode);
	while (off < n) {
		ssize_t w = write(fd, p + off, n - off);
		if (w < 0 && (errno == EINTR || errno == EAGAIN)) continue;
		if (w <= 0) break;
		off += (size_t)w;
	}
	int ok = off == n;
	int err = errno;
	while (ok && fsync(fd) != 0 && errno == EINTR) ;
	close(fd);
	CFRelease(d);
	if (!ok || rename(tmp, s->path) != 0) {
		unlink(tmp);
		if (!ok && err == ENOSPC) return -1;	/* stays dirty (handleWritingResult:) */
		logf_("cfprefsd: writing %s failed%s\n", s->path, "");
		return -1;
	}
	s->on_disk = 1;
	s->disk_size = (long)n;
	s->dirty = 0;
	if (s->mirror && s->mirror->dirty) source_write(s->mirror);
	return 0;
}

static void sync_all(void);

static void
mark_dirty(struct source *s)	/* setDirty:YES: the slot moves at once, the file follows with the timer */
{
	s->dirty = 1;
	shmem_bump(s->shmidx);
	if (!timer_running) { timer_running = 1; dispatch_resume(timer); }
}

static void
sync_all(void)
{
	for (int b = 0; b < NBUCKET; b++)
		for (struct source *s = buckets[b]; s; s = s->next)
			if (s->dirty) source_write(s);
}

/* ------------------------------------------------------------------ values: XPC -> CF (_CFPrefsApplyChangesQueueToDictionary) */
static CFTypeRef
cf_from_xpc(xpc_object_t v)
{
	xpc_type_t t = xpc_get_type(v);
	if (t == &_xpc_type_string) return cfstr(xpc_string_get_string_ptr(v));
	if (t == &_xpc_type_bool) return CFRetain(xpc_bool_get_value(v) ? kCFBooleanTrue : kCFBooleanFalse);
	if (t == &_xpc_type_data) {		/* everything else comes as a binary property list */
		CFDataRef d = CFDataCreate(NULL, xpc_data_get_bytes_ptr(v), (CFIndex)xpc_data_get_length(v));
		CFTypeRef pl = d ? CFPropertyListCreateWithData(NULL, d, kCFPropertyListMutableContainersAndLeaves, NULL, NULL) : NULL;
		if (d) CFRelease(d);
		return pl;
	}
	if (t == &_xpc_type_int64) { long x = xpc_int64_get_value(v); return CFNumberCreate(NULL, kCFNumberSInt64Type, &x); }
	if (t == &_xpc_type_uint64) { long x = (long)xpc_uint64_get_value(v); return CFNumberCreate(NULL, kCFNumberSInt64Type, &x); }
	if (t == &_xpc_type_double) { double x = xpc_double_get_value(v); return CFNumberCreate(NULL, kCFNumberDoubleType, &x); }
	if (t == &_xpc_type_date) return CFDateCreate(NULL, (double)xpc_date_get_value(v) / 1e9 - 978307200.0);
	if (t == &_xpc_type_array) {
		CFMutableArrayRef a = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
		for (size_t i = 0; i < xpc_array_get_count(v); i++) {
			CFTypeRef e = cf_from_xpc(xpc_array_get_value(v, i));
			if (e) { CFArrayAppendValue(a, e); CFRelease(e); }
		}
		return a;
	}
	if (t == &_xpc_type_dictionary) {
		CFMutableDictionaryRef d = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		xpc_dictionary_apply(v, ^_Bool(const char *k, xpc_object_t e) {
			CFTypeRef ce = cf_from_xpc(e);
			CFStringRef ck = cfstr(k);
			if (ce && ck) CFDictionarySetValue(d, ck, ce);
			if (ce) CFRelease(ce);
			if (ck) CFRelease(ck);
			return 1;
		});
		return d;
	}
	return NULL;
}

/* ------------------------------------------------------------------ replies */
static xpc_object_t top_msg;		/* the message being handled for the peer, and whether its reply was made */
static int top_replied;

static xpc_object_t
make_reply(xpc_object_t msg)		/* the reply takes the message's send-once right: never make two for one message */
{
	xpc_object_t r = msg == top_msg ? xpc_dictionary_create_reply(msg) : NULL;
	if (r) top_replied = 1;
	return r ? r : xpc_dictionary_create(NULL, NULL, 0);
}

static xpc_object_t
error_reply(xpc_object_t msg, const char *desc, int type, int clientfault)	/* _populateErrorReply */
{
	xpc_object_t r = make_reply(msg);
	xpc_dictionary_set_string(r, "CFPreferencesErrorDescription", desc);
	xpc_dictionary_set_int64(r, "CFPreferencesErrorType", type);
	if (clientfault) xpc_dictionary_set_bool(r, "CFPreferencesErrorClientFault", 1);
	return r;
}

static xpc_object_t
new_reply(xpc_object_t msg)
{
	return make_reply(msg);
}

static void
add_shmem_fields(xpc_object_t r, struct source *s)
{
	xpc_dictionary_set_int64(r, "CFPreferencesShmemIndex", s->shmidx);
	xpc_dictionary_set_string(r, "CFPreferencesShmemName", shmem_name);
	xpc_dictionary_set_uint64(r, "CFPreferencesShmemState", shmem[s->shmidx]);
}

/* ------------------------------------------------------------------ change notifications (asyncNotifyObserversOfWriteFromConnection:message:) */
static void client_send_next(struct client *c);

static void
client_notify(struct client *c, xpc_object_t note)
{
	if (c->dead) return;
	if (c->inflight) {			/* one in flight per client: hold this one (a newer one for the same source replaces it) */
		if (!c->pending) c->pending = xpc_array_create(NULL, 0);
		xpc_object_t keep = xpc_array_create(NULL, 0);
		for (size_t i = 0; i < xpc_array_get_count(c->pending); i++) {
			xpc_object_t o = xpc_array_get_value(c->pending, i);
			if (!(streq(xpc_dictionary_get_string(o, "CFPreferencesDomain"), xpc_dictionary_get_string(note, "CFPreferencesDomain")) &&
			    streq(xpc_dictionary_get_string(o, "CFPreferencesUser"), xpc_dictionary_get_string(note, "CFPreferencesUser")) &&
			    xpc_dictionary_get_bool(o, "CFPreferencesIsByHost") == xpc_dictionary_get_bool(note, "CFPreferencesIsByHost")))
				xpc_array_append_value(keep, o);
		}
		xpc_array_append_value(keep, note);
		xpc_release(c->pending);
		c->pending = keep;
		return;
	}
	c->inflight = 1;
	if (verbose) dprintf(2, "cfprefsd: notify pid %d of %s\n", c->pid, xpc_dictionary_get_string(note, "CFPreferencesDomain"));
	xpc_retain(c->conn);
	xpc_connection_send_message_with_reply(c->conn, note, q, ^(xpc_object_t reply) {
		if (verbose) { char *d = xpc_copy_description(reply); dprintf(2, "cfprefsd: notification reply from pid %d: %s\n", c->pid, d); free(d); }
		c->inflight = 0;
		client_send_next(c);
		xpc_release(c->conn);
	});
}

static void
client_send_next(struct client *c)
{
	if (!c->pending || xpc_array_get_count(c->pending) == 0 || c->dead) return;
	xpc_object_t first = xpc_retain(xpc_array_get_value(c->pending, 0));
	xpc_object_t rest = xpc_array_create(NULL, 0);
	for (size_t i = 1; i < xpc_array_get_count(c->pending); i++) xpc_array_append_value(rest, xpc_array_get_value(c->pending, i));
	xpc_release(c->pending);
	c->pending = rest;
	client_notify(c, first);
	xpc_release(first);
}

static void
notify_observers(struct source *s, struct client *writer)
{
	if (s->nobservers == 0) return;
	xpc_object_t note = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_string(note, "CFPreferencesDomain", s->domain);
	xpc_dictionary_set_string(note, "CFPreferencesUser", s->user);
	xpc_dictionary_set_bool(note, "CFPreferencesIsByHost", s->byhost);
	if (s->container) xpc_dictionary_set_string(note, "CFPreferencesContainer", s->container);
	xpc_dictionary_set_int64(note, "CFPreferencesPID", writer ? writer->pid : 0);
	for (int i = 0; i < s->nobservers; i++) if (s->observers[i] != writer) client_notify(s->observers[i], note);
	xpc_release(note);
}

/* ------------------------------------------------------------------ agents (daemon side) and the daemon (agent side) */
struct agent { char *user; xpc_connection_t conn; };
static struct agent *agentv;
static int nagent;
static const char *svc_agent, *svc_daemon;

static void
send_op_async(xpc_connection_t c, int64_t op, const char *domain)
{
	xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_int64(m, "CFPreferencesOperation", op);
	if (domain) xpc_dictionary_set_string(m, "CFPreferencesDomain", domain);
	xpc_connection_send_message_with_reply(c, m, q, ^(xpc_object_t r) { (void)r; });
	xpc_release(m);
}

static void
agent_forget(const char *user)
{
	for (int i = 0; i < nagent; i++)
		if (streq(agentv[i].user, user)) {
			xpc_connection_cancel(agentv[i].conn);
			xpc_release(agentv[i].conn);
			free(agentv[i].user);
			agentv[i] = agentv[--nagent];
			return;
		}
}

static void
flush_in_agent(const char *user, const char *domain)	/* flushDomain:inAgentForUser: */
{
	for (int i = 0; i < nagent; i++) if (streq(agentv[i].user, user)) send_op_async(agentv[i].conn, 4, domain);
}

static void
handle_checkin(xpc_object_t msg)		/* op 2 (handleAgentCheckInMessage:) */
{
	const char *user = xpc_dictionary_get_string(msg, "CFPreferencesUser");
	xpc_object_t ep = xpc_dictionary_get_value(msg, "CFPreferencesEndpoint");
	if (role != ROLE_DAEMON) { logf_("cfprefsd: agent got a check-in%s%s\n", "", ""); return; }
	if (!user || !ep || xpc_get_type(ep) != &_xpc_type_endpoint) return;
	agent_forget(user);
	xpc_connection_t c = xpc_connection_create_from_endpoint(ep);
	char *u = strdup(user);
	xpc_connection_set_target_queue(c, q);
	xpc_connection_set_event_handler(c, ^(xpc_object_t e) {
		if (xpc_get_type(e) == &_xpc_type_error) {
			for (int i = 0; i < nagent; i++) if (agentv[i].conn == c) { free(agentv[i].user); xpc_release(agentv[i].conn); agentv[i] = agentv[--nagent]; break; }
		}
	});
	xpc_connection_resume(c);
	agentv = realloc(agentv, sizeof *agentv * (nagent + 1));
	agentv[nagent].user = u;
	agentv[nagent].conn = c;
	nagent++;
	logf_("cfprefsd: agent of %s checked in%s\n", user, "");
}

static void
checkin(void)					/* the agent introduces itself to the daemon (-[CFPrefsDaemon checkIn]) */
{
	xpc_connection_t d = xpc_connection_create_mach_service(svc_daemon, q, testmode ? 0 : XPC_CONNECTION_MACH_SERVICE_PRIVILEGED);
	xpc_connection_set_event_handler(d, ^(xpc_object_t e) { (void)e; });
	xpc_connection_resume(d);
	xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_int64(m, "CFPreferencesOperation", 2);
	xpc_dictionary_set_string(m, "CFPreferencesUser", my_user[0] ? my_user : "InvalidUserName");
	xpc_object_t ep = xpc_endpoint_create(listener);
	xpc_dictionary_set_value(m, "CFPreferencesEndpoint", ep);
	xpc_connection_send_message(d, m);
	xpc_release(ep);
	xpc_release(m);
	static xpc_connection_t last;
	if (last) { xpc_connection_cancel(last); xpc_release(last); }
	last = d;
}

/* ------------------------------------------------------------------ source messages: ops 1, 7, 8, 9 */
static const char *
fixed_domain(const char *d)			/* _CFPrefsGetFixedUpDomainForMessage */
{
	if (!d) return NULL;
	if (streq(d, "Apple Global Domain") || streq(d, ".GlobalPreferences") || streq(d, "kCFPreferencesAnyApplication")) return "kCFPreferencesAnyApplication";
	return d;
}

static struct source *
source_get(const char *domain, const char *user, int byhost, int managed, const char *container)
{
	char path[MAXPATH];
	if (!source_path(domain, user, byhost, managed, container, path)) return NULL;
	struct source *s = source_find(path);
	if (s) return s;
	s = calloc(1, sizeof *s);
	s->path = strdup(path);
	s->domain = strdup(domain);
	s->user = strdup(user);
	s->container = container ? strdup(container) : NULL;
	s->byhost = byhost;
	s->managed = managed;
	s->anyuser = streq(user, "kCFPreferencesAnyUser");
	int variant = container ? 6 : managed ? (s->anyuser ? 4 : 3) : s->anyuser ? 2 : byhost ? 0 : 1;
	s->shmidx = (int16_t)(domain_base(domain) + variant);
	if (streq(domain, "kCFPreferencesAnyApplication") && !s->anyuser && !byhost && !managed && !container) {
		struct source *m = calloc(1, sizeof *m);	/* CFPDMirroredSource: <dir>/.GlobalPreferences_m.plist */
		char mp[MAXPATH + 8];
		dirname_of(path, mp);
		snprintf(mp + strlen(mp), sizeof mp - strlen(mp), "/.GlobalPreferences_m.plist");
		m->path = strdup(mp);
		m->domain = strdup(domain);
		m->user = strdup(user);
		m->shmidx = s->shmidx;
		s->mirror = m;
	}
	unsigned h = hash_path(path);
	s->next = buckets[h];
	buckets[h] = s;
	return s;
}

/* validatePOSIXPermissionsForMessage:accessType:...: 0 allowed, 4 an access token is needed */
static int
check_posix(struct source *s, struct client *c, xpc_object_t msg, int write)
{
	struct stat st;
	if (c->euid == 0 || c->euid == my_euid) return 0;
	int have = stat(s->path, &st) == 0;
	if (!write && have && ((st.st_mode & S_IROTH) || (c->euid == st.st_uid && (st.st_mode & S_IRUSR)))) return 0;
	if (!write && !have) return 0;
	int fd = xpc_dictionary_dup_fd(msg, "CFPreferencesAccessToken");
	if (fd >= 0) {				/* the token: an open file of the same plist, opened for the access asked */
		struct stat ft;
		int fl = fcntl(fd, F_GETFL), ok = 0;
		if (fstat(fd, &ft) == 0 && have && ft.st_dev == st.st_dev && ft.st_ino == st.st_ino) {
			int acc = fl & O_ACCMODE;
			ok = write ? (acc == O_WRONLY || acc == O_RDWR) : (acc == O_RDONLY || acc == O_RDWR);
		}
		close(fd);
		if (ok) return 0;
		return 2;
	}
	return 4;
}

static long
value_size(xpc_object_t v)
{
	xpc_type_t t = xpc_get_type(v);
	if (t == &_xpc_type_data) return (long)xpc_data_get_length(v);
	if (t == &_xpc_type_string) return (long)strlen(xpc_string_get_string_ptr(v));
	if (t == &_xpc_type_dictionary) { __block long n = 0; xpc_dictionary_apply(v, ^_Bool(const char *k, xpc_object_t e) { n += (long)strlen(k) + value_size(e); return 1; }); return n; }
	return 8;
}

static void
mirror_sign(struct source *m, const char *key, pid_t pid)	/* enqueueMirrorSignatureForKey:fromMessage: "Sig_<key>" = "<process>(<pid>): <local time> (<zone>)" */
{
	char name[256] = "", when[64] = "", sig[400], k[300];
	time_t t = time(NULL);
	struct tm tm;
	if (proc_name(pid, name, sizeof name) <= 0) snprintf(name, sizeof name, "?");
	localtime_r(&t, &tm);
	strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S (%Z)", &tm);
	snprintf(sig, sizeof sig, "%s(%d): %s", name, (int)pid, when);
	snprintf(k, sizeof k, "Sig_%s", key);
	CFStringRef ck = cfstr(k), cv = cfstr(sig);
	if (ck && cv) CFDictionarySetValue(m->plist, ck, cv);
	if (ck) CFRelease(ck);
	if (cv) CFRelease(cv);
}

static void
set_key(struct source *s, CFStringRef k, xpc_object_t v, pid_t pid)
{
	if (!s->plist) s->plist = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	if (xpc_get_type(v) == &_xpc_type_null) CFDictionaryRemoveValue(s->plist, k);
	else {
		CFTypeRef cv = cf_from_xpc(v);
		if (!cv) return;
		CFDictionarySetValue(s->plist, k, cv);
		CFRelease(cv);
	}
	if (s->mirror) {			/* the mirrored keys of the global domain */
		char *kc = cstr(k);
		if (streq(kc, "AppleLanguages") || streq(kc, "AppleLocale")) {
			source_load(s->mirror);
			if (!s->mirror->plist) s->mirror->plist = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
			if (xpc_get_type(v) == &_xpc_type_null) CFDictionaryRemoveValue(s->mirror->plist, k);
			else { CFTypeRef cv = cf_from_xpc(v); if (cv) { CFDictionarySetValue(s->mirror->plist, k, cv); CFRelease(cv); } }
			mirror_sign(s->mirror, kc, pid);
			s->mirror->dirty = 1;
			s->mirror->owner_uid = s->owner_uid; s->mirror->owner_gid = s->owner_gid; s->mirror->have_owner = s->have_owner;
		}
		free(kc);
	}
}

static xpc_object_t
handle_source(xpc_object_t msg, struct client *c, int64_t op)
{
	const char *domain = fixed_domain(xpc_dictionary_get_string(msg, "CFPreferencesDomain"));
	const char *uarg = xpc_dictionary_get_string(msg, "CFPreferencesUser");
	const char *container = xpc_dictionary_get_string(msg, "CFPreferencesContainer");
	char user[256];
	int byhost = xpc_dictionary_get_bool(msg, "CFPreferencesIsByHost");
	int managed = xpc_dictionary_get_bool(msg, "CFPreferencesIsManaged");

	if (!domain) return error_reply(msg, "Domain not found", 1, 0);
	if (streq(domain, "kCFPreferencesCurrentApplication")) return error_reply(msg, "Domain not found", 1, 0);
	if (container && streq(container, "kCFPreferencesNoContainer")) container = NULL;
	if (xpc_dictionary_get_string(msg, "CFPreferencesCloudConfig") || xpc_dictionary_get_bool(msg, "FullCloudSync"))
		return error_reply(msg, "Domain not cloud-backed", 1, 0);
	if (!uarg) snprintf(user, sizeof user, "kCFPreferencesAnyUser");
	else if (streq(uarg, "kCFPreferencesCurrentUser")) {	/* _CFPrefsCopyFixedUpUserForMessage: the user of the peer's euid */
		struct passwd *pw = getpwuid(c->euid);
		if (!pw || !pw->pw_name) return error_reply(msg, "Domain or user not found", 1, 0);
		snprintf(user, sizeof user, "%s", pw->pw_name);
	} else snprintf(user, sizeof user, "%s", uarg);
	if (streq(user, "kCFPreferencesAnyUser") && container && strncmp(container, "/private/var/containers/Shared/SystemGroup/", 43) != 0)
		return error_reply(msg, "Using kCFPreferencesAnyUser with a container is only allowed for System Containers", 1, 0);

	struct source *s = source_get(domain, user, byhost, managed, container);
	if (!s) return error_reply(msg, "Domain or user not found", 1, 0);
	xpc_object_t idx = xpc_dictionary_get_value(msg, "CFPreferencesShmemIndex");
	if (idx && xpc_get_type(idx) == &_xpc_type_int64 && xpc_int64_get_value(idx) != -1 && xpc_int64_get_value(idx) != s->shmidx) {
		xpc_object_t r = error_reply(msg, "Lookup inconsistency", 1, 0);
		add_shmem_fields(r, s);
		return r;
	}

	xpc_object_t r;
	if (op == 9) {
		r = new_reply(msg);
		if (s->mirror) {
			xpc_object_t a = xpc_array_create(NULL, 0), p1 = xpc_string_create(s->path), p2 = xpc_string_create(s->mirror->path);
			xpc_array_append_value(a, p1); xpc_array_append_value(a, p2);
			xpc_dictionary_set_value(r, "CFPreferencesUncanonicalizedPath", a);
			xpc_release(p1); xpc_release(p2); xpc_release(a);
		} else xpc_dictionary_set_string(r, "CFPreferencesUncanonicalizedPath", s->path);
		return r;
	}
	if (xpc_dictionary_get_bool(msg, "CFPreferencesRestrictedReadability")) s->restricted = 1;
	if (xpc_dictionary_get_bool(msg, "CFPreferencesAvoidCache")) {	/* "we're avoiding the cache" */
		if (s->dirty) source_write(s);
		source_unload(s);
	}
	if (op == 7 || op == 8) {				/* setObserved:bySenderOfMessage: */
		if (op == 7) { list_add((void ***)&s->observers, &s->nobservers, c); list_add((void ***)&c->observed, &c->nobserved, s); }
		else { list_remove((void ***)&s->observers, &s->nobservers, c); list_remove((void ***)&c->observed, &c->nobserved, s); }
		r = new_reply(msg);
		add_shmem_fields(r, s);
		return r;
	}

	xpc_object_t key = xpc_dictionary_get_value(msg, "Key"), val = xpc_dictionary_get_value(msg, "Value");
	int multi = xpc_dictionary_get_bool(msg, "MultiKeySet");
	int is_write = val && (multi || (key && xpc_get_type(key) == &_xpc_type_string));
	source_load(s);
	int v = check_posix(s, c, msg, is_write);
	if (v == 4) {
		r = error_reply(msg, "Access token needed", 3, 0);
		xpc_dictionary_set_string(r, "CFPreferencesAccessToken", s->path);
		add_shmem_fields(r, s);
		return r;
	}
	if (v) {
		r = error_reply(msg, is_write ? "writing to another user's preferences requires sufficient permissions to write to the file"
		    : "accessing another user's preferences requires sufficient permissions to be able to read the file", 0, 1);
		add_shmem_fields(r, s);
		return r;
	}
	if (!is_write) {					/* read */
		r = new_reply(msg);
		if (s->plist) {				/* a missing, empty or unreadable file: no plist at all */
			CFDataRef d = source_data(s);
			if (d) { xpc_dictionary_set_data(r, "CFPreferencesPropertyList", CFDataGetBytePtr(d), (size_t)CFDataGetLength(d)); CFRelease(d); }
		}
		if (s->mirror) {
			source_load(s->mirror);
			if (s->mirror->plist) {
				CFDataRef d = source_data(s->mirror);
				if (d) { xpc_dictionary_set_data(r, "CFPreferencesValidationPropertyList", CFDataGetBytePtr(d), (size_t)CFDataGetLength(d)); CFRelease(d); }
			}
		}
		add_shmem_fields(r, s);
		return r;
	}

	/* write */
	char dir[MAXPATH];
	dirname_of(s->path, dir);
	if (!s->on_disk && !is_dir(dir) && !streq(dir, "/Library/Preferences")) {	/* the client makes the directory (0700) and asks again */
		r = error_reply(msg, "Directory needed", 4, 0);
		xpc_dictionary_set_string(r, "CFPreferencesUncanonicalizedPath", s->path);
		add_shmem_fields(r, s);
		return r;
	}
	uid_t ou = c->euid;
	gid_t og = c->egid;
	if (xpc_dictionary_get_bool(msg, "CFPreferencesUseCorrectOwner")) {
		struct passwd *pw = getpwnam(s->user);
		if (pw) { ou = pw->pw_uid; og = pw->pw_gid; }
	}
	if (s->anyuser || s->managed) { ou = c->euid; og = c->egid; }
	if (s->have_owner && (s->owner_uid != ou || s->owner_gid != og) && s->dirty) source_write(s);
	s->owner_uid = ou; s->owner_gid = og; s->have_owner = 1;

	long added = 0;
	if (multi && xpc_get_type(val) == &_xpc_type_dictionary) {
		xpc_dictionary_apply(val, ^_Bool(const char *k, xpc_object_t e) {
			CFStringRef ck = cfstr(k);
			if (ck) { set_key(s, ck, e, c->pid); CFRelease(ck); }
			return 1;
		});
		added = value_size(val);
	} else {
		CFStringRef ck = cfstr(xpc_string_get_string_ptr(key));
		if (ck) { set_key(s, ck, val, c->pid); CFRelease(ck); }
		if (xpc_get_type(val) != &_xpc_type_null) added = (long)strlen(xpc_string_get_string_ptr(key)) + value_size(val);
	}
	int created = !s->on_disk;
	mark_dirty(s);
	if (xpc_dictionary_get_bool(msg, "CFPreferencesShouldWriteSynchronously") || created || (role == ROLE_DAEMON && c->euid < 501)) source_write(s);
	r = new_reply(msg);
	if (added) {						/* _CFPrefsDomainSizeAcceptabilityForByteCount */
		long n = s->disk_size + added;
		xpc_dictionary_set_int64(r, "CFPreferencesByteCountThreshold", n <= 0x300000 ? 0 : n <= 0x400000 ? 1 : 2);
	}
	add_shmem_fields(r, s);
	if (role == ROLE_DAEMON && !s->anyuser && !streq(s->user, my_user)) flush_in_agent(s->user, s->domain);
	if (role == ROLE_AGENT && (streq(s->domain, "com.apple.CoreGraphics") || streq(s->domain, "com.apple.assistant.backedup")
	    || (streq(s->domain, "kCFPreferencesAnyApplication") && key && streq(xpc_string_get_string_ptr(key), "com.apple.ColorSync.Devices")))) {
		xpc_connection_t d = xpc_connection_create_mach_service(svc_daemon, q, testmode ? 0 : XPC_CONNECTION_MACH_SERVICE_PRIVILEGED);
		xpc_connection_set_event_handler(d, ^(xpc_object_t e) { (void)e; });
		xpc_connection_resume(d);
		send_op_async(d, 4, s->domain);
		xpc_release(d);
	}
	notify_observers(s, c);
	return r;
}

/* ------------------------------------------------------------------ the other operations */
static void
bump_domain(const char *domain)		/* updateShmemForDomain: the seven slots of the domain */
{
	int b = domain_base(domain);
	for (int i = 0; i < 7; i++) shmem_bump(b + i);
}

static xpc_object_t handle_message(xpc_object_t msg, struct client *c);

static xpc_object_t
handle_multi(xpc_object_t msg, struct client *c)	/* op 5 */
{
	xpc_object_t arr = xpc_dictionary_get_value(msg, "CFPreferencesMessages");
	if (!arr || xpc_get_type(arr) != &_xpc_type_array) return error_reply(msg, "Multi-message without messages", 1, 0);
	size_t n = xpc_array_get_count(arr);
	for (size_t i = 0; i < n; i++) {
		xpc_type_t t = xpc_get_type(xpc_array_get_value(arr, i));
		if (t != &_xpc_type_dictionary && t != &_xpc_type_null) return error_reply(msg, "Multi-message with an invalid item", 1, 0);
	}
	xpc_object_t out = xpc_array_create(NULL, 0);
	for (size_t i = 0; i < n; i++) {
		xpc_object_t m = xpc_array_get_value(arr, i), sub = NULL;
		if (xpc_get_type(m) == &_xpc_type_dictionary) sub = handle_message(m, c);
		if (sub) { xpc_array_append_value(out, sub); xpc_release(sub); }
		else { xpc_object_t nul = xpc_null_create(); xpc_array_append_value(out, nul); xpc_release(nul); }
	}
	xpc_object_t r = new_reply(msg);
	xpc_dictionary_set_value(r, "CFPreferencesMessages", out);
	xpc_release(out);
	return r;
}

static xpc_object_t
handle_message(xpc_object_t msg, struct client *c)	/* -[CFPrefsDaemon handleMessage:fromPeer:replyHandler:] */
{
	int64_t op = xpc_dictionary_get_int64(msg, "CFPreferencesOperation");
	if (verbose) { char *d = xpc_copy_description(msg); dprintf(2, "cfprefsd: pid %d: %s\n", c->pid, d); free(d); }
	switch (op) {
	case 1: case 7: case 8: case 9:
		return handle_source(msg, c, op);
	case 2:
		handle_checkin(msg);
		return NULL;
	case 3:					/* flush managed */
		for (int b = 0; b < NBUCKET; b++)
			for (struct source *s = buckets[b]; s; s = s->next)
				if (s->managed) { source_unload(s); shmem_bump(s->shmidx); notify_observers(s, NULL); }
		if (role == ROLE_DAEMON) for (int i = 0; i < nagent; i++) send_op_async(agentv[i].conn, 3, NULL);
		return new_reply(msg);
	case 4: {				/* flush a domain: the file was written behind our back */
		const char *domain = fixed_domain(xpc_dictionary_get_string(msg, "CFPreferencesDomain"));
		if (domain) {
			for (int b = 0; b < NBUCKET; b++)
				for (struct source *s = buckets[b]; s; s = s->next)
					if (streq(s->domain, domain)) source_unload(s);
			bump_domain(domain);
		}
		return new_reply(msg);
	}
	case 5:
		return handle_multi(msg, c);
	case 6: {				/* a user was deleted */
		const char *user = xpc_dictionary_get_string(msg, "CFPreferencesUser");
		if (role != ROLE_DAEMON) return error_reply(msg, "User deleted message sent to an agent", 1, 0);
		if (c->euid != 0) return error_reply(msg, "Only root processes are allowed to send user deleted messages", 1, 0);
		if (!user) return error_reply(msg, "No username in user deleted message", 1, 0);
		agent_forget(user);
		for (int b = 0; b < NBUCKET; b++) for (struct source *s = buckets[b]; s; s = s->next) source_unload(s);
		for (int i = 1; i < SHMEM_SLOTS; i++) shmem_bump(i);
		return new_reply(msg);
	}
	case 999:
		sync_all();
		return NULL;
	default:
		return error_reply(msg, "Unsupported CFPreferences Daemon Operation", 1, 0);
	}
}

/* ------------------------------------------------------------------ connections */
static void
client_gone(struct client *c)
{
	for (int i = 0; i < c->nobserved; i++) list_remove((void ***)&c->observed[i]->observers, &c->observed[i]->nobservers, c);
	free(c->observed);
	c->observed = NULL;
	c->nobserved = 0;
	c->dead = 1;
	xpc_connection_cancel(c->conn);
	if (c->pending) { xpc_release(c->pending); c->pending = NULL; }
	/* the struct stays: a reply block of a notification may still point at it (a few bytes per finished client) */
}

static void
new_peer(xpc_connection_t peer)
{
	struct client *c = calloc(1, sizeof *c);
	c->conn = xpc_retain(peer);	/* the listener hands the peer over without a reference */
	c->pid = xpc_connection_get_pid(peer);
	c->euid = xpc_connection_get_euid(peer);
	c->egid = xpc_connection_get_egid(peer);
	xpc_connection_set_target_queue(peer, q);
	xpc_connection_set_event_handler(peer, ^(xpc_object_t m) {
		if (xpc_get_type(m) == &_xpc_type_error) {
			if (m == XPC_ERROR_CONNECTION_INVALID) client_gone(c);
			return;
		}
		if (xpc_get_type(m) != &_xpc_type_dictionary) return;
		top_msg = m;
		top_replied = 0;
		xpc_object_t r = handle_message(m, c);
		top_msg = NULL;
		if (!r) return;
		if (verbose) { char *d = xpc_copy_description(r); dprintf(2, "cfprefsd: reply: %s\n", d); free(d); }
		if (top_replied) xpc_connection_send_message(peer, r);	/* only a message that waits for a reply gets one */
		xpc_release(r);
	});
	xpc_connection_resume(peer);
}

/* /usr/sbin/cfprefsd's own main: the process is quarantined as "com.apple.cfprefsd" (flags 6), so every file it creates gets
 * com.apple.quarantine "0086;<time>;com.apple.cfprefsd;" (seen on the plists of the Mac).  Apple's exits when the quarantine
 * cannot be applied; a system without the quarantine (pearOS) goes on without it. */
static void
quarantine_self(void)
{
	if (!_qtn_proc_alloc) return;
	void *qt = _qtn_proc_alloc();
	if (!qt) { logf_("cfprefsd: Failure to configure quarantine: qtn_proc_alloc() failed%s%s\n", "", ""); return; }
	_qtn_proc_set_identifier(qt, "com.apple.cfprefsd");
	_qtn_proc_set_flags(qt, 6);
	int e = _qtn_proc_apply_to_self(qt);
	if (verbose) dprintf(2, "cfprefsd: qtn_proc_apply_to_self: %d\n", e);
	if (e != 0) logf_("cfprefsd: Failure to configure quarantine: qtn_proc_apply_to_self() failed%s%s\n", "", "");
	_qtn_proc_free(qt);
}

int
main(int argc, char **argv)
{
	const char *rolearg = argc > 1 ? argv[1] : NULL;
	if (argc < 1) { dprintf(2, "cfprefsd is not intended to be used directly\n"); exit(0); }
	for (int i = 1; i < argc; i++) if (streq(argv[i], "-v")) verbose = 1;
	quarantine_self();
	testmode = getenv("__CFPreferencesTestDaemon") != NULL;
	my_euid = geteuid();
	if (streq(rolearg, "daemon")) role = ROLE_DAEMON;
	else if (streq(rolearg, "agent")) role = ROLE_AGENT;
	else role = getuid() == 0 ? ROLE_DAEMON : ROLE_AGENT;
	svc_agent = testmode ? "com.apple.cfprefsd.agent.test" : "com.apple.cfprefsd.agent";
	svc_daemon = testmode ? "com.apple.cfprefsd.daemon.test" : "com.apple.cfprefsd.daemon";
	struct passwd *pw = getpwuid(my_euid);
	if (pw && pw->pw_name) snprintf(my_user, sizeof my_user, "%s", pw->pw_name);
	hostid_init();
	q = dispatch_queue_create("com.apple.cfprefsd", NULL);
	shmem_init();

	timer = dispatch_source_create(&_dispatch_source_type_timer, 0, 0, q);	/* "CFPreferences Periodic Sync Queue": every 10 s */
	dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC), 10 * NSEC_PER_SEC, NSEC_PER_SEC);
	dispatch_source_set_event_handler(timer, ^{
		sync_all();
		int any = 0;
		for (int b = 0; b < NBUCKET && !any; b++) for (struct source *s = buckets[b]; s; s = s->next) if (s->dirty) { any = 1; break; }
		if (!any && timer_running) { timer_running = 0; dispatch_suspend(timer); }
	});
	signal(SIGTERM, SIG_IGN);
	dispatch_source_t term = dispatch_source_create(&_dispatch_source_type_signal, SIGTERM, 0, q);
	dispatch_source_set_event_handler(term, ^{ sync_all(); exit(0); });
	dispatch_resume(term);

	listener = xpc_connection_create_mach_service(role == ROLE_DAEMON ? svc_daemon : svc_agent, q, XPC_CONNECTION_MACH_SERVICE_LISTENER);
	xpc_connection_set_event_handler(listener, ^(xpc_object_t peer) {
		if (xpc_get_type(peer) == &_xpc_type_connection) new_peer(peer);
	});
	xpc_connection_resume(listener);
	if (role == ROLE_AGENT) {
		int tok;
		dispatch_async(q, ^{ checkin(); });
		notify_register_dispatch("CFPreferencesAgentsShouldCheckIn", &tok, q, ^(int t) { (void)t; checkin(); });
	} else notify_post("CFPreferencesAgentsShouldCheckIn");
	logf_("cfprefsd: serving %s (shared memory %s)\n", role == ROLE_DAEMON ? svc_daemon : svc_agent, shmem_name);
	dispatch_main();
}
