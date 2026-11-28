/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Alexandru Balan
 *
 * The parts of the Darwin (macOS x86_64) interfaces that cfprefsd uses, declared by hand so that the daemon builds on
 * FreeBSD with clang + ld64.lld and no Apple SDK: libSystem (libc, libdispatch, libxpc, notify) and CoreFoundation.
 * Layouts and constants are those of the macOS x86_64 ABI (struct stat with 64-bit inodes, struct passwd, open flags).
 */
#ifndef CFPREFSD_DARWIN_H
#define CFPREFSD_DARWIN_H

typedef unsigned long size_t;
typedef long ssize_t;
typedef long off_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef int pid_t;
typedef unsigned short mode_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef short int16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef long int64_t;
typedef int int32_t;
#define NULL ((void *)0)

/* ---- libc ---- */
struct timespec { long tv_sec; long tv_nsec; };
struct stat {				/* struct stat64 (INODE64), 144 bytes */
	int st_dev; mode_t st_mode; unsigned short st_nlink; uint64_t st_ino; uid_t st_uid; gid_t st_gid; int st_rdev;
	struct timespec st_atimespec, st_mtimespec, st_ctimespec, st_birthtimespec;
	off_t st_size; long st_blocks; int st_blksize; unsigned int st_flags; unsigned int st_gen; int st_lspare; long st_qspare[2];
};
struct passwd { char *pw_name; char *pw_passwd; uid_t pw_uid; gid_t pw_gid; long pw_change; char *pw_class; char *pw_gecos; char *pw_dir; char *pw_shell; long pw_expire; };

#define O_RDONLY	0x0000
#define O_WRONLY	0x0001
#define O_RDWR		0x0002
#define O_ACCMODE	0x0003
#define O_CREAT		0x0200
#define O_TRUNC		0x0400
#define O_EXCL		0x0800
#define O_DIRECTORY	0x100000
#define O_CLOEXEC	0x1000000
#define F_GETFL		3
#define S_IFMT		0170000
#define S_IFDIR		0040000
#define S_IRUSR		0400
#define S_IWUSR		0200
#define S_IROTH		0004
#define S_IWOTH		0002
#define S_IRGRP		0040
#define S_IWGRP		0020
#define PROT_READ	1
#define PROT_WRITE	2
#define MAP_SHARED	1
#define MAP_FAILED	((void *)-1)
#define SIGTERM		15
#define SIG_IGN		((void (*)(int))1)
#define EINTR		4
#define ENOENT		2
#define EAGAIN		35
#define ENOTDIR		20
#define ENOSPC		28

extern int open(const char *path, int flags, ...);
extern int close(int fd);
extern ssize_t read(int fd, void *buf, size_t n);
extern ssize_t write(int fd, const void *buf, size_t n);
extern int fsync(int fd);
extern int fcntl(int fd, int cmd, ...);
extern int fchown(int fd, uid_t u, gid_t g);
extern int fchmod(int fd, mode_t m);
extern int mkdir(const char *path, mode_t m);
extern int rename(const char *from, const char *to);
extern int unlink(const char *path);
extern int mkstemp(char *tmpl);
extern int stat(const char *path, struct stat *st) __asm("_stat$INODE64");
extern int fstat(int fd, struct stat *st) __asm("_fstat$INODE64");
extern int ftruncate(int fd, off_t len);
extern void *mmap(void *a, size_t len, int prot, int flags, int fd, off_t off);
extern int shm_open(const char *name, int flags, ...);
extern struct passwd *getpwnam(const char *name);
extern struct passwd *getpwuid(uid_t uid);
extern uid_t geteuid(void);
extern uid_t getuid(void);
extern gid_t getegid(void);
extern int gethostuuid(unsigned char uuid[16], const struct timespec *wait);
extern void (*signal(int sig, void (*fn)(int)))(int);
extern void exit(int code) __attribute__((noreturn));
extern void *malloc(size_t n);
extern void *calloc(size_t n, size_t m);
extern void *realloc(void *p, size_t n);
extern void free(void *p);
extern char *strdup(const char *s);
extern size_t strlen(const char *s);
extern int strcmp(const char *a, const char *b);
extern int strncmp(const char *a, const char *b, size_t n);
extern int strcasecmp(const char *a, const char *b);
extern void *memcpy(void *d, const void *s, size_t n);
extern void *memset(void *d, int c, size_t n);
extern int snprintf(char *buf, size_t n, const char *fmt, ...);
extern int dprintf(int fd, const char *fmt, ...);
extern char *getenv(const char *name);
extern int *__error(void);
#define errno (*__error())

