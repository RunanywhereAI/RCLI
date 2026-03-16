// rcli_camera_preview — standalone Cocoa app showing a live camera preview
// in a floating PIP-style window. Communicates with parent RCLI via stdin/stdout.
//
// Commands (one per line on stdin):
//   capture <path>  → freezes frame, saves JPEG to <path>, replies "ok\n"
//   snap <path>     → saves JPEG to <path> WITHOUT freezing, replies "ok\n"
//   freeze          → pauses the live feed on current frame, replies "ok\n"
//   unfreeze        → resumes live camera feed, replies "ok\n"
//   quit            → exits

#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

// ── Camera preview window ─────────────────────────────────────────────

@interface CameraPreviewView : NSView <AVCaptureVideoDataOutputSampleBufferDelegate> {
    AVCaptureSession *_session;
    AVCaptureVideoDataOutput *_output;
    dispatch_queue_t _captureQueue;
    CIContext *_ciContext;
    CGImageRef _currentFrame;
    BOOL _frozen;
    NSString *_pendingCapturePath;
    NSLock *_frameLock;
}
@property (nonatomic, strong) NSTextField *statusLabel;
@end

@implementation CameraPreviewView

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _frameLock = [[NSLock alloc] init];
        _ciContext = [CIContext contextWithOptions:nil];
        _frozen = NO;
        _currentFrame = NULL;
        self.wantsLayer = YES;
        self.layer.cornerRadius = 12;
        self.layer.masksToBounds = YES;
        self.layer.backgroundColor = [NSColor blackColor].CGColor;

        _statusLabel = [[NSTextField alloc] initWithFrame:NSZeroRect];
        _statusLabel.stringValue = @"  RCLI Camera  ";
        _statusLabel.font = [NSFont systemFontOfSize:11 weight:NSFontWeightHeavy];
        _statusLabel.textColor = [NSColor blackColor];
        _statusLabel.backgroundColor = [NSColor colorWithRed:0.1 green:0.85 blue:0.4 alpha:1.0];
        _statusLabel.bezeled = NO;
        _statusLabel.editable = NO;
        _statusLabel.selectable = NO;
        _statusLabel.alignment = NSTextAlignmentCenter;
        _statusLabel.wantsLayer = YES;
        _statusLabel.layer.cornerRadius = 8;
        _statusLabel.layer.masksToBounds = YES;
        [_statusLabel sizeToFit];
        [self addSubview:_statusLabel];

        [self startCamera];
    }
    return self;
}

- (void)layout {
    [super layout];
    NSSize sz = _statusLabel.frame.size;
    CGFloat x = (self.bounds.size.width - sz.width) / 2;
    CGFloat y = self.bounds.size.height - sz.height - 8;
    _statusLabel.frame = NSMakeRect(x, y, sz.width + 8, sz.height + 4);
}

- (void)startCamera {
    _session = [[AVCaptureSession alloc] init];
    _session.sessionPreset = AVCaptureSessionPresetHigh;

    AVCaptureDevice *device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    if (!device) return;

    NSError *err = nil;
    AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&err];
    if (!input) return;
    if ([_session canAddInput:input]) [_session addInput:input];

    _output = [[AVCaptureVideoDataOutput alloc] init];
    _output.videoSettings = @{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)};
    _output.alwaysDiscardsLateVideoFrames = YES;
    _captureQueue = dispatch_queue_create("camera.preview", DISPATCH_QUEUE_SERIAL);
    [_output setSampleBufferDelegate:self queue:_captureQueue];
    if ([_session canAddOutput:_output]) [_session addOutput:_output];

    [_session startRunning];
}

- (void)captureOutput:(AVCaptureOutput *)output
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection *)connection {
    if (_frozen) return;

    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!imageBuffer) return;

    CIImage *ciImage = [CIImage imageWithCVImageBuffer:imageBuffer];
    CGRect extent = ciImage.extent;
    CGImageRef cgImage = [_ciContext createCGImage:ciImage fromRect:extent];
    if (!cgImage) return;

    [_frameLock lock];
    if (_currentFrame) CGImageRelease(_currentFrame);
    _currentFrame = cgImage;
    [_frameLock unlock];

    dispatch_async(dispatch_get_main_queue(), ^{
        [self setNeedsDisplay:YES];
    });
}

- (void)drawRect:(NSRect)dirtyRect {
    [[NSColor blackColor] set];
    NSRectFill(dirtyRect);

    [_frameLock lock];
    CGImageRef frame = _currentFrame;
    if (frame) CGImageRetain(frame);
    [_frameLock unlock];

    if (frame) {
        NSGraphicsContext *ctx = [NSGraphicsContext currentContext];
        CGContextRef cgctx = (CGContextRef)[ctx CGContext];

        CGFloat imgW = CGImageGetWidth(frame);
        CGFloat imgH = CGImageGetHeight(frame);
        CGFloat viewW = self.bounds.size.width;
        CGFloat viewH = self.bounds.size.height;

        CGFloat scale = fmax(viewW / imgW, viewH / imgH);
        CGFloat drawW = imgW * scale;
        CGFloat drawH = imgH * scale;
        CGFloat drawX = (viewW - drawW) / 2;
        CGFloat drawY = (viewH - drawH) / 2;

        CGContextDrawImage(cgctx, CGRectMake(drawX, drawY, drawW, drawH), frame);
        CGImageRelease(frame);
    }

    if (_frozen) {
        [[NSColor colorWithRed:1.0 green:0.3 blue:0.2 alpha:0.08] set];
        NSRectFillUsingOperation(self.bounds, NSCompositingOperationSourceOver);
    }

    // Green border
    NSColor *green = [NSColor colorWithRed:0.1 green:0.85 blue:0.4 alpha:1.0];
    NSBezierPath *border = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 2, 2)
                                                           xRadius:12 yRadius:12];
    [border setLineWidth:4];
    [green set];
    [border stroke];
}

