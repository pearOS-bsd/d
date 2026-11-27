/* uninstalld for pearOS: the application uninstall service com.apple.uninstall.service of macOS 26.7.2
 * (Uninstall.framework/Resources/uninstalld, job com.apple.uninstalld, root; its other name com.apple.uninstalld is the old Mach interface of
 * UNDaemonConnection: not here).  Uninstall.framework's UNUninstallClient (the Dock's SystemApplicationUninstaller: initWithDelegate: at start,
 * queueRequest:error: when the user deletes an application from Launchpad / the Apps folder) talks to it with NSXPC:
 *   UNUninstallServiceProtocol (our declaration of Apple's 2 selectors), exported here:
 *     queueRequest:withPriority:reply:   (BOOL, NSError *)   sent asynchronously: queueRequest:error: returns YES at once (captured)
 *     abortRequest:reply:                (BOOL)
 *   UNUninstallClientProtocol, exported by the client: uninstallRequestDidBegin:, uninstallRequest:didMakeProgress:,
 *     uninstallRequestCannotBeAborted:, completedUninstallOfBundlePaths:
 * At start the Dock only makes the connection (decompiled: no message), so nothing waits for this service then.  Removing applications is
 * not done on pearOS yet: a request is answered NO with NSCocoaErrorDomain 3328 (NSFeatureUnsupportedError: ours, Apple's daemon uninstalls),
 * an abort NO; a client without the entitlement com.apple.private.uninstalld.client is refused.  Evidence: research/docs/DOCK-SERVICES-2.md 9.
 *
 *   uninstalld [service-name]     (default com.apple.uninstall.service; UN_LOG=1 logs; UN_NO_ENTITLEMENT_CHECK=1 accepts every client, for tests)
 * Our own code (BSD-2-Clause). */
#import "../common/objc-more.h"
#include "../common/csent.h"

static int logging;
static void L(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void L(const char *fmt, ...)
{
	va_list a;

	if (!logging) return;
	va_start(a, fmt); dprintf(2, "uninstalld: "); vdprintf(2, fmt, a); dprintf(2, "\n"); va_end(a);
}

@class UNUninstallClientRequest;	/* Uninstall.framework's class (NSSecureCoding): bundleURLs, requestID, preserveApplicationUserData, forceQuitForegroundApps */
@protocol UNService
- (void)abortRequest:(UNUninstallClientRequest *)request reply:(void (^)(BOOL))reply;
- (void)queueRequest:(UNUninstallClientRequest *)request withPriority:(long long)priority reply:(void (^)(BOOL, NSError *))reply;
@end
@protocol UNClient
- (void)completedUninstallOfBundlePaths:(NSArray *)paths;
- (void)uninstallRequest:(UNUninstallClientRequest *)request didMakeProgress:(int)percent;
- (void)uninstallRequestCannotBeAborted:(UNUninstallClientRequest *)request;
- (void)uninstallRequestDidBegin:(UNUninstallClientRequest *)request;
@end

@interface UNDService : NSObject <UNService, NSXPCListenerDelegate>
@end
@implementation UNDService
- (void)abortRequest:(UNUninstallClientRequest *)request reply:(void (^)(BOOL))reply
{
	L("abortRequest %s", request ? [(id)request description].UTF8String : "(nil)");
	if (reply) reply(NO);
}
- (void)queueRequest:(UNUninstallClientRequest *)request withPriority:(long long)priority reply:(void (^)(BOOL, NSError *))reply
{
	L("queueRequest %s priority %lld: not done on pearOS", request ? [(id)request description].UTF8String : "(nil)", priority);
	if (reply) reply(NO, [NSError errorWithDomain:NSCocoaErrorDomain code:3328 userInfo:nil]);
}
- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)c
{
	int pid = c.processIdentifier;
	id ent = [c valueForEntitlement:@"com.apple.private.uninstalld.client"];
	BOOL ok = (ent && [ent respondsToSelector:@selector(boolValue)] && [ent boolValue]) || csent_bool(pid, "com.apple.private.uninstalld.client") || getenv("UN_NO_ENTITLEMENT_CHECK");

	L("connection from pid %d: %s", pid, ok ? "accepted" : "refused (no com.apple.private.uninstalld.client)");
	if (!ok) return NO;
	c.exportedInterface = [NSXPCInterface interfaceWithProtocol:@protocol(UNService)];
	c.exportedObject = self;
	c.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(UNClient)];
	[c resume];
	return YES;
}
@end

int main(int argc, char **argv)
{
	@autoreleasepool {
		const char *name = argc > 1 ? argv[1] : "com.apple.uninstall.service";
		logging = getenv("UN_LOG") != 0;
		dlopen("/System/Library/PrivateFrameworks/Uninstall.framework/Uninstall", RTLD_NOW);	/* UNUninstallClientRequest */
		UNDService *d = [UNDService new];
		NSXPCListener *l = [[NSXPCListener alloc] initWithMachServiceName:[NSString stringWithUTF8String:name]];
		l.delegate = d;
		[l resume];
		L("serving %s", name);
		[[NSRunLoop currentRunLoop] run];
	}
	return 0;
}
