/*
 * wsbridge: the front of the window server for programs of the Mac.  SkyLight (the client library in the dyld cache) sends its MIG messages to a Mach port named by the bootstrap
 * service com.apple.windowserver.active; WindowServer (github.com/pearOS-bsd/misc, wsbsd) speaks the same messages on a Unix socket (SOCK_SEQPACKET: a datagram is a message and then the
 * out-of-line payloads in the order of the descriptors; the port descriptors are descriptors of SCM_RIGHTS: the name is an index, 1 based, in the list of them).  This program is the
 * service: it takes the name from the launchd, receives the Mach messages, forwards them on a socket of WindowServer (one for each client process) and brings the replies back.
 *
 * The rights: a port in a reply of WindowServer becomes a new Mach port of this program (it keeps the receive right, the client gets a send right); the messages that the client sends to it go
 * to WindowServer on the socket of the client.  A reply whose port name is not an index of a descriptor passed (WindowServer replays some replies recorded on a Mac) gives a port that is only a name for the
 * server.  The out-of-line payloads of a reply are copied into memory of this program and handed over as out-of-line descriptors.
 *
 * Not done yet: complex requests (ports and memory from the client to WindowServer), the events that WindowServer sends to a client (they come on the socket and have to go to the port that the client gave),
 * memory of the surfaces (shared memory objects as Mach memory entries).  Own code (BSD); no header of Apple's.
 */
typedef unsigned int u32;
typedef unsigned long u64;
extern u32 mach_task_self_;
extern u32 bootstrap_port;
extern int mach_msg(void *msg, int option, u32 send_size, u32 rcv_size, u32 rcv_name, u32 timeout, u32 notify);
extern int mach_port_allocate(u32 task, u32 right, u32 *name);
extern int mach_port_insert_right(u32 task, u32 name, u32 poly, u32 poly_poly);
extern int mach_port_move_member(u32 task, u32 member, u32 after);
extern int bootstrap_check_in(u32 bp, const char *name, u32 *port);
extern long write(int fd, const void *buf, unsigned long n);
extern int socket(int domain, int type, int protocol);
extern int connect(int s, const void *name, unsigned int namelen);
extern long send(int s, const void *buf, unsigned long len, int flags);
extern long recvmsg(int s, void *msg, int flags);
extern long sendmsg(int s, const void *msg, int flags);
extern long recv(int s, void *buf, unsigned long len, int flags);
extern int socketpair(int domain, int type, int protocol, int *sv);
extern int close(int fd);
extern long lseek(int fd, long off, int whence);
extern void *mmap(void *addr, unsigned long len, int prot, int flags, int fd, long off);
extern int ftruncate(int fd, long len);
extern int mach_make_memory_entry_64(u32 task, u64 *size, u64 offset, int perm, u32 *handle, u32 parent);
extern char *getenv(const char *name);

#define	MACH_SEND_MSG	1
#define	MACH_RCV_MSG	2
#define	MACH_RCV_LARGE	4

struct hdr { u32 bits, size, remote, local, voucher, id; };
struct iovec { void *base; unsigned long len; };
struct msghdr { void *name; u32 namelen; struct iovec *iov; int iovlen; void *control; u32 controllen; int flags; };

static void out(const char *s) { unsigned long n = 0; while (s[n]) n++; write(2, s, n); }
static void outx(u32 v) { char b[9]; int i; for (i = 7; i >= 0; i--, v >>= 4) b[i] = "0123456789abcdef"[v & 15]; b[8] = 0; out(b); }
extern unsigned long long mach_absolute_time(void);
static void tms(void) { out(" t="); outx((u32)(mach_absolute_time() / 1000000)); }      /* the time of a request and of its answer (ms): where the time of a round trip goes */
static void copy(void *d, const void *s, unsigned long n) { unsigned long i; for (i = 0; i < n; i++) ((char *)d)[i] = ((const char *)s)[i]; }

static union { struct hdr h; char raw[0x12100]; } rq, rp;
static char wbuf[0x12100];
static char oolpool[1 << 20];		/* the out-of-line payloads of the replies in flight: a ring */
static unsigned long oolpos;
static u32 set;

