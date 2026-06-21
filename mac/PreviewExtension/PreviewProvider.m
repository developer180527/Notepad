#import "PreviewProvider.h"
#import "NoteParser.h"

@implementation PreviewViewController

- (void)loadView
{
    NSImageView *iv = [[NSImageView alloc] initWithFrame:NSMakeRect(0, 0, 612, 792)];
    iv.imageScaling = NSImageScaleProportionallyUpOrDown;
    iv.imageAlignment = NSImageAlignCenter;
    iv.imageFrameStyle = NSImageFrameNone;
    iv.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.view = iv;
}

- (void)preparePreviewOfFileAtURL:(NSURL *)url
                completionHandler:(void (^)(NSError *_Nullable))handler
{
    NSData *png = [NoteParser previewPNGFromURL:url];
    NSImage *image = png ? [[NSImage alloc] initWithData:png] : nil;
    if (image == nil) {
        handler([NSError errorWithDomain:NSCocoaErrorDomain
                                    code:NSFileReadCorruptFileError
                                userInfo:nil]);
        return;
    }
    ((NSImageView *)self.view).image = image;
    handler(nil);
}

@end
