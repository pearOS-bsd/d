/*
 * launchd for pearOS: a Mach-O program (built with clang and ld64.lld, linked against the libSystem of the Mac, with no header of Apple's) that serves the bootstrap
 * port of the system, like the launchd of a Mac: it makes a port, sets it as its own bootstrap port (the kernel hands it to every task that has none) and answers the
 * XPC requests that libxpc sends there.  License: BSD (own code; the protocol was read from the messages that libxpc sends).
 *
 * The XPC message: the header of Mach (24 bytes), for a complex message the descriptors, and then "CPX@", the version 5 and a dictionary: the type 0xf000, its size, the
 * count of entries and for each entry the key (a C string padded to 4) and the value (the type word and the data).
 */
typedef unsigned int u32;
typedef unsigned long u64;
typedef long i64;

extern u32 mach_task_self_;
extern int mach_msg(void *msg, int option, u32 send_size, u32 rcv_size, u32 rcv_name, u32 timeout, u32 notify);
extern int mach_port_allocate(u32 task, u32 right, u32 *name);
extern int mach_port_insert_right(u32 task, u32 name, u32 poly, u32 poly_poly);
extern int mach_port_type(u32 task, u32 name, u32 *ptype);
extern int task_set_special_port(u32 task, int which, u32 port);
extern long write(int fd, const void *buf, unsigned long n);
extern int getpid(void);
extern int posix_spawn(int *pid, const char *path, const void *fa, const void *attr, char *const argv[], char *const envp[]);
extern int mach_port_move_member(u32 task, u32 member, u32 after);
extern int mkdir(const char *path, unsigned short mode);
extern int chown(const char *path, unsigned int uid, unsigned int gid);
extern int chmod(const char *path, unsigned short mode);
extern void exit(int);
extern int open(const char *path, int flags, ...);
extern long read(int fd, void *buf, unsigned long n);
extern int close(int fd);

#define	MACH_SEND_MSG	1
#define	MACH_RCV_MSG	2
#define	MACH_RCV_LARGE	4
#define	MACH_MSG_TIMEOUT_NONE	0

struct hdr { u32 bits, size, remote, local, voucher, id; };

static void
out(const char *s)
{
	unsigned long n = 0;

	while (s[n])
		n++;
	write(2, s, n);
}

static void
outx(u32 v)
{
	char b[9];
	int i;

	for (i = 7; i >= 0; i--, v >>= 4)
		b[i] = "0123456789abcdef"[v & 15];
	b[8] = 0;
	out(b);
}

static union { struct hdr h; char raw[8192]; } rq, rp;

/* the dictionary of a request: the position of the XPC data (after "CPX@"), or 0 */
static u32
xpc_start(const struct hdr *h)
{
	u32 off = 24, *w = (u32 *)h;

	if (h->bits & 0x80000000u)
		off += 4 + 12 * w[6];			/* the count of descriptors and the descriptors (ports: 12 bytes) */
	if (off + 16 <= h->size && w[off / 4] == 0x40585043)
		return (off);
	return (0);
}

/* debugging: the keys of the dictionary with the types and the small values */
static void
xpc_dump(const struct hdr *h, u32 start)
{
	const u32 *w = (const u32 *)h;
	u32 count = w[start / 4 + 4], p = start / 4 + 5, i, n, t;
	const char *k;

	for (i = 0; i < count && p * 4 < h->size; i++) {
		k = (const char *)&w[p];
		for (n = 0; k[n]; n++)
			;
		out("launchd:     "); out(k); out(" type "); outx(w[p + (n + 4) / 4]);
		p += (n + 4) / 4;
		t = w[p] >> 12;
		if (t == 3 || t == 4 || t == 2) { out(" = "); outx(w[p + 2]); out("/"); outx(w[p + 1]); }
		if (t == 9) { out(" = "); out((const char *)&w[p + 2]); }
		out("\n");
		switch (t) {
		case 0x2: p += 2; break;
		case 0x3: case 0x4: case 0x5: case 0x6: p += 3; break;
		case 0xa: p += 5; break;
		case 0x9: case 0x8: p += 2 + (w[p + 1] + 3) / 4; break;
		case 0xd: p += 1; break;
		default: return;
		}
	}
}

static int
same(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return (*a == *b);
}

/* the value of the key in the dictionary (one level): the pointer to the type word, or 0 */
static const u32 *
xpc_find(const struct hdr *h, u32 start, const char *key)
{
	const u32 *w = (const u32 *)h;
	u32 count = w[start / 4 + 4], p = start / 4 + 5, i, n;
	const char *k;

	for (i = 0; i < count && p * 4 < h->size; i++) {
		k = (const char *)&w[p];
		for (n = 0; k[n]; n++)
			;
		if (same(k, key))
			return (&w[p + (n + 4) / 4]);
		p += (n + 4) / 4;				/* the key with its NUL, padded */
		switch (w[p] >> 12) {
		case 0x2: p += 2; break;			/* bool: the type word and 4 bytes */
		case 0x3: case 0x4: case 0x5: case 0x6: p += 3; break;
		case 0xa: p += 5; break;			/* uuid */
		case 0x9: case 0x8: p += 2 + (w[p + 1] + 3) / 4; break;
		case 0xd: p += 1; break;
		default: return (0);
		}
	}
	return (0);
}