/* ---- libquarantine (re-exported by libSystem; weak: a system without it simply has no quarantine) ---- */
extern void *_qtn_proc_alloc(void) __attribute__((weak_import));
extern int _qtn_proc_set_identifier(void *q, const char *ident) __attribute__((weak_import));
extern int _qtn_proc_set_flags(void *q, uint32_t flags) __attribute__((weak_import));
extern int _qtn_proc_apply_to_self(void *q) __attribute__((weak_import));
extern void _qtn_proc_free(void *q) __attribute__((weak_import));

/* ---- libdispatch ---- */
typedef void *dispatch_queue_t;
typedef void *dispatch_source_t;
typedef void *dispatch_object_t;
typedef uint64_t dispatch_time_t;
extern dispatch_queue_t dispatch_queue_create(const char *label, void *attr);
extern void dispatch_async(dispatch_queue_t q, void (^b)(void));
extern void dispatch_main(void) __attribute__((noreturn));
extern dispatch_source_t dispatch_source_create(const void *type, uint64_t handle, unsigned long mask, dispatch_queue_t q);
extern void dispatch_source_set_event_handler(dispatch_source_t s, void (^b)(void));
extern void dispatch_source_set_timer(dispatch_source_t s, dispatch_time_t start, uint64_t interval, uint64_t leeway);
extern void dispatch_resume(dispatch_object_t o);
extern void dispatch_suspend(dispatch_object_t o);
extern dispatch_time_t dispatch_time(dispatch_time_t when, int64_t delta);
extern const struct dispatch_source_type_s _dispatch_source_type_timer, _dispatch_source_type_signal;
#define DISPATCH_TIME_NOW 0
#define NSEC_PER_SEC 1000000000ull

/* ---- notify ---- */
extern unsigned int notify_post(const char *name);
extern unsigned int notify_register_dispatch(const char *name, int *token, dispatch_queue_t q, void (^h)(int));

/* ---- libxpc ---- */
typedef void *xpc_object_t;
typedef xpc_object_t xpc_connection_t;
typedef const struct _xpc_type_s *xpc_type_t;
extern const struct _xpc_type_s _xpc_type_dictionary, _xpc_type_array, _xpc_type_null, _xpc_type_bool, _xpc_type_string,
	_xpc_type_data, _xpc_type_int64, _xpc_type_uint64, _xpc_type_double, _xpc_type_date, _xpc_type_error, _xpc_type_fd,
	_xpc_type_connection, _xpc_type_endpoint;