/* one socket to WindowServer for each client process (the window server counts a connection for each client) */
static struct { u32 pid; int fd; int shaken; } clients[64];		/* shaken: NewConnectionPort went over this socket (WindowServer refuses a second one on it) */
static int reply_conn_fd = -1;						/* the socket of a connection that is being made: the connection port of the reply stands for it */
/* the ports that this program made for ports of WindowServer's replies: the port, the client that got it, and the descriptor that stands for it at WindowServer (-1: none, the socket of the client) */
static struct { u32 port; u32 pid; int fd; } pmap[256];

/* the event channels: WindowServer sends events on a socket (the end that this program keeps), and they go as Mach messages to the port that the client gave in its request (a send right that this program holds) */
static struct { int fd; u32 port; u32 pid; u32 tapport; } evch[64];		/* tapport: the port a client gave to PlaceTap (event taps): WindowServer's tap data goes there as the CGYEventTap message that the client library expects */
static u32 cur_pid;									/* the client whose request is being forwarded */
#define WSD_NOTE_TAP_DATA 0x54415044u

static int
wsd_socket(u32 pid)
{
	int i, fd;
	struct { unsigned char len, family; char path[104]; } sa;
	const char *p = getenv("WS_SOCKET");
	unsigned k;

	for (i = 0; i < 64; i++)
		if (clients[i].fd != 0 && clients[i].pid == pid) return (clients[i].fd);
	if (p == 0) p = "/tmp/WindowServer.sock";
	for (k = 0; k < sizeof(sa); k++) ((char *)&sa)[k] = 0;
	for (k = 0; p[k] && k < 103; k++) sa.path[k] = p[k];
	sa.len = sizeof(sa); sa.family = 1;				/* AF_UNIX */
	fd = socket(1, 5, 0);						/* SOCK_SEQPACKET */
	if (fd < 0 || connect(fd, &sa, sizeof(sa)) != 0) {
		out("wsbridge: cannot connect to WindowServer at "); out(p); out("\n");
		if (fd >= 0) close(fd);
		return (-1);
	}
	for (i = 0; i < 64; i++)
		if (clients[i].fd == 0) { clients[i].pid = pid; clients[i].fd = fd; break; }
	{						/* the hello: WindowServer knows the peer of the socket (this program), not the client whose requests go over it */
		u32 hello[7] = { 0, 28, 0, 0, 0, 0x50494400, pid };

		send(fd, hello, 28, 0);
	}
	return (fd);
}

/* a socket of its own for one more connection of the same client (SkyLight makes several: SLSMainConnection of a thread, ...); not in the table of the clients, only the connection port of its reply refers to it */
static int
wsd_socket_new(u32 pid)
{
	int fd;
	struct { unsigned char len, family; char path[104]; } sa;
	const char *p = getenv("WS_SOCKET");
	unsigned k;

	if (p == 0) p = "/tmp/WindowServer.sock";
	for (k = 0; k < sizeof(sa); k++) ((char *)&sa)[k] = 0;
	for (k = 0; p[k] && k < 103; k++) sa.path[k] = p[k];
	sa.len = sizeof(sa); sa.family = 1;
	fd = socket(1, 5, 0);
	if (fd < 0 || connect(fd, &sa, sizeof(sa)) != 0) { if (fd >= 0) close(fd); return (-1); }
	{ u32 hello[7] = { 0, 28, 0, 0, 0, 0x50494400, pid }; send(fd, hello, 28, 0); }
	return (fd);
}

/* a new port for the client: this program holds the receive right (in the set that it listens on) and the client gets a send right made from it */
static u32
new_port(u32 pid, int fd)
{
	u32 p = 0;
	int i;

	if (mach_port_allocate(mach_task_self_, 1, &p) != 0 || mach_port_insert_right(mach_task_self_, p, p, 20) != 0 || mach_port_move_member(mach_task_self_, p, set) != 0)
		return (0);
	for (i = 0; i < 256; i++)
		if (pmap[i].port == 0) { pmap[i].port = p; pmap[i].pid = pid; pmap[i].fd = fd; break; }
	return (p);
}

