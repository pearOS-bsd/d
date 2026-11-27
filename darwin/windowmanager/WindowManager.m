/* WindowManager: the server of com.apple.windowmanager.external (WindowManager.app of macOS 26.7.2) for pearOS.
 * The Dock (WindowManager.framework, class AgentXPCProxy) connects to it with NSXPC; the exported interface is the framework's Swift protocol
 * AgentXPCProxyServerInterface (46 methods), the client exports AgentXPCProxyClientInterface (8 callbacks).  The arguments are NSSecureCoding
 * boxes (WindowManagerXPC*) that carry one NSData under one key: the JSON of a Swift Codable value.
 * Evidence (selectors, type encodings, coder keys, the entitlement, the Dock's start-up conversation): research/docs/DOCK-WINDOWMANAGER-WALLPAPER.md.
 *
 * What the Dock does at start: xpcBeginDesktopPeekMonitor:completion: (asynchronous).  The Mac's server answers the completion and sends
 * desktopPeekMonitorStateDidChange: to the client.  That is what this server does; the other methods answer their reply with an error
 * (NSCocoaErrorDomain 3328, NSFeatureUnsupportedError: ours) or NO / 0, so that no caller waits for a reply that never comes.
 *
 *   WindowManager [service-name]      (default com.apple.windowmanager.external; WM_LOG=1 logs to stderr;
 *                                      WM_NO_ENTITLEMENT_CHECK=1 accepts clients without com.apple.private.windowmanager, for tests)
 * Our own code (BSD-2-Clause). */
#import "../common/objc-min.h"
#include "../common/csent.h"

