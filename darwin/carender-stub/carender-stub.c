/* A stand-in for com.apple.CARenderServer, only to see what a client (the Dock) sends it: it takes the service with bootstrap_check_in, receives every message on the port and prints the header and the first
 * bytes of the body to stderr (a complex message: also its descriptors).  It sends no answer.  It is the first step of the replacement of the CoreAnimation render server of the WindowServer. */
typedef unsigned int u32;
typedef unsigned long u64;
extern u32 bootstrap_port;
extern int bootstrap_check_in(u32 bp, const char *name, u32 *port);
extern int getpid(void);
extern long time(long *t);
extern int mach_msg(void *msg, int opt, u32 ssz, u32 rsz, u32 rname, u32 to, u32 notify);
extern int mach_port_allocate(u32 task, u32 right, u32 *name);
extern int mach_port_insert_right(u32 task, u32 name, u32 poly, u32 how);
extern int mach_port_destroy(u32 task, u32 name);
extern int mach_port_insert_member(u32 task, u32 name, u32 pset);
extern u32 mach_task_self_;
extern int mach_vm_map(u32 task, u64 *addr, u64 size, u64 mask, int flags, u32 object, u64 offset, int copy, int cur, int max, int inherit);
extern int mach_vm_deallocate(u32 task, u64 addr, u64 size);
extern long write(int fd, const void *buf, unsigned long n);

static void out(const char *s) { unsigned long n = 0; while (s[n]) n++; write(2, s, n); }
extern int open(const char *, int, ...);
extern char *getenv(const char *);
extern int socket(int, int, int);
extern int connect(int, const void *, unsigned int);
extern void *signal(int, void *);
extern int close(int);
/* the commits also go to WindowServer (the render server of the window server: src/sls_carender.c) on a stream socket: records u32 type, u32 length, payload (1: a commit message, 2: u32 message number, u32 descriptor and the out-of-line bytes) */
static int cafd = -1;
static void ca_connect(void)
{
	struct { unsigned char len, family; char path[104]; } sa;
	const char *p = "/tmp/WindowServer-ca.sock";
	unsigned k;

	if (cafd >= 0) return;
	for (k = 0; k < sizeof(sa); k++) ((char *)&sa)[k] = 0;
	for (k = 0; p[k]; k++) sa.path[k] = p[k];
	sa.len = sizeof(sa); sa.family = 1;
	cafd = socket(1, 1, 0);			/* AF_UNIX, SOCK_STREAM */
	if (cafd >= 0 && connect(cafd, &sa, sizeof(sa)) != 0) { close(cafd); cafd = -1; }
}

/* the shared memory of a commit (CA::Shmem: the pixels of an Image) arrives as a memory-entry port: map it, and send the bytes like out-of-line data.  The size of an entry is not asked of anybody (the
 * render server knows it from the object that names the memory): the biggest size that mach_vm_map accepts is the size of the entry, found by doubling and halving. */
static int map_entry(u32 port, u64 *addr, u64 *size)
{
	u64 lo = 0, hi = 4096, a = 0;

	while (hi <= (64ul << 20) && mach_vm_map(mach_task_self_, &a, hi, 0, 1, port, 0, 0, 3, 3, 1) == 0) { mach_vm_deallocate(mach_task_self_, a, hi); lo = hi; hi *= 2; a = 0; }
	if (lo == 0) return -1;
	while (hi - lo > 4096) {
		u64 mid = (lo + (hi - lo) / 2) & ~4095ul;

		a = 0;
		if (mid > lo && mach_vm_map(mach_task_self_, &a, mid, 0, 1, port, 0, 0, 3, 3, 1) == 0) { mach_vm_deallocate(mach_task_self_, a, mid); lo = mid; }
		else hi = mid > lo ? mid : hi - 4096;
	}
	a = 0;
	if (mach_vm_map(mach_task_self_, &a, lo, 0, 1, port, 0, 0, 3, 3, 1) != 0) return -1;
	*addr = a; *size = lo;
	return 0;
}
static void ca_send(u32 type, const void *a, u32 alen, const void *b, u32 blen)
{
	u32 h[2];
	const char *parts[3]; u32 lens[3]; int i;

	ca_connect();
	if (cafd < 0) return;
	h[0] = type; h[1] = alen + blen;
	parts[0] = (const char *)h; lens[0] = 8; parts[1] = a; lens[1] = alen; parts[2] = b; lens[2] = blen;
	for (i = 0; i < 3; i++) {
		u32 done = 0;
		while (done < lens[i]) {
			long w = write(cafd, parts[i] + done, lens[i] - done);
			if (w <= 0) { close(cafd); cafd = -1; return; }	/* WindowServer went away: the next commit connects again */
			done += (u32)w;
		}
	}
}
static void hex(u64 v, int digits) { char b[20]; int i; for (i = 0; i < digits; i++) b[i] = "0123456789abcdef"[(v >> (4 * (digits - 1 - i))) & 15]; b[digits] = 0; out(b); }

