/* dmd for pearOS: the Screen Time / device management policy service com.apple.dmd.policy of macOS 26.7.2 (/usr/libexec/dmd, the user's job
 * com.apple.dmd, which also serves com.apple.dmd and com.apple.dmd.emergency-mode: not here).  DeviceManagement.framework's DMFPolicyMonitor
 * (the Dock's ECScreenTimeController: +[DMFApplicationPolicyMonitor createPolicyMonitorWithPolicyChangeHandler:completionHandler:], which dims
 * the tiles of blocked applications) talks to it with NSXPC, protocol DMFPolicyRemoteInterface (our declaration of Apple's 7 selectors):
 *   addRegistration:replyHandler:                          (NSDictionary *, NSError *)   the client registers its policy types
 *   requestPoliciesForBundleIdentifiers:replyHandler:      (NSDictionary *, NSError *)   bundle id -> NSNumber (DMFPolicy)
 *   requestCommunicationPoliciesForBundleIdentifiers: / requestPoliciesForCategoryIdentifiers: / requestPoliciesForWebsiteURLs: (same shape)
 *   requestPoliciesForTypes:replyHandler:                  (NSDictionary *, NSError *)
 *   allExpiredScreenTimeBudgetsShouldBeSynchronous:replyHandler:   (NSArray *, NSError *)
 * What is Apple's and what is inferred (research/docs/DOCK-SERVICES-2.md 7): a client without the entitlement com.apple.private.dmd.policy is
 * refused (captured: the connection is closed at once); the framework turns an addRegistration answer with an empty dictionary into "Screen Time
 * is not enabled" (decompiled: the monitor's completion gets [reply count] != 0).  pearOS has no Screen Time, so: addRegistration and
 * requestPoliciesForTypes answer an empty dictionary, the per-identifier requests answer DMFPolicyOK (0) for every identifier, the budgets an
 * empty array; that is what a Mac without Screen Time must answer, but the answers of Apple's daemon to an entitled client were not observed.
 *
 *   dmd [service-name]     (default com.apple.dmd.policy; DMD_LOG=1 logs; DMD_NO_ENTITLEMENT_CHECK=1 accepts every client, for tests)
 * Our own code (BSD-2-Clause). */
#import "../common/objc-more.h"
#include "../common/csent.h"

