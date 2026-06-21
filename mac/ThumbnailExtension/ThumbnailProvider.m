#import "ThumbnailProvider.h"
#import "NoteParser.h"
#import <AppKit/AppKit.h>

@implementation ThumbnailProvider

- (void)provideThumbnailForFileRequest:(QLFileThumbnailRequest *)request
                     completionHandler:(void (^)(QLThumbnailReply *_Nullable, NSError *_Nullable))handler
{
    NSData *png = [NoteParser previewPNGFromURL:request.fileURL];
    NSImage *image = png ? [[NSImage alloc] initWithData:png] : nil;
    if (image == nil || image.size.width <= 0 || image.size.height <= 0) {
        handler(nil, nil);   // fall back to the generic icon
        return;
    }

    const CGSize maxSize = request.maximumSize;
    const NSSize imgSize = image.size;
    const CGFloat scale = MIN(maxSize.width / imgSize.width, maxSize.height / imgSize.height);
    const CGSize drawSize = CGSizeMake(MAX(1.0, imgSize.width * scale),
                                       MAX(1.0, imgSize.height * scale));

    QLThumbnailReply *reply = [QLThumbnailReply replyWithContextSize:drawSize
                                                       drawingBlock:^BOOL(CGContextRef context) {
        NSGraphicsContext *gc = [NSGraphicsContext graphicsContextWithCGContext:context flipped:NO];
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:gc];
        [image drawInRect:NSMakeRect(0, 0, drawSize.width, drawSize.height)
                 fromRect:NSZeroRect
                operation:NSCompositingOperationSourceOver
                 fraction:1.0];
        [NSGraphicsContext restoreGraphicsState];
        return YES;
    }];
    handler(reply, nil);
}

@end
