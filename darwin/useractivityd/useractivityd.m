/* useractivityd for pearOS: the Handoff best-app service com.apple.coreservices.lsbestappsuggestionmanager.xpc of macOS 26.7.2
 * (UserActivity.framework/Agents/useractivityd, the user's job com.apple.coreservices.useractivityd, which also serves the user activity,
 * shared pasteboard and administration services: not here).  UserActivity.framework's UABestAppSuggestionManager (the Dock: alloc/init,
 * setDelegate:, startListeningForBestAppSuggestions, for the Handoff tile at the left/top end of the Dock) talks to it with NSXPC:
 *   UABestAppSuggestionManagerProtocol (our declaration of Apple's 10 selectors), exported here
 *   UABestAppSuggestionManagerResponseProtocol: notifyBestAppsChanged:when:confidence:, exported by the client
 * startListeningForBestAppSuggestions sends doRegisterForBestAppChangeNotification (one-way; captured).  A client without the entitlement
 * com.apple.private.coreservices.lsuseractivityd.bestappsuggestion is refused, as by Apple's daemon (captured: the connection is closed,
 * the framework connects again and again).  pearOS has no Continuity, so there is never a suggestion: the replies say "none" (empty arrays,
 * no date, confidence 0, NO), no notifyBestAppsChanged is ever sent, and doQueueFetchOfPayloadForBestAppSuggestion: (a fetch for a suggestion
 * that cannot exist) answers NSCocoaErrorDomain 3328 (NSFeatureUnsupportedError: ours, Apple's error for it is not known).
 * Evidence: research/docs/DOCK-SERVICES-2.md 8.
 *
 *   useractivityd [service-name]     (default com.apple.coreservices.lsbestappsuggestionmanager.xpc; UA_LOG=1 logs;
 *                                     UA_NO_ENTITLEMENT_CHECK=1 accepts every client, for tests)
 * Our own code (BSD-2-Clause). */
#import "../common/objc-more.h"
#include "../common/csent.h"

static int logging;
static void L(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void L(const char *fmt, ...)
{
	va_list a;

	if (!logging) return;
	va_start(a, fmt); dprintf(2, "useractivityd: "); vdprintf(2, fmt, a); dprintf(2, "\n"); va_end(a);
}

@protocol UABestAppManager
- (void)doConnected;
- (void)doDetermineBestAppSuggestionWithCompletionHandler:(void (^)(NSArray *, NSDate *, double))done;
- (void)doDetermineBestAppSuggestions:(long long)max completionHandler:(void (^)(NSArray *, NSError *))done;
- (void)doLaunchFollowUp:(NSUUID *)uuid interactionType:(unsigned long long)type cancelled:(BOOL)cancelled;
- (void)doQueueFetchOfPayloadForBestAppSuggestion:(NSUUID *)uuid completionHandler:(void (^)(NSError *))done;
- (void)doRegisterForBestAppChangeNotification;
- (void)doRemoveBestAppSuggestion:(NSUUID *)uuid options:(NSDictionary *)options;
- (void)doStartBestAppSuggestion:(double)interval;
- (void)doUnregisterForBestAppChangeNotification;
- (void)fetchAllNearbyAppSuggestions:(void (^)(BOOL))done;
@end
@protocol UABestAppManagerResponse
@optional
- (void)notifyBestAppsChanged:(NSArray *)suggestions when:(NSDate *)when confidence:(double)confidence;
@end

@interface UAClient : NSObject <UABestAppManager>
@property (assign) int pid;
@property (assign) BOOL registered;
@end
@implementation UAClient
- (void)doConnected { L("pid %d doConnected", _pid); }
- (void)doDetermineBestAppSuggestionWithCompletionHandler:(void (^)(NSArray *, NSDate *, double))done { if (done) done([NSArray array], nil, 0.0); }
- (void)doDetermineBestAppSuggestions:(long long)max completionHandler:(void (^)(NSArray *, NSError *))done { if (done) done([NSArray array], nil); }
- (void)doLaunchFollowUp:(NSUUID *)uuid interactionType:(unsigned long long)type cancelled:(BOOL)cancelled { }
- (void)doQueueFetchOfPayloadForBestAppSuggestion:(NSUUID *)uuid completionHandler:(void (^)(NSError *))done
{
	if (done) done([NSError errorWithDomain:NSCocoaErrorDomain code:3328 userInfo:nil]);
}
- (void)doRegisterForBestAppChangeNotification { _registered = YES; L("pid %d doRegisterForBestAppChangeNotification", _pid); }
- (void)doRemoveBestAppSuggestion:(NSUUID *)uuid options:(NSDictionary *)options { }
- (void)doStartBestAppSuggestion:(double)interval { L("pid %d doStartBestAppSuggestion %g", _pid, interval); }
- (void)doUnregisterForBestAppChangeNotification { _registered = NO; }
- (void)fetchAllNearbyAppSuggestions:(void (^)(BOOL))done { if (done) done(NO); }
@end

@interface UAListener : NSObject <NSXPCListenerDelegate>
@property (strong) NSMutableDictionary *clients;
@end
@implementation UAListener
- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)c
{
	static const char *const ent = "com.apple.private.coreservices.lsuseractivityd.bestappsuggestion";
	int pid = c.processIdentifier;
	id v = [c valueForEntitlement:[NSString stringWithUTF8String:ent]];
	BOOL ok = (v && [v respondsToSelector:@selector(boolValue)] && [v boolValue]) || csent_bool(pid, ent) || getenv("UA_NO_ENTITLEMENT_CHECK");
	UAClient *cl;
	NSNumber *key;
	__weak UAListener *weakSelf = self;

	L("connection from pid %d: %s", pid, ok ? "accepted" : "refused (no bestappsuggestion entitlement)");
	if (!ok) return NO;
	cl = [UAClient new];
	cl.pid = pid;
	key = [NSNumber numberWithLongLong:(long long)(intptr_t)(__bridge void *)c];
	c.exportedInterface = [NSXPCInterface interfaceWithProtocol:@protocol(UABestAppManager)];
	c.exportedObject = cl;
	c.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(UABestAppManagerResponse)];
	c.invalidationHandler = ^{ @synchronized (weakSelf.clients) { [weakSelf.clients removeObjectForKey:key]; } };
	@synchronized (_clients) { [_clients setObject:cl forKey:key]; }
	[c resume];
	return YES;
}
@end

int main(int argc, char **argv)
{
	@autoreleasepool {
		const char *name = argc > 1 ? argv[1] : "com.apple.coreservices.lsbestappsuggestionmanager.xpc";
		logging = getenv("UA_LOG") != 0;
		UAListener *d = [UAListener new];
		d.clients = [NSMutableDictionary new];
		NSXPCListener *l = [[NSXPCListener alloc] initWithMachServiceName:[NSString stringWithUTF8String:name]];
		l.delegate = d;
		[l resume];
		L("serving %s", name);
		[[NSRunLoop currentRunLoop] run];
	}
	return 0;
}
