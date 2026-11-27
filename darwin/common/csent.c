/* csent_bool: the entitlements of a process from the code signature of its executable on disk (LC_CODE_SIGNATURE of the x86_64 slice,
 * the SuperBlob 0xfade0cc0, the XML entitlements blob 0xfade7171).  Same reading as darwin/tccd (subject_signature), kept as a small library
 * for the Objective-C daemons: on pearOS csops(CS_OPS_ENTITLEMENTS_BLOB) reports nothing, so -[NSXPCConnection valueForEntitlement:] is nil.
 * Our own file (BSD-2-Clause). */
#include "csent.h"
#include <stddef.h>
extern int open(const char *, int, ...);
extern long pread(int, void *, unsigned long, long long);
extern int close(int);
extern void *malloc(unsigned long);
extern void free(void *);
extern int proc_pidpath(int pid, void *buf, unsigned int size);

static unsigned int be32(const unsigned char *p) { return (unsigned int)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static unsigned int le32(const unsigned char *p) { return (unsigned int)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]; }

/* the XML entitlements of an executable (malloc'd, NUL-terminated) or 0 */
static char *
read_entitlements(const char *path)
{
	enum { HSZ = 65536 };
	unsigned char *h = malloc(HSZ), *sb = 0;
	char *ents = 0;
	long long base = 0;
	unsigned int i, n, off, sz;
	int fd = open(path, 0);

	if (fd < 0 || h == 0) { if (fd >= 0) close(fd); free(h); return 0; }
	if (pread(fd, h, HSZ, 0) < 32) goto done;
	if (be32(h) == 0xcafebabe) {
		n = be32(h + 4);
		for (i = 0; i < n && 8 + i * 20 + 20 <= HSZ; i++)
			if (be32(h + 8 + i * 20) == 0x01000007) { base = be32(h + 8 + i * 20 + 8); break; }
		if (base == 0 || pread(fd, h, HSZ, base) < 32) goto done;
	}
	if (le32(h) != 0xfeedfacf) goto done;
	n = le32(h + 16); off = 32; sz = 0;
	for (i = 0; i < n && off + 16 <= HSZ; i++) {
		if (le32(h + off) == 0x1d) { sz = le32(h + off + 12); off = le32(h + off + 8); break; }
		off += le32(h + off + 4);
	}
	if (sz < 12 || sz > (1 << 22) || (sb = malloc(sz)) == 0) goto done;
	if (pread(fd, sb, sz, base + off) != (long)sz || be32(sb) != 0xfade0cc0) goto done;
	n = be32(sb + 8);
	for (i = 0; i < n && 12 + i * 8 + 8 <= sz; i++) {
		unsigned int ty = be32(sb + 12 + i * 8), bo = be32(sb + 12 + i * 8 + 4), bl, k;

		if (ty != 5 || bo + 8 > sz) continue;
		bl = be32(sb + bo + 4);
		if (bl > sz - bo || bl < 8 || be32(sb + bo) != 0xfade7171 || (ents = malloc(bl - 8 + 1)) == 0) continue;
		for (k = 0; k < bl - 8; k++) ents[k] = (char)sb[bo + 8 + k];
		ents[bl - 8] = 0;
		break;
	}
done:
	free(sb);
	free(h);
	close(fd);
	return ents;
}

static const char *
find(const char *h, const char *n)
{
	for (; *h; h++) {
		size_t i;

		for (i = 0; n[i] && h[i] == n[i]; i++) ;
		if (n[i] == 0) return h;
	}
	return 0;
}

int
csent_bool(int pid, const char *key)
{
	char path[1024], k[256];
	const char *p;
	char *ents;
	size_t i = 0, j;
	int r = 0;

	if (pid <= 0 || proc_pidpath(pid, path, sizeof path) <= 0 || (ents = read_entitlements(path)) == 0) return 0;
	for (j = 0; "<key>"[j]; j++) k[i++] = "<key>"[j];
	for (j = 0; key[j] && i < 240; j++) k[i++] = key[j];
	for (j = 0; "</key>"[j]; j++) k[i++] = "</key>"[j];
	k[i] = 0;
	if ((p = find(ents, k)) != 0) {
		p += i;
		while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
		r = find(p, "<true/>") == p;
	}
	free(ents);
	return r;
}