/* the datagram of WindowServer (a message, then payloads, with descriptors from SCM_RIGHTS) -> a Mach message in rp for the client */
static int
get_reply(int fd, u32 pid, u32 reply_remote)
{
	struct iovec iov = { wbuf, sizeof(wbuf) };
	char ctl[128];
	struct msghdr mh;
	long n;
	int fds[8], nfds = 0, i;
	u32 size, ndesc, off, tail;
	unsigned long k;

	for (k = 0; k < sizeof(mh); k++) ((char *)&mh)[k] = 0;
	mh.iov = &iov; mh.iovlen = 1; mh.control = ctl; mh.controllen = sizeof(ctl);
	n = recvmsg(fd, &mh, 0);
	if (n < 24)
		return (-1);
	if (mh.controllen >= 16) {					/* SCM_RIGHTS: the layout of the control message of this kernel is FreeBSD's: 16 bytes of header, then the descriptors */
		u32 *c = (u32 *)ctl;

		if (c[1] == 0xffff && c[2] == 1) {
			nfds = (int)((c[0] - 16) / 4);
			if (nfds > 8) nfds = 8;
			for (i = 0; i < nfds; i++) fds[i] = ((int *)(ctl + 16))[i];
		}
	}
	copy(&rp, wbuf, (unsigned long)n < sizeof(rp) ? (unsigned long)n : sizeof(rp));
	size = rp.h.size;
	tail = size;
	if (rp.h.bits & 0x80000000u) {
		u32 *w = (u32 *)&rp.h;

		ndesc = w[6];
		off = 0x1c;
		for (i = 0; i < (int)ndesc && off + 12 <= size; i++) {
			unsigned char *d = (unsigned char *)&rp + off;

			if (d[11] == 0 && rp.h.id == 0x7d64 && i == 1) {	/* the answer to NewConnectionPort: its second port is the memory of the packages session (0x40 bytes, SLSPackagesSessionShmem: the client maps it with mach_vm_map; found with REA in SLSNewConnection); WindowServer has none, so it is zeroed memory */
				u64 sz = 0x40;
				u32 entry = 0;
				void *mem = mmap(0, 0x1000, 3, 0x1002 /* MAP_ANON | MAP_PRIVATE */, -1, 0);

				if ((long)mem != -1) {
					volatile u64 *q = (volatile u64 *)mem;

					q[0] = 0; q[1] = 1; q[2] = 1;			/* the page also has to be touched (an untouched anonymous mapping has no object yet: the memory entry needs one); what the client reads in it (SkyLight: _SLSGetActiveSpace, get_space_state_snapshot): +8 the seed of the state of the spaces, +0x10 the active space.  With zeros AppKit finds no active space, takes every window for one that is not on it and does not draw it */
				}

				if ((long)mem != -1 && mach_make_memory_entry_64(mach_task_self_, &sz, (u64)(unsigned long)mem, 3, &entry, 0) == 0) {
					*(u32 *)d = entry; d[10] = 17;
					out("wsbridge:   second port of the connection: a memory entry "); outx(entry); out("\n");
					off += 12;
					continue;
				}
			}
			if (d[11] == 0) {				/* a port descriptor: the name is an index of the descriptors passed, or only a name */
				u32 name = *(u32 *)d, np;
				int pfd = (name >= 1 && (int)name <= nfds) ? fds[name - 1] : -1;
				long fsz = pfd >= 0 ? lseek(pfd, 0, 2) : -1;		/* a memory object has a size; a socket does not */

				if (fsz > 0 && fsz < 4096 && ftruncate(pfd, 4096) == 0)
					fsz = 4096;				/* the client maps more than the pixels of a window of 1x1 (the state of the window): at least a page */
				if (fsz > 0) {						/* shared memory of WindowServer: a Mach memory entry for it, which the client maps with mach_vm_map */
					void *mem = mmap(0, (unsigned long)fsz, 3, 1, pfd, 0);
					u64 sz = (u64)fsz;
					u32 entry = 0;

					if ((long)mem != -1 && mach_make_memory_entry_64(mach_task_self_, &sz, (u64)(unsigned long)mem, 3, &entry, 0) == 0) {
						*(u32 *)d = entry; d[10] = 17;		/* MOVE_SEND */
						out("wsbridge:   port descriptor "); outx(name); out(" -> memory entry "); outx(entry); out(" of "); outx((u32)fsz); out(" bytes\n");
						off += 12;
						continue;
					}
					out("wsbridge:   no memory entry for the descriptor\n");
				}
				np = new_port(pid, (rp.h.id == 0x7d64 && i == 0 && reply_conn_fd >= 0) ? reply_conn_fd : pfd);
				*(u32 *)d = np;
				d[10] = 20;				/* MAKE_SEND: from the receive right that this program keeps */
				out("wsbridge:   port descriptor "); outx(name); out(" -> port "); outx(np); out(pfd >= 0 ? " (with a descriptor)\n" : " (a name)\n");
				off += 12;
			} else if (d[11] == 1) {			/* out-of-line memory: the payload follows the message in the datagram */
				u32 len = *(u32 *)(d + 12);
				u64 addr = 0;

				if (oolpos + len > sizeof(oolpool)) oolpos = 0;
				if (tail + len <= (u32)n && len <= sizeof(oolpool)) {
					copy(oolpool + oolpos, wbuf + tail, len);
					addr = (u64)(unsigned long)(oolpool + oolpos);
					oolpos = (oolpos + len + 15) & ~15UL;
					tail += len;
				}
				*(u64 *)d = addr; d[8] = 0; d[9] = 1;	/* deallocate: no; copy: virtual */
				off += 16;
			} else
				break;
		}
	}
	rp.h.bits = (rp.h.bits & 0x80000000u) | 0x12;			/* the reply right of the client: a send-once right, moved */
	rp.h.remote = reply_remote; rp.h.local = 0; rp.h.voucher = 0;
	rp.h.size = size;
	for (i = 0; i < nfds; i++) { /* the descriptors that were mapped stay open: the port stands for them */ }
	return (0);
}

