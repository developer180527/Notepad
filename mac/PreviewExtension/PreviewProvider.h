#import <Cocoa/Cocoa.h>
#import <QuickLookUI/QuickLookUI.h>

// View-based Quick Look preview showing the embedded preview PNG in an image
// view. (The data-based QLPreviewProvider path never completes on macOS 26 —
// Quick Look just spins.)
@interface PreviewViewController : NSViewController <QLPreviewingController>
@end
