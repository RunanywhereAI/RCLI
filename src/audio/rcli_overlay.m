// rcli_overlay — standalone Cocoa app showing a draggable/resizable overlay
// frame for screen capture. Communicates with parent RCLI via stdin/stdout.
//
// Commands (one per line on stdin):
//   frame   → replies "x,y,w,h\n" (screen coords, top-left origin)
//   hide    → sets alpha to 0 (for capture)
//   show    → restores alpha to 1
//   quit    → exits

#import <AppKit/AppKit.h>

static const CGFloat kBorder    = 8.0;
static const CGFloat kRadius    = 14.0;
static const CGFloat kHandle    = 28.0;   // corner handle size
static const CGFloat kEdgeGrab  = 20.0;   // invisible edge grab zone

// ── Custom view: bold border + corner handles + label pill ─────────────
@interface OverlayView : NSView
@end

@implementation OverlayView

- (void)drawRect:(NSRect)dirtyRect {
    [[NSColor clearColor] set];
    NSRectFill(dirtyRect);

    NSRect inner = NSInsetRect(self.bounds, kBorder, kBorder);
    NSColor *green = [NSColor colorWithRed:0.1 green:0.85 blue:0.4 alpha:1.0];

    // Outer glow — wide, soft, two layers for depth
    NSBezierPath *glow2 = [NSBezierPath bezierPathWithRoundedRect:inner
                                                           xRadius:kRadius yRadius:kRadius];
    [glow2 setLineWidth:kBorder + 16];
    [[green colorWithAlphaComponent:0.08] set];
    [glow2 stroke];

    NSBezierPath *glow = [NSBezierPath bezierPathWithRoundedRect:inner
                                                         xRadius:kRadius yRadius:kRadius];
    [glow setLineWidth:kBorder + 8];
    [[green colorWithAlphaComponent:0.18] set];
    [glow stroke];

    // Main border — bold, solid, rounded
    NSBezierPath *border = [NSBezierPath bezierPathWithRoundedRect:inner
                                                           xRadius:kRadius yRadius:kRadius];
    [border setLineWidth:kBorder];
    [green set];
    [border stroke];

    // Inner highlight — thin white line for depth
    NSRect innerHL = NSInsetRect(inner, 1.5, 1.5);
    NSBezierPath *highlight = [NSBezierPath bezierPathWithRoundedRect:innerHL
                                                              xRadius:kRadius - 1.5 yRadius:kRadius - 1.5];
    [highlight setLineWidth:1.0];
    [[NSColor colorWithWhite:1.0 alpha:0.15] set];
    [highlight stroke];

    // Corner handles — large rounded squares with shadow + white center dot
    CGFloat hs = kHandle;
    CGFloat off = kBorder / 2;
    NSRect corners[4] = {
        NSMakeRect(NSMinX(inner) - off, NSMinY(inner) - off, hs, hs),
        NSMakeRect(NSMaxX(inner) + off - hs, NSMinY(inner) - off, hs, hs),
        NSMakeRect(NSMinX(inner) - off, NSMaxY(inner) + off - hs, hs, hs),
        NSMakeRect(NSMaxX(inner) + off - hs, NSMaxY(inner) + off - hs, hs, hs),
    };
    for (int i = 0; i < 4; i++) {
        // Drop shadow
        NSRect shadowRect = NSOffsetRect(corners[i], 0, -1);
        NSBezierPath *shadow = [NSBezierPath bezierPathWithRoundedRect:shadowRect
                                                               xRadius:6 yRadius:6];
        [[NSColor colorWithWhite:0.0 alpha:0.25] set];
        [shadow fill];

        // Handle body
        NSBezierPath *h = [NSBezierPath bezierPathWithRoundedRect:corners[i]
                                                          xRadius:6 yRadius:6];
        [green set];
        [h fill];

        // White border on handle
        [h setLineWidth:1.5];
        [[NSColor colorWithWhite:1.0 alpha:0.4] set];
        [h stroke];

        // White center dot
        NSRect dot = NSInsetRect(corners[i], hs * 0.3, hs * 0.3);
        [[NSColor colorWithWhite:1.0 alpha:0.9] set];
        [[NSBezierPath bezierPathWithOvalInRect:dot] fill];
    }

    // Edge midpoint handles — small bars to hint at edge dragging
    CGFloat eh = 5.0;   // half-thickness
    CGFloat el = 32.0;  // bar length
    NSRect edges[4] = {
        NSMakeRect(NSMidX(inner) - el/2, NSMaxY(inner) - eh/2, el, eh),   // top
        NSMakeRect(NSMidX(inner) - el/2, NSMinY(inner) - eh/2, el, eh),   // bottom
        NSMakeRect(NSMinX(inner) - eh/2, NSMidY(inner) - el/2, eh, el),   // left
        NSMakeRect(NSMaxX(inner) - eh/2, NSMidY(inner) - el/2, eh, el),   // right
    };
    for (int i = 0; i < 4; i++) {
        NSBezierPath *ep = [NSBezierPath bezierPathWithRoundedRect:edges[i]
                                                           xRadius:2.5 yRadius:2.5];
        [[green colorWithAlphaComponent:0.7] set];
        [ep fill];
    }

    // Label pill — centered at top
    NSString *label = @"  RCLI Visual Mode  ";
    NSDictionary *attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:12 weight:NSFontWeightHeavy],
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