/*
 * The answers.  Every answer is a dictionary; the ones for a requester that the launchd must vouch for carry "req_pid" and "rec_execcnt" (the pid and the exec count of the
 * requester, from the audit token of the request): libxpc checks them when the answer comes from a user 0 (_xpc_domain_routine_authenticate).
 */
static u32 *xw;			/* the message that is being built */
static u32 xn;			/* its size in words */
static u32 xcount, xsizeat, xcountat;

static void
xbegin(const struct hdr *q, int ndesc)
{
	xw = (u32 *)&rp.h;
	rp.h.bits = ndesc ? 0x80000012u : 0x12;		/* MACH_MSG_TYPE_MOVE_SEND_ONCE for the remote port; complex if there are descriptors */
	rp.h.remote = q->remote;
	rp.h.local = 0;
	rp.h.voucher = 0;
	rp.h.id = 0x20000000;				/* the id of an XPC reply: libxpc checks it */
	xn = 6;
	if (ndesc) { xw[xn++] = ndesc; }
}

static void
xdict(void)
{
	xw[xn++] = 0x40585043; xw[xn++] = 5; xw[xn++] = 0xf000;
	xsizeat = xn; xw[xn++] = 0;
	xcountat = xn; xw[xn++] = 0;
	xcount = 0;
}

static void
xkey(const char *k)
{
	u32 n = 0;

	while (k[n]) n++;
	n++;						/* the NUL */
	{
		char *d = (char *)&xw[xn];
		u32 i;

		for (i = 0; i < ((n + 3) & ~3u); i++) d[i] = i < n ? k[i] : 0;
	}
	xn += (n + 3) / 4;
	xcount++;
}

static void xu64(const char *k, u64 v) { xkey(k); xw[xn++] = 0x4000; xw[xn++] = (u32)v; xw[xn++] = (u32)(v >> 32); }
static void xi64(const char *k, i64 v) { xkey(k); xw[xn++] = 0x3000; xw[xn++] = (u32)v; xw[xn++] = (u32)((u64)v >> 32); }
static void
xstr(const char *k, const char *v)
{
	u32 n = 0, i;

	while (v[n]) n++;
	n++;
	xkey(k);
	xw[xn++] = 0x9000; xw[xn++] = n;
	for (i = 0; i < ((n + 3) & ~3u); i++) ((char *)&xw[xn])[i] = i < n ? v[i] : 0;
	xn += (n + 3) / 4;
}

static void xemptydict(const char *k) { xkey(k); xw[xn++] = 0xf000; xw[xn++] = 4; xw[xn++] = 0; }
static void xport(const char *k, u32 type) { xkey(k); xw[xn++] = type; }

static void
xsend(void)
{
	xw[xsizeat] = (xn - xsizeat - 1) * 4;
	xw[xcountat] = xcount;
	rp.h.size = xn * 4;
	mach_msg(&rp.h, MACH_SEND_MSG, xn * 4, 0, 0, MACH_MSG_TIMEOUT_NONE, 0);
}

/* the audit token of the request, in the trailer */
static const u32 *
trailer_of(const struct hdr *h)
{
	return ((const u32 *)((const char *)h + ((h->size + 3) & ~3u)));
}

static void
vouch(const struct hdr *q)
{
	const u32 *tr = trailer_of(q);

	xu64("req_pid", tr[10]);
	xu64("rec_execcnt", tr[12]);
}

/* the answer: a dictionary with no entry, or with {"error": code} */
static void
reply(const struct hdr *q, int with_error, int code)
{
	if (q->remote == 0) {
		out("launchd: no reply port for "); outx(q->id); out("\n");
		return;
	}
	xbegin(q, 0);
	xdict();
	if (with_error)
		xi64("error", code);
	vouch(q);
	xsend();
}

/* the answer with a port: {"port": <right>}.  disp: 20 makes a send right from the receive right that this task holds, 19 copies a send right that it holds, 16 moves the receive right to
 * the requester (the check-in of a service; the type word of a receive right in the XPC data is 0x15000, of a send right 0xd000) */
static void
reply_port(const struct hdr *q, u32 svc, u32 disp)
{
	if (q->remote == 0)
		return;
	xbegin(q, 1);
	xw[xn++] = svc; xw[xn++] = 0; xw[xn++] = (disp << 16) | 0;	/* the descriptor: the right, type 0 = port */
	xdict();
	xport("port", disp == 16 ? 0x15000 : 0xd000);
	vouch(q);
	xsend();
}