static int logging;
static void L(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void L(const char *fmt, ...)
{
	va_list a;

	if (!logging) return;
	va_start(a, fmt); dprintf(2, "dmd: "); vdprintf(2, fmt, a); dprintf(2, "\n"); va_end(a);
}

@class DMFPolicyRegistration;	/* DeviceManagement.framework's class (NSSecureCoding): identifier, policyTypes */
@protocol DMDPolicyRemoteInterface
- (void)addRegistration:(DMFPolicyRegistration *)registration replyHandler:(void (^)(NSDictionary *, NSError *))reply;
- (void)allExpiredScreenTimeBudgetsShouldBeSynchronous:(BOOL)sync replyHandler:(void (^)(NSArray *, NSError *))reply;
- (void)requestCommunicationPoliciesForBundleIdentifiers:(NSArray *)ids replyHandler:(void (^)(NSDictionary *, NSError *))reply;
- (void)requestPoliciesForBundleIdentifiers:(NSArray *)ids replyHandler:(void (^)(NSDictionary *, NSError *))reply;
- (void)requestPoliciesForCategoryIdentifiers:(NSArray *)ids replyHandler:(void (^)(NSDictionary *, NSError *))reply;
- (void)requestPoliciesForTypes:(NSArray *)types replyHandler:(void (^)(NSDictionary *, NSError *))reply;
- (void)requestPoliciesForWebsiteURLs:(NSArray *)urls replyHandler:(void (^)(NSDictionary *, NSError *))reply;
@end

enum { DMFPolicyOK = 0 };

/* identifier -> DMFPolicyOK for every identifier of the request */
static NSDictionary *
allowed(NSArray *ids)
{
	NSMutableDictionary *d = [NSMutableDictionary new];

	for (id i in ids) [d setObject:[NSNumber numberWithInteger:DMFPolicyOK] forKey:i];
	return d;
}

@interface DMDService : NSObject <DMDPolicyRemoteInterface, NSXPCListenerDelegate>
@end
@implementation DMDService
- (void)addRegistration:(DMFPolicyRegistration *)registration replyHandler:(void (^)(NSDictionary *, NSError *))reply
{
	L("addRegistration %s", registration ? [[(id)registration valueForKey:@"identifier"] description].UTF8String : "(nil)");
	if (reply) reply([NSDictionary dictionary], nil);
}
- (void)allExpiredScreenTimeBudgetsShouldBeSynchronous:(BOOL)sync replyHandler:(void (^)(NSArray *, NSError *))reply { if (reply) reply([NSArray array], nil); }
- (void)requestCommunicationPoliciesForBundleIdentifiers:(NSArray *)ids replyHandler:(void (^)(NSDictionary *, NSError *))reply { if (reply) reply(allowed(ids), nil); }
- (void)requestPoliciesForBundleIdentifiers:(NSArray *)ids replyHandler:(void (^)(NSDictionary *, NSError *))reply
{
	L("requestPoliciesForBundleIdentifiers (%lu)", (unsigned long)ids.count);
	if (reply) reply(allowed(ids), nil);
}
- (void)requestPoliciesForCategoryIdentifiers:(NSArray *)ids replyHandler:(void (^)(NSDictionary *, NSError *))reply { if (reply) reply(allowed(ids), nil); }
- (void)requestPoliciesForTypes:(NSArray *)types replyHandler:(void (^)(NSDictionary *, NSError *))reply { if (reply) reply([NSDictionary dictionary], nil); }
- (void)requestPoliciesForWebsiteURLs:(NSArray *)urls replyHandler:(void (^)(NSDictionary *, NSError *))reply { if (reply) reply(allowed(urls), nil); }
- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)c
{
	int pid = c.processIdentifier;
	id ent = [c valueForEntitlement:@"com.apple.private.dmd.policy"];
	BOOL ok = (ent && [ent respondsToSelector:@selector(boolValue)] && [ent boolValue]) || csent_bool(pid, "com.apple.private.dmd.policy") || getenv("DMD_NO_ENTITLEMENT_CHECK");
	NSXPCInterface *i;

	L("connection from pid %d: %s", pid, ok ? "accepted" : "refused (no com.apple.private.dmd.policy)");
	if (!ok) return NO;
	i = [NSXPCInterface interfaceWithProtocol:@protocol(DMDPolicyRemoteInterface)];
	[i setClasses:[NSSet setWithObjects:[NSArray class], [NSString class], nil] forSelector:@selector(requestPoliciesForBundleIdentifiers:replyHandler:) argumentIndex:0 ofReply:NO];
	[i setClasses:[NSSet setWithObjects:[NSArray class], [NSString class], nil] forSelector:@selector(requestCommunicationPoliciesForBundleIdentifiers:replyHandler:) argumentIndex:0 ofReply:NO];
	[i setClasses:[NSSet setWithObjects:[NSArray class], [NSString class], nil] forSelector:@selector(requestPoliciesForCategoryIdentifiers:replyHandler:) argumentIndex:0 ofReply:NO];
	[i setClasses:[NSSet setWithObjects:[NSArray class], [NSString class], nil] forSelector:@selector(requestPoliciesForTypes:replyHandler:) argumentIndex:0 ofReply:NO];
	[i setClasses:[NSSet setWithObjects:[NSArray class], [NSURL class], nil] forSelector:@selector(requestPoliciesForWebsiteURLs:replyHandler:) argumentIndex:0 ofReply:NO];
	c.exportedInterface = i;
	c.exportedObject = self;
	[c resume];
	return YES;
}
@end

int main(int argc, char **argv)
{
	@autoreleasepool {
		const char *name = argc > 1 ? argv[1] : "com.apple.dmd.policy";
		logging = getenv("DMD_LOG") != 0;
		/* DMFPolicyRegistration, the class of addRegistration:'s argument */
		dlopen("/System/Library/PrivateFrameworks/DeviceManagement.framework/DeviceManagement", RTLD_NOW);
		DMDService *d = [DMDService new];
		NSXPCListener *l = [[NSXPCListener alloc] initWithMachServiceName:[NSString stringWithUTF8String:name]];
		l.delegate = d;
		[l resume];
		L("serving %s", name);
		[[NSRunLoop currentRunLoop] run];
	}
	return 0;
}
