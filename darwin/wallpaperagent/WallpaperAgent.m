/* WallpaperAgent: the server of com.apple.wallpaper (WallpaperAgent.app of macOS 26.7.2) for pearOS.
 * Wire (captured on the Mac, research/docs/DOCK-WINDOWMANAGER-WALLPAPER.md): plain XPC; every request is a dictionary with one key,
 * "AgentXPCMessage", whose value is the JSON (Swift JSONEncoder) of the message: {"<case>":{<arguments>}}.
 *   setDisplaySpacesInfo {"info":{"firstSpaces":[display UUID, space name, ...],"mainDisplay":UUID}}   (sent without a reply)
 *   takeAssertion {"value":{"display":{"display":UUID,"spaceName":S,"contentType":{"desktop":{}},"isPreview":false}},"id":N}
 *       -> reply {"AssertionReply": JSON {"id":N,"contextID":C}}: C is the id of a CoreAnimation remote context that draws the wallpaper; the
 *          client (Wallpaper.framework, WallpaperDisplayAssertion.layer) puts a CALayerHost of it in the Dock's desktop window.
 *   releaseAssertion {"id":N} -> reply {} (empty dictionary)
 *   anything else, or a message that does not decode -> an empty dictionary when a reply is asked for (what Apple's agent does), nothing otherwise.
 * The Dock sends setDisplaySpacesInfo (twice), takeAssertion, and later setDisplaySpacesInfo again; nothing else at start.
 * The picture: WP_IMAGE=<file> (ImageIO), else a plain colour (WP_COLOR="r,g,b" in 0..1).  The wallpaper store of the Mac
 * (~/Library/Application Support/com.apple.wallpaper/Store/Index.plist) is not read yet.
 *
 *   WallpaperAgent [service-name]      (default com.apple.wallpaper; WP_LOG=1 logs to stderr)
 * Our own code (BSD-2-Clause). */
#import "../common/objc-min.h"

/* libxpc (declarations of the macOS SDK; there is no <xpc/xpc.h> in the open header tree) */
typedef void *xpc_object_t;
typedef void (^xpc_handler_t)(xpc_object_t);
typedef const struct _xpc_type_s *xpc_type_t;
extern const struct _xpc_type_s _xpc_type_connection, _xpc_type_dictionary, _xpc_type_error;
extern const struct _xpc_dictionary_s _xpc_error_connection_invalid;
#define XPC_CONNECTION_MACH_SERVICE_LISTENER (1 << 0)
xpc_object_t xpc_connection_create_mach_service(const char *, dispatch_queue_t, uint64_t);
void xpc_connection_set_event_handler(xpc_object_t, xpc_handler_t);
void xpc_connection_resume(xpc_object_t);
void xpc_connection_send_message(xpc_object_t, xpc_object_t);
int xpc_connection_get_pid(xpc_object_t);
xpc_type_t xpc_get_type(xpc_object_t);
xpc_object_t xpc_retain(xpc_object_t);
void xpc_release(xpc_object_t);
xpc_object_t xpc_dictionary_create_reply(xpc_object_t);
const void *xpc_dictionary_get_data(xpc_object_t, const char *, size_t *);
void xpc_dictionary_set_data(xpc_object_t, const char *, const void *, size_t);
xpc_object_t xpc_dictionary_get_remote_connection(xpc_object_t);

/* CoreGraphics / ImageIO / CoreFoundation, looked up at run time */
typedef void *CGColorRef, *CGImageRef, *CFTypeRef;
static void *sym(const char *n) { return dlsym(RTLD_DEFAULT, n); }