/*
 * CASRegisterClient (id 40202 = 0x9d0a), as the QuartzCore of the Mac sends it and reads the answer (found in its code): the request is complex with five port descriptors; the answer (id 0x9d6e) is complex
 * with two send rights, then NDR(8), two numbers, the offset and the count of a byte array (at most 64 bytes, padded to 4) and one more number at the end: size 0x50 + the padded count.
 */
static void
answer_register(u32 *buf, u32 p1, u32 p2, u32 n1, u32 n2, u32 last)
{
	u32 rep[32];
	int i;

	for (i = 0; i < 32; i++) rep[i] = 0;
	rep[0] = 0x80000012; rep[1] = 0x50; rep[2] = buf[2]; rep[5] = 0x9d6e;
	rep[6] = 2;
	rep[7] = p1; rep[8] = 0; rep[9] = 0x00130000;			/* the descriptors: a send right each (copied) */
	rep[10] = p2; rep[11] = 0; rep[12] = 0x00110000;		/* __XRegisterClient: the first descriptor is a copy of the send right of the server (0x13), the second a MOVE_SEND of a new port of this client (0x11) */
	rep[15] = n1; rep[16] = n2;					/* after the NDR (words 13, 14): the two numbers */
	rep[17] = 0; rep[18] = 0;					/* offset, count of the array: empty */
	rep[19] = last;
	mach_msg(rep, 1, 0x50, 0, 0, 0, 0);
}

/* After CASRegisterClient the real render server sends three one-way messages to the client's port (captured on a Mac with SwiftUI/tools/msgtrace.c): ids 0x9dd0, 0x9dd1 (size 36, body 0, 1, 0)
 * and 0x9dd2 (size 40, body 0, 1, 0, 0).  The meaning is not known (the QuartzCore of the client does not commit before them?); which of the two real ports of the request is the client's is not
 * known either, so both get them.  Sent with a copy of the send right (bits 0x13). */
static void
send_registered(u32 port)
{
	u32 m[10];
	int i, k;

	if (port == 0) return;
	for (k = 0; k < 3; k++) {
		for (i = 0; i < 10; i++) m[i] = 0;
		m[0] = 0x13; m[1] = k == 2 ? 40 : 36; m[2] = port; m[5] = 0x9dd0 + k;
		m[7] = 1;							/* body: 0, 1, 0 (and a fourth 0 for the third message) */
		mach_msg(m, 1 | 0x80 /* MACH_SEND_TIMEOUT */, m[1], 0, 0, 100, 0);
	}
	out("carender-stub: sent the three notifications to port "); hex(port, 8); out("\n");
}

/* The messages of the real render server to the fence observer port of a client (CA::Render::Fence::Transaction::Observer::activate and Fence::extract_msg of QuartzCore 26.7.2, REA):
 * complex, one out-of-line descriptor = an array of u64 fence ids.  id 'cfmr' (0x726d6663), size 0x34: after the descriptor two u32 (a time-stamp pair); the client then sends its deferrals (ids 40004 / 40005).
 * id 'gflr' (0x726c6667), size 0x48: after the descriptor 12 bytes, a u64 at 0x38 and a double at 0x40 (the time); it removes the ids from the pending set of the observer and so ends the fence. */