/* a complex request of the client: the port descriptors become descriptors of the socket (the first: this connection itself, the others: the end of a new socketpair; this program keeps the other end, and what comes on it
 * goes to the port that the client gave), the out-of-line memory follows the message in the datagram */
static int
send_complex(int fd)
{
	static char dg[0x12100];
	struct iovec iov;
	struct msghdr mh;
	char ctl[64];
	int fds[8], nfds = 0, i, k;
	u32 *w = (u32 *)&rq.h, ndesc = w[6], off = 0x1c, size = rq.h.size, tail;
	long n;

	copy(dg, &rq.h, size);
	tail = size;
	for (i = 0; i < (int)ndesc && off + 12 <= size; i++) {
		unsigned char *d = (unsigned char *)dg + off;

		if (d[11] == 0) {					/* a port descriptor */
			u32 name = *(u32 *)(dg + off);
			int sv[2];

			if (nfds >= 8 && rq.h.id != 32000) return (-1);
			if (rq.h.id == 32000) {
				/* NewConnectionPort: the first port is the notification port of the client (a receive right of its own that it gives a send right to), the second a task token: the event channel is for the first; WindowServer gets the
				 * end that it writes to, and what it writes goes to that port (poll_events): the client wakes and pulls the datagrams with GetPortStreamOutofline */
				if (i == 0) {
					if (socketpair(1, 5, 0, sv) != 0) return (-1);
					fds[nfds++] = sv[1];
					for (k = 0; k < 64; k++)
						if (evch[k].fd == 0) { evch[k].fd = sv[0]; evch[k].port = name; evch[k].pid = cur_pid; evch[k].tapport = 0; break; }
					out("wsbridge:   event channel for the notification port "); outx(name); out("\n");
				}
				*(u32 *)d = 1;
				off += 12;
				continue;
			}
			if (rq.h.id == 32028 || rq.h.id == 32029) {			/* PlaceTap: the port of the tap; remembered for the event channel of this client */
				for (k = 0; k < 64; k++) if (evch[k].fd != 0 && evch[k].pid == cur_pid) evch[k].tapport = name;
				out("wsbridge:   tap port "); outx(name); out(" of pid "); outx(cur_pid); out("\n");
			}
			if (nfds == 0)
				fds[nfds++] = fd;			/* the first port stands for this connection */
			else {
				if (socketpair(1, 5, 0, sv) != 0) return (-1);
				fds[nfds++] = sv[1];
				for (k = 0; k < 64; k++)
					if (evch[k].fd == 0) { evch[k].fd = sv[0]; evch[k].port = name; break; }
				out("wsbridge:   event channel for the port "); outx(name); out("\n");
			}
			*(u32 *)d = (u32)nfds;				/* 1-based index into the descriptors that go with the datagram */
			off += 12;
		} else if (d[11] == 1) {				/* out-of-line memory: the bytes follow the message */
			u64 addr = *(u64 *)d;
			u32 len = *(u32 *)(d + 12);

			if (tail + len > sizeof(dg)) return (-1);
			copy(dg + tail, (const void *)(unsigned long)addr, len);
			tail += len;
			off += 16;
		} else
			return (-1);
	}
	iov.base = dg; iov.len = tail;
	for (k = 0; k < (int)sizeof(mh); k++) ((char *)&mh)[k] = 0;
	{
		u32 *c = (u32 *)ctl;

		for (k = 0; k < (int)sizeof(ctl); k++) ctl[k] = 0;
		c[0] = 16 + 4 * nfds; c[1] = 0xffff; c[2] = 1;			/* the control message of FreeBSD: SOL_SOCKET, SCM_RIGHTS */
		for (k = 0; k < nfds; k++) ((int *)(ctl + 16))[k] = fds[k];
		mh.iov = &iov; mh.iovlen = 1; mh.control = ctl; mh.controllen = (16 + 4 * nfds + 7) & ~7u;
	}
	n = sendmsg(fd, &mh, 0);
	for (k = (rq.h.id == 32000 ? 0 : 1); k < nfds; k++) close(fds[k]);			/* the ends that went to WindowServer */
	return (n == (long)tail ? 0 : -1);
}

