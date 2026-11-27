/* filecoordinationd for pearOS: the progress registry com.apple.ProgressReporting of macOS 26.7.2 (/usr/sbin/filecoordinationd, job
 * com.apple.FileCoordination, which also serves com.apple.FileCoordination: not here).  Foundation's NSProgress talks to it with NSXPC:
 *
 *   client -> registry   NSProgressRegistrar
 *     addPublisher:forID:acknowledgementAppBundleIDs:category:fileURL:initialValues:completionHandler:   (-[NSProgress publish])
 *     addSubscriber:forID:appBundleID:fileURL:completionHandler:     (+[NSProgress addSubscriberForFileURL:withPublishingHandler:], the Dock's
 *     addSubscriber:forID:appBundleID:category:completionHandler:     Downloads stack: synchronous, the Dock waits for the completion)
 *     observePublisherForID:values:forKeys:  observePublisherUserInfoForID:value:forKey:   (a publisher's changes)
 *     removePublisherForID:  removeSubscriberForID:
 *   registry -> subscriber   NSProgressSubscriber: addPublisher:forID:withValues:isOld:, the two observe messages, removePublisherForID:
 *   registry -> publisher    NSProgressPublisher: appWithBundleID:didAcknowledgeWithSuccess:   (and the subscribers' cancel/pause/resume/
 *                            prioritize, which go straight to the publisher's proxy handed to them)
 * Captured on the Mac (research/docs/DOCK-SERVICES-2.md 3): a subscriber's completion is answered at once with no arguments, a publisher's
 * with a nil set; a publisher whose file is the subscriber's URL or an item directly in that directory (or whose category is the
 * subscriber's) goes to the subscriber with the publisher's own proxy and its NSProgressValues, isOld NO when it came after the subscriber,
 * YES for the publishers that were there before; every change of the publisher is forwarded as the same message; the publisher is told
 * appWithBundleID:<the subscriber's application> didAcknowledgeWithSuccess:<whether it listed that application>.
 *
 *   filecoordinationd [service-name]     (default com.apple.ProgressReporting; FC_LOG=1 logs to stderr)
 * Our own code (BSD-2-Clause). */
#import "../common/objc-more.h"