- (BOOL)saveFrameToPath:(NSString *)path {
    [_frameLock lock];
    CGImageRef frame = _currentFrame;
    if (frame) CGImageRetain(frame);
    [_frameLock unlock];

    if (!frame) return NO;

    NSURL *url = [NSURL fileURLWithPath:path];
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL(
        (__bridge CFURLRef)url, (__bridge CFStringRef)UTTypeJPEG.identifier, 1, NULL);
    if (!dest) { CGImageRelease(frame); return NO; }

    NSDictionary *opts = @{(__bridge id)kCGImageDestinationLossyCompressionQuality: @(0.92)};
    CGImageDestinationAddImage(dest, frame, (__bridge CFDictionaryRef)opts);
    BOOL ok = CGImageDestinationFinalize(dest);
    CFRelease(dest);
    CGImageRelease(frame);
    return ok;
}

- (void)freeze {
    _frozen = YES;
    dispatch_async(dispatch_get_main_queue(), ^{
        self.statusLabel.stringValue = @"  FROZEN  ";
        self.statusLabel.backgroundColor = [NSColor colorWithRed:1.0 green:0.3 blue:0.2 alpha:1.0];
        [self.statusLabel sizeToFit];
        [self layout];
        [self setNeedsDisplay:YES];
    });
}

- (void)unfreeze {
    _frozen = NO;
    dispatch_async(dispatch_get_main_queue(), ^{
        self.statusLabel.stringValue = @"  RCLI Camera  ";
        self.statusLabel.backgroundColor = [NSColor colorWithRed:0.1 green:0.85 blue:0.4 alpha:1.0];
        [self.statusLabel sizeToFit];
        [self layout];
        [self setNeedsDisplay:YES];
    });
}

- (void)stopCamera {
    [_session stopRunning];
}

- (void)dealloc {
    [_frameLock lock];
    if (_currentFrame) CGImageRelease(_currentFrame);
    _currentFrame = NULL;
    [_frameLock unlock];
    [super dealloc];
}

@end

// ── Camera window ─────────────────────────────────────────────────────

@interface CameraWindow : NSWindow
@end

@implementation CameraWindow

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
        self.hasShadow = YES;
        self.movableByWindowBackground = YES;
        self.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                                  NSWindowCollectionBehaviorStationary;
        self.minSize = NSMakeSize(240, 180);

        CameraPreviewView *preview = [[CameraPreviewView alloc] initWithFrame:rect];
        self.contentView = preview;
    }
    return self;
}

- (BOOL)canBecomeKeyWindow  { return YES; }
- (BOOL)canBecomeMainWindow { return NO; }

@end

// ── Stdin reader ──────────────────────────────────────────────────────

@interface StdinReader : NSObject
@property (nonatomic, strong) CameraWindow *window;
- (void)startReading;
- (void)handleCommand:(NSString *)cmd;
@end

@implementation StdinReader

- (void)startReading {
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        char buf[1024];
        while (fgets(buf, sizeof(buf), stdin)) {
            NSString *cmd = [[NSString stringWithUTF8String:buf]
                stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if (cmd.length == 0) continue;
            [self performSelectorOnMainThread:@selector(handleCommand:)
                                   withObject:cmd
                                waitUntilDone:YES];
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            [(CameraPreviewView *)self.window.contentView stopCamera];
            [NSApp terminate:nil];
        });
    });
}

- (void)handleCommand:(NSString *)cmd {
    CameraPreviewView *preview = (CameraPreviewView *)self.window.contentView;

    if ([cmd hasPrefix:@"capture "]) {
        NSString *path = [cmd substringFromIndex:8];
        [preview freeze];
        [NSThread sleepForTimeInterval:0.05];
        BOOL ok = [preview saveFrameToPath:path];
        printf("%s\n", ok ? "ok" : "error");
        fflush(stdout);
    } else if ([cmd hasPrefix:@"snap "]) {
        NSString *path = [cmd substringFromIndex:5];
        BOOL ok = [preview saveFrameToPath:path];
        printf("%s\n", ok ? "ok" : "error");
        fflush(stdout);
    } else if ([cmd isEqualToString:@"freeze"]) {
        [preview freeze];
        printf("ok\n");
        fflush(stdout);
    } else if ([cmd isEqualToString:@"unfreeze"]) {
        [preview unfreeze];
        printf("ok\n");
        fflush(stdout);
    } else if ([cmd isEqualToString:@"quit"]) {
        [preview stopCamera];
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
        CGFloat w = 480, h = 360;
        CGFloat x = sf.size.width - w - 24;
        CGFloat y = sf.size.height - h - 60;

        CameraWindow *win = [[CameraWindow alloc]
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