static int logging;
static void L(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void L(const char *fmt, ...)
{
	va_list a;

	if (!logging) return;
	va_start(a, fmt); dprintf(2, "WallpaperAgent: "); vdprintf(2, fmt, a); dprintf(2, "\n"); va_end(a);
}

#define MSG(R, ...) ((R (*)(id, SEL, ##__VA_ARGS__))objc_msgSend)

static CGRect display_bounds(void)
{
	unsigned (*mainid)(void) = (unsigned (*)(void))sym("CGMainDisplayID");
	CGRect (*bounds)(unsigned) = (CGRect (*)(unsigned))sym("CGDisplayBounds");
	CGRect r = { { 0, 0 }, { 1280, 800 } };

	if (mainid && bounds) { CGRect b = bounds(mainid()); if (b.size.width >= 1 && b.size.height >= 1) r = b; }
	r.origin.x = r.origin.y = 0;
	return r;
}

static CGImageRef load_image(const char *path)
{
	CFTypeRef (*urlc)(void *, const unsigned char *, long, BOOL) = (CFTypeRef (*)(void *, const unsigned char *, long, BOOL))sym("CFURLCreateFromFileSystemRepresentation");
	CFTypeRef (*src)(CFTypeRef, CFTypeRef) = (CFTypeRef (*)(CFTypeRef, CFTypeRef))sym("CGImageSourceCreateWithURL");
	CGImageRef (*img)(CFTypeRef, size_t, CFTypeRef) = (CGImageRef (*)(CFTypeRef, size_t, CFTypeRef))sym("CGImageSourceCreateImageAtIndex");
	void (*rel)(CFTypeRef) = (void (*)(CFTypeRef))sym("CFRelease");
	CFTypeRef u, s;
	CGImageRef i = 0;

	if (!path || !urlc || !src || !img || !rel) return 0;
	if ((u = urlc(0, (const unsigned char *)path, (long)strlen(path), NO)) == 0) return 0;
	if ((s = src(u, 0)) != 0) { i = img(s, 0, 0); rel(s); }
	rel(u);
	return i;
}

/* a CoreAnimation remote context whose root layer is the picture; its id goes to the client */
static id make_context(unsigned *cid)
{
	Class CAContext = objc_getClass("CAContext"), CALayer = objc_getClass("CALayer"), CATransaction = objc_getClass("CATransaction");
	CGColorRef (*srgb)(CGFloat, CGFloat, CGFloat, CGFloat) = (CGColorRef (*)(CGFloat, CGFloat, CGFloat, CGFloat))sym("CGColorCreateSRGB");
	static CGImageRef image; static int tried;
	double r = 0.13, g = 0.23, b = 0.40;
	id ctx, layer;

	*cid = 0;
	if (!CAContext || !CALayer) return nil;
	ctx = MSG(id, NSDictionary *)((id)CAContext, sel_registerName("remoteContextWithOptions:"), [NSDictionary dictionary]);
	if (!ctx) return nil;
	layer = MSG(id)((id)CALayer, sel_registerName("layer"));
	MSG(void, CGRect)(layer, sel_registerName("setFrame:"), display_bounds());
	if (!tried) { tried = 1; image = load_image(getenv("WP_IMAGE")); }
	if (image) {
		MSG(void, id)(layer, sel_registerName("setContents:"), (__bridge id)image);
		MSG(void, id)(layer, sel_registerName("setContentsGravity:"), @"resizeAspectFill");
	} else if (srgb) {
		if (getenv("WP_COLOR")) sscanf(getenv("WP_COLOR"), "%lf,%lf,%lf", &r, &g, &b);
		MSG(void, CGColorRef)(layer, sel_registerName("setBackgroundColor:"), srgb(r, g, b, 1));
	}
	MSG(void, id)(ctx, sel_registerName("setLayer:"), layer);
	if (CATransaction) MSG(void)((id)CATransaction, sel_registerName("flush"));
	*cid = MSG(unsigned)(ctx, sel_registerName("contextId"));
	return ctx;
}

/* one client: its assertions (id -> CAContext) */
@interface WPClient : NSObject
@property (strong) NSMutableDictionary *assertions;
@property (strong) NSDictionary *displaySpacesInfo;
@property (assign) int pid;
@end
@implementation WPClient
@end

static void reply_data(xpc_object_t msg, const char *key, NSString *json)
{
	xpc_object_t r = xpc_dictionary_create_reply(msg);

	if (!r) return;
	if (key && json) xpc_dictionary_set_data(r, key, json.UTF8String, strlen(json.UTF8String));
	xpc_connection_send_message(xpc_dictionary_get_remote_connection(msg), r);
	xpc_release(r);
}

static void handle(WPClient *cl, xpc_object_t msg)
{
	size_t n = 0;
	const void *p = xpc_dictionary_get_data(msg, "AgentXPCMessage", &n);
	NSDictionary *m = p ? [NSJSONSerialization JSONObjectWithData:[NSData dataWithBytes:p length:n] options:0 error:NULL] : nil;
	NSString *kind = [m respondsToSelector:@selector(allKeys)] && m.count == 1 ? [m.allKeys objectAtIndex:0] : nil;
	NSDictionary *args = kind ? [m objectForKey:kind] : nil;

	L("pid %d: %s %.*s", cl.pid, kind ? kind.UTF8String : "(not decoded)", p ? (int)n : 0, p ? (const char *)p : "");
	if (![args respondsToSelector:@selector(objectForKey:)]) args = nil;
	if (kind && args && [kind isEqualToString:@"setDisplaySpacesInfo"]) {
		cl.displaySpacesInfo = [args objectForKey:@"info"];
		reply_data(msg, 0, nil);
	} else if (kind && args && [kind isEqualToString:@"takeAssertion"] && [[args objectForKey:@"id"] respondsToSelector:@selector(unsignedLongLongValue)]) {
		NSNumber *aid = [args objectForKey:@"id"];
		unsigned cid = 0;
		id ctx = make_context(&cid);

		if (ctx) [cl.assertions setObject:ctx forKey:aid];
		L("pid %d: assertion %llu -> context %u", cl.pid, aid.unsignedLongLongValue, cid);
		reply_data(msg, "AssertionReply", [NSString stringWithFormat:@"{\"id\":%llu,\"contextID\":%u}", aid.unsignedLongLongValue, cid]);
	} else if (kind && args && [kind isEqualToString:@"releaseAssertion"]) {
		NSNumber *aid = [args objectForKey:@"id"];
		id ctx = aid ? [cl.assertions objectForKey:aid] : nil;

		if (ctx) { MSG(void)(ctx, sel_registerName("invalidate")); [cl.assertions removeObjectForKey:aid]; }
		reply_data(msg, 0, nil);
	} else
		reply_data(msg, 0, nil);
}

int main(int argc, char **argv)
{
	@autoreleasepool {
		const char *name = argc > 1 ? argv[1] : "com.apple.wallpaper";
		NSMutableDictionary *clients = [NSMutableDictionary new];
		xpc_object_t listener;

		logging = getenv("WP_LOG") != 0;
		dlopen("/System/Library/Frameworks/QuartzCore.framework/QuartzCore", RTLD_NOW);
		dlopen("/System/Library/Frameworks/ImageIO.framework/ImageIO", RTLD_NOW);
		listener = xpc_connection_create_mach_service(name, dispatch_get_main_queue(), XPC_CONNECTION_MACH_SERVICE_LISTENER);
		xpc_connection_set_event_handler(listener, ^(xpc_object_t peer) {
			if (xpc_get_type(peer) != &_xpc_type_connection) return;
			WPClient *cl = [WPClient new];
			NSNumber *key = [NSNumber numberWithLongLong:(long long)(intptr_t)peer];

			cl.assertions = [NSMutableDictionary new];
			cl.pid = xpc_connection_get_pid(peer);
			[clients setObject:cl forKey:key];
			L("connection from pid %d", cl.pid);
			xpc_retain(peer);
			xpc_connection_set_event_handler(peer, ^(xpc_object_t e) {
				if (xpc_get_type(e) == &_xpc_type_dictionary) { handle(cl, e); return; }
				if (xpc_get_type(e) == &_xpc_type_error && e == (xpc_object_t)&_xpc_error_connection_invalid) {
					NSArray *ids = cl.assertions.allKeys;
					for (NSUInteger i = 0; i < ids.count; i++) MSG(void)([cl.assertions objectForKey:[ids objectAtIndex:i]], sel_registerName("invalidate"));
					L("pid %d: connection invalidated, %lu assertion(s) released", cl.pid, (unsigned long)ids.count);
					[clients removeObjectForKey:key];
					xpc_release(peer);
				}
			});
			xpc_connection_resume(peer);
		});
		xpc_connection_resume(listener);
		L("serving %s", name);
		[[NSRunLoop currentRunLoop] run];
	}
	return 0;
}
