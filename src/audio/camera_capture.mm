#import <AVFoundation/AVFoundation.h>
#import <CoreImage/CoreImage.h>
#import <AppKit/AppKit.h>
#import <dispatch/dispatch.h>
#include "camera_capture.h"
#include <atomic>

// Delegate that skips warmup frames then captures one properly-exposed frame
@interface RCLISingleFrameCapture : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property (nonatomic, strong) NSString *outputPath;
@property (nonatomic, assign) BOOL captured;
@property (nonatomic, strong) dispatch_semaphore_t semaphore;
@property (nonatomic, assign) int frameCount;
@property (nonatomic, assign) int framesToSkip;
@end

@implementation RCLISingleFrameCapture

- (void)captureOutput:(AVCaptureOutput *)output
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection *)connection {
    if (self.captured) return;

    // Skip initial frames to let auto-exposure/white-balance stabilize
    self.frameCount++;
    if (self.frameCount < self.framesToSkip) return;

    self.captured = YES;

    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!imageBuffer) {
        dispatch_semaphore_signal(self.semaphore);
        return;
    }

    CIImage *ciImage = [CIImage imageWithCVImageBuffer:imageBuffer];
    NSCIImageRep *rep = [NSCIImageRep imageRepWithCIImage:ciImage];
    NSImage *nsImage = [[NSImage alloc] initWithSize:rep.size];
    [nsImage addRepresentation:rep];

    // Convert to JPEG at high quality
    NSData *tiffData = [nsImage TIFFRepresentation];
    NSBitmapImageRep *bitmapRep = [NSBitmapImageRep imageRepWithData:tiffData];
    NSData *jpegData = [bitmapRep representationUsingType:NSBitmapImageFileTypeJPEG
                                               properties:@{NSImageCompressionFactor: @0.92}];
    [jpegData writeToFile:self.outputPath atomically:YES];

    dispatch_semaphore_signal(self.semaphore);
}

@end

int camera_capture_photo(const char* output_path) {
    @autoreleasepool {
        // Check camera permission
        AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
        if (status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted) {
            return -1;
        }
        if (status == AVAuthorizationStatusNotDetermined) {
            dispatch_semaphore_t perm_sem = dispatch_semaphore_create(0);
            __block BOOL granted = NO;
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL g) {
                granted = g;
                dispatch_semaphore_signal(perm_sem);
            }];
            dispatch_semaphore_wait(perm_sem, DISPATCH_TIME_FOREVER);
            if (!granted) return -1;
        }

        // Find default camera
        AVCaptureDevice *device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
        if (!device) return -1;

        // Configure camera for best quality and let auto-exposure do its thing
        NSError *error = nil;
        if ([device lockForConfiguration:&error]) {
            // Enable continuous auto-exposure and white balance
            if ([device isExposureModeSupported:AVCaptureExposureModeContinuousAutoExposure]) {
                device.exposureMode = AVCaptureExposureModeContinuousAutoExposure;
            }
            if ([device isWhiteBalanceModeSupported:AVCaptureWhiteBalanceModeContinuousAutoWhiteBalance]) {
                device.whiteBalanceMode = AVCaptureWhiteBalanceModeContinuousAutoWhiteBalance;
            }
            if ([device isFocusModeSupported:AVCaptureFocusModeContinuousAutoFocus]) {
                device.focusMode = AVCaptureFocusModeContinuousAutoFocus;
            }
            [device unlockForConfiguration];
        }

        AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&error];
        if (!input) return -1;

        AVCaptureSession *session = [[AVCaptureSession alloc] init];
        // Use Photo preset for highest quality
        if ([session canSetSessionPreset:AVCaptureSessionPresetPhoto]) {
            session.sessionPreset = AVCaptureSessionPresetPhoto;
        } else if ([session canSetSessionPreset:AVCaptureSessionPresetHigh]) {
            session.sessionPreset = AVCaptureSessionPresetHigh;
        } else {
            session.sessionPreset = AVCaptureSessionPresetMedium;
        }

        if (![session canAddInput:input]) return -1;
        [session addInput:input];

        AVCaptureVideoDataOutput *videoOutput = [[AVCaptureVideoDataOutput alloc] init];
        videoOutput.videoSettings = @{(NSString *)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)};
        videoOutput.alwaysDiscardsLateVideoFrames = YES;

        RCLISingleFrameCapture *delegate = [[RCLISingleFrameCapture alloc] init];
        delegate.outputPath = [NSString stringWithUTF8String:output_path];
        delegate.captured = NO;
        delegate.semaphore = dispatch_semaphore_create(0);
        delegate.frameCount = 0;
        // Skip ~60 frames (~2 seconds at 30fps) to let auto-exposure fully stabilize
        delegate.framesToSkip = 60;

        dispatch_queue_t queue = dispatch_queue_create("com.rcli.camera", DISPATCH_QUEUE_SERIAL);
        [videoOutput setSampleBufferDelegate:delegate queue:queue];

        if (![session canAddOutput:videoOutput]) return -1;
        [session addOutput:videoOutput];

        // Start capture — delegate will skip first 60 frames for AE stabilization
        [session startRunning];

        // Wait for frame capture (timeout 10 seconds — allows for warmup + capture)
        long result = dispatch_semaphore_wait(delegate.semaphore,
            dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));

        [session stopRunning];

        if (result != 0) return -1; // timeout

        // Verify the file was written
        NSFileManager *fm = [NSFileManager defaultManager];
        if (![fm fileExistsAtPath:delegate.outputPath]) return -1;

        return 0;
    }
}
