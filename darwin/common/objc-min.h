/* The few Foundation / libobjc declarations the Objective-C daemons of darwin/ use (windowmanager, wallpaperagent), so that they build with any
 * clang + ld64.lld (FreeBSD, Linux CI, a Mac) without an SDK.  Prototypes and values are Apple's; the symbols bind at run time to the Foundation,
 * CoreFoundation and libobjc that dyld finds (the Mac's own, on pearOS through macdeps).  Our own file (BSD-2-Clause). */
#ifndef DARWIN_OBJC_MIN_H
#define DARWIN_OBJC_MIN_H
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

/* libSystem (Darwin values) */
int vdprintf(int fd, const char *fmt, va_list ap);
int dprintf(int fd, const char *fmt, ...);
int sscanf(const char *s, const char *fmt, ...);
char *getenv(const char *name);
size_t strlen(const char *s);
void *dlopen(const char *path, int mode);
void *dlsym(void *handle, const char *name);
#define RTLD_NOW 0x2
#define RTLD_DEFAULT ((void *)-2)
typedef struct dispatch_queue_s *dispatch_queue_t;
extern struct dispatch_queue_s _dispatch_main_q;
#define dispatch_get_main_queue() ((dispatch_queue_t)&_dispatch_main_q)
typedef signed char BOOL;
#define YES ((BOOL)1)
#define NO ((BOOL)0)
#define nil ((id)0)
typedef long NSInteger;
typedef unsigned long NSUInteger;
typedef double CGFloat;
typedef struct CGSize { CGFloat width, height; } CGSize;
typedef struct CGPoint { CGFloat x, y; } CGPoint;
typedef struct CGRect { CGPoint origin; CGSize size; } CGRect;
typedef struct NSEdgeInsets { CGFloat top, left, bottom, right; } NSEdgeInsets;
typedef struct _NSZone NSZone;

id objc_msgSend(id, SEL, ...);
Class objc_getClass(const char *);
SEL sel_registerName(const char *);
const char *sel_getName(SEL);
const char *class_getName(Class);
Class object_getClass(id);

@class Protocol;
@class NSString, NSData, NSError, NSCoder, NSArray, NSDictionary, NSSet, NSNumber, NSXPCConnection, NSXPCListener, NSXPCInterface, NSRunLoop;

@protocol NSObject
- (BOOL)isEqual:(id)object;
- (Class)class;
- (BOOL)respondsToSelector:(SEL)aSelector;
- (NSString *)description;
@end
@protocol NSCopying
- (id)copyWithZone:(NSZone *)zone;
@end
@protocol NSCoding
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end
@protocol NSSecureCoding <NSCoding>
@property (class, readonly) BOOL supportsSecureCoding;
@end

__attribute__((objc_root_class))
@interface NSObject <NSObject> { Class isa; }
+ (instancetype)alloc;
+ (instancetype)new;
+ (Class)class;
- (instancetype)init;
- (id)performSelector:(SEL)s;
- (id)valueForKey:(NSString *)k;
- (void)setValue:(id)v forKey:(NSString *)k;
@end

@interface NSString : NSObject <NSCopying, NSSecureCoding>
+ (instancetype)stringWithUTF8String:(const char *)s;
+ (instancetype)stringWithFormat:(NSString *)fmt, ...;
@property (readonly) const char *UTF8String;
@property (readonly) NSUInteger length;
- (BOOL)isEqualToString:(NSString *)s;
- (NSData *)dataUsingEncoding:(NSUInteger)encoding;
@end
#define NSUTF8StringEncoding 4
@interface NSData : NSObject <NSCopying, NSSecureCoding>
+ (instancetype)dataWithBytes:(const void *)bytes length:(NSUInteger)length;
@property (readonly) const void *bytes;
@property (readonly) NSUInteger length;
@end
@interface NSNumber : NSObject <NSCopying, NSSecureCoding>
+ (NSNumber *)numberWithUnsignedLongLong:(unsigned long long)v;
+ (NSNumber *)numberWithLongLong:(long long)v;
+ (NSNumber *)numberWithBool:(BOOL)v;
@property (readonly) unsigned long long unsignedLongLongValue;
@property (readonly) long long longLongValue;
@property (readonly) unsigned int unsignedIntValue;
@property (readonly) BOOL boolValue;
@end
@interface NSArray : NSObject <NSCopying, NSSecureCoding>
+ (instancetype)array;
@property (readonly) NSUInteger count;
- (id)objectAtIndex:(NSUInteger)i;
@end
@interface NSDictionary : NSObject <NSCopying, NSSecureCoding>
+ (instancetype)dictionary;
@property (readonly) NSUInteger count;
@property (readonly) NSArray *allKeys;
- (id)objectForKey:(id)key;
@end
@interface NSMutableDictionary : NSDictionary
- (void)setObject:(id)o forKey:(id)k;
- (void)removeObjectForKey:(id)k;
@end
@interface NSSet : NSObject <NSCopying, NSSecureCoding>
+ (instancetype)setWithObjects:(id)first, ...;
@end
@interface NSError : NSObject <NSCopying, NSSecureCoding>
+ (instancetype)errorWithDomain:(NSString *)domain code:(NSInteger)code userInfo:(NSDictionary *)info;
@end
@interface NSCoder : NSObject
- (id)decodeObjectOfClass:(Class)c forKey:(NSString *)k;
- (void)encodeObject:(id)o forKey:(NSString *)k;
@end
@interface NSJSONSerialization : NSObject
+ (id)JSONObjectWithData:(NSData *)data options:(NSUInteger)opt error:(NSError **)error;
@end
@interface NSUUID : NSObject <NSCopying, NSSecureCoding>
+ (instancetype)UUID;
@property (readonly, copy) NSString *UUIDString;
@end
@interface NSRunLoop : NSObject
+ (NSRunLoop *)currentRunLoop;
- (void)run;
@end

@interface NSXPCInterface : NSObject
+ (NSXPCInterface *)interfaceWithProtocol:(Protocol *)protocol;
- (void)setClasses:(NSSet *)classes forSelector:(SEL)sel argumentIndex:(NSUInteger)arg ofReply:(BOOL)ofReply;
@end
@protocol NSXPCListenerDelegate <NSObject>
- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)newConnection;
@end
@interface NSXPCConnection : NSObject
@property (retain) NSXPCInterface *exportedInterface;
@property (retain) id exportedObject;
@property (retain) NSXPCInterface *remoteObjectInterface;
@property (copy) void (^invalidationHandler)(void);
@property (copy) void (^interruptionHandler)(void);
@property (readonly) int processIdentifier;
+ (NSXPCConnection *)currentConnection;
- (id)valueForEntitlement:(NSString *)entitlement;
- (id)remoteObjectProxy;
- (void)resume;
@end
@interface NSXPCListener : NSObject
- (instancetype)initWithMachServiceName:(NSString *)name;
@property (assign) id <NSXPCListenerDelegate> delegate;
- (void)resume;
@end
#endif