/* the services: the ones that this task serves (the dirhelper) and the ones that a program checked in (this task keeps a send right for the look-ups) */
/* the domain (the name of the manager: "System", "Aqua" for a user session) of the jobs that this launchd started, by pid; a program that was not started here is in "System" */
static struct { int pid; char domain[16]; } jobs[32];

static const char *
domain_of(u32 pid)
{
	int i;

	for (i = 0; i < 32; i++)
		if (jobs[i].pid == (int)pid) return (jobs[i].domain);
	return ("System");
}

struct svc { int used, own, pre, claimed, job, hidden; char name[96]; u32 port; };	/* job: index in ljobs + 1 (0: none); hidden: HideUntilCheckIn (not found by a look-up before the check-in) */		/* pre: made at the start (the MachServices of a plist without HideUntilCheckIn: look-ups work before the check-in) */
static struct svc svcs[256];

/*
 * Jobs from launchd property lists (the format of /System/Library/LaunchDaemons and LaunchAgents on the Mac): an argument that ends in ".plist" is a job.
 * Read: Label, ProgramArguments (or Program), MachServices (the names exist from the start: a look-up before the check-in works, the messages wait in the port),
 * RunAtLoad (started now; otherwise at the first look-up of one of its names, the on-demand start of the Mac's launchd), UserName (the job runs as that user:
 * started through daemon(8) -u; a check-in for one of its names is accepted from that user's processes).
 */
struct ljob { char label[96]; char args[8][160]; int nargs; char user[32]; int uid, runatload, started; };
static struct ljob ljobs[32];
static int nljobs;
static char plbuf[16384], pwbuf[16384];		/* the property list being read; /etc/passwd */

static int
pl_find(const char *buf, int from, const char *pat)		/* index just after pat in buf, -1 if not found */
{
	int i, k;

	for (i = from; buf[i]; i++) {
		for (k = 0; pat[k] && buf[i + k] == pat[k]; k++) ;
		if (!pat[k]) return (i + k);
	}
	return (-1);
}

static int
pl_string(const char *buf, int from, char *out, int max)	/* the text of the next <string>...</string> from `from`; index after it, -1 */
{
	int i = pl_find(buf, from, "<string>"), k = 0;

	if (i < 0) return (-1);
	while (buf[i] && buf[i] != '<' && k < max - 1) out[k++] = buf[i++];
	out[k] = 0;
	return (i);
}

static int
pl_value_of(const char *buf, const char *key)			/* index just after <key>key</key>, -1 */
{
	char pat[80];
	int i = 0, k;

	for (k = 0; "<key>"[k]; k++) pat[i++] = "<key>"[k];
	for (k = 0; key[k] && i < 70; k++) pat[i++] = key[k];
	for (k = 0; "</key>"[k]; k++) pat[i++] = "</key>"[k];
	pat[i] = 0;
	return (pl_find(buf, 0, pat));
}

static int
user_uid(const char *name)					/* the uid of a user in /etc/passwd, -1 */
{
	int fd = open("/etc/passwd", 0), n, i = 0;

	if (fd < 0) return (-1);
	n = (int)read(fd, pwbuf, sizeof(pwbuf) - 1);
	close(fd);
	if (n <= 0) return (-1);
	pwbuf[n] = 0;
	while (i < n) {
		int k = 0, f, uid = 0;

		while (name[k] && pwbuf[i + k] == name[k]) k++;
		if (!name[k] && pwbuf[i + k] == ':') {
			for (f = i + k + 1; pwbuf[f] && pwbuf[f] != ':'; f++) ;	/* the password field */
			for (f++; pwbuf[f] >= '0' && pwbuf[f] <= '9'; f++) uid = uid * 10 + (pwbuf[f] - '0');
			return (uid);
		}
		while (i < n && pwbuf[i] != '\n') i++;
		i++;
	}
	return (-1);
}

static struct { u32 pid, port; char name[96]; } preg[64];	/* names registered by processes in their own domain (bootstrap_register2 with flags 1): the send rights stay here */
static struct svc *svc_add(const char *n, u32 port, int own);
static struct svc *svc_find(const char *n);

