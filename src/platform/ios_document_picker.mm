#include "platform/ios_document_picker.h"

#include <SDL3/SDL.h>

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

@interface ChaotixPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property(nonatomic) BOOL done;
@property(nonatomic, strong) NSURL* picked;
@end

@implementation ChaotixPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    self.picked = urls.firstObject;
    self.done = YES;
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
    self.done = YES;
}
@end

namespace chaotix {

std::string ios_pick_document(SDL_Window* window, const std::string& start_dir) {
    @autoreleasepool {
        UIWindow* ui_window = (__bridge UIWindow*)SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
        UIViewController* parent = ui_window.rootViewController;
        while (parent.presentedViewController) parent = parent.presentedViewController;
        if (!parent) return "";
        // Any file: ROMs come as .32x, .bin and more, and a provider may not
        // know those extensions at all. The verification after this decides.
        UIDocumentPickerViewController* picker =
            [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeData] asCopy:YES];
        ChaotixPickerDelegate* delegate = [ChaotixPickerDelegate new];
        picker.delegate = delegate;
        picker.allowsMultipleSelection = NO;
        if (!start_dir.empty())
            picker.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:start_dir.c_str()]
                                             isDirectory:YES];
        [parent presentViewController:picker animated:YES completion:nil];
        // The setup page waits here, as it would for a desktop dialog; UIKit
        // goes on running in the main run loop meanwhile.
        while (!delegate.done) {
            @autoreleasepool {
                [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode
                                       beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
            }
        }
        NSURL* picked = delegate.picked;
        if (!picked || !picked.fileURL) return "";
        return std::string(picked.path.fileSystemRepresentation);
    }
}

} // namespace chaotix