extern const struct _xpc_dictionary_s _xpc_error_connection_invalid, _xpc_error_connection_interrupted, _xpc_error_termination_imminent;
#define XPC_ERROR_CONNECTION_INVALID ((xpc_object_t)&_xpc_error_connection_invalid)
#define XPC_CONNECTION_MACH_SERVICE_LISTENER (1 << 0)
#define XPC_CONNECTION_MACH_SERVICE_PRIVILEGED (1 << 1)
extern xpc_type_t xpc_get_type(xpc_object_t o);
extern xpc_object_t xpc_retain(xpc_object_t o);
extern void xpc_release(xpc_object_t o);
extern char *xpc_copy_description(xpc_object_t o);
extern xpc_object_t xpc_dictionary_create(const char *const *k, const xpc_object_t *v, size_t n);
extern xpc_object_t xpc_dictionary_create_reply(xpc_object_t m);
extern xpc_object_t xpc_dictionary_get_value(xpc_object_t d, const char *k);
extern void xpc_dictionary_set_value(xpc_object_t d, const char *k, xpc_object_t v);
extern const char *xpc_dictionary_get_string(xpc_object_t d, const char *k);
extern void xpc_dictionary_set_string(xpc_object_t d, const char *k, const char *v);
extern int64_t xpc_dictionary_get_int64(xpc_object_t d, const char *k);
extern void xpc_dictionary_set_int64(xpc_object_t d, const char *k, int64_t v);
extern void xpc_dictionary_set_uint64(xpc_object_t d, const char *k, uint64_t v);
extern _Bool xpc_dictionary_get_bool(xpc_object_t d, const char *k);
extern void xpc_dictionary_set_bool(xpc_object_t d, const char *k, _Bool v);
extern void xpc_dictionary_set_data(xpc_object_t d, const char *k, const void *p, size_t n);
extern int xpc_dictionary_dup_fd(xpc_object_t d, const char *k);
extern _Bool xpc_dictionary_apply(xpc_object_t d, _Bool (^f)(const char *k, xpc_object_t v));
extern size_t xpc_dictionary_get_count(xpc_object_t d);
extern xpc_object_t xpc_array_create(const xpc_object_t *o, size_t n);
extern void xpc_array_append_value(xpc_object_t a, xpc_object_t v);
extern size_t xpc_array_get_count(xpc_object_t a);
extern xpc_object_t xpc_array_get_value(xpc_object_t a, size_t i);
extern xpc_object_t xpc_null_create(void);
extern xpc_object_t xpc_bool_create(_Bool v);
extern _Bool xpc_bool_get_value(xpc_object_t o);
extern xpc_object_t xpc_string_create(const char *s);
extern const char *xpc_string_get_string_ptr(xpc_object_t o);
extern xpc_object_t xpc_data_create(const void *p, size_t n);
extern const void *xpc_data_get_bytes_ptr(xpc_object_t o);
extern size_t xpc_data_get_length(xpc_object_t o);
extern int64_t xpc_int64_get_value(xpc_object_t o);
extern uint64_t xpc_uint64_get_value(xpc_object_t o);
extern double xpc_double_get_value(xpc_object_t o);
extern int64_t xpc_date_get_value(xpc_object_t o);
extern xpc_connection_t xpc_connection_create_mach_service(const char *name, dispatch_queue_t q, uint64_t flags);
extern xpc_connection_t xpc_connection_create_from_endpoint(xpc_object_t ep);
extern xpc_object_t xpc_endpoint_create(xpc_connection_t c);
extern void xpc_connection_set_event_handler(xpc_connection_t c, void (^h)(xpc_object_t));
extern void xpc_connection_set_target_queue(xpc_connection_t c, dispatch_queue_t q);
extern void xpc_connection_set_context(xpc_connection_t c, void *ctx);
extern void *xpc_connection_get_context(xpc_connection_t c);
extern void xpc_connection_resume(xpc_connection_t c);
extern void xpc_connection_cancel(xpc_connection_t c);
extern void xpc_connection_send_message(xpc_connection_t c, xpc_object_t m);
extern void xpc_connection_send_message_with_reply(xpc_connection_t c, xpc_object_t m, dispatch_queue_t q, void (^h)(xpc_object_t));
extern uid_t xpc_connection_get_euid(xpc_connection_t c);
extern gid_t xpc_connection_get_egid(xpc_connection_t c);
extern pid_t xpc_connection_get_pid(xpc_connection_t c);

