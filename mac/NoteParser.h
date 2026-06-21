#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// Extracts the preview PNG embedded in a Notepad .note file (format v3+).
// The .note is a Qt QDataStream (big-endian): QByteArray magic ("PPNOTE"),
// quint32 version, then — for v3+ — a QByteArray PNG. We only need to walk to
// that PNG, so no Qt dependency is required here.
@interface NoteParser : NSObject
+ (nullable NSData *)previewPNGFromURL:(NSURL *)url;
@end

NS_ASSUME_NONNULL_END