/* what WindowServer sent on the event channels: a message of the window server (the events of a client) -> the port of the client */
static void
poll_events(void)
{
	int k;

	for (k = 0; k < 64; k++) {
		long n;

		if (evch[k].fd == 0) continue;
		while ((n = recv(evch[k].fd, wbuf, sizeof(wbuf), 0x80 /* MSG_DONTWAIT */)) >= 24) {
			struct hdr *h = (struct hdr *)wbuf;

			if (n >= 0x28 && *(u32 *)(wbuf + 0x20) == WSD_NOTE_TAP_DATA && evch[k].tapport) {		/* tap data: PostEventTapData (CGYEventTap id 1) to the tap port: NDR, type, sequence, proxy id, length, the data stream */
				static char tb[0x4100]; u32 len = *(u32 *)(wbuf + 0x24), dl = len >= 16 ? *(u32 *)(wbuf + 0x28 + 12) : 0, pad = (dl + 3) & ~3u, i;
				if (len >= 16 && 16 + dl <= len && dl <= 0x4000) {
					struct hdr *t = (struct hdr *)tb; for (i = 0; i < 0x30 + pad; i++) tb[i] = 0;
					t->bits = 0x13; t->size = 0x30 + pad; t->remote = evch[k].tapport; t->local = 0; t->voucher = 0; t->id = 1;
					for (i = 0; i < 8; i++) tb[0x18 + i] = wbuf[0x18 + i];
					for (i = 0; i < 16 + dl; i++) tb[0x20 + i] = wbuf[0x28 + i];
					{ static int tl; int kr = mach_msg(t, MACH_SEND_MSG | 0x80, t->size, 0, 0, 0, 0); if (tl < 20) { tl++; out("wsbridge: tap data to port "); outx(evch[k].tapport); out(" size "); outx(t->size); out(" kr "); outx((u32)kr); out("\n"); } }
					continue;
				}
			}
			h->bits = (h->bits & 0x80000000u) | 0x13;		/* a copy of the send right that this program holds */
			h->remote = evch[k].port; h->local = 0; h->voucher = 0;
			h->size = (u32)n;
			{ static int logged; int kr = mach_msg(h, MACH_SEND_MSG | 0x80 /* MACH_SEND_TIMEOUT */, (u32)n, 0, 0, 0, 0);
			  if (logged < 5) { logged++; out("wsbridge: event message id "); outx(h->id); out(" size "); outx((u32)n); out(" to port "); outx(evch[k].port); out(" kr "); outx((u32)kr); out("\n"); } }
		}
	}
}

