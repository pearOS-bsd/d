/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Alexandru Balan
 *
 * opendirectoryd for pearOS: the two pipe services of the Mac's opendirectoryd that libsystem_info (Libinfo) asks for the
 * accounts, wire compatible with macOS 26.7.2:
 *   com.apple.system.opendirectoryd.libinfo     getpwuid getpwnam getpwuuid getpwent getgrgid getgrnam getgruuid getgrent
 *                                               getgrouplist (also initgroups and getgroups of a user) and the other *ent lists
 *   com.apple.system.opendirectoryd.membership  mbr_identifier_translate (mbr_uid_to_uuid, mbr_uuid_to_id, mbr_user_name_to_uuid ...),
 *                                               mbr_check_membership, mbr_check_service_membership, mbr_cache_flush, mbr_set_identifier_ttl
 * The client is Apple's: _od_rpc_call of Libinfo sends a dictionary through an xpc pipe ("rpc_name", "rpc_version" 2 and the keys of
 * the call) and takes the reply only when the audit token of the sender has uid 0: this daemon runs as root.
 * The accounts are those of the system: /etc/passwd, /etc/group (FreeBSD's files, read with plain file I/O), and /etc/master.passwd
 * when it can be read (only to tell whether the account has a password: Apple's pw_passwd is "********" then, "*" otherwise).
 * UUIDs: the files have no GeneratedUID, so a user is FFFFEEEE-DDDD-CCCC-BBBB-AAAA<uid> and a group of /etc/group is
 * ABCDEFAB-CDEF-ABCD-EFAB-CDEF<gid> (the GeneratedUID that macOS gives to its own accounts, root, daemon, staff, admin ...); an id
 * that is in no file gets what opendirectoryd answers for it: FFFFEEEE-DDDD-CCCC-BBBB-AAAA<uid> / AAAABBBB-CCCC-DDDD-EEEE-FFFF<gid>.
 * A call that opendirectoryd does not serve gets no reply (the request is released, its reply right dies and the client's
 * xpc_pipe_routine returns at once with EIO (5), as on the Mac); no request is ever left waiting.
 * Protocol and evidence: research/docs/OPENDIRECTORY-DISTNOTED-WIRE.md.  No Apple header or file is used.
 *
 * opendirectoryd                 both services (the job of launchd/com.apple.opendirectoryd.plist)
 * opendirectoryd libinfo=NAME membership=NAME passwd=PATH group=PATH master=PATH    other names / files (tests on a Mac)
 * opendirectoryd -v ...          log every request on stderr
 */
typedef void *xpc_object_t;
typedef void *dq;
typedef unsigned long size_t;

extern void xpc_connection_set_event_handler(xpc_object_t c, void (^h)(xpc_object_t));
extern xpc_object_t xpc_connection_create_mach_service(const char *name, dq q, unsigned long flags);
extern void xpc_connection_set_target_queue(xpc_object_t c, dq q);
extern void xpc_connection_resume(xpc_object_t c);
extern int xpc_connection_get_pid(xpc_object_t c);
extern xpc_object_t xpc_dictionary_create(const char *const *k, const xpc_object_t *v, size_t n);
extern xpc_object_t xpc_dictionary_create_reply(xpc_object_t m);
extern xpc_object_t xpc_dictionary_get_value(xpc_object_t d, const char *k);
extern const char *xpc_dictionary_get_string(xpc_object_t d, const char *k);
extern long xpc_dictionary_get_int64(xpc_object_t d, const char *k);
extern const void *xpc_dictionary_get_data(xpc_object_t d, const char *k, size_t *len);
extern const unsigned char *xpc_dictionary_get_uuid(xpc_object_t d, const char *k);
extern void xpc_dictionary_set_value(xpc_object_t d, const char *k, xpc_object_t v);
extern void xpc_dictionary_set_string(xpc_object_t d, const char *k, const char *v);
extern void xpc_dictionary_set_int64(xpc_object_t d, const char *k, long v);
extern void xpc_dictionary_set_bool(xpc_object_t d, const char *k, int v);
extern void xpc_dictionary_set_data(xpc_object_t d, const char *k, const void *p, size_t n);
extern void xpc_dictionary_set_uuid(xpc_object_t d, const char *k, const unsigned char *u);
extern xpc_object_t xpc_array_create(const xpc_object_t *o, size_t n);
extern void xpc_array_append_value(xpc_object_t a, xpc_object_t v);
extern xpc_object_t xpc_string_create(const char *s);
extern void xpc_release(xpc_object_t o);
extern void *xpc_get_type(xpc_object_t o);
extern int xpc_pipe_routine_reply(xpc_object_t reply);
extern const struct _xpc_type_s _xpc_type_dictionary, _xpc_type_int64;
extern dq dispatch_queue_create(const char *label, void *attr);
extern void dispatch_main(void);
extern long write(int fd, const void *buf, size_t n);
extern int open(const char *path, int flags, ...);
extern long read(int fd, void *buf, size_t n);
extern long lseek(int fd, long off, int whence);
extern int close(int fd);
extern void *malloc(size_t n);
extern void free(void *p);

static size_t slen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static void out(const char *s) { write(2, s, slen(s)); }
static int streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int prefix(const char *s, const char *p) { while (*p && *s == *p) { s++; p++; } return *p == 0; }
static void
outn(long v)
{
	char b[24]; int i = 23;

	b[i] = 0;
	if (v < 0) { out("-"); v = -v; }
	do { b[--i] = '0' + v % 10; v /= 10; } while (v);
	out(b + i);
}
static void
numstr(long v, char *b)			/* the decimal string of v (the *ent lists carry the ids as strings, as Apple's) */
{
	char t[24]; int i = 0, j = 0;

	if (v < 0) { b[j++] = '-'; v = -v; }
	do { t[i++] = '0' + v % 10; v /= 10; } while (v);
	while (i) b[j++] = t[--i];
	b[j] = 0;
}

static const char *passwd_path = "/etc/passwd", *group_path = "/etc/group", *master_path = "/etc/master.passwd";
static int verbose;

/* ---- the account files ---- */

struct file { char *buf; int pos; };

static int
file_load(struct file *f, const char *path)
{
	int fd = open(path, 0), n = 0;
	long sz, r;

	f->buf = 0; f->pos = 0;
	if (fd < 0) return (0);
	sz = lseek(fd, 0, 2);
	lseek(fd, 0, 0);
	if (sz < 0 || (f->buf = malloc(sz + 1)) == 0) { close(fd); return (0); }
	while (n < sz && (r = read(fd, f->buf + n, sz - n)) > 0) n += (int)r;
	close(fd);
	f->buf[n] = 0;
	return (1);
}
static void file_free(struct file *f) { free(f->buf); f->buf = 0; }

/* the next line: its fields (up to 10) cut at ':' in place; returns the number of fields, 0 at the end; comments and blank lines skipped */
static int
file_line(struct file *f, char *fl[10])
{
	char *p;
	int nf;

	if (f->buf == 0) return (0);
	for (;;) {
		p = f->buf + f->pos;
		while (*p == '\n' || *p == '\r') p++;
		if (*p == 0) { f->pos = (int)(p - f->buf); return (0); }
		if (*p != '#') break;
		while (*p && *p != '\n') p++;
		f->pos = (int)(p - f->buf);
	}
	nf = 0;
	fl[nf++] = p;
	while (*p && *p != '\n') { if (*p == ':' && nf < 10) { *p = 0; fl[nf++] = p + 1; } else if (*p == '\r') *p = 0; p++; }
	if (*p == '\n') *p++ = 0;
	f->pos = (int)(p - f->buf);
	return (nf);
}
static long
num(const char *s)
{
	long v = 0; int neg = 0;

	if (*s == '-') { neg = 1; s++; }
	while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
	return (neg ? -v : v);
}

struct user { char name[256], gecos[256], dir[1024], shell[256]; long uid, gid; int haspw; };
struct group { char name[256]; long gid; char *mem; struct file f; };	/* mem: the member list of the line (commas), inside f */

static void
copy(char *d, const char *s, int n)
{
	int i;

	for (i = 0; s[i] && i < n - 1; i++) d[i] = s[i];
	d[i] = 0;
}

/* whether the account has a password (the hash of master.passwd is not "*..." nor empty); only root can read that file */
static int
user_haspw(const char *name)
{
	struct file f;
	char *fl[10];
	int nf, r = 0;

	if (!file_load(&f, master_path)) return (0);
	while ((nf = file_line(&f, fl)) > 0)
		if (nf >= 2 && streq(fl[0], name)) { r = fl[1][0] != 0 && fl[1][0] != '*' && fl[1][0] != '!'; break; }
	file_free(&f);
	return (r);
}

/* by == 0: by name; 1: by uid.  Returns 1 when found. */
static int
user_find(int by, const char *name, long uid, struct user *u)
{
	struct file f;
	char *fl[10];
	int nf, found = 0;

	if (!file_load(&f, passwd_path)) return (0);
	while ((nf = file_line(&f, fl)) > 0) {
		if (nf < 7) continue;
		if (by == 0 ? !(name && streq(fl[0], name)) : num(fl[2]) != uid) continue;
		copy(u->name, fl[0], sizeof(u->name)); copy(u->gecos, fl[4], sizeof(u->gecos));
		copy(u->dir, fl[5], sizeof(u->dir)); copy(u->shell, fl[6], sizeof(u->shell));
		u->uid = num(fl[2]); u->gid = num(fl[3]);
		found = 1;
		break;
	}
	file_free(&f);
	if (found) u->haspw = user_haspw(u->name);
	return (found);
}

/* by == 0: by name; 1: by gid.  The caller frees g->f with group_done. */
static int
group_find(int by, const char *name, long gid, struct group *g)
{
	char *fl[10];
	int nf;

	if (!file_load(&g->f, group_path)) return (0);
	while ((nf = file_line(&g->f, fl)) > 0) {
		if (nf < 3) continue;
		if (by == 0 ? !(name && streq(fl[0], name)) : num(fl[2]) != gid) continue;
		copy(g->name, fl[0], sizeof(g->name));
		g->gid = num(fl[2]);
		g->mem = nf > 3 ? fl[3] : (char *)"";
		return (1);
	}
	file_free(&g->f);
	return (0);
}
static void group_done(struct group *g) { file_free(&g->f); }

/* whether a comma list of members has the name (the list is cut and restored in place) */
static int
members_has(char *mem, const char *name)
{
	char *p = mem, *q;
	int r = 0;

	while (*p) {
		char c;

		q = p;
		while (*q && *q != ',') q++;
		c = *q; *q = 0;
		if (*p && streq(p, name)) r = 1;
		*q = c;
		p = c ? q + 1 : q;
	}
	return (r);
}
static xpc_object_t
members_array(char *mem)
{
	xpc_object_t a = xpc_array_create(0, 0);
	char *p = mem, *q;

	while (*p) {
		char c;

		q = p;
		while (*q && *q != ',') q++;
		c = *q; *q = 0;
		if (*p) { xpc_object_t s = xpc_string_create(p); xpc_array_append_value(a, s); xpc_release(s); }
		*q = c;
		p = c ? q + 1 : q;
	}
	return (a);
}

/* ---- UUIDs ---- */

static const unsigned char user_prefix[12] = { 0xff, 0xff, 0xee, 0xee, 0xdd, 0xdd, 0xcc, 0xcc, 0xbb, 0xbb, 0xaa, 0xaa };	/* Apple's accounts and unknown uids */
static const unsigned char group_prefix[12] = { 0xab, 0xcd, 0xef, 0xab, 0xcd, 0xef, 0xab, 0xcd, 0xef, 0xab, 0xcd, 0xef };	/* Apple's groups */
static const unsigned char nogroup_prefix[12] = { 0xaa, 0xaa, 0xbb, 0xbb, 0xcc, 0xcc, 0xdd, 0xdd, 0xee, 0xee, 0xff, 0xff };	/* a gid that no group has */

static void
uuid_make(unsigned char u[16], const unsigned char *pre, long id)
{
	int i;

	for (i = 0; i < 12; i++) u[i] = pre[i];
	u[12] = (unsigned char)(id >> 24); u[13] = (unsigned char)(id >> 16); u[14] = (unsigned char)(id >> 8); u[15] = (unsigned char)id;
}
static int
uuid_has(const unsigned char *u, const unsigned char *pre)
{
	int i;

	for (i = 0; i < 12; i++) if (u[i] != pre[i]) return (0);
	return (1);
}
static long uuid_id(const unsigned char *u) { return (long)(int)((unsigned)u[12] << 24 | u[13] << 16 | u[14] << 8 | u[15]); }
static void
group_uuid(unsigned char u[16], long gid)
{
	struct group g;

	if (group_find(1, 0, gid, &g)) { group_done(&g); uuid_make(u, group_prefix, gid); }
	else uuid_make(u, nogroup_prefix, gid);
}

/* ---- replies ---- */

enum { E_NOTFOUND = 2, E_INVAL = 22 };

static void
reply_error(xpc_object_t r, int e, const char *desc)
{
	xpc_dictionary_set_int64(r, "error", e);
	if (desc) xpc_dictionary_set_string(r, "error_desc", desc);
}
static void reply_notfound(xpc_object_t r) { reply_error(r, E_NOTFOUND, "record not found"); }
static void reply_badtype(xpc_object_t r) { reply_error(r, E_INVAL, "<missing or invalid type>"); }

static int
get_int(xpc_object_t m, const char *k, long *v)
{
	xpc_object_t o = xpc_dictionary_get_value(m, k);

	if (o == 0 || xpc_get_type(o) != (void *)&_xpc_type_int64) return (0);
	*v = xpc_dictionary_get_int64(m, k);
	return (1);
}

/* a user: single look-ups carry ints and pw_uuid, the getpwent list carries strings and no uuid (both as the Mac) */
static void
user_fill(xpc_object_t d, struct user *u, int list)
{
	xpc_dictionary_set_string(d, "pw_name", u->name);
	xpc_dictionary_set_string(d, "pw_passwd", u->haspw ? "********" : "*");
	if (list) {
		char b[24];

		numstr(u->uid, b); xpc_dictionary_set_string(d, "pw_uid", b);
		numstr(u->gid, b); xpc_dictionary_set_string(d, "pw_gid", b);
	} else {
		unsigned char uu[16];

		xpc_dictionary_set_int64(d, "pw_uid", u->uid);
		xpc_dictionary_set_int64(d, "pw_gid", u->gid);
		uuid_make(uu, user_prefix, u->uid);
		xpc_dictionary_set_uuid(d, "pw_uuid", uu);
	}
	xpc_dictionary_set_string(d, "pw_gecos", u->gecos);
	xpc_dictionary_set_string(d, "pw_dir", u->dir);
	xpc_dictionary_set_string(d, "pw_shell", u->shell);
}
static void
group_fill(xpc_object_t d, const char *name, long gid, char *mem, int list)
{
	xpc_object_t a = members_array(mem);

	xpc_dictionary_set_string(d, "gr_name", name);
	xpc_dictionary_set_string(d, "gr_passwd", "*");
	if (list) { char b[24]; numstr(gid, b); xpc_dictionary_set_string(d, "gr_gid", b); }
	else {
		unsigned char uu[16];

		xpc_dictionary_set_int64(d, "gr_gid", gid);
		uuid_make(uu, group_prefix, gid);
		xpc_dictionary_set_uuid(d, "gr_uuid", uu);
	}
	xpc_dictionary_set_value(d, "gr_mem", a);
	xpc_release(a);
}

static void
list_users(xpc_object_t r)
{
	struct file f;
	char *fl[10];
	int nf;
	xpc_object_t a = xpc_array_create(0, 0);

	if (file_load(&f, passwd_path)) {
		while ((nf = file_line(&f, fl)) > 0) {
			struct user u;
			xpc_object_t d;

			if (nf < 7) continue;
			copy(u.name, fl[0], sizeof(u.name)); copy(u.gecos, fl[4], sizeof(u.gecos));
			copy(u.dir, fl[5], sizeof(u.dir)); copy(u.shell, fl[6], sizeof(u.shell));
			u.uid = num(fl[2]); u.gid = num(fl[3]); u.haspw = user_haspw(u.name);
			d = xpc_dictionary_create(0, 0, 0);
			user_fill(d, &u, 1);
			xpc_array_append_value(a, d);
			xpc_release(d);
		}
		file_free(&f);
	}
	xpc_dictionary_set_value(r, "result", a);
	xpc_release(a);
}
static void
list_groups(xpc_object_t r)
{
	struct file f;
	char *fl[10];
	int nf;
	xpc_object_t a = xpc_array_create(0, 0);

	if (file_load(&f, group_path)) {
		while ((nf = file_line(&f, fl)) > 0) {
			xpc_object_t d;

			if (nf < 3) continue;
			d = xpc_dictionary_create(0, 0, 0);
			group_fill(d, fl[0], num(fl[2]), nf > 3 ? fl[3] : (char *)"", 1);
			xpc_array_append_value(a, d);
			xpc_release(d);
		}
		file_free(&f);
	}
	xpc_dictionary_set_value(r, "result", a);
	xpc_release(a);
}

/* the groups of a user: his primary group first, then every group of /etc/group that lists him (no duplicate); as int32 */
static void
grouplist(xpc_object_t r, const char *name)
{
	struct user u;
	struct file f;
	char *fl[10];
	int nf, n = 0, cap = 64, i, dup;
	int *g = malloc(cap * sizeof(int));

	if (g == 0) { reply_notfound(r); return; }
	if (!user_find(0, name, 0, &u)) {		/* the Mac: count 1, the group -1 */
		g[0] = -1;
		xpc_dictionary_set_data(r, "groups", g, 4);
		xpc_dictionary_set_int64(r, "count", 1);
		free(g);
		return;
	}
	g[n++] = (int)u.gid;
	if (file_load(&f, group_path)) {
		while ((nf = file_line(&f, fl)) > 0) {
			long gid;

			if (nf < 4 || !members_has(fl[3], name)) continue;
			gid = num(fl[2]);
			for (dup = 0, i = 0; i < n; i++) if (g[i] == (int)gid) dup = 1;
			if (dup) continue;
			if (n == cap) {
				int *ng = malloc(cap * 2 * sizeof(int));

				if (ng == 0) break;
				for (i = 0; i < n; i++) ng[i] = g[i];
				free(g); g = ng; cap *= 2;
			}
			g[n++] = (int)gid;
		}
		file_free(&f);
	}
	xpc_dictionary_set_data(r, "groups", g, n * sizeof(int));
	xpc_dictionary_set_int64(r, "count", n);
	free(g);
}

/* the libinfo service: returns 0 for a call opendirectoryd does not serve (no reply then) */
static int
libinfo(xpc_object_t m, xpc_object_t r, const char *rn)
{
	struct user u;
	struct group g;
	long id;
	const char *name = xpc_dictionary_get_string(m, "name");
	const unsigned char *uu;

	if (streq(rn, "getpwuid") || streq(rn, "getpwnam") || streq(rn, "getpwuuid")) {
		int ok;

		if (rn[5] == 'u' && rn[6] == 'i') { if (!get_int(m, "uid", &id)) { reply_badtype(r); return (1); } ok = user_find(1, 0, id, &u); }
		else if (rn[5] == 'n') { if (name == 0) { reply_badtype(r); return (1); } ok = user_find(0, name, 0, &u); }
		else {
			if ((uu = xpc_dictionary_get_uuid(m, "uuid")) == 0) { reply_badtype(r); return (1); }
			ok = uuid_has(uu, user_prefix) && user_find(1, 0, uuid_id(uu), &u);
		}
		if (ok) user_fill(r, &u, 0); else reply_notfound(r);
		return (1);
	}
	if (streq(rn, "getgrgid") || streq(rn, "getgrnam") || streq(rn, "getgruuid")) {
		int ok;

		if (rn[5] == 'g') { if (!get_int(m, "gid", &id)) { reply_badtype(r); return (1); } ok = group_find(1, 0, id, &g); }
		else if (rn[5] == 'n') { if (name == 0) { reply_badtype(r); return (1); } ok = group_find(0, name, 0, &g); }
		else {
			if ((uu = xpc_dictionary_get_uuid(m, "uuid")) == 0) { reply_badtype(r); return (1); }
			ok = uuid_has(uu, group_prefix) && group_find(1, 0, uuid_id(uu), &g);
		}
		if (ok) { group_fill(r, g.name, g.gid, g.mem, 0); group_done(&g); } else reply_notfound(r);
		return (1);
	}
	if (streq(rn, "getpwent")) { list_users(r); return (1); }
	if (streq(rn, "getgrent")) { list_groups(r); return (1); }
	if (streq(rn, "getgrouplist")) { if (name == 0) reply_badtype(r); else grouplist(r, name); return (1); }
	/* the other lists: the Mac answers an empty reply (no "result"); the other look-ups get no reply */
	if (streq(rn, "getservent") || streq(rn, "getprotoent") || streq(rn, "getrpcent") || streq(rn, "getnetent") || streq(rn, "alias_getent") ||
	    streq(rn, "getfsent"))
		return (1);
	return (0);
}

/* ---- membership ---- */

enum { ID_UID = 0, ID_GID = 1, ID_USERNAME = 4, ID_GROUPNAME = 5, ID_UUID = 6, ID_UID_OR_GID = 30, ID_NAME = 31 };
enum { REC_USER = 1, REC_GROUP = 2 };

struct ident { int rec; long id; int known; char name[256]; };

/* what an identifier of the request names; returns 0 (found), E_NOTFOUND or E_INVAL */
static int
ident_resolve(int type, const unsigned char *p, size_t len, struct ident *x)
{
	char nm[256];
	struct user u;
	struct group g;
	size_t i;

	x->known = 0; x->name[0] = 0;
	if (p == 0) return (E_INVAL);
	switch (type) {
	case ID_UID: case ID_GID:
		if (len != 4) return (E_INVAL);
		x->rec = type == ID_UID ? REC_USER : REC_GROUP;
		x->id = (long)(int)((unsigned)p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24);
		break;
	case ID_UUID:
		if (len != 16) return (E_INVAL);
		x->id = uuid_id(p);
		if (uuid_has(p, user_prefix)) x->rec = REC_USER;
		else if (uuid_has(p, group_prefix) || uuid_has(p, nogroup_prefix)) x->rec = REC_GROUP;
		else return (E_NOTFOUND);
		if (uuid_has(p, group_prefix)) {		/* only a group of the files has this one */
			if (!group_find(1, 0, x->id, &g)) return (E_NOTFOUND);
			group_done(&g);
		}
		break;
	case ID_USERNAME: case ID_GROUPNAME: case ID_NAME:
		for (i = 0; i < len && i < sizeof(nm) - 1 && p[i]; i++) nm[i] = (char)p[i];
		nm[i] = 0;
		if (type != ID_GROUPNAME && user_find(0, nm, 0, &u)) { x->rec = REC_USER; x->id = u.uid; x->known = 1; copy(x->name, u.name, sizeof(x->name)); return (0); }
		if (type != ID_USERNAME && group_find(0, nm, 0, &g)) { x->rec = REC_GROUP; x->id = g.gid; x->known = 1; copy(x->name, g.name, sizeof(x->name)); group_done(&g); return (0); }
		return (E_NOTFOUND);
	default:
		return (E_NOTFOUND);
	}
	if (x->rec == REC_USER && user_find(1, 0, x->id, &u)) { x->known = 1; copy(x->name, u.name, sizeof(x->name)); }
	if (x->rec == REC_GROUP && group_find(1, 0, x->id, &g)) { x->known = 1; copy(x->name, g.name, sizeof(x->name)); group_done(&g); }
	return (0);
}

static void
translate(xpc_object_t m, xpc_object_t r)
{
	size_t len = 0;
	const unsigned char *p = xpc_dictionary_get_data(m, "identifier", &len);
	long type = -1, want = -1;
	struct ident x;
	int e;

	get_int(m, "type", &type);
	get_int(m, "requesting", &want);
	if (p == 0 || ((type == ID_UID || type == ID_GID) && len != 4) || (type == ID_UUID && len != 16)) { reply_error(r, E_INVAL, "invalid size for identifier"); return; }
	if ((e = ident_resolve((int)type, p, len, &x)) != 0) { reply_error(r, e, 0); return; }
	switch (want) {
	case ID_UUID: {
		unsigned char u[16];

		if (x.rec == REC_USER) uuid_make(u, user_prefix, x.id); else group_uuid(u, x.id);
		xpc_dictionary_set_data(r, "identifier", u, 16);
		break;
	}
	case ID_UID: case ID_GID: case ID_UID_OR_GID: {
		unsigned char b[4];

		if ((want == ID_UID && x.rec != REC_USER) || (want == ID_GID && x.rec != REC_GROUP)) { reply_error(r, E_NOTFOUND, 0); return; }
		b[0] = (unsigned char)x.id; b[1] = (unsigned char)(x.id >> 8); b[2] = (unsigned char)(x.id >> 16); b[3] = (unsigned char)(x.id >> 24);
		xpc_dictionary_set_data(r, "identifier", b, 4);
		break;
	}
	case ID_USERNAME: case ID_GROUPNAME: case ID_NAME:
		if (!x.known || (want == ID_USERNAME && x.rec != REC_USER) || (want == ID_GROUPNAME && x.rec != REC_GROUP)) { reply_error(r, E_NOTFOUND, 0); return; }
		xpc_dictionary_set_data(r, "identifier", x.name, slen(x.name) + 1);	/* with its NUL, as the Mac */
		break;
	default:
		reply_error(r, E_NOTFOUND, 0);
		return;
	}
	xpc_dictionary_set_int64(r, "rectype", x.rec);
}

/* whether the user is in the group: his primary group or a member of it in /etc/group */
static int
is_member(struct ident *user, long gid)
{
	struct user u;
	struct group g;
	int r;

	if (!user->known || !user_find(1, 0, user->id, &u)) return (0);
	if (u.gid == gid) return (1);
	if (!group_find(1, 0, gid, &g)) return (0);
	r = members_has(g.mem, u.name);
	group_done(&g);
	return (r);
}

static void
check_membership(xpc_object_t m, xpc_object_t r)
{
	size_t ul = 0, gl = 0;
	const unsigned char *up = xpc_dictionary_get_data(m, "user_id", &ul), *gp = xpc_dictionary_get_data(m, "group_id", &gl);
	long ut = -1, gt = -1;
	struct ident u, g;
	int e;

	if (!get_int(m, "user_idtype", &ut) || !get_int(m, "group_idtype", &gt) || up == 0 || gp == 0) { reply_error(r, E_INVAL, 0); return; }
	if ((e = ident_resolve((int)ut, up, ul, &u)) == E_INVAL || (e == 0 && u.rec != REC_USER)) { reply_error(r, E_INVAL, 0); return; }
	if (e != 0) { xpc_dictionary_set_bool(r, "ismember", 0); return; }
	if ((e = ident_resolve((int)gt, gp, gl, &g)) == E_INVAL) { reply_error(r, E_INVAL, 0); return; }
	xpc_dictionary_set_bool(r, "ismember", e == 0 && g.rec == REC_GROUP && is_member(&u, g.id));
}

/* mbr_check_service_membership: the group com.apple.access_<service>; no such group: error 2 (the client then allows everyone) */
static void
check_service(xpc_object_t m, xpc_object_t r)
{
	size_t ul = 0;
	const unsigned char *up = xpc_dictionary_get_data(m, "user_id", &ul);
	const char *svc = xpc_dictionary_get_string(m, "service");
	long ut = -1;
	char gn[300];
	struct group g;
	struct ident u;
	int i = 0, j;

	if (svc == 0 || up == 0 || !get_int(m, "user_idtype", &ut)) { reply_error(r, E_INVAL, 0); return; }
	for (j = 0; "com.apple.access_"[j]; j++) gn[i++] = "com.apple.access_"[j];
	for (j = 0; svc[j] && i < (int)sizeof(gn) - 1; j++) gn[i++] = svc[j];
	gn[i] = 0;
	if (!group_find(0, gn, 0, &g)) { reply_error(r, E_NOTFOUND, 0); return; }
	group_done(&g);
	if (ident_resolve((int)ut, up, ul, &u) != 0 || u.rec != REC_USER) { xpc_dictionary_set_bool(r, "ismember", 0); return; }
	xpc_dictionary_set_bool(r, "ismember", is_member(&u, g.gid));
}

static int
membership(xpc_object_t m, xpc_object_t r, const char *rn)
{
	if (streq(rn, "mbr_identifier_translate")) translate(m, r);
	else if (streq(rn, "mbr_check_membership")) check_membership(m, r);
	else if (streq(rn, "mbr_check_service_membership")) check_service(m, r);
	else if (streq(rn, "mbr_cache_flush")) ;				/* nothing is cached */
	else if (streq(rn, "mbr_set_identifier_ttl")) { if (xpc_dictionary_get_data(m, "identifier", 0) == 0) reply_error(r, E_NOTFOUND, 0); }
	else return (0);
	return (1);
}

/* ---- the services ---- */

static void
handle(xpc_object_t peer, xpc_object_t m, int mbr)
{
	const char *rn;
	xpc_object_t r;
	int served;

	if (xpc_get_type(m) != (void *)&_xpc_type_dictionary) return;		/* the end of a connection */
	rn = xpc_dictionary_get_string(m, "rpc_name");
	if ((r = xpc_dictionary_create_reply(m)) == 0) return;			/* a message that wants no answer */
	served = rn != 0 && (mbr ? membership(m, r, rn) : libinfo(m, r, rn));
	if (verbose) { out("opendirectoryd: pid "); outn(xpc_connection_get_pid(peer)); out(mbr ? " membership " : " libinfo "); out(rn ? rn : "(no rpc_name)"); out(served ? "\n" : ": not served, no reply\n"); }
	if (served) {
		xpc_dictionary_set_int64(r, "rpc_version", 2);
		if (xpc_pipe_routine_reply(r) != 0) out("opendirectoryd: the reply could not be sent\n");
	}
	xpc_release(r);
}

static void
serve(const char *name, dq q, int mbr)
{
	xpc_object_t svc = xpc_connection_create_mach_service(name, q, 1 /* XPC_CONNECTION_MACH_SERVICE_LISTENER */);

	xpc_connection_set_event_handler(svc, ^(xpc_object_t peer) {
		if (xpc_get_type(peer) == (void *)&_xpc_type_dictionary) return;	/* an error of the listener */
		xpc_connection_set_target_queue(peer, q);
		xpc_connection_set_event_handler(peer, ^(xpc_object_t m) { handle(peer, m, mbr); });
		xpc_connection_resume(peer);
	});
	xpc_connection_resume(svc);
	out("opendirectoryd: serving "); out(name); out("\n");
}

int
main(int argc, char **argv)
{
	dq q = dispatch_queue_create("com.apple.opendirectoryd", 0);
	const char *li = "com.apple.system.opendirectoryd.libinfo", *mb = "com.apple.system.opendirectoryd.membership";
	int i;

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (streq(a, "-v")) verbose = 1;
		else if (prefix(a, "libinfo=")) li = a + 8;
		else if (prefix(a, "membership=")) mb = a + 11;
		else if (prefix(a, "passwd=")) passwd_path = a + 7;
		else if (prefix(a, "group=")) group_path = a + 6;
		else if (prefix(a, "master=")) master_path = a + 7;
	}
	serve(li, q, 0);
	serve(mb, q, 1);
	dispatch_main();
	return (0);
}