static void
job_load(const char *path)
{
	struct ljob *j;
	int fd, n, i, e;

	if (nljobs >= 32 || (fd = open(path, 0)) < 0) { out("launchd: cannot read the job "); out(path); out("\n"); return; }
	n = (int)read(fd, plbuf, sizeof(plbuf) - 1);
	close(fd);
	if (n <= 0) return;
	plbuf[n] = 0;
	j = &ljobs[nljobs];
	if ((i = pl_value_of(plbuf, "Label")) >= 0) pl_string(plbuf, i, j->label, sizeof(j->label));
	if ((i = pl_value_of(plbuf, "ProgramArguments")) >= 0) {
		e = pl_find(plbuf, i, "</array>");
		while (j->nargs < 7 && (i = pl_string(plbuf, i, j->args[j->nargs], sizeof(j->args[0]))) >= 0 && (e < 0 || i < e)) j->nargs++;
	} else if ((i = pl_value_of(plbuf, "Program")) >= 0 && pl_string(plbuf, i, j->args[0], sizeof(j->args[0])) >= 0)
		j->nargs = 1;
	if ((i = pl_value_of(plbuf, "UserName")) >= 0) pl_string(plbuf, i, j->user, sizeof(j->user));
	j->uid = j->user[0] ? user_uid(j->user) : 0;
	if ((i = pl_value_of(plbuf, "RunAtLoad")) >= 0) { int t = pl_find(plbuf, i, "<"); j->runatload = t >= 0 && plbuf[t] == 't'; }
	if (j->nargs == 0) { out("launchd: job without a program: "); out(path); out("\n"); return; }
	nljobs++;
	if ((i = pl_value_of(plbuf, "MachServices")) >= 0 && (i = pl_find(plbuf, i, "<dict>")) >= 0) {
		/* the keys at depth 1 are the names; a value may be a dictionary (HideUntilCheckIn, ResetAtClose) */
		int depth = 1;

		while (depth > 0 && plbuf[i]) {
			if (plbuf[i] != '<') { i++; continue; }
			if (pl_find(plbuf + i, 0, "<dict>") == 6) { depth++; i += 6; continue; }
			if (pl_find(plbuf + i, 0, "</dict>") == 7) { depth--; i += 7; continue; }
			if (depth == 1 && pl_find(plbuf + i, 0, "<key>") == 5) {
				char name[96]; int k = 0, hide = 0; u32 pp = 0; struct svc *ps;

				i += 5;
				while (plbuf[i] && plbuf[i] != '<' && k < 95) name[k++] = plbuf[i++];
				name[k] = 0;
				{	/* the value: a dictionary with HideUntilCheckIn true? (up to the next key at this depth) */
					int v = i, d = 0;

					while (plbuf[v]) {
						if (pl_find(plbuf + v, 0, "<dict>") == 6) d++;
						else if (pl_find(plbuf + v, 0, "</dict>") == 7) { if (d == 0) break; if (--d == 0) break; }
						else if (d == 0 && v > i + 6 && pl_find(plbuf + v, 0, "<key>") == 5) break;
						else if (d == 1 && pl_find(plbuf + v, 0, "<key>HideUntilCheckIn</key>") == 27) { int t = pl_find(plbuf, v + 27, "<"); hide = t >= 0 && plbuf[t] == 't'; }
						v++;
					}
				}
				if ((ps = svc_find(name)) != 0) { ps->job = nljobs; ps->hidden = hide; continue; }	/* a name made at the start (the fixed list): it now belongs to the job */
				if (mach_port_allocate(mach_task_self_, 1, &pp) == 0 && mach_port_insert_right(mach_task_self_, pp, pp, 20) == 0 && (ps = svc_add(name, pp, 0)) != 0) { ps->pre = 1; ps->job = nljobs; ps->hidden = hide; }
				continue;
			}
			i++;
		}
	}
	out("launchd: job "); out(j->label); out(" ("); out(j->args[0]); out(")\n");
}

static void
job_start(int idx, char **envp)
{
	struct ljob *j = &ljobs[idx];
	char *av[12];
	int na = 0, k, pid = 0;

	if (j->user[0] && j->uid != 0) { av[na++] = "/usr/sbin/daemon"; av[na++] = "-u"; av[na++] = j->user; }
	for (k = 0; k < j->nargs && na < 11; k++) av[na++] = j->args[k];
	av[na] = 0;
	j->started = posix_spawn(&pid, av[0], 0, 0, av, envp) == 0;
	out(j->started ? "launchd: started the job " : "launchd: cannot start the job "); out(j->label); out("\n");
}

static struct svc *
svc_find(const char *n)
{
	int i;

	for (i = 0; i < 256; i++)
		if (svcs[i].used && same(n, svcs[i].name)) return (&svcs[i]);
	return (0);
}

static struct svc *
svc_add(const char *n, u32 port, int own)
{
	int i, k;

	for (i = 0; i < 256; i++)
		if (!svcs[i].used) {
			for (k = 0; k < 95 && n[k]; k++) svcs[i].name[k] = n[k];
			svcs[i].name[k] = 0; svcs[i].used = 1; svcs[i].port = port; svcs[i].own = own;
			return (&svcs[i]);
		}
	return (0);
}

