/* csnameddatad for pearOS: CarbonCore's named data store com.apple.carboncore.csnameddata of macOS 26.7.2 (the XPC service
 * CoreServices.framework/.../CarbonCore.framework/XPCServices/csnameddatad.xpc, ServiceType User: one instance per user).
 * CarbonCore's _CSSetNamedData(name, 0, bytes, length) / _CSCopyNamedData(name, 0, &error) / _CSGetNamedData / _CSFlushNamedData talk to it
 * with NSXPC, protocol _CSNamedDataReadWriteProtocol (our declaration of Apple's 8 selectors and signatures):
 *   setNamedData:data:                    one-way; nil data removes the name
 *   setNamedData:data:withReply:          -> (YES, nil)
 *   getNamedData:withReply:               -> (data or nil, nil)        a missing name is (nil, nil), not an error
 *   flushNamedData: / flushNamedData:withReply:   remove the name     -> (YES, nil), also for a missing name
 *   getDataKeysWithReply:                 -> (array of names, nil)
 *   getDataItemsWithReply:                -> (dictionary name -> data, nil)
 *   terminate                             the service exits
 * Every answer above is what Apple's service gives (captured on the Mac, research/docs/DOCK-SERVICES-2.md 6).  The store lives in memory, as
 * Apple's: it is empty after a restart of the session.  The Dock writes "com.apple.dock,anwrite.<uid>" (8 bytes, a CFAbsoluteTime) here.
 *
 *   csnameddatad [service-name]     (default com.apple.carboncore.csnameddata; ND_LOG=1 logs to stderr)
 * Our own code (BSD-2-Clause). */
#import "../common/objc-more.h"

extern void exit(int);

static int logging;
static void L(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void L(const char *fmt, ...)
{
	va_list a;

	if (!logging) return;
	va_start(a, fmt); dprintf(2, "csnameddatad: "); vdprintf(2, fmt, a); dprintf(2, "\n"); va_end(a);
}

@protocol NDNamedDataReadWrite
- (void)terminate;
- (void)flushNamedData:(NSString *)name;
- (void)flushNamedData:(NSString *)name withReply:(void (^)(BOOL, NSError *))reply;
- (void)getDataItemsWithReply:(void (^)(NSDictionary *, NSError *))reply;
- (void)getDataKeysWithReply:(void (^)(NSArray *, NSError *))reply;
- (void)getNamedData:(NSString *)name withReply:(void (^)(NSData *, NSError *))reply;
- (void)setNamedData:(NSString *)name data:(NSData *)data;
- (void)setNamedData:(NSString *)name data:(NSData *)data withReply:(void (^)(BOOL, NSError *))reply;
@end

static NSMutableDictionary *store;

@interface NDService : NSObject <NDNamedDataReadWrite, NSXPCListenerDelegate>
@end
@implementation NDService
- (void)terminate { L("terminate"); exit(0); }
- (void)flushNamedData:(NSString *)name { if (name) @synchronized (store) { [store removeObjectForKey:name]; } }
- (void)flushNamedData:(NSString *)name withReply:(void (^)(BOOL, NSError *))reply { [self flushNamedData:name]; if (reply) reply(YES, nil); }
- (void)getDataItemsWithReply:(void (^)(NSDictionary *, NSError *))reply { NSDictionary *d; @synchronized (store) { d = [store copy]; } if (reply) reply(d, nil); }
- (void)getDataKeysWithReply:(void (^)(NSArray *, NSError *))reply { NSArray *k; @synchronized (store) { k = store.allKeys; } if (reply) reply(k, nil); }
- (void)getNamedData:(NSString *)name withReply:(void (^)(NSData *, NSError *))reply
{
	NSData *d = nil;

	if (name) @synchronized (store) { d = [store objectForKey:name]; }
	L("get %s: %s", name ? name.UTF8String : "(nil)", d ? "found" : "none");
	if (reply) reply(d, nil);
}
- (void)setNamedData:(NSString *)name data:(NSData *)data
{
	if (!name) return;
	L("set %s (%lu bytes)", name.UTF8String, data ? (unsigned long)data.length : 0UL);
	@synchronized (store) { if (data) [store setObject:data forKey:name]; else [store removeObjectForKey:name]; }
}
- (void)setNamedData:(NSString *)name data:(NSData *)data withReply:(void (^)(BOOL, NSError *))reply { [self setNamedData:name data:data]; if (reply) reply(YES, nil); }
- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)c
{
	c.exportedInterface = [NSXPCInterface interfaceWithProtocol:@protocol(NDNamedDataReadWrite)];
	c.exportedObject = self;
	L("connection from pid %d", c.processIdentifier);
	[c resume];
	return YES;
}
@end

int main(int argc, char **argv)
{
	@autoreleasepool {
		const char *name = argc > 1 ? argv[1] : "com.apple.carboncore.csnameddata";
		logging = getenv("ND_LOG") != 0;
		store = [NSMutableDictionary new];
		NDService *d = [NDService new];
		NSXPCListener *l = [[NSXPCListener alloc] initWithMachServiceName:[NSString stringWithUTF8String:name]];
		l.delegate = d;
		[l resume];
		L("serving %s", name);
		[[NSRunLoop currentRunLoop] run];
	}
	return 0;
}
