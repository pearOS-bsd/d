/* More Foundation declarations for the Objective-C daemons of darwin/ (usernoted's neighbours: filecoordinationd, csnameddatad, dmd,
 * useractivityd, uninstalld), on top of objc-min.h.  Prototypes are Apple's; the symbols bind at run time to the Foundation dyld finds.
 * Our own file (BSD-2-Clause). */
#ifndef DARWIN_OBJC_MORE_H
#define DARWIN_OBJC_MORE_H
#import "objc-min.h"

@class NSURL, NSDate, NSMutableArray, NSMutableSet;
@interface NSObject (DMore)
- (id)copy;
- (id)mutableCopy;
@end
@interface NSString (DMore)
- (BOOL)hasPrefix:(NSString *)s;
- (NSString *)lowercaseString;
- (NSString *)stringByDeletingLastPathComponent;
- (NSString *)stringByStandardizingPath;
@end
@interface NSURL : NSObject <NSCopying, NSSecureCoding>
+ (instancetype)fileURLWithPath:(NSString *)path;
@property (readonly, copy) NSString *path;
@property (readonly, copy) NSString *absoluteString;
@property (readonly, getter=isFileURL) BOOL fileURL;
@end
@interface NSDate : NSObject <NSCopying, NSSecureCoding>
+ (instancetype)date;
@end
@interface NSArray (DMore)
+ (instancetype)arrayWithObject:(id)o;
- (BOOL)containsObject:(id)o;
- (NSUInteger)countByEnumeratingWithState:(void *)state objects:(id __unsafe_unretained *)buf count:(NSUInteger)len;
@end
@interface NSMutableArray : NSArray
- (void)addObject:(id)o;
- (void)removeObject:(id)o;
- (void)removeObjectAtIndex:(NSUInteger)i;
@end
@interface NSDictionary (DMore)
+ (instancetype)dictionaryWithObject:(id)o forKey:(id)k;
@property (readonly) NSArray *allValues;
- (id)objectForKeyedSubscript:(id)key;
- (NSUInteger)countByEnumeratingWithState:(void *)state objects:(id __unsafe_unretained *)buf count:(NSUInteger)len;
@end
@interface NSMutableDictionary (DMore)
- (void)setObject:(id)o forKeyedSubscript:(id)k;
@end
@interface NSSet (DMore)
+ (instancetype)set;
+ (instancetype)setWithObject:(id)o;
@property (readonly) NSUInteger count;
- (BOOL)containsObject:(id)o;
@end
@interface NSMutableSet : NSSet
- (void)addObject:(id)o;
@end
@interface NSNumber (DMore)
+ (NSNumber *)numberWithInteger:(NSInteger)v;
@end
@interface NSXPCInterface (DMore)
- (void)setInterface:(NSXPCInterface *)ifc forSelector:(SEL)sel argumentIndex:(NSUInteger)arg ofReply:(BOOL)ofReply;
@end
@interface NSXPCConnection (DMore)
- (void)invalidate;
- (id)remoteObjectProxyWithErrorHandler:(void (^)(NSError *error))handler;
@end
extern NSString *const NSCocoaErrorDomain;
extern NSString *const NSOSStatusErrorDomain;
extern NSString *const NSPOSIXErrorDomain;
#endif