static int logging;
static void L(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void L(const char *fmt, ...)
{
	va_list a;

	if (!logging) return;
	va_start(a, fmt); dprintf(2, "filecoordinationd: "); vdprintf(2, fmt, a); dprintf(2, "\n"); va_end(a);
}

@class NSProgressValues;	/* Foundation's own class (NSSecureCoding), carried as it is */
@protocol PRPublisher
- (oneway void)resume;
- (oneway void)cancel;
- (oneway void)pause;
- (oneway void)appWithBundleID:(NSString *)bundleID didAcknowledgeWithSuccess:(BOOL)success;
- (oneway void)prioritize;
@end
@protocol PRSubscriber
- (oneway void)addPublisher:(id <PRPublisher>)publisher forID:(NSString *)i withValues:(NSProgressValues *)values isOld:(BOOL)old;
- (oneway void)observePublisherForID:(NSString *)i values:(NSArray *)values forKeys:(NSArray *)keys;
- (oneway void)observePublisherUserInfoForID:(NSString *)i value:(id)value forKey:(NSString *)key;
- (oneway void)removePublisherForID:(NSString *)i;
@end
@protocol PRRegistrar
- (oneway void)addPublisher:(id <PRPublisher>)publisher forID:(NSString *)i acknowledgementAppBundleIDs:(NSSet *)ack category:(NSString *)category fileURL:(NSURL *)url initialValues:(NSProgressValues *)values completionHandler:(void (^)(NSSet *))done;
- (oneway void)addSubscriber:(id <PRSubscriber>)subscriber forID:(NSString *)i appBundleID:(NSString *)app category:(NSString *)category completionHandler:(void (^)(void))done;
- (oneway void)addSubscriber:(id <PRSubscriber>)subscriber forID:(NSString *)i appBundleID:(NSString *)app fileURL:(NSURL *)url completionHandler:(void (^)(void))done;
- (oneway void)observePublisherForID:(NSString *)i values:(NSArray *)values forKeys:(NSArray *)keys;
- (oneway void)observePublisherUserInfoForID:(NSString *)i value:(id)value forKey:(NSString *)key;
- (oneway void)removePublisherForID:(NSString *)i;
- (oneway void)removeSubscriberForID:(NSString *)i;
@end

/* What a subscriber gets for a publisher: an object of the registry that passes the subscriber's calls on to the publisher's proxy (a proxy
 * cannot be sent over another connection; on the Mac the subscriber also gets a registry-side object: proxy number 2 of its own connection) */
@interface PRForwarder : NSObject <PRPublisher>
@property (strong) id <PRPublisher> target;
@end
@implementation PRForwarder
- (oneway void)resume { [_target resume]; }
- (oneway void)cancel { [_target cancel]; }
- (oneway void)pause { [_target pause]; }
- (oneway void)prioritize { [_target prioritize]; }
- (oneway void)appWithBundleID:(NSString *)b didAcknowledgeWithSuccess:(BOOL)ok { [_target appWithBundleID:b didAcknowledgeWithSuccess:ok]; }
@end

@interface PRPublisherRecord : NSObject
@property (strong) id <PRPublisher> proxy;
@property (strong) PRForwarder *forwarder;
@property (copy) NSString *ident, *category, *path;
@property (strong) NSSet *ack;
@property (strong) id values;		/* NSProgressValues: the initial values (later changes are forwarded, not merged) */
@property (strong) NSXPCConnection *connection;
@end
@implementation PRPublisherRecord @end
@interface PRSubscriberRecord : NSObject
@property (strong) id <PRSubscriber> proxy;
@property (copy) NSString *ident, *app, *category, *path;
@property (strong) NSXPCConnection *connection;
@end
@implementation PRSubscriberRecord @end

static NSMutableDictionary *publishers, *subscribers;	/* id -> record */

static BOOL
matches(PRSubscriberRecord *s, PRPublisherRecord *p)
{
	if (s.category) return p.category && [s.category isEqualToString:p.category];
	if (!s.path || !p.path) return NO;
	return [p.path isEqualToString:s.path] || [p.path.stringByDeletingLastPathComponent isEqualToString:s.path];
}
static void
deliver(PRSubscriberRecord *s, PRPublisherRecord *p, BOOL old)
{
	L("publisher %s -> subscriber %s%s", p.ident.UTF8String, s.ident.UTF8String, old ? " (old)" : "");
	[s.proxy addPublisher:p.forwarder forID:p.ident withValues:p.values isOld:old];
	if (s.app) [p.proxy appWithBundleID:s.app didAcknowledgeWithSuccess:p.ack && [p.ack containsObject:s.app]];
}
static NSString *
standard(NSURL *u)
{
	return u.isFileURL ? u.path.stringByStandardizingPath : nil;
}

@interface PRRegistry : NSObject <PRRegistrar, NSXPCListenerDelegate>
@property (strong) NSXPCConnection *connection;		/* the client this object serves (one per connection) */
@property (strong) NSMutableArray *mine;			/* ids published / subscribed through this connection */
@end
@implementation PRRegistry
- (oneway void)addPublisher:(id <PRPublisher>)publisher forID:(NSString *)i acknowledgementAppBundleIDs:(NSSet *)ack category:(NSString *)category fileURL:(NSURL *)url initialValues:(id)values completionHandler:(void (^)(NSSet *))done
{
	@synchronized (publishers) {
		PRPublisherRecord *p = [PRPublisherRecord new];
	
		p.proxy = publisher; p.forwarder = [PRForwarder new]; p.forwarder.target = publisher; p.ident = i; p.ack = ack; p.category = category; p.path = standard(url); p.values = values; p.connection = _connection;
		L("addPublisher %s file %s category %s", i.UTF8String, p.path ? p.path.UTF8String : "-", category ? category.UTF8String : "-");
		if (done) done(nil);
		if (!i) return;
		[publishers setObject:p forKey:i];
		[_mine addObject:i];
		for (PRSubscriberRecord *s in subscribers.allValues) if (matches(s, p)) deliver(s, p, NO);
	}
}
- (void)addSubscriber:(id <PRSubscriber>)subscriber forID:(NSString *)i appBundleID:(NSString *)app path:(NSString *)path category:(NSString *)category completion:(void (^)(void))done
{
	@synchronized (publishers) {
		PRSubscriberRecord *s = [PRSubscriberRecord new];
	
		s.proxy = subscriber; s.ident = i; s.app = app; s.path = path; s.category = category; s.connection = _connection;
		L("addSubscriber %s app %s file %s category %s", i.UTF8String, app ? app.UTF8String : "-", path ? path.UTF8String : "-", category ? category.UTF8String : "-");
		if (done) done();
		if (!i) return;
		[subscribers setObject:s forKey:i];
		[_mine addObject:i];
		for (PRPublisherRecord *p in publishers.allValues) if (matches(s, p)) deliver(s, p, YES);
	}
}
- (oneway void)addSubscriber:(id <PRSubscriber>)subscriber forID:(NSString *)i appBundleID:(NSString *)app category:(NSString *)category completionHandler:(void (^)(void))done
{
	[self addSubscriber:subscriber forID:i appBundleID:app path:nil category:category completion:done];
}
- (oneway void)addSubscriber:(id <PRSubscriber>)subscriber forID:(NSString *)i appBundleID:(NSString *)app fileURL:(NSURL *)url completionHandler:(void (^)(void))done
{
	[self addSubscriber:subscriber forID:i appBundleID:app path:standard(url) category:nil completion:done];
}
- (oneway void)observePublisherForID:(NSString *)i values:(NSArray *)values forKeys:(NSArray *)keys
{
	@synchronized (publishers) {
		PRPublisherRecord *p = [publishers objectForKey:i];
	
		if (!p) return;
		for (PRSubscriberRecord *s in subscribers.allValues) if (matches(s, p)) [s.proxy observePublisherForID:i values:values forKeys:keys];
	}
}
- (oneway void)observePublisherUserInfoForID:(NSString *)i value:(id)value forKey:(NSString *)key
{
	@synchronized (publishers) {
		PRPublisherRecord *p = [publishers objectForKey:i];
	
		if (!p) return;
		for (PRSubscriberRecord *s in subscribers.allValues) if (matches(s, p)) [s.proxy observePublisherUserInfoForID:i value:value forKey:key];
	}
}
- (oneway void)removePublisherForID:(NSString *)i
{
	@synchronized (publishers) {
		PRPublisherRecord *p = i ? [publishers objectForKey:i] : nil;
	
		if (!p) return;
		L("removePublisher %s", i.UTF8String);
		[publishers removeObjectForKey:i];
		for (PRSubscriberRecord *s in subscribers.allValues) if (matches(s, p)) [s.proxy removePublisherForID:i];
	}
}
- (oneway void)removeSubscriberForID:(NSString *)i
{
	@synchronized (publishers) {
		if (!i) return;
		L("removeSubscriber %s", i.UTF8String);
		[subscribers removeObjectForKey:i];
	}
}
/* the connection went away: its publishers are removed (the subscribers are told), its subscribers dropped */
- (void)clientGone
{
	for (NSString *i in _mine) {
		if ([publishers objectForKey:i]) [self removePublisherForID:i];
		else [subscribers removeObjectForKey:i];
	}
	_mine = nil;
}
- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)c
{
	NSXPCInterface *pub = [NSXPCInterface interfaceWithProtocol:@protocol(PRPublisher)];
	NSXPCInterface *sub = [NSXPCInterface interfaceWithProtocol:@protocol(PRSubscriber)];
	NSXPCInterface *reg = [NSXPCInterface interfaceWithProtocol:@protocol(PRRegistrar)];
	PRRegistry *r = [PRRegistry new];
	int pid = c.processIdentifier;

	[sub setInterface:pub forSelector:@selector(addPublisher:forID:withValues:isOld:) argumentIndex:0 ofReply:NO];
	[reg setInterface:pub forSelector:@selector(addPublisher:forID:acknowledgementAppBundleIDs:category:fileURL:initialValues:completionHandler:) argumentIndex:0 ofReply:NO];
	[reg setInterface:sub forSelector:@selector(addSubscriber:forID:appBundleID:category:completionHandler:) argumentIndex:0 ofReply:NO];
	[reg setInterface:sub forSelector:@selector(addSubscriber:forID:appBundleID:fileURL:completionHandler:) argumentIndex:0 ofReply:NO];
	r.connection = c;
	r.mine = [NSMutableArray new];
	c.exportedInterface = reg;
	c.exportedObject = r;
	c.invalidationHandler = ^{ L("pid %d gone", pid); @synchronized (publishers) { [r clientGone]; r.connection = nil; } };
	L("connection from pid %d", pid);
	[c resume];
	return YES;
}
@end

int main(int argc, char **argv)
{
	@autoreleasepool {
		const char *name = argc > 1 ? argv[1] : "com.apple.ProgressReporting";
		logging = getenv("FC_LOG") != 0;
		publishers = [NSMutableDictionary new];
		subscribers = [NSMutableDictionary new];
		PRRegistry *d = [PRRegistry new];
		NSXPCListener *l = [[NSXPCListener alloc] initWithMachServiceName:[NSString stringWithUTF8String:name]];
		l.delegate = d;
		[l resume];
		L("serving %s", name);
		[[NSRunLoop currentRunLoop] run];
	}
	return 0;
}
