// rcli_overlay — standalone Cocoa app showing a draggable/resizable overlay
// frame for screen capture. Communicates with parent RCLI via stdin/stdout.
//
// Commands (one per line on stdin):
//   frame   → replies "x,y,w,h\n" (screen coords, top-left origin)
//   hide    → sets alpha to 0 (for capture)
//   show    → restores alpha to 1
//   quit    → exits

#import <AppKit/AppKit.h>

static const CGFloat kBorder    = 6.0;
static const CGFloat kRadius    = 12.0;
static const CGFloat kHandle    = 18.0;   // corner handle size
static const CGFloat kEdgeGrab  = 14.0;   // invisible edge grab zone

// ── Custom view: bold border + corner handles + label pill ─────────────
@interface OverlayView : NSView
@end

@implementation OverlayView

- (void)drawRect:(NSRect)dirtyRect {
    [[NSColor clearColor] set];
    NSRectFill(dirtyRect);

    NSRect inner = NSInsetRect(self.bounds, kBorder, kBorder);
    NSColor *green = [NSColor colorWithRed:0.15 green:0.9 blue:0.45 alpha:0.92];

    // Outer glow
    NSBezierPath *glow = [NSBezierPath bezierPathWithRoundedRect:inner
                                                         xRadius:kRadius yRadius:kRadius];
    [glow setLineWidth:kBorder + 6];
    [[green colorWithAlphaComponent:0.12] set];
    [glow stroke];

    // Main border — solid, thick, rounded
    NSBezierPath *border = [NSBezierPath bezierPathWithRoundedRect:inner
                                                           xRadius:kRadius yRadius:kRadius];
    [border setLineWidth:kBorder];
    [green set];
    [border stroke];

    // Corner handles — filled rounded squares with white dot
    CGFloat hs = kHandle;
    CGFloat off = kBorder / 2;
    NSRect corners[4] = {
        NSMakeRect(NSMinX(inner) - off, NSMinY(inner) - off, hs, hs),
        NSMakeRect(NSMaxX(inner) + off - hs, NSMinY(inner) - off, hs, hs),
        NSMakeRect(NSMinX(inner) - off, NSMaxY(inner) + off - hs, hs, hs),
        NSMakeRect(NSMaxX(inner) + off - hs, NSMaxY(inner) + off - hs, hs, hs),
    };
    for (int i = 0; i < 4; i++) {
        NSBezierPath *h = [NSBezierPath bezierPathWithRoundedRect:corners[i]
                                                          xRadius:4 yRadius:4];
        [green set];
        [h fill];
        // White center dot
        NSRect dot = NSInsetRect(corners[i], 5, 5);
        [[NSColor colorWithWhite:1.0 alpha:0.85] set];
        [[NSBezierPath bezierPathWithOvalInRect:dot] fill];
    }

    // Label pill — centered at top
    NSString *label = @"  RCLI Visual Mode  ";
    NSDictionary *attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:11 weight:NSFontWeightBold],
        NSForegroundColorAttributeName: [NSColor blackColor],
    };
    NSSize sz = [label sizeWithAttributes:attrs];
    CGFloat px = NSMidX(self.bounds) - sz.width / 2 - 6;
    CGFloat py = NSMaxY(inner) - 2;
    NSRect pill = NSMakeRect(px, py, sz.width + 12, sz.height + 6);
    NSBezierPath *pillPath = [NSBezierPath bezierPathWithRoundedRect:pill
                                                             xRadius:10 yRadius:10];
    [green set];
    [pillPath fill];
    [label drawAtPoint:NSMakePoint(px + 6, py + 3) withAttributes:attrs];
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
        self.minSize = NSMakeSize(120, 80);
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
        dispatch_async(dispatch_get_main_queue(), ^{
            [NSApp terminate:nil];
        });
    });
}

- (void)handleCommand:(NSString *)cmd {
    if ([cmd isEqualToString:@"frame"]) {
        NSRect f = self.window.frame;
        CGFloat screenH = [NSScreen mainScreen].frame.size.height;
        int x = (int)f.origin.x;
        int y = (int)(screenH - f.origin.y - f.size.height);
        int w = (int)f.size.width;
        int h = (int)f.size.height;
        printf("%d,%d,%d,%d\n", x, y, w, h);
        fflush(stdout);
    } else if ([cmd isEqualToString:@"hide"]) {
        [self.window setAlphaValue:0.0];
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

        printf("ready\n");
        fflush(stdout);

        [app run];
    }
    return 0;
}