/*
 * The service com.apple.bsd.dirhelper: the per-user directories of libsystem_coreservices (confstr(_CS_DARWIN_USER_DIR, _TEMP_DIR, _CACHE_DIR)) live in
 * /var/folders/<2 characters>/<30 characters>/{0,T,C}.  The name is the UUID of the user (16 bytes: the compatibility prefix FFFFEEEE-DDDD-CCCC-BBBB-AAAA and the uid, as
 * membership gives it without a directory service) and the uid again (4 bytes, big endian), 20 bytes in all, written in base 32 with the alphabet below (5 bits at a
 * time, the most significant first).  Found by reading libsystem_coreservices and checked against the names that it asks for (root: zz/zyxvpxvq6csfxvn_n0000000000000).
 * The client asks the service to make the directories when they are missing: the message 0xb86d (no body); the answer is a MIG reply, id 0xb8d1, with the return code.
 */
static const char b32[] = "0123456789_bcdfghjklmnpqrstvwxyz";

static void
user_dir_name(u32 uid, char out[33])
{
	unsigned char b[20] = { 0xff, 0xff, 0xee, 0xee, 0xdd, 0xdd, 0xcc, 0xcc, 0xbb, 0xbb, 0xaa, 0xaa };
	int i, bit;

	for (i = 0; i < 4; i++)
		b[12 + i] = b[16 + i] = (unsigned char)(uid >> (24 - 8 * i));
	for (i = 0; i < 32; i++) {
		u32 v = 0;

		for (bit = 0; bit < 5; bit++) {
			int p = i * 5 + bit;

			v = (v << 1) | ((b[p / 8] >> (7 - p % 8)) & 1);
		}
		out[i] = b32[v];
	}
	out[32] = 0;
}

static void
make_user_dirs(u32 uid, u32 gid)
{
	char n[33], top[24], dir[64], sub[72];
	int i;
	const char *pre = "/var/folders/", *names[] = { "/0", "/T", "/C" };

	user_dir_name(uid, n);
	for (i = 0; pre[i]; i++) top[i] = pre[i];
	top[i++] = n[0]; top[i++] = n[1]; top[i] = 0;
	mkdir("/var/folders", 0755);
	mkdir(top, 0755);
	for (i = 0; top[i]; i++) dir[i] = top[i];
	dir[i++] = '/';
	{
		int k;

		for (k = 2; k < 32; k++) dir[i++] = n[k];
		dir[i] = 0;
	}
	mkdir(dir, 0700);
	chown(dir, uid, gid);
	chmod(dir, 0700);
	for (i = 0; i < 3; i++) {
		int j = 0, k;

		for (k = 0; dir[k]; k++) sub[j++] = dir[k];
		for (k = 0; names[i][k]; k++) sub[j++] = names[i][k];
		sub[j] = 0;
		mkdir(sub, 0700);
		chown(sub, uid, gid);
		chmod(sub, 0700);
	}
}

/* "/var/folders/XX/<30>/" + the subdirectory of which (0: "0", 1: "T", 2: "C") + "/" + the suffix + "/" -> out; the directories are made (mode 0700, owned by the user) when make is set */
static int
suffix_dir(u32 uid, u32 gid, u32 which, const char *suffix, u32 slen, int make, char *out, u32 outsz)
{
	char n[33];
	const char *sub = which == 1 ? "T" : which == 2 ? "C" : "0";
	u32 i, p = 0;

	if (which > 2 || slen == 0 || slen > 200)
		return (22);
	for (i = 0; i < slen; i++) {			/* the suffix is a relative path of plain components */
		char c = suffix[i];

		if (c == 0 && i == slen - 1) break;
		if (c == 0 || c == '\\' || c < 32) return (22);
		if (c == '/' && (i == 0 || suffix[i + 1] == '/')) return (22);
		if (c == '.' && (i == 0 || suffix[i - 1] == '/') && (suffix[i + 1] == '.' || suffix[i + 1] == '/' || suffix[i + 1] == 0)) return (22);
	}
	user_dir_name(uid, n);
	{
		const char *pre = "/var/folders/";

		for (i = 0; pre[i]; i++) out[p++] = pre[i];
	}
	out[p++] = n[0]; out[p++] = n[1]; out[p++] = '/';
	for (i = 2; i < 32; i++) out[p++] = n[i];
	out[p++] = '/'; out[p++] = sub[0]; out[p++] = '/';
	if (make)
		make_user_dirs(uid, gid);
	for (i = 0; i < slen && suffix[i]; i++) {
		if (p + 3 >= outsz) return (63);
		out[p++] = suffix[i];
		if (make && suffix[i + 1] == '/') {
			out[p] = 0; mkdir(out, 0700); chown(out, uid, gid); chmod(out, 0700);
		}
	}
	if (make) { out[p] = 0; mkdir(out, 0700); chown(out, uid, gid); chmod(out, 0700); }
	if (out[p - 1] != '/') out[p++] = '/';
	out[p] = 0;
	return (0);
}

