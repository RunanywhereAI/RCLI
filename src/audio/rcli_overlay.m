// rcli_overlay — tiny standalone Cocoa app that shows a draggable/resizable
// green-bordered transparent overlay.  Communicates with the parent RCLI
// process via stdin (commands) and stdout (responses).
//
// Commands (one per line on stdin):
//   frame   → replies "x,y,w,h\n" (screen coords, top-left origin)
//   hide    → sets alpha to 0 (for capture)
//   show    → restores alpha to 1
//   quit    → exits
//
// Compile: clang -framework AppKit -framework CoreGraphics -o rcli_overlay rcli_overlay.m

#import <AppKit/AppKit.h>

// ── Custom view: green dashed border + label ──────────────────────────
@interface OverlayView : NSView
@end

@implementation OverlayView
- (void)drawRect:(NSRect)dirtyRect {
    [[NSColor clearColor] set];
    NSRectFill(dirtyRect);

    NSBezierPath *border = [NSBezierPath bezierPathWithRect:
        NSInsetRect(self.bounds, 3, 3)];
    [border setLineWidth:4.0];
    CGFloat dash[] = {10, 5};
    [border setLineDash:dash count:2 phase:0];
    [[NSColor colorWithRed:0.0 green:1.0 blue:0.4 alpha:0.9] set];
    [border stroke];

    NSDictionary *attrs = @{
        NSFontAttributeName: [NSFont boldSystemFontOfSize:12],
        NSForegroundColorAttributeName:
            [NSColor colorWithRed:0.0 green:1.0 blue:0.4 alpha:0.9],
    };
    [@" RCLI Visual Mode " drawAtPoint:NSMakePoint(10, self.bounds.size.height - 22)
                        withAttributes:attrs];
}
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
@end

// ── Custom window: borderless, transparent, floating, draggable ───────
@interface OverlayWindow : NSWindow
@end

@implementation OverlayWindow
- (instancetype)initWithRect:(NSRect)rect {
    self = [super initWithContentRect:rect
                            styleMask:NSWindowStyleMaskBorderless |
                                      NSWindowStyleMaskResizable
                              backing:NSBackingStoreBuffered
                                defer:NO];
    if (self) {
        self.opaque = NO;
        self.backgroundColor = [NSColor clearColor];
        self.level = NSFloatingWindowLevel;
        self.hasShadow = NO;
        self.movableByWindowBackground = YES;
        self.contentView = [[OverlayView alloc] initWithFrame:rect];
        self.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                                  NSWindowCollectionBehaviorStationary;
    }
    return self;
}
- (BOOL)canBecomeKeyWindow  { return YES; }
- (BOOL)canBecomeMainWindow { return NO; }
@end

// ── Stdin reader (runs on a background thread) ────────────────────────
@interface StdinReader : NSObject
@property (nonatomic, strong) OverlayWindow *window;
- (void)startReading;
- (void)handleCommand:(NSString *)cmd;
@end

@implementation StdinReader

- (void)startReading {
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        char buf[256];
        while (fgets(buf, sizeof(buf), stdin)) {
            NSString *cmd = [[NSString stringWithUTF8String:buf]
                stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if (cmd.length == 0) continue;
            [self performSelectorOnMainThread:@selector(handleCommand:)
                                   withObject:cmd
                                waitUntilDone:YES];
        }
        // stdin closed — parent died, exit gracefully
        dispatch_async(dispatch_get_main_queue(), ^{
            [NSApp terminate:nil];
        });
    });
}

- (void)handleCommand:(NSString *)cmd {
    if ([cmd isEqualToString:@"frame"]) {
        NSRect f = self.window.frame;
        // Convert to top-left origin (Cocoa uses bottom-left)
        CGFloat screenH = [NSScreen mainScreen].frame.size.height;
        int x = (int)f.origin.x;
        int y = (int)(screenH - f.origin.y - f.size.height);
        int w = (int)f.size.width;
        int h = (int)f.size.height;
        printf("%d,%d,%d,%d\n", x, y, w, h);
        fflush(stdout);
    } else if ([cmd isEqualToString:@"hide"]) {
        [self.window setAlphaValue:0.0];
        // Small delay for window server
        [NSThread sleepForTimeInterval:0.05];
        printf("ok\n");
        fflush(stdout);
    } else if ([cmd isEqualToString:@"show"]) {
        [self.window setAlphaValue:1.0];
        printf("ok\n");
        fflush(stdout);
    } else if ([cmd isEqualToString:@"quit"]) {
        [NSApp terminate:nil];
    }
}

@end

// ── Main ──────────────────────────────────────────────────────────────
int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];

        // Default: 800×600 centered
        NSScreen *scr = [NSScreen mainScreen];
        NSRect sf = scr.frame;
        CGFloat w = 800, h = 600;
        CGFloat x = (sf.size.width - w) / 2;
        CGFloat y = (sf.size.height - h) / 2;

        OverlayWindow *win = [[OverlayWindow alloc]
            initWithRect:NSMakeRect(x, y, w, h)];
        [win makeKeyAndOrderFront:nil];
        [app activateIgnoringOtherApps:YES];

        StdinReader *reader = [[StdinReader alloc] init];
        reader.window = win;
        [reader startReading];

        // Signal parent that we're ready
        printf("ready\n");
        fflush(stdout);

        [app run];
    }
    return 0;
}