static int logging;
static void L(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void L(const char *fmt, ...)
{
	va_list a;

	if (!logging) return;
	va_start(a, fmt); dprintf(2, "WindowManager: "); vdprintf(2, fmt, a); dprintf(2, "\n"); va_end(a);
}

/* ---- the argument classes: one NSData under one key (keys read from WindowManager.framework's initWithCoder:, see the research doc) ---- */
@interface WMXPCBox : NSObject <NSSecureCoding>
@property (strong) NSData *raw;
+ (NSString *)coderKey;
- (NSString *)json;
@end
@implementation WMXPCBox
+ (BOOL)supportsSecureCoding { return YES; }
+ (NSString *)coderKey { return nil; }
- (instancetype)initWithCoder:(NSCoder *)c
{
	if (!(self = [super init])) return nil;
	_raw = [c decodeObjectOfClass:[NSData class] forKey:[[self class] coderKey]];
	return _raw ? self : nil;
}
- (void)encodeWithCoder:(NSCoder *)c { [c encodeObject:_raw forKey:[[self class] coderKey]]; }
- (NSString *)json { return _raw ? [NSString stringWithFormat:@"%.*s", (int)_raw.length, (const char *)_raw.bytes] : @"(none)"; }
@end
#define BOX(name, key) @interface name : WMXPCBox @end @implementation name + (NSString *)coderKey { return @key; } @end
BOX(WindowManagerXPCLayoutState, "WMXPCLS")
BOX(WindowManagerXPCTransitionAnimations, "WMXPCTA")
BOX(WindowManagerXPCDisplayAnimations, "WMXPCDA")
BOX(WindowManagerXPCDraggableOverlayTarget, "WMXPCDOT")
BOX(WindowManagerXPCDraggableOverlaySessionID, "WMXPCDOSID")
BOX(WindowManagerXPCRemoteTransitionType, "WMXPCRTT")
BOX(WindowManagerXPCWindowPickingSessionWindow, "WMXPCWPSW")
BOX(WindowManagerXPCWindowPickingSessionClientInfo, "WMXPCWPOVCI")
BOX(WindowManagerXPCDesktopPeekMonitorID, "WMXPCDPMID")
BOX(WindowManagerXPCDesktopState, "WMXPCDS")
BOX(WindowManagerXPCModalRevealAssertionID, "WMXPCRAID")
BOX(WindowManagerXPCHideWindowsInclusion, "WMXPCHWI")
BOX(WindowManagerXPCWindowPickingSessionExclusionList, "WMXPCWPSEL")
BOX(WindowManagerXPCWindowPickingSessionConfiguration, "WMXPCWPSC")
BOX(WindowManagerXPCExposeCoordinatorID, "WMXPCWVID")
BOX(WindowManagerXPCExposeMode, "WMXPCWVWM")
BOX(WindowManagerXPCExposeToggleTransition, "WMXPCETT")
BOX(WindowManagerXPCExposeGestureAssertionID, "WMXPCEGAID")
BOX(WindowManagerXPCExposeGestureAssertionDirection, "WMXPCEGAD")
/* a Swift class of the framework without an @objc name: its Objective-C name is the mangled one */
__attribute__((objc_runtime_name("_TtC13WindowManager37WindowManagerXPCTilingPositionRequest")))
@interface WMXPCTilingPositionRequest : WMXPCBox @end
@implementation WMXPCTilingPositionRequest + (NSString *)coderKey { return @"WMXPCTILPOS"; } @end
@class CAFenceHandle;

/* ---- the protocols: the selectors and (extended) type encodings of the framework's, one for one ---- */
@protocol WMAgentXPCProxyServerInterface
- (void)xpcActivateWindowWithWindow:(unsigned int)w completion:(void (^)(WindowManagerXPCLayoutState *, NSError *))c;
- (void)xpcAddWindowsToSpaceWithWindowIDs:(NSArray *)w spaceID:(unsigned long long)s completion:(void (^)(BOOL))c;
- (void)xpcAssignWindows:(NSArray *)w toSpaces:(NSArray *)s removingFrom:(unsigned int)o fence:(CAFenceHandle *)f completion:(void (^)(NSError *))c;
- (void)xpcBeginDesktopPeekMonitor:(WindowManagerXPCDesktopPeekMonitorID *)i completion:(void (^)(NSError *))c;
- (void)xpcBeginDraggableOverlaySession:(WindowManagerXPCDraggableOverlaySessionID *)i target:(WindowManagerXPCDraggableOverlayTarget *)t overlayWindowID:(unsigned int)w contentInsets:(NSEdgeInsets)e completion:(void (^)(NSError *))c;
- (void)xpcBeginExposeCoordinator:(WindowManagerXPCExposeCoordinatorID *)i completion:(void (^)(NSError *))c;
- (void)xpcBeginWindowPickingModeWithSelectedWindow:(unsigned int)w completion:(void (^)(NSError *))c;
- (void)xpcBeginWindowPickingSessionWithConfiguration:(WindowManagerXPCWindowPickingSessionConfiguration *)k completion:(void (^)(NSError *))c;
- (void)xpcCancelRemoteTransitionInCompletion:(void (^)(NSError *))c;
- (void)xpcCancelRemoteTransitionOutWithCompletion:(void (^)(NSError *))c;
- (void)xpcCommitPendingUpdatesWithCompletion:(void (^)(BOOL))c;
- (void)xpcCreateManagedSpaceWithDisplayUUID:(NSString *)u completion:(void (^)(unsigned long long, NSError *))c;
- (void)xpcDestroySpaceWithSpace:(unsigned long long)s completion:(void (^)(NSError *))c;
- (void)xpcDiagnose:(void (^)(NSData *, NSError *))c;
- (void)xpcEndDesktopPeekMonitor:(WindowManagerXPCDesktopPeekMonitorID *)i completion:(void (^)(NSError *))c;
- (void)xpcEndDraggableOverlaySession:(WindowManagerXPCDraggableOverlaySessionID *)i completion:(void (^)(NSError *))c;
- (void)xpcEndExposeCoordinator:(WindowManagerXPCExposeCoordinatorID *)i completion:(void (^)(NSError *))c;
- (void)xpcEndWindowPickingModeWithCompletion:(void (^)(NSError *))c;
- (void)xpcEndWindowPickingSessionWithCompletion:(void (^)(NSError *))c;
- (void)xpcExposeCoordinator:(WindowManagerXPCExposeCoordinatorID *)i toggleExposeMode:(WindowManagerXPCExposeMode *)m transition:(WindowManagerXPCExposeToggleTransition *)t completion:(void (^)(NSError *))c;
- (void)xpcFinalizeRemoteTransitionInCompletion:(void (^)(NSError *))c;
- (void)xpcFinalizeRemoteTransitionOutWithCompletion:(void (^)(NSError *))c;
- (void)xpcGetCurrentWindowPickingOrderedWindowsPerSpaceWithCompletion:(void (^)(NSDictionary *, NSError *))c;
- (void)xpcGetExpectedFrameWithTarget:(WindowManagerXPCDraggableOverlayTarget *)t overlaySize:(CGSize)s contentInsets:(NSEdgeInsets)e completion:(void (^)(id, NSError *))c;
- (void)xpcHighlightWindowForPicking:(unsigned int)w completion:(void (^)(NSError *))c;
- (void)xpcInitializeRemoteTransitionCoordinationOutWithInitialLayoutState:(WindowManagerXPCLayoutState *)l windowAnimations:(WindowManagerXPCTransitionAnimations *)w displayAnimations:(WindowManagerXPCDisplayAnimations *)d options:(unsigned char)o startingAt:(double)s completion:(void (^)(NSError *))c;
- (void)xpcInitializeRemoteTransitionInWithFinalLayoutState:(WindowManagerXPCLayoutState *)l windowAnimations:(WindowManagerXPCTransitionAnimations *)w displayAnimations:(WindowManagerXPCDisplayAnimations *)d options:(unsigned char)o startingAt:(double)s completion:(void (^)(NSError *))c;
- (void)xpcProvideVisibilityHintForSpace:(unsigned long long)s completion:(void (^)(NSError *))c;
- (void)xpcReleaseExposeGestureAssertion:(WindowManagerXPCExposeGestureAssertionID *)i completion:(void (^)(NSError *))c;
- (void)xpcReleaseLayoutControlWithCompletion:(void (^)(NSError *))c;
- (void)xpcReleaseModalRevealAssertion:(WindowManagerXPCModalRevealAssertionID *)i completion:(void (^)(NSError *))c;
- (void)xpcReleaseRemoteTransitionCoordinationWithCompletion:(void (^)(NSError *))c;
- (void)xpcRequestLayoutControlWithCompletion:(void (^)(WindowManagerXPCLayoutState *, NSError *))c;
- (void)xpcRequestLayoutStateWithCompletion:(void (^)(WindowManagerXPCLayoutState *, NSError *))c;
- (void)xpcRequestRemoteTransitionCoordinationWithTransitionType:(WindowManagerXPCRemoteTransitionType *)t completion:(void (^)(WindowManagerXPCLayoutState *, NSError *))c;
- (void)xpcSetStageManagerEnabled:(BOOL)e completion:(void (^)(BOOL))c;
- (void)xpcTakeExposeGestureAssertion:(WindowManagerXPCExposeGestureAssertionID *)i direction:(WindowManagerXPCExposeGestureAssertionDirection *)d completion:(void (^)(NSError *))c;
- (void)xpcTakeModalRevealAssertion:(WindowManagerXPCModalRevealAssertionID *)i withInclusions:(NSArray *)n animated:(BOOL)a completion:(void (^)(NSError *))c;
- (void)xpcTileWindow:(WMXPCTilingPositionRequest *)r completion:(void (^)(NSError *))c;
- (void)xpcUpdateRemoteTransitionInTo:(double)p completion:(void (^)(NSError *))c;
- (void)xpcUpdateRemoteTransitionOutTo:(double)p completion:(void (^)(NSError *))c;
- (void)xpcUpdateWindowPickingSessionClientInfo:(WindowManagerXPCWindowPickingSessionClientInfo *)i completion:(void (^)(NSError *))c;
@end
@protocol WMAgentXPCProxyClientInterface
- (void)assertionsDidCancelWithIDs:(NSArray *)ids;
- (void)desktopPeekMonitorStateDidChange:(NSDictionary *)states;
- (void)didCancelWindowPickingMode;
- (void)didEndWindowPickingModeWithSelectedWindow:(unsigned int)w;
- (void)draggableOverlaySessionTargetDidClose:(WindowManagerXPCDraggableOverlaySessionID *)i;
- (void)exposeModeDidChange:(WindowManagerXPCExposeMode *)m;
- (void)minimumDockRevealAmountDidChange:(double)a;
- (void)windowPickingSessionDidUpdateWithOrderedWindows:(NSDictionary *)w;
@end

static NSError *unsupported(void) { return [NSError errorWithDomain:@"NSCocoaErrorDomain" code:3328 userInfo:nil]; }
#define NOTYET(sel) L("%s: not implemented, answered with an error", sel)

/* ---- one client connection ---- */
@interface WMClient : NSObject <WMAgentXPCProxyServerInterface>
@property (strong) NSXPCConnection *connection;
@property (strong) NSMutableDictionary *peekMonitors;		/* the JSON of each WindowManagerXPCDesktopPeekMonitorID (its id) -> the box */
@end
@implementation WMClient
- (void)xpcBeginDesktopPeekMonitor:(WindowManagerXPCDesktopPeekMonitorID *)i completion:(void (^)(NSError *))c
{
	L("pid %d: xpcBeginDesktopPeekMonitor %s", _connection.processIdentifier, [i json].UTF8String);
	if (i) [_peekMonitors setObject:i forKey:[i json]];
	if (c) c(nil);
	/* the Mac's server then sends the last desktop state it computed, per space: nothing is peeked on pearOS (no click-to-reveal-desktop yet),
	 * so the dictionary of space ID -> WindowManagerXPCDesktopState is empty */
	[(id <WMAgentXPCProxyClientInterface>)[_connection remoteObjectProxy] desktopPeekMonitorStateDidChange:[NSDictionary dictionary]];
}
- (void)xpcEndDesktopPeekMonitor:(WindowManagerXPCDesktopPeekMonitorID *)i completion:(void (^)(NSError *))c
{
	L("pid %d: xpcEndDesktopPeekMonitor %s", _connection.processIdentifier, [i json].UTF8String);
	if (i) [_peekMonitors removeObjectForKey:[i json]];
	if (c) c(nil);
}
- (void)xpcBeginExposeCoordinator:(WindowManagerXPCExposeCoordinatorID *)i completion:(void (^)(NSError *))c { L("xpcBeginExposeCoordinator %s", [i json].UTF8String); if (c) c(nil); }
- (void)xpcEndExposeCoordinator:(WindowManagerXPCExposeCoordinatorID *)i completion:(void (^)(NSError *))c { L("xpcEndExposeCoordinator %s", [i json].UTF8String); if (c) c(nil); }
- (void)xpcProvideVisibilityHintForSpace:(unsigned long long)s completion:(void (^)(NSError *))c { L("xpcProvideVisibilityHintForSpace %llu", s); if (c) c(nil); }
- (void)xpcCommitPendingUpdatesWithCompletion:(void (^)(BOOL))c { L("xpcCommitPendingUpdates"); if (c) c(YES); }

/* the rest: no Stage Manager, Mission Control, window picking, tiling or remote transitions on pearOS yet */
- (void)xpcActivateWindowWithWindow:(unsigned int)w completion:(void (^)(WindowManagerXPCLayoutState *, NSError *))c { NOTYET("xpcActivateWindowWithWindow"); if (c) c(nil, unsupported()); }
- (void)xpcAddWindowsToSpaceWithWindowIDs:(NSArray *)w spaceID:(unsigned long long)s completion:(void (^)(BOOL))c { NOTYET("xpcAddWindowsToSpace"); if (c) c(NO); }
- (void)xpcAssignWindows:(NSArray *)w toSpaces:(NSArray *)s removingFrom:(unsigned int)o fence:(CAFenceHandle *)f completion:(void (^)(NSError *))c { NOTYET("xpcAssignWindows"); if (c) c(unsupported()); }
- (void)xpcBeginDraggableOverlaySession:(WindowManagerXPCDraggableOverlaySessionID *)i target:(WindowManagerXPCDraggableOverlayTarget *)t overlayWindowID:(unsigned int)w contentInsets:(NSEdgeInsets)e completion:(void (^)(NSError *))c { NOTYET("xpcBeginDraggableOverlaySession"); if (c) c(unsupported()); }
- (void)xpcBeginWindowPickingModeWithSelectedWindow:(unsigned int)w completion:(void (^)(NSError *))c { NOTYET("xpcBeginWindowPickingMode"); if (c) c(unsupported()); }
- (void)xpcBeginWindowPickingSessionWithConfiguration:(WindowManagerXPCWindowPickingSessionConfiguration *)k completion:(void (^)(NSError *))c { NOTYET("xpcBeginWindowPickingSession"); if (c) c(unsupported()); }
- (void)xpcCancelRemoteTransitionInCompletion:(void (^)(NSError *))c { NOTYET("xpcCancelRemoteTransitionIn"); if (c) c(unsupported()); }
- (void)xpcCancelRemoteTransitionOutWithCompletion:(void (^)(NSError *))c { NOTYET("xpcCancelRemoteTransitionOut"); if (c) c(unsupported()); }
- (void)xpcCreateManagedSpaceWithDisplayUUID:(NSString *)u completion:(void (^)(unsigned long long, NSError *))c { NOTYET("xpcCreateManagedSpace"); if (c) c(0, unsupported()); }
- (void)xpcDestroySpaceWithSpace:(unsigned long long)s completion:(void (^)(NSError *))c { NOTYET("xpcDestroySpace"); if (c) c(unsupported()); }
- (void)xpcDiagnose:(void (^)(NSData *, NSError *))c { NOTYET("xpcDiagnose"); if (c) c(nil, unsupported()); }
- (void)xpcEndDraggableOverlaySession:(WindowManagerXPCDraggableOverlaySessionID *)i completion:(void (^)(NSError *))c { NOTYET("xpcEndDraggableOverlaySession"); if (c) c(unsupported()); }
- (void)xpcEndWindowPickingModeWithCompletion:(void (^)(NSError *))c { NOTYET("xpcEndWindowPickingMode"); if (c) c(unsupported()); }
- (void)xpcEndWindowPickingSessionWithCompletion:(void (^)(NSError *))c { NOTYET("xpcEndWindowPickingSession"); if (c) c(unsupported()); }
- (void)xpcExposeCoordinator:(WindowManagerXPCExposeCoordinatorID *)i toggleExposeMode:(WindowManagerXPCExposeMode *)m transition:(WindowManagerXPCExposeToggleTransition *)t completion:(void (^)(NSError *))c { NOTYET("xpcExposeCoordinator:toggleExposeMode"); if (c) c(unsupported()); }
- (void)xpcFinalizeRemoteTransitionInCompletion:(void (^)(NSError *))c { NOTYET("xpcFinalizeRemoteTransitionIn"); if (c) c(unsupported()); }
- (void)xpcFinalizeRemoteTransitionOutWithCompletion:(void (^)(NSError *))c { NOTYET("xpcFinalizeRemoteTransitionOut"); if (c) c(unsupported()); }
- (void)xpcGetCurrentWindowPickingOrderedWindowsPerSpaceWithCompletion:(void (^)(NSDictionary *, NSError *))c { NOTYET("xpcGetCurrentWindowPickingOrderedWindowsPerSpace"); if (c) c(nil, unsupported()); }
- (void)xpcGetExpectedFrameWithTarget:(WindowManagerXPCDraggableOverlayTarget *)t overlaySize:(CGSize)s contentInsets:(NSEdgeInsets)e completion:(void (^)(id, NSError *))c { NOTYET("xpcGetExpectedFrame"); if (c) c(nil, unsupported()); }
- (void)xpcHighlightWindowForPicking:(unsigned int)w completion:(void (^)(NSError *))c { NOTYET("xpcHighlightWindowForPicking"); if (c) c(unsupported()); }
- (void)xpcInitializeRemoteTransitionCoordinationOutWithInitialLayoutState:(WindowManagerXPCLayoutState *)l windowAnimations:(WindowManagerXPCTransitionAnimations *)w displayAnimations:(WindowManagerXPCDisplayAnimations *)d options:(unsigned char)o startingAt:(double)s completion:(void (^)(NSError *))c { NOTYET("xpcInitializeRemoteTransitionCoordinationOut"); if (c) c(unsupported()); }
- (void)xpcInitializeRemoteTransitionInWithFinalLayoutState:(WindowManagerXPCLayoutState *)l windowAnimations:(WindowManagerXPCTransitionAnimations *)w displayAnimations:(WindowManagerXPCDisplayAnimations *)d options:(unsigned char)o startingAt:(double)s completion:(void (^)(NSError *))c { NOTYET("xpcInitializeRemoteTransitionIn"); if (c) c(unsupported()); }
- (void)xpcReleaseExposeGestureAssertion:(WindowManagerXPCExposeGestureAssertionID *)i completion:(void (^)(NSError *))c { NOTYET("xpcReleaseExposeGestureAssertion"); if (c) c(unsupported()); }
- (void)xpcReleaseLayoutControlWithCompletion:(void (^)(NSError *))c { NOTYET("xpcReleaseLayoutControl"); if (c) c(unsupported()); }
- (void)xpcReleaseModalRevealAssertion:(WindowManagerXPCModalRevealAssertionID *)i completion:(void (^)(NSError *))c { NOTYET("xpcReleaseModalRevealAssertion"); if (c) c(unsupported()); }
- (void)xpcReleaseRemoteTransitionCoordinationWithCompletion:(void (^)(NSError *))c { NOTYET("xpcReleaseRemoteTransitionCoordination"); if (c) c(unsupported()); }
- (void)xpcRequestLayoutControlWithCompletion:(void (^)(WindowManagerXPCLayoutState *, NSError *))c { NOTYET("xpcRequestLayoutControl"); if (c) c(nil, unsupported()); }
- (void)xpcRequestLayoutStateWithCompletion:(void (^)(WindowManagerXPCLayoutState *, NSError *))c { NOTYET("xpcRequestLayoutState"); if (c) c(nil, unsupported()); }
- (void)xpcRequestRemoteTransitionCoordinationWithTransitionType:(WindowManagerXPCRemoteTransitionType *)t completion:(void (^)(WindowManagerXPCLayoutState *, NSError *))c { NOTYET("xpcRequestRemoteTransitionCoordination"); if (c) c(nil, unsupported()); }
- (void)xpcSetStageManagerEnabled:(BOOL)e completion:(void (^)(BOOL))c { NOTYET("xpcSetStageManagerEnabled"); if (c) c(NO); }
- (void)xpcTakeExposeGestureAssertion:(WindowManagerXPCExposeGestureAssertionID *)i direction:(WindowManagerXPCExposeGestureAssertionDirection *)d completion:(void (^)(NSError *))c { NOTYET("xpcTakeExposeGestureAssertion"); if (c) c(unsupported()); }
- (void)xpcTakeModalRevealAssertion:(WindowManagerXPCModalRevealAssertionID *)i withInclusions:(NSArray *)n animated:(BOOL)a completion:(void (^)(NSError *))c { NOTYET("xpcTakeModalRevealAssertion"); if (c) c(unsupported()); }
- (void)xpcTileWindow:(WMXPCTilingPositionRequest *)r completion:(void (^)(NSError *))c { NOTYET("xpcTileWindow"); if (c) c(unsupported()); }
- (void)xpcUpdateRemoteTransitionInTo:(double)p completion:(void (^)(NSError *))c { NOTYET("xpcUpdateRemoteTransitionIn"); if (c) c(unsupported()); }
- (void)xpcUpdateRemoteTransitionOutTo:(double)p completion:(void (^)(NSError *))c { NOTYET("xpcUpdateRemoteTransitionOut"); if (c) c(unsupported()); }
- (void)xpcUpdateWindowPickingSessionClientInfo:(WindowManagerXPCWindowPickingSessionClientInfo *)i completion:(void (^)(NSError *))c { NOTYET("xpcUpdateWindowPickingSessionClientInfo"); if (c) c(unsupported()); }
@end

/* ---- the listener (the framework's AdminXPCListener: a client needs the entitlement com.apple.private.windowmanager) ---- */
@interface WMListener : NSObject <NSXPCListenerDelegate>
@property (strong) NSMutableDictionary *clients;
@end
@implementation WMListener
- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)c
{
	int pid = c.processIdentifier;
	id ent = [c valueForEntitlement:@"com.apple.private.windowmanager"];
	BOOL ok = (ent && [ent respondsToSelector:@selector(boolValue)] && [ent boolValue]) || csent_bool(pid, "com.apple.private.windowmanager") || getenv("WM_NO_ENTITLEMENT_CHECK");

	L("connection from pid %d: %s", pid, ok ? "accepted" : "refused (no com.apple.private.windowmanager)");
	if (!ok) return NO;
	NSXPCInterface *exported = [NSXPCInterface interfaceWithProtocol:@protocol(WMAgentXPCProxyServerInterface)];
	Class inc = [WindowManagerXPCHideWindowsInclusion class];
	[exported setClasses:[NSSet setWithObjects:[NSArray class], inc, nil] forSelector:@selector(xpcTakeModalRevealAssertion:withInclusions:animated:completion:) argumentIndex:1 ofReply:NO];
	NSXPCInterface *remote = [NSXPCInterface interfaceWithProtocol:@protocol(WMAgentXPCProxyClientInterface)];
	WMClient *cl = [WMClient new];
	NSNumber *key = [NSNumber numberWithLongLong:(long long)(intptr_t)(__bridge void *)c];
	__weak WMListener *weakSelf = self;

	cl.connection = c;
	cl.peekMonitors = [NSMutableDictionary new];
	c.exportedInterface = exported;
	c.exportedObject = cl;
	c.remoteObjectInterface = remote;
	c.invalidationHandler = ^{ L("pid %d: connection invalidated", pid); [weakSelf.clients removeObjectForKey:key]; };
	[_clients setObject:cl forKey:key];
	[c resume];
	return YES;
}
@end

int main(int argc, char **argv)
{
	@autoreleasepool {
		const char *name = argc > 1 ? argv[1] : "com.apple.windowmanager.external";
		logging = getenv("WM_LOG") != 0;
		dlopen("/System/Library/Frameworks/QuartzCore.framework/QuartzCore", RTLD_NOW);		/* CAFenceHandle, a class of the interface */
		WMListener *d = [WMListener new];
		d.clients = [NSMutableDictionary new];
		NSXPCListener *l = [[NSXPCListener alloc] initWithMachServiceName:[NSString stringWithUTF8String:name]];
		l.delegate = d;
		[l resume];
		L("serving %s", name);
		[[NSRunLoop currentRunLoop] run];
	}
	return 0;
}
