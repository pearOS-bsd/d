/* A stand-in for com.apple.CARenderServer, only to see what a client (the Dock) sends it: it takes the service with bootstrap_check_in, receives every message on the port and prints the header and the first
 * bytes of the body to stderr (a complex message: also its descriptors).  It sends no answer.  It is the first step of the replacement of the CoreAnimation render server of the WindowServer. */
typedef unsigned int u32;
typedef unsigned long u64;
extern u32 bootstrap_port;
extern int bootstrap_check_in(u32 bp, const char *name, u32 *port);
extern int mach_msg(void *msg, int opt, u32 ssz, u32 rsz, u32 rname, u32 to, u32 notify);
extern int mach_port_allocate(u32 task, u32 right, u32 *name);
extern int mach_port_insert_right(u32 task, u32 name, u32 poly, u32 how);
extern int mach_port_destroy(u32 task, u32 name);
extern int mach_port_insert_member(u32 task, u32 name, u32 pset);
extern u32 mach_task_self_;
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
	rep[10] = p2; rep[11] = 0; rep[12] = 0x00130000;
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
static unsigned long long fence_ids[1] = { 1 };

static void
send_fence_msg(u32 port, u32 id)
{
	u32 m[18];
	int i;

	for (i = 0; i < 18; i++) m[i] = 0;
	m[0] = 0x80000013; m[1] = id == 0x726d6663 ? 52 : 72; m[2] = port; m[5] = id;
	m[6] = 1; *(unsigned long *)&m[7] = (unsigned long)fence_ids; m[9] = 0x01000100; m[10] = sizeof fence_ids;
	mach_msg(m, 1 | 0x80, m[1], 0, 0, 100, 0);
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
		kr = mach_msg(buf, 2 | 0x100000 /* RCV_MSG | RCV_LARGE */ | 0x03000000 /* the audit trailer: who sent it */, 0, sizeof(buf), set, 0, 0);
		if (kr != 0) { out("carender-stub: receive error "); hex((u32)kr, 8); out("\n"); continue; }
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
					} else
						off += 12;
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
		if (buf[5] == 0x9c42 && (buf[0] & 0x80000000) && buf[6] >= 1) {	/* a commit with a send-once right among its descriptors (the client waits on it: no new commit before the answer): an empty message to it.  The descriptors are 12 bytes (port) or 16 (out of line memory) */
			u32 k, off = 7;		/* word index of the first descriptor */


			for (k = 0; k < buf[6] && off + 3 <= 64; k++) {
				u32 type = (buf[off + 2] >> 24) & 0xff, disp = (buf[off + 2] >> 16) & 0xff;

				if (type == 0 && disp == 0x12) {
					u32 ack[6], kr2;

					ack[0] = 0x12; ack[1] = 24; ack[2] = buf[off]; ack[3] = 0; ack[4] = 0; ack[5] = 0;
					kr2 = mach_msg(ack, 1, 24, 0, 0, 0, 0);
					out("carender-stub: commit acknowledged "); hex((u32)kr2, 8); out("\n");
				}
				if (type == 0 && disp == 0x11) {		/* a fence that this stub made comes back with a commit: it is done (the client learns it when the port dies: its commit waits for that before it goes on) */
					u32 n;

					int mine = 0;

					for (n = 0; n < nfence; n++)
						if (fences[n] == buf[off]) mine = 1, fences[n] = 0;
					out(mine ? "carender-stub: fence completed " : "carender-stub: port of a commit released "); hex(buf[off], 8); out("\n");
					if (!mine) { send_fence_msg(buf[off], 0x726d6663); send_fence_msg(buf[off], 0x726c6667); out("carender-stub: fence messages sent to "); hex(buf[off], 8); out("\n"); }
					mach_port_destroy(mach_task_self_, buf[off]);		/* the right is given up: a client that waits for the last sender to go (or for the dead name) goes on */
				}
				off += type == 1 ? 4 : 3;
			}
		}
		if (buf[5] == 0x9d10 && buf[2] != 0) {			/* CASCreateFencePort: the answer carries a send right to a new port (a fence: nothing waits on it here) */
			u32 fp = 0, fm[16];

			mach_port_allocate(mach_task_self_, 1, &fp);
			mach_port_insert_right(mach_task_self_, fp, fp, 20);
			mach_port_insert_member(mach_task_self_, fp, set);
			if (nfence < 64) fences[nfence++] = fp;
			for (i = 0; i < 16; i++) fm[i] = 0;
			fm[0] = 0x80000012; fm[1] = 0x34; fm[2] = buf[2]; fm[5] = 0x9d10 + 100;
			fm[6] = 1; fm[7] = fp; fm[8] = 0; fm[9] = 0x00130000;
			fm[10] = 0; fm[11] = 1;						/* NDR */
			fm[12] = 0;							/* the return code */
			mach_msg(fm, 1, 0x34, 0, 0, 0, 0);
			out("carender-stub: fence port made\n");
		}
		if (buf[5] == 0x9d0a) {
			static u32 client_id = 1;		/* the two numbers of the answer: the id of the client; the commits of its contexts carry it and SetWindowLayerContext names it (they were always 1: every client looked the same) */

			client_id++;
			answer_register(buf, p1, p2, client_id, client_id, 0);
			if (buf[0] & 0x80000000) { send_registered(buf[7]); send_registered(buf[10]); }
			out("carender-stub: answered the register, client "); hex(client_id, 8); out("\n");
		}
	}
}