static unsigned long long fence_ids[16];
static unsigned long long fence_names[64];
static unsigned long long pend[16][8], reply_names[8];		/* the names of the fences of every transaction that was applied and whose observer port has not come back with a commit yet (first in, first out) */
static u32 pendn[16], pendp[16], npend, pendhead;


static int
send_fence_msg(u32 port, u32 id, unsigned long long *ids, u32 nids)
{
	u32 m[18];
	int i;

	for (i = 0; i < 18; i++) m[i] = 0;
	m[0] = 0x80000013; m[1] = id == 0x726d6663 ? 52 : 72; m[2] = port; m[5] = id;
	m[6] = 1; *(unsigned long *)&m[7] = (unsigned long)ids; m[9] = 0x01000100; m[10] = nids * 8;
	if (id == 0x726d6663) m[11] = 0;			/* the pid of the server at 0x2c (send_rmfc_msg); 0 and 0 match every deferred commit in CA::Context::send_deferrals */
	return mach_msg(m, 1 | 0x80, m[1], 0, 0, 100, 0);
}

/* 'gflr' as the Mac's client receives it (captured with SwiftUI/tools/msgtrace.c, dispatch_mach_msg_get_msg): complex, size 0x48, two descriptors: the out-of-line names (u64) and a MOVE_SEND port (a fence port for the deferred commits),
 * a u64 at 0x38 and the time (a double, seconds of the host clock) at 0x40.  It is this message, not 'cfmr', that makes the client send its deferred commits (40005 / 40004). */
static int
send_gflr(u32 port, unsigned long long *ids, u32 nids)
{
	u32 m[18], fp = 0;
	static double t0;
	int i;
	double now;

	if (t0 == 0) t0 = 100000.0 - (double)time(0);
	now = t0 + (double)time(0);
	for (i = 0; i < 18; i++) m[i] = 0;
	mach_port_allocate(mach_task_self_, 1, &fp);
	mach_port_insert_right(mach_task_self_, fp, fp, 20);
	m[0] = 0x80000013; m[1] = 72; m[2] = port; m[5] = 0x726c6667; m[6] = 2;
	*(unsigned long *)&m[7] = (unsigned long)ids; m[9] = 0x01000100; m[10] = nids * 8;
	m[11] = fp; m[13] = 0x00110000;
	*(double *)&m[16] = now;
	return mach_msg(m, 1 | 0x80, 72, 0, 0, 100, 0);
}

/* 'cfmr' goes out about 0.5 s after the answer of ApplyFenceTransaction on the Mac (ftimeout_callback of the server, armed in applyTransaction); sent at once the client has no deferred commit yet and ignores it */
static struct { u32 port, n; unsigned long long ids[8]; long due; } delayq[16];
static u32 ndelay;

static void
flush_delayed(void)
{
	u32 i, j;
	long now = time(0);

	for (i = 0; i < ndelay; ) {
		if (delayq[i].due > now) { i++; continue; }
		{
			int k1 = send_gflr(delayq[i].port, delayq[i].ids, delayq[i].n);

			out("carender-stub: gflr sent, kr "); hex((u32)k1, 8); out("\n");
		}
		for (j = i + 1; j < ndelay; j++) delayq[j - 1] = delayq[j];
		ndelay--;
	}
}

static u32 fences[64], nfence;