/* a message of a client to WindowServer and the reply back (the requests that carry rights or memory are not forwarded yet) */
static void
forward(const u32 *tr, u32 via_port)
{
	int fd = -1, i;
	u32 *w = (u32 *)&rp.h;
	u32 pid = tr[10];

	cur_pid = pid;

	for (i = 0; via_port != 0 && i < 256; i++)
		if (pmap[i].port == via_port && pmap[i].fd >= 0) fd = pmap[i].fd;
	reply_conn_fd = -1;
	if (fd < 0 && rq.h.id == 32000) {					/* a connection of the client that already has one: a socket of its own */
		for (i = 0; i < 64; i++)
			if (clients[i].fd != 0 && clients[i].pid == pid && clients[i].shaken) { fd = wsd_socket_new(pid); reply_conn_fd = fd; out("wsbridge:   one more connection of pid "); outx(pid); out(": a socket of its own\n"); break; }
	}
	if (fd < 0)
		fd = wsd_socket(pid);
	if (fd < 0)
		goto fail;
	if (rq.h.id == 32000 && reply_conn_fd < 0)
		for (i = 0; i < 64; i++)
			if (clients[i].fd == fd) clients[i].shaken = 1;
	if (rq.h.bits & 0x80000000u) {
		if (send_complex(fd) != 0) {
			out("wsbridge:   complex request: not forwarded\n");
			goto fail;
		}
	} else if (send(fd, &rq.h, rq.h.size, 0) != (long)rq.h.size) goto fail;
	if (rq.h.remote == 0) {
		/* no reply port (a routine without a reply, like SetWindowLevel): WindowServer answers every request anyway, so its answer is read and dropped; left on the socket it would be taken as the answer of the next request */
		(void)get_reply(fd, pid, 0);
		return;
	}
	if (get_reply(fd, pid, rq.h.remote) != 0) goto fail;
	out("wsbridge:   reply"); tms(); out(" id "); outx(rp.h.id); out(" size "); outx(rp.h.size); out(" bits "); outx(rp.h.bits);
	if (rp.h.size == 0x24 && !(rp.h.bits & 0x80000000u)) { out(" code "); outx(((u32 *)&rp.h)[8]); }
	out("\n");
	mach_msg(&rp.h, MACH_SEND_MSG, rp.h.size, 0, 0, 0, 0);
	return;
fail:
	if (rq.h.remote != 0) {						/* MIG_BAD_ID */
		rp.h.bits = 0x12; rp.h.remote = rq.h.remote; rp.h.local = 0; rp.h.voucher = 0; rp.h.id = rq.h.id + 100;
		w[6] = 0; w[7] = 1; w[8] = (u32)-303; rp.h.size = 36;
		mach_msg(&rp.h, MACH_SEND_MSG, 36, 0, 0, 0, 0);
	}
}

int
main(void)
{
	u32 port = 0;
	int kr;

	if ((kr = mach_port_allocate(mach_task_self_, 3, &set)) != 0 ||
	    (kr = bootstrap_check_in(bootstrap_port, "com.apple.windowserver.active", &port)) != 0 ||
	    (kr = mach_port_move_member(mach_task_self_, port, set)) != 0) {
		out("wsbridge: no service, error "); outx((u32)kr); out("\n");
		return (1);
	}
	out("wsbridge: serving com.apple.windowserver.active\n");
	for (;;) {
		const u32 *tr;

		kr = mach_msg(&rq.h, MACH_RCV_MSG | MACH_RCV_LARGE | 0x100 /* MACH_RCV_TIMEOUT */ | (3u << 24), 0, sizeof(rq), set, 10, 0);
		poll_events();
		if (kr == 0x10004003)				/* timed out: only the event channels were looked at */
			continue;
		if (kr != 0) {
			out("wsbridge: receive error "); outx((u32)kr); out("\n");
			if (kr == 0x10004002) return (1);
			continue;
		}
		tr = (const u32 *)(rq.raw + ((rq.h.size + 3) & ~3u));
		out("wsbridge: id "); outx(rq.h.id); tms(); out(" size "); outx(rq.h.size); out(" bits "); outx(rq.h.bits); out(" port "); outx(rq.h.local); out(" from pid "); outx(tr[10]); out("\n");
		forward(tr, rq.h.local == port ? 0 : rq.h.local);
	}
}