/* the answer of a MIG routine: the return code only */
static void
mig_reply_code(const struct hdr *q, u32 code)
{
	u32 *w = (u32 *)&rp.h;

	if (q->remote == 0)
		return;
	rp.h.bits = 0x12;
	rp.h.remote = q->remote;
	rp.h.local = 0;
	rp.h.voucher = 0;
	rp.h.id = q->id + 100;
	rp.h.size = 36;
	w[6] = 0; w[7] = 1;				/* NDR record */
	w[8] = code;
	mach_msg(&rp.h, MACH_SEND_MSG, 36, 0, 0, MACH_MSG_TIMEOUT_NONE, 0);
}

static u32 svc_port;
static u32 dir_port;
static int debug_dump = 1;

static u32
tr_uid(const struct hdr *h)
{
	return (trailer_of(h)[6]);
}

int
main(int argc, char **argv, char **envp)
{
	u32 port = 0, set = 0;
	int kr;

	if ((kr = mach_port_allocate(mach_task_self_, 1, &port)) != 0 ||
	    (kr = mach_port_insert_right(mach_task_self_, port, port, 20)) != 0 ||
	    (kr = mach_port_allocate(mach_task_self_, 1, &dir_port)) != 0 ||
	    (kr = mach_port_allocate(mach_task_self_, 3, &set)) != 0 ||
	    (kr = mach_port_move_member(mach_task_self_, port, set)) != 0 ||
	    (kr = mach_port_move_member(mach_task_self_, dir_port, set)) != 0 ||
	    (kr = task_set_special_port(mach_task_self_, 4, port)) != 0) {
		out("launchd: no bootstrap port, error "); outx((u32)kr); out("\n");
		return (1);
	}
	svc_add("com.apple.bsd.dirhelper", dir_port, 1);
	{							/* the MachServices that the plist of the window server has without HideUntilCheckIn: they exist (and can be looked up) before the window server checks in */
		static const char *pre[] = { "com.apple.iohideventsystem", "com.apple.windowserver", "com.apple.PowerManagement.control", "com.apple.lsd.mapdb", "com.apple.lsd.modifydb", "com.apple.coreservices.launchservicesd", 0 };
		int pi;

		for (pi = 0; pre[pi]; pi++) {
			u32 pp = 0;

			if (mach_port_allocate(mach_task_self_, 1, &pp) == 0 && mach_port_insert_right(mach_task_self_, pp, pp, 20) == 0) {
				struct svc *ps = svc_add(pre[pi], pp, 0);

				if (ps) ps->pre = 1;
			}
		}
	}
	out("launchd: serving the bootstrap port\n");
	{
		/* the services of the system that are started with the launchd (on a Mac the plists of /System/Library/LaunchDaemons say which): the programs on the command line, or notifyd */
		static const char *dflt[] = { "/usr/sbin/notifyd", 0 };
		const char **progs = dflt;
		int i, n = argc > 1 ? argc - 1 : 1;
		static const char *given[40];

		if (argc > 1) {
			for (i = 0; i < argc - 1 && i < 39; i++) given[i] = argv[i + 1];
			given[i] = 0; progs = given;
		}
		for (i = 0; i < n && progs[i]; i++) {
			int pid = 0, na = 0, k;
			char buf[256], *av[8];

			for (k = 0; progs[i][k] && k < 255; k++) buf[k] = progs[i][k];
			buf[k] = 0;
			if (k > 6 && same(buf + k - 6, ".plist")) { job_load(buf); continue; }	/* a launchd property list: a job */
			for (k = 0; buf[k] && na < 7; ) {		/* "path arg arg": the words are separated by spaces */
				av[na++] = &buf[k];
				while (buf[k] && buf[k] != ' ') k++;
				if (buf[k] == ' ') buf[k++] = 0;
			}
			av[na] = 0;
			if (posix_spawn(&pid, av[0], 0, 0, av, envp) == 0) {
				int j;

				for (j = 0; j < 32 && jobs[j].pid; j++) ;
				if (j < 32) {
					const char *dm = na > 1 && same(av[na - 1], "agent") ? "Aqua" : "System";
					int q;

					jobs[j].pid = pid;
					for (q = 0; dm[q]; q++) jobs[j].domain[q] = dm[q];
					jobs[j].domain[q] = 0;
				}
				out("launchd: started "); out(progs[i]); out("\n");
			}
			else { out("launchd: cannot start "); out(progs[i]); out("\n"); }
		}
	}
	{
		int ji;

		for (ji = 0; ji < nljobs; ji++) if (ljobs[ji].runatload) job_start(ji, envp);
	}
	for (;;) {
		const u32 *tr;

		kr = mach_msg(&rq.h, MACH_RCV_MSG | MACH_RCV_LARGE | (3u << 24), 0, sizeof(rq), set, MACH_MSG_TIMEOUT_NONE, 0);
		if (kr != 0) {
			out("launchd: receive error "); outx((u32)kr); out("\n");
			continue;
		}
		tr = (const u32 *)(rq.raw + ((rq.h.size + 3) & ~3u));	/* the trailer: type, size, seqno, security token (2 words), audit token (auid euid egid ruid rgid pid ...) */
		if (rq.h.local == dir_port) {			/* the dirhelper: the directories of the sender */
			out("launchd: dirhelper request "); outx(rq.h.id); out(" uid "); outx(tr[6]); out("\n");
			if (rq.h.id != 0xb86d) {
				u32 i;

				out("launchd:   size "); outx(rq.h.size); out(":");
				for (i = 0; i < rq.h.size / 4 && i < 40; i++) { out(" "); outx(((u32 *)&rq.h)[i]); }
				out("\n");
			}
			if (rq.h.id == 0xb86d) {
				make_user_dirs(tr[6], tr[7]);
				mig_reply_code(&rq.h, 0);
			} else if (rq.h.id == 0xb872 && rq.h.size >= 48) {		/* (which, flags, suffix: a string, size of the answer): the directory of the process in the user directory, made if need be; the answer has its path */
				const u32 *w = (const u32 *)&rq.h;
				u32 which = w[8], slen = w[11], plen, i;
				static char path[1100];
				int rc = rq.h.size >= 48 + ((slen + 3) & ~3u) - 4 ? suffix_dir(tr[6], tr[7], which, (const char *)&w[12], slen, (w[9] & 0xff) != 0, path, 1024) : 22;

				if (rc != 0) { mig_reply_code(&rq.h, 0x10000000 | rc); continue; }
				for (plen = 0; path[plen]; plen++)
					;
				plen++;
				{
					u32 *x = (u32 *)&rp.h;
					u32 pad = (plen + 3) & ~3u;

					rp.h.bits = 0x12; rp.h.remote = rq.h.remote; rp.h.local = 0; rp.h.voucher = 0; rp.h.id = rq.h.id + 100;
					x[6] = 0; x[7] = 1;
					x[8] = 0;		/* RetCode */
					x[9] = 0;		/* the offset of the string */
					x[10] = plen;		/* its length with the NUL */
					for (i = 0; i < pad; i++) ((char *)&x[11])[i] = i < plen ? path[i] : 0;
					x[11 + pad / 4] = 0;	/* the out int that follows */
					rp.h.size = (11 + pad / 4 + 1) * 4;
					mach_msg(&rp.h, MACH_SEND_MSG, rp.h.size, 0, 0, MACH_MSG_TIMEOUT_NONE, 0);
				}
			} else
				mig_reply_code(&rq.h, 0x47);	/* MIG_BAD_ID-like */
			continue;
		}
		out("launchd: request "); outx(rq.h.id); out(" from pid "); outx(trailer_of(&rq.h)[10]); out("\n");
		if (xpc_start(&rq.h) == 0) {			/* not an XPC message that is in the message itself (the payload may be out of line): an error answer, so that the sender does not wait for ever */
			out("launchd:   (the body is not in the message: an error answer)\n");
			reply(&rq.h, 1, 3);
			continue;
		}
		if (debug_dump) xpc_dump(&rq.h, xpc_start(&rq.h));
		if ((rq.h.id & 0xfff) == 0x12d) {		/* vproc_swap_*: a property of the job or of the manager (VPROC_GSK_*) */
			const u32 *g = xpc_find(&rq.h, xpc_start(&rq.h), "outgsk");

			if (g && (*g >> 12) == 4 && g[1] == 6 && g[2] == 0 /* VPROC_GSK_MGR_NAME */) {
				xbegin(&rq.h, 0); xdict(); xstr("out", domain_of(trailer_of(&rq.h)[10])); vouch(&rq.h); xsend();
			} else
				reply(&rq.h, 1, 45);	/* not supported */
			continue;
		}
		if ((rq.h.id & 0xfff) == 0x2c8) {		/* xpc_connection attrs of a process (BoardServices asks for the peer of a connection): an empty set of attributes */
			xbegin(&rq.h, 0); xdict(); xemptydict("attrs"); xsend();
			continue;
		}
		{
			const u32 *v = xpc_find(&rq.h, xpc_start(&rq.h), "name");

			if (v != 0 && (*v >> 12) == 9) {
				out("launchd:   name "); out((const char *)(v + 2)); out("\n");
				{
					u32 rt = rq.h.id & 0xfff;
					struct svc *sv = svc_find((const char *)(v + 2));

					if ((rt == 0xce || rt == 0x325) && sv != 0 && sv->pre && !sv->claimed && (sv->job == 0 || tr_uid(&rq.h) == 0 || (int)tr_uid(&rq.h) == ljobs[sv->job - 1].uid)) {	/* a job's name: its user only */	/* the check-in of a service that was made at the start: the receive right goes to it */
						sv->claimed = 1;
						reply_port(&rq.h, sv->port, 16);
						continue;
					}
					if (rt == 0xce || rt == 0x325) {		/* check-in (bootstrap_check_in: 0xce; the listener of an XPC connection: 0x325): the service gets the receive right of its port */
						u32 ty = 0;

						/* the program that held the receive right exited: our send right is a dead name.  launchd of the Mac gets the right back (port-destroyed
						 * notification) and hands it to the next instance of the job; here the next instance gets a new port, and clients that held the old one look it up again */
						if (sv != 0 && (tr_uid(&rq.h) == 0 || (sv->job && (int)tr_uid(&rq.h) == ljobs[sv->job - 1].uid)) && mach_port_type(mach_task_self_, sv->port, &ty) == 0 && (ty & 0x100000)) {
							u32 p = 0;

							if (mach_port_allocate(mach_task_self_, 1, &p) == 0 && mach_port_insert_right(mach_task_self_, p, p, 20) == 0) {
								out("launchd:   check-in again (the previous instance exited)\n");
								sv->port = p; sv->claimed = 1;
								reply_port(&rq.h, sv->port, 16);
								continue;
							}
						}
						if (sv == 0 && tr_uid(&rq.h) == 0) {
							u32 p = 0;

							if (mach_port_allocate(mach_task_self_, 1, &p) == 0 && mach_port_insert_right(mach_task_self_, p, p, 20) == 0)
								sv = svc_add((const char *)(v + 2), p, 0);
							if (sv) { reply_port(&rq.h, sv->port, 16); continue; }
						}
						reply(&rq.h, 1, 1);		/* known already, or not allowed: BOOTSTRAP_NOT_PRIVILEGED-like */
						continue;
					}
					if ((rt >= 0x320 || rt == 0xcf) && sv && sv->hidden && !sv->claimed) sv = 0;	/* HideUntilCheckIn: not there yet */
					if ((rt >= 0x320 || rt == 0xcf) && sv && sv->job) {	/* a job's name: start the job on demand; a job that exited gets a new port (the messages wait in it until the check-in) */
						u32 ty = 0, p = 0;

						if (sv->claimed && mach_port_type(mach_task_self_, sv->port, &ty) == 0 && (ty & 0x100000) &&
						    mach_port_allocate(mach_task_self_, 1, &p) == 0 && mach_port_insert_right(mach_task_self_, p, p, 20) == 0) {
							sv->port = p; sv->claimed = 0; ljobs[sv->job - 1].started = 0;
						}
						if (!ljobs[sv->job - 1].started) job_start(sv->job - 1, envp);
					}
					if (rt == 0xcc) {		/* register (bootstrap_register2): a name for a send right that the process gives; HIServices registers its accessibility server port as
								 * com.apple.axserver in the domain of the process (flags 1) and drops its own send right, so the right must stay here: without it the port has no sender,
								 * the no-more-senders notification comes back at once and the run loop of the app re-arms it for ever (the real Dock spun at 100% CPU) */
						const u32 *w = (const u32 *)&rq.h;
						u32 pid = trailer_of(&rq.h)[10], port = (rq.h.bits & 0x80000000u) && w[6] >= 1 ? w[7] : 0, i, slot = 64;

						if (port != 0) {
							for (i = 0; i < 64; i++) {
								if (preg[i].port != 0 && preg[i].pid == pid && same(preg[i].name, (const char *)(v + 2))) { slot = i; break; }
								if (preg[i].port == 0 && slot == 64) slot = i;
							}
							if (slot < 64) {
																preg[slot].pid = pid; preg[slot].port = port; { const char *nm = (const char *)(v + 2); int k; for (k = 0; k < 95 && nm[k]; k++) preg[slot].name[k] = nm[k]; preg[slot].name[k] = 0; }
								out("launchd:   registered for pid "); outx(pid); out("\n");
								reply(&rq.h, 0, 0);
								continue;
							}
						}
						reply(&rq.h, 1, 3);
						continue;
					}
					if ((rt >= 0x320 || rt == 0xcf) && !sv) {	/* a name that a process registered in its own domain: found by its look-ups */
						u32 pid = trailer_of(&rq.h)[10], i;

						for (i = 0; i < 64; i++)
							if (preg[i].port != 0 && preg[i].pid == pid && same(preg[i].name, (const char *)(v + 2))) break;
						if (i < 64) { reply_port(&rq.h, preg[i].port, 19); continue; }
					}
					if ((rt >= 0x320 || rt == 0xcf) && sv) {	/* look-up */
						reply_port(&rq.h, sv->port, sv->own ? 20 : 19);
						continue;
					}
					if (rt >= 0x320 || rt == 0xcf) { out("launchd:   NOT FOUND: "); out((const char *)(v + 2)); out("\n"); }
				}
			}
		}
		out("launchd: no handler for "); outx(rq.h.id); out(", error answer\n");
		reply(&rq.h, (rq.h.id & 0xfff) >= 0x320, 3);
	}
}