int
main(void)
{
	static u32 buf[4096];
	u32 port = 0, set = 0, p1 = 0, p2 = 0, i, n;
	int kr = bootstrap_check_in(bootstrap_port, "com.apple.CARenderServer", &port);

	signal(13, (void *)1);			/* SIGPIPE ignored: WindowServer may go away */
	out("carender-stub: check-in "); hex((u32)kr, 8); out(" port "); hex(port, 8); out("\n");
	mach_port_allocate(mach_task_self_, 3 /* PORT_SET */, &set);
	mach_port_insert_member(mach_task_self_, port, set);
	mach_port_allocate(mach_task_self_, 1, &p1); mach_port_insert_right(mach_task_self_, p1, p1, 20); mach_port_insert_member(mach_task_self_, p1, set);
	mach_port_allocate(mach_task_self_, 1, &p2); mach_port_insert_right(mach_task_self_, p2, p2, 20); mach_port_insert_member(mach_task_self_, p2, set);
	/* CARENDER_TRACE=1: every message and its words go to the output, the command streams to /tmp/ca.bin and the out-of-line data to /tmp/ca-ool-*.bin (a debugging aid: on a machine with a small /tmp it fills it in seconds, and the output of every commit is a lot of work) */
	int trace = getenv("CARENDER_TRACE") != 0;
	for (;;) {
		kr = mach_msg(buf, 2 | (ndelay ? 0x100 : 0) | 0x100000 /* RCV_MSG | RCV_TIMEOUT (while a cfmr is waiting) | RCV_LARGE */ | 0x03000000 /* the audit trailer: who sent it */, 0, sizeof(buf), set, ndelay ? 200 : 0, 0);
		flush_delayed();
		if (kr == 0x10004003) continue;
		if (kr != 0) { out("carender-stub: receive error "); hex((u32)kr, 8); out("\n"); continue; }
		if (buf[5] == 0x0ca0c7e1 && buf[2] != 0) {	/* the window server asks for a render context of its own (on the Mac WindowServer calls CARenderContextNew, CARenderContextGetId and
							 * CARenderContextGetServerPort for a client's SLSCreateLayerContext (decompiled __XCreateLayerContext of SkyLight) and the client commits to that port): here wsbridge asks
							 * this server, which makes a port for the context, receives on it with the others (the commits carry the context id) and answers: a send right to it + the id */
			static u32 ctx_seed = 0x62c37b8b;
			u32 cp = 0, rp2[11], ctx;

			ctx_seed = ctx_seed * 1103515245u + 12345u;
			ctx = ctx_seed | 1;
			if (mach_port_allocate(mach_task_self_, 1, &cp) == 0 && mach_port_insert_member(mach_task_self_, cp, set) == 0) {
				rp2[0] = 0x80000012; rp2[1] = 44; rp2[2] = buf[2]; rp2[3] = 0; rp2[4] = 0; rp2[5] = buf[5] + 100;
				rp2[6] = 1;				/* one descriptor */
				rp2[7] = cp; rp2[8] = 0; rp2[9] = (20u << 16);	/* port descriptor: MAKE_SEND of the context's port, type 0 */
				rp2[10] = ctx;
				mach_msg(rp2, 1, 44, 0, 0, 0, 0);
				out("carender-stub: render context "); hex(ctx, 8); out(" on port "); hex(cp, 8); out("\n");
			}
			continue;
		}
		if (trace) { out("carender-stub: msg id "); hex(buf[5], 8); out(" size "); hex(buf[1], 8); out(" bits "); hex(buf[0], 8); out(" remote "); hex(buf[2], 8); out(" local "); hex(buf[3], 8); out("\n  "); }
		if (buf[5] >= 0x9c42 && buf[5] <= 0x9c45) {	/* the command streams are kept whole in /tmp/ca.bin: a u32 length, then the message */
			int cf = trace ? open("/tmp/ca.bin", 0x209, 0644) : -1;

			if (cf >= 0) { write(cf, &buf[1], 4); write(cf, buf, buf[1]); close(cf); }
			if (buf[0] & 0x80000000) {		/* the out-of-line data of the commit (an Image's pixels...): /tmp/ca-ool-<message>-<descriptor>.bin */
				static u32 nmsg;
				u32 off = 28, k;

				for (k = 0; k < buf[6] && off + 16 <= buf[1]; k++) {
					unsigned char *dsc = (unsigned char *)buf + off;

					if (dsc[11] == 1) {
						unsigned long ad = *(unsigned long *)dsc;
						u32 sz = *(u32 *)(dsc + 12);
						char nm[40] = "/tmp/ca-ool-00-0.bin";
						int of;

						nm[12] = '0' + (nmsg / 10) % 10; nm[13] = '0' + nmsg % 10; nm[15] = '0' + k;
						of = trace ? open(nm, 0x601, 0644) : -1;
						if (of >= 0) { write(of, (void *)ad, sz); close(of); }
						{ u32 hd[2]; hd[0] = nmsg; hd[1] = k; ca_send(2, hd, 8, (void *)ad, sz); }
						off += 16;
					} else {
						if (dsc[11] == 0 && *(u32 *)dsc != 0 && (buf[5] != 0x9c42 || dsc[10] != 0x12)) {	/* a port: a memory entry (the shared memory of an Image)? */
							u64 ad = 0, sz = 0;

							int mr = map_entry(*(u32 *)dsc, &ad, &sz);

							if (trace) { out("carender-stub: port descriptor "); hex(k, 4); out(" name "); hex(*(u32 *)dsc, 8); out(mr == 0 ? " mapped, size " : " is not a memory entry "); hex((u32)sz, 8); out("\n"); }
							if (mr == 0) {
								u32 hd[2]; hd[0] = nmsg; hd[1] = k;
								if (trace) {	/* the first bytes of every shared memory of a commit: /tmp/ca-shm-<message>-<descriptor>.bin (the layout of an image queue was found this way) */
									char nm2[40] = "/tmp/ca-shm-00-0.bin";
									int of2;

									nm2[12] = '0' + (nmsg / 10) % 10; nm2[13] = '0' + nmsg % 10; nm2[15] = '0' + k;
									of2 = open(nm2, 0x601, 0644);
									if (of2 >= 0) { write(of2, (void *)ad, sz > 0x1000 ? 0x1000 : sz); close(of2); }
								}
								ca_send(2, hd, 8, (void *)ad, (u32)sz);
								mach_vm_deallocate(mach_task_self_, ad, sz);
							}
						}
						off += 12;
					}
				}
				nmsg++;
			}
			{	/* with the pid of the sender (word 10 of the audit trailer, after the message): WindowServer keeps a scene and a window for each client */
				u32 tr = (buf[1] + 3) / 4, pid[1];

				pid[0] = tr + 10 < sizeof(buf) / 4 ? buf[tr + 10] : 0;
				ca_send(3, pid, 4, buf, buf[1]);
			}
		}
		n = buf[1] / 4; if (n > 4000) n = 4000;
		if (trace) { for (i = 6; i < n; i++) { hex(buf[i], 8); out(i % 8 == 5 ? "\n  " : " "); }
		out("\n"); }
		{
			/* the simple answers (found in the code of QuartzCore that sends the request and reads the answer): the id of the request, the size of the answer, the numbers that follow the return code */
			static const struct { u32 id, size, v[4]; } simple[] = {
				{ 0x9d3e, 0x28, { 16, 0, 0, 0 } },			/* CARenderServerGetNeededAlignment */
				{ 0x9d51, 0x30, { 16384, 16384, 0, 0 } },		/* CARenderServerGetMaxRenderableIOSurfaceSize */
			};
			u32 k, rp[16];

			for (k = 0; k < sizeof(simple) / sizeof(simple[0]); k++)
				if (simple[k].id == buf[5]) {
					for (i = 0; i < 16; i++) rp[i] = 0;
					rp[0] = 0x12; rp[1] = simple[k].size; rp[2] = buf[2]; rp[5] = buf[5] + 100;
					for (i = 0; i < 4 && 9 + i < simple[k].size / 4; i++) rp[9 + i] = simple[k].v[i];
					mach_msg(rp, 1, simple[k].size, 0, 0, 0, 0);
					out("carender-stub: answered "); hex(buf[5], 8); out("\n");
				}
		}
		if (buf[5] >= 0x9c42 && buf[5] <= 0x9c45 && (buf[0] & 0x80000000) && buf[6] >= 1) {	/* a commit with a send-once right among its descriptors (the client waits on it: no new commit before the answer): an empty message to it.  The descriptors are 12 bytes (port) or 16 (out of line memory) */
			u32 k, off = 7;		/* word index of the first descriptor */
			u32 obs[8], nobs = 0, nm = 0;


			for (k = 0; k < buf[6] && off + 3 <= 64; k++) {
				u32 type = (buf[off + 2] >> 24) & 0xff, disp = (buf[off + 2] >> 16) & 0xff;

				if (type == 0 && disp == 0x12) {
					u32 ack[6], kr2;

					ack[0] = 0x12; ack[1] = 24; ack[2] = buf[off]; ack[3] = 0; ack[4] = 0; ack[5] = 0;
					kr2 = mach_msg(ack, 1, 24, 0, 0, 0, 0);
					out("carender-stub: commit acknowledged "); hex((u32)kr2, 8); out("\n");
				}
				if (type == 0 && disp == 0x11) {		/* a port of the commit: a fence that this stub made (its name goes to the observers below), or the observer port of the client */
					u32 n;
					int mine = 0;

					for (n = 0; n < nfence; n++)
						if (fences[n] == buf[off]) { mine = 1; fences[n] = 0; if (nm < 16) fence_ids[nm++] = fence_names[n]; }
					if (mine) {
						out("carender-stub: fence completed "); hex(buf[off], 8); out("\n");
						mach_port_destroy(mach_task_self_, buf[off]);		/* the right is given up: a client that waits for the last sender to go (or for the dead name) goes on */
					} else if (nobs < 8) obs[nobs++] = buf[off];
				}
				off += type == 1 ? 4 : 3;
			}
			/* what the real server does with the names (send_rmfc_msg / send_rlfg_msg of QuartzCore): the observer ports of the client get 'cfmr' (the client sends its deferred commits) and then 'gflr' (the fences are released) */
			for (k = 0; k < nobs; k++) {
				unsigned long long *ids = fence_ids;
				u32 cnt = nm;
				int k1, k2;

				u32 pp = 0;

				if (npend > 0) { ids = pend[pendhead]; cnt = pendn[pendhead]; pp = pendp[pendhead]; pendhead = (pendhead + 1) % 16; npend--; }
				if (cnt == 0) continue;
				k1 = 0; k2 = 0; (void)pp;
				out("carender-stub: fence messages sent to "); hex(obs[k], 8); out(" names "); hex(cnt, 1); out(" kr "); hex((u32)k1, 8); out(" "); hex((u32)k2, 8); out("\n");
			}
			for (k = 0; k < nobs; k++) { out("carender-stub: port of a commit released "); hex(obs[k], 8); out("\n"); mach_port_destroy(mach_task_self_, obs[k]); }
		}
		if (buf[5] == 0x9d11 && buf[2] != 0) {			/* ApplyFenceTransaction (__XApplyFenceTransaction of QuartzCore: the request has 2 descriptors, an array of ports and an array of u32; the answer is complex, size 0x70, 4 descriptors: two out-of-line arrays (u64 names, u32 ids: empty here), the receive right of a new port (the client's fence observer listens on it: Observer::activate -> dispatch_mach_connect) and a send right to it (the client puts it into its commits, the server writes 'cfmr' / 'gflr' to it), NDR, four u32) */
			u32 op = 0, am[28];

			u32 nn = 0, pc = buf[10], q;
			u32 *pl = (u32 *)*(unsigned long *)&buf[7];
			unsigned long long *slot = npend < 16 ? pend[(pendhead + npend) % 16] : reply_names;

			if (buf[6] >= 1 && pl != 0) for (q = 0; q < pc && q < buf[17] && nn < 8; q++) {
				u32 n;
				for (n = 0; n < nfence; n++) if (fences[n] == pl[q] && fences[n] != 0) { slot[nn++] = fence_names[n]; break; }
			}
			if (slot != reply_names) { pendp[(pendhead + npend) % 16] = 0; pendn[(pendhead + npend++) % 16] = nn; }
			for (q = 0; q < nn; q++) reply_names[q] = slot[q];
			mach_port_allocate(mach_task_self_, 1, &op);
			mach_port_insert_right(mach_task_self_, op, op, 20);
			mach_port_insert_right(mach_task_self_, op, op, 20);		/* a second send right: the stub keeps it to write to the observer, the first one is moved to the client */
			for (i = 0; i < 28; i++) am[i] = 0;
			am[0] = 0x80000012; am[1] = 0x70; am[2] = buf[2]; am[5] = 0x9d11 + 100; am[6] = 4;
			*(unsigned long *)&am[7] = (unsigned long)reply_names; am[9] = 0x01000100; am[10] = nn * 8;	/* the names of the fences of the transaction (not deallocated: the memory is ours) */
			{	/* the u32 array of the answer: the ids of the contexts, grouped per fence and ended by 0, then those of the batch (the request has them in the same shape: echoed) */
				static u32 idecho[64];
				u32 *src = (u32 *)*(unsigned long *)&buf[11], cnt = buf[19] < 64 ? buf[19] : 64, z;

				if (buf[6] >= 2 && src != 0) for (z = 0; z < cnt; z++) idecho[z] = src[z]; else cnt = 0;
				*(unsigned long *)&am[11] = (unsigned long)idecho; am[13] = 0x01000100; am[14] = cnt * 4; am[24] = cnt;
			}
			am[23] = nn;						/* the count of the names, again as a number (0x5c: __XApplyFenceTransaction writes the count of the array there, the client reads it) */
			am[15] = op; am[17] = 0x00100000;			/* MOVE_RECEIVE */
			am[18] = 0; am[20] = 0x00110000;			/* the batch port: none (no batch: the transaction has only fences) */
			am[22] = 1;						/* NDR */
			mach_msg(am, 1, 0x70, 0, 0, 0, 0);
			out("carender-stub: answered the fence transaction, observer port "); hex(op, 8); out("\n");
			if (nn > 0 && ndelay < 16) {		/* cfmr later (see flush_delayed) */
				u32 z;

				delayq[ndelay].port = op; delayq[ndelay].n = nn; delayq[ndelay].due = time(0);
				for (z = 0; z < nn; z++) delayq[ndelay].ids[z] = reply_names[z];
				ndelay++;
				if (npend > 0) pendp[(pendhead + npend - 1) % 16] = op;
			}
		}
		if (buf[5] == 0x9d10 && buf[2] != 0) {			/* CASCreateFencePort: the answer carries a send right to a new port (a fence: nothing waits on it here) */
			u32 fp = 0, fm[16];

			mach_port_allocate(mach_task_self_, 1, &fp);
			mach_port_insert_right(mach_task_self_, fp, fp, 20);
			mach_port_insert_member(mach_task_self_, fp, set);
			if (nfence < 64) { fence_names[nfence] = 0x1000000000ULL + nfence + 1; fences[nfence++] = fp; }
			for (i = 0; i < 16; i++) fm[i] = 0;
			fm[0] = 0x80000012; fm[1] = 0x38; fm[2] = buf[2]; fm[5] = 0x9d10 + 100;
			fm[6] = 1; fm[7] = fp; fm[8] = 0; fm[9] = 0x00130000;
			fm[10] = 0; fm[11] = 1;						/* NDR */
			*(unsigned long long *)&fm[12] = fence_names[nfence - 1];	/* __XCreateFencePort (QuartzCore): no return code on success, the u64 name of the fence at 0x30, size 0x38 */
			mach_msg(fm, 1, 0x38, 0, 0, 0, 0);
			out("carender-stub: fence port made\n");
		}
		if (buf[5] == 0x9d0a) {
			static u32 client_id = 1;		/* the two numbers of the answer: the id of the client; the commits of its contexts carry it and SetWindowLayerContext names it (they were always 1: every client looked the same) */

			client_id++;
			{
				u32 cp = 0;

				mach_port_allocate(mach_task_self_, 1, &cp);
				mach_port_insert_right(mach_task_self_, cp, cp, 20);
				mach_port_insert_member(mach_task_self_, cp, set);	/* the commands of this client (40004 / 40005 ...) arrive on its own port */
				answer_register(buf, p1, cp, client_id, client_id, 0);
			}
			if (buf[0] & 0x80000000) { send_registered(buf[7]); send_registered(buf[10]); }
			out("carender-stub: answered the register, client "); hex(client_id, 8); out("\n");
		}
	}
}