/* ---- CoreFoundation ---- */
typedef const void *CFTypeRef;
typedef const struct __CFString *CFStringRef;
typedef const struct __CFDictionary *CFDictionaryRef;
typedef struct __CFDictionary *CFMutableDictionaryRef;
typedef const struct __CFArray *CFArrayRef;
typedef struct __CFArray *CFMutableArrayRef;
typedef const struct __CFData *CFDataRef;
typedef const struct __CFNumber *CFNumberRef;
typedef const struct __CFBoolean *CFBooleanRef;
typedef const struct __CFDate *CFDateRef;
typedef const struct __CFAllocator *CFAllocatorRef;
typedef const struct __CFError *CFErrorRef;
typedef unsigned long CFTypeID;
typedef unsigned long CFHashCode;
typedef long CFIndex;
typedef unsigned int CFStringEncoding;
typedef unsigned long CFOptionFlags;
typedef double CFAbsoluteTime;
typedef struct { CFIndex version; void *retain, *release, *copyDescription, *equal, *hash; } CFDictionaryKeyCallBacks;
typedef struct { CFIndex version; void *retain, *release, *copyDescription, *equal; } CFDictionaryValueCallBacks;
typedef struct { CFIndex version; void *retain, *release, *copyDescription, *equal; } CFArrayCallBacks;
#define kCFStringEncodingUTF8 0x08000100
#define kCFPropertyListImmutable 0
#define kCFPropertyListMutableContainersAndLeaves 2
#define kCFPropertyListBinaryFormat_v1_0 200
#define kCFNumberSInt64Type 4
#define kCFNumberDoubleType 13
extern const CFDictionaryKeyCallBacks kCFTypeDictionaryKeyCallBacks;
extern const CFDictionaryValueCallBacks kCFTypeDictionaryValueCallBacks;
extern const CFArrayCallBacks kCFTypeArrayCallBacks;
extern const CFBooleanRef kCFBooleanTrue, kCFBooleanFalse;
extern CFTypeRef CFRetain(CFTypeRef o);
extern void CFRelease(CFTypeRef o);
extern CFTypeID CFGetTypeID(CFTypeRef o);
extern CFHashCode CFHash(CFTypeRef o);
extern CFTypeID CFDictionaryGetTypeID(void), CFArrayGetTypeID(void), CFStringGetTypeID(void), CFDataGetTypeID(void),
	CFNumberGetTypeID(void), CFBooleanGetTypeID(void), CFDateGetTypeID(void);
extern CFStringRef CFStringCreateWithCString(CFAllocatorRef a, const char *s, CFStringEncoding e);
extern CFIndex CFStringGetLength(CFStringRef s);
extern _Bool CFStringGetCString(CFStringRef s, char *buf, CFIndex n, CFStringEncoding e);
extern CFIndex CFStringGetMaximumSizeForEncoding(CFIndex len, CFStringEncoding e);
extern CFMutableDictionaryRef CFDictionaryCreateMutable(CFAllocatorRef a, CFIndex cap, const CFDictionaryKeyCallBacks *k, const CFDictionaryValueCallBacks *v);
extern CFMutableDictionaryRef CFDictionaryCreateMutableCopy(CFAllocatorRef a, CFIndex cap, CFDictionaryRef d);
extern CFIndex CFDictionaryGetCount(CFDictionaryRef d);
extern const void *CFDictionaryGetValue(CFDictionaryRef d, const void *k);
extern void CFDictionarySetValue(CFMutableDictionaryRef d, const void *k, const void *v);
extern void CFDictionaryRemoveValue(CFMutableDictionaryRef d, const void *k);
extern void CFDictionaryApplyFunction(CFDictionaryRef d, void (*f)(const void *k, const void *v, void *ctx), void *ctx);
extern CFMutableArrayRef CFArrayCreateMutable(CFAllocatorRef a, CFIndex cap, const CFArrayCallBacks *cb);
extern void CFArrayAppendValue(CFMutableArrayRef a, const void *v);
extern CFDataRef CFDataCreate(CFAllocatorRef a, const uint8_t *p, CFIndex n);
extern const uint8_t *CFDataGetBytePtr(CFDataRef d);
extern CFIndex CFDataGetLength(CFDataRef d);
extern CFNumberRef CFNumberCreate(CFAllocatorRef a, CFIndex type, const void *v);
extern CFDateRef CFDateCreate(CFAllocatorRef a, CFAbsoluteTime t);
extern CFTypeRef CFPropertyListCreateWithData(CFAllocatorRef a, CFDataRef d, CFOptionFlags opt, CFIndex *fmt, CFErrorRef *err);
extern CFDataRef CFPropertyListCreateData(CFAllocatorRef a, CFTypeRef pl, CFIndex fmt, CFOptionFlags opt, CFErrorRef *err);
extern CFTypeRef CFPropertyListCreateDeepCopy(CFAllocatorRef a, CFTypeRef pl, CFOptionFlags opt);

#endif
