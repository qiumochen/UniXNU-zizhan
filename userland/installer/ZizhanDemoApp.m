/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* ZizhanDemoApp: Real third-party containerized UIKit app packaged as a 16 KiB page-signed
 * decrypted .ipa (com.zizhan.demoapp) to validate end-to-end IPA sideloading, CoreLocation GNSS,
 * and system-level AVCaptureSession (live camera preview + photo capture + QR code recognition)
 * inside /private/var/containers/Bundle/Application/<UUID>/ZizhanDemoApp.app! */
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreLocation/CoreLocation.h>
#include <sys/sysctl.h>

static bool demo_has_token(const char *args, const char *opt) {
    const size_t n = strlen(opt);
    for (const char *p = args; (p = strstr(p, opt)) != NULL; ++p) {
        if ((p == args || p[-1] == ' ' || p[-1] == '\t') &&
            (p[n] == '\0' || p[n] == ' ' || p[n] == '\t')) return true;
    }
    return false;
}

static bool demo_boot_option(const char *opt) {
    char args[2048] = {0};
    size_t len = sizeof(args) - 1;
    if (sysctlbyname("kern.zizhan_bootargs", args, &len, NULL, 0) == 0 && demo_has_token(args, opt))
        return true;
    memset(args, 0, sizeof(args));
    len = sizeof(args) - 1;
    if (sysctlbyname("kern.bootargs", args, &len, NULL, 0) == 0 && demo_has_token(args, opt))
        return true;
    return false;
}

@interface ZizhanDemoController : UIViewController <AVCapturePhotoCaptureDelegate, AVCaptureMetadataOutputObjectsDelegate, CLLocationManagerDelegate>
@property (nonatomic, strong) UILabel *statusLabel;
@property (nonatomic, strong) UILabel *qrLabel;
@property (nonatomic, strong) UIView *previewHost;
@property (nonatomic, strong) AVCaptureSession *session;
@property (nonatomic, strong) AVCaptureVideoPreviewLayer *previewLayer;
@property (nonatomic, strong) AVCapturePhotoOutput *photoOutput;
@property (nonatomic, strong) AVCaptureMetadataOutput *metaOutput;
@property (nonatomic, strong) CLLocationManager *locMgr;
@property (nonatomic, assign) AVCaptureDevicePosition currentPos;
@property (nonatomic, assign) unsigned qrHits;
@property (nonatomic, assign) unsigned photosSaved;
@property (nonatomic, copy) NSString *lastQrPayload;
@end

@implementation ZizhanDemoController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithRed:0.06 green:0.08 blue:0.12 alpha:1.0];
    self.title = @"Zizhan 16K IPA · 相机与扫码验证";
    self.currentPos = AVCaptureDevicePositionBack;

    UILabel *banner = [[UILabel alloc] init];
    banner.translatesAutoresizingMaskIntoConstraints = NO;
    banner.text = @"第三方 16K 脱壳 IPA 已安装运行 (com.zizhan.demoapp)";
    banner.font = [UIFont boldSystemFontOfSize:20.0];
    banner.textColor = [UIColor colorWithRed:0.35 green:0.92 blue:0.62 alpha:1.0];
    banner.textAlignment = NSTextAlignmentCenter;
    banner.numberOfLines = 0;

    self.statusLabel = [[UILabel alloc] init];
    self.statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    self.statusLabel.text = [NSString stringWithFormat:@"Bundle Path: %@\n正在初始化 AVCaptureSession + CoreLocation...",
                             [[NSBundle mainBundle] bundlePath]];
    self.statusLabel.font = [UIFont monospacedSystemFontOfSize:13.0 weight:UIFontWeightRegular];
    self.statusLabel.textColor = [UIColor colorWithWhite:0.88 alpha:1.0];
    self.statusLabel.numberOfLines = 0;

    self.previewHost = [[UIView alloc] init];
    self.previewHost.translatesAutoresizingMaskIntoConstraints = NO;
    self.previewHost.backgroundColor = [UIColor blackColor];
    self.previewHost.layer.cornerRadius = 14.0;
    self.previewHost.layer.masksToBounds = YES;
    self.previewHost.layer.borderWidth = 2.0;
    self.previewHost.layer.borderColor = [UIColor colorWithRed:0.25 green:0.65 blue:1.0 alpha:0.8].CGColor;

    self.qrLabel = [[UILabel alloc] init];
    self.qrLabel.translatesAutoresizingMaskIntoConstraints = NO;
    self.qrLabel.text = @"二维码识别: 等待帧输入...";
    self.qrLabel.font = [UIFont boldSystemFontOfSize:16.0];
    self.qrLabel.textColor = [UIColor colorWithRed:1.0 green:0.84 blue:0.30 alpha:1.0];
    self.qrLabel.textAlignment = NSTextAlignmentCenter;
    self.qrLabel.numberOfLines = 0;

    UIButton *flipBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    flipBtn.translatesAutoresizingMaskIntoConstraints = NO;
    [flipBtn setTitle:@"切换前/后摄" forState:UIControlStateNormal];
    flipBtn.titleLabel.font = [UIFont boldSystemFontOfSize:16.0];
    flipBtn.backgroundColor = [UIColor colorWithRed:0.16 green:0.42 blue:0.85 alpha:1.0];
    [flipBtn setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    flipBtn.layer.cornerRadius = 10.0;
    [flipBtn addTarget:self action:@selector(toggleCamera) forControlEvents:UIControlEventTouchUpInside];

    UIButton *snapBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    snapBtn.translatesAutoresizingMaskIntoConstraints = NO;
    [snapBtn setTitle:@"拍照 (JPEG)" forState:UIControlStateNormal];
    snapBtn.titleLabel.font = [UIFont boldSystemFontOfSize:16.0];
    snapBtn.backgroundColor = [UIColor colorWithRed:0.15 green:0.65 blue:0.42 alpha:1.0];
    [snapBtn setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    snapBtn.layer.cornerRadius = 10.0;
    [snapBtn addTarget:self action:@selector(takePhoto) forControlEvents:UIControlEventTouchUpInside];

    UIStackView *btnRow = [[UIStackView alloc] initWithArrangedSubviews:@[flipBtn, snapBtn]];
    btnRow.translatesAutoresizingMaskIntoConstraints = NO;
    btnRow.axis = UILayoutConstraintAxisHorizontal;
    btnRow.spacing = 16.0;
    btnRow.distribution = UIStackViewDistributionFillEqually;

    [self.view addSubview:banner];
    [self.view addSubview:self.statusLabel];
    [self.view addSubview:self.previewHost];
    [self.view addSubview:self.qrLabel];
    [self.view addSubview:btnRow];

    UILayoutGuide *g = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [banner.topAnchor constraintEqualToAnchor:g.topAnchor constant:12.0],
        [banner.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:16.0],
        [banner.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-16.0],

        [self.statusLabel.topAnchor constraintEqualToAnchor:banner.bottomAnchor constant:8.0],
        [self.statusLabel.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:16.0],
        [self.statusLabel.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-16.0],

        [self.previewHost.topAnchor constraintEqualToAnchor:self.statusLabel.bottomAnchor constant:12.0],
        [self.previewHost.centerXAnchor constraintEqualToAnchor:g.centerXAnchor],
        [self.previewHost.widthAnchor constraintLessThanOrEqualToAnchor:g.widthAnchor constant:-32.0],
        [self.previewHost.widthAnchor constraintEqualToConstant:480.0],
        [self.previewHost.heightAnchor constraintEqualToConstant:360.0],

        [self.qrLabel.topAnchor constraintEqualToAnchor:self.previewHost.bottomAnchor constant:12.0],
        [self.qrLabel.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:16.0],
        [self.qrLabel.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-16.0],

        [btnRow.topAnchor constraintEqualToAnchor:self.qrLabel.bottomAnchor constant:14.0],
        [btnRow.centerXAnchor constraintEqualToAnchor:g.centerXAnchor],
        [btnRow.widthAnchor constraintEqualToConstant:360.0],
        [btnRow.heightAnchor constraintEqualToConstant:44.0],
    ]];

    fprintf(stderr, "ZIZHAN_DEMOAPP: LAUNCHED bundle=%s path=%s\n",
            [[[NSBundle mainBundle] bundleIdentifier] UTF8String],
            [[[NSBundle mainBundle] bundlePath] UTF8String]);
    fflush(stderr);

    self.locMgr = [[CLLocationManager alloc] init];
    self.locMgr.delegate = self;
    [self.locMgr startUpdatingLocation];

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [self startCameraPosition:self.currentPos];
    });
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    if (self.previewLayer) {
        self.previewLayer.frame = self.previewHost.bounds;
    }
}

- (void)startCameraPosition:(AVCaptureDevicePosition)pos {
    if (self.session) {
        [self.session stopRunning];
        [self.previewLayer removeFromSuperlayer];
        self.session = nil;
    }
    AVAuthorizationStatus auth = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    AVCaptureDevice *dev = [AVCaptureDevice defaultDeviceWithDeviceType:AVCaptureDeviceTypeBuiltInWideAngleCamera
                                                              mediaType:AVMediaTypeVideo
                                                               position:pos];
    if (!dev) dev = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    NSError *err = nil;
    AVCaptureDeviceInput *inp = dev ? [AVCaptureDeviceInput deviceInputWithDevice:dev error:&err] : nil;
    AVCaptureSession *sess = [[AVCaptureSession alloc] init];
    if (inp && [sess canAddInput:inp]) [sess addInput:inp];

    AVCapturePhotoOutput *pout = [[AVCapturePhotoOutput alloc] init];
    if ([sess canAddOutput:pout]) [sess addOutput:pout];
    self.photoOutput = pout;

    AVCaptureMetadataOutput *mout = [[AVCaptureMetadataOutput alloc] init];
    if ([sess canAddOutput:mout]) {
        [sess addOutput:mout];
        [mout setMetadataObjectsDelegate:self queue:dispatch_get_main_queue()];
        if ([[mout availableMetadataObjectTypes] containsObject:AVMetadataObjectTypeQRCode]) {
            mout.metadataObjectTypes = @[AVMetadataObjectTypeQRCode];
        }
    }
    self.metaOutput = mout;

    AVCaptureVideoPreviewLayer *layer = [AVCaptureVideoPreviewLayer layerWithSession:sess];
    layer.videoGravity = AVLayerVideoGravityResizeAspectFill;
    layer.frame = self.previewHost.bounds;
    [self.previewHost.layer addSublayer:layer];
    self.previewLayer = layer;
    self.session = sess;
    [sess startRunning];

    CLLocation *loc = self.locMgr.location;
    self.statusLabel.text = [NSString stringWithFormat:
        @"Bundle: %@\nCamera Auth=%ld Device=%@ (pos=%ld) running=%d | GPS valid=%d hAcc=%.1fm",
        [[NSBundle mainBundle] bundleIdentifier], (long)auth,
        dev ? dev.localizedName : @"nil", (long)pos, (int)sess.isRunning,
        loc != nil, loc ? loc.horizontalAccuracy : -1.0];
    fprintf(stderr, "ZIZHAN_DEMOAPP: CAMERA_START pos=%ld auth=%ld dev=%s running=%d gps_ok=%d\n",
            (long)pos, (long)auth, dev ? [[dev uniqueID] UTF8String] : "nil",
            (int)sess.isRunning, loc != nil);
    fflush(stderr);

    if (demo_boot_option("zizhan_cam_autotest=1") && self.photosSaved == 0 && pos == AVCaptureDevicePositionBack) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 4 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            [self takePhoto];
        });
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 9 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            [self toggleCamera];
        });
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 14 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            [self takePhoto];
        });
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 18 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            if (self.session) {
                [self.session stopRunning];
                fprintf(stderr, "ZIZHAN_DEMOAPP: AUTOTEST_DONE photos=%u qr_hits=%u session_stopped=1\n",
                        self.photosSaved, self.qrHits);
                fflush(stderr);
            }
        });
    }
}

- (void)toggleCamera {
    self.currentPos = (self.currentPos == AVCaptureDevicePositionBack)
                      ? AVCaptureDevicePositionFront : AVCaptureDevicePositionBack;
    [self startCameraPosition:self.currentPos];
}

- (void)takePhoto {
    if (!self.photoOutput) return;
    AVCapturePhotoSettings *settings = [AVCapturePhotoSettings photoSettings];
    [self.photoOutput capturePhotoWithSettings:settings delegate:self];
}

- (void)captureOutput:(AVCapturePhotoOutput *)output didFinishProcessingPhoto:(AVCapturePhoto *)photo error:(NSError *)error {
    (void)output;
    NSData *jpeg = photo ? [photo fileDataRepresentation] : nil;
    NSString *outPath = [NSTemporaryDirectory() stringByAppendingPathComponent:
                         [NSString stringWithFormat:@"zizhan-photo-%u.jpg", ++self.photosSaved]];
    BOOL wrote = jpeg ? [jpeg writeToFile:outPath atomically:YES] : NO;
    fprintf(stderr, "ZIZHAN_DEMOAPP: PHOTO_CAPTURED idx=%u bytes=%lu wrote=%d err=%s\n",
            self.photosSaved, (unsigned long)(jpeg ? jpeg.length : 0), (int)wrote,
            error ? [[error localizedDescription] UTF8String] : "none");
    fflush(stderr);
    self.qrLabel.text = self.lastQrPayload
        ? [NSString stringWithFormat:@"扫码: %@ | 拍照 #%u (%lu B)",
           self.lastQrPayload, self.photosSaved, (unsigned long)(jpeg ? jpeg.length : 0)]
        : [NSString stringWithFormat:@"拍照成功 #%u: %lu 字节 JPEG | 二维码命中=%u",
           self.photosSaved, (unsigned long)(jpeg ? jpeg.length : 0), self.qrHits];
}

- (void)captureOutput:(AVCaptureOutput *)output didOutputMetadataObjects:(NSArray<__kindof AVMetadataObject *> *)metadataObjects fromConnection:(AVCaptureConnection *)connection {
    (void)output; (void)connection;
    for (AVMetadataObject *obj in metadataObjects) {
        if ([obj isKindOfClass:[AVMetadataMachineReadableCodeObject class]]) {
            NSString *val = [(AVMetadataMachineReadableCodeObject *)obj stringValue];
            if (val.length > 0) {
                ++self.qrHits;
                self.lastQrPayload = val;
                self.qrLabel.text = self.photosSaved > 0
                    ? [NSString stringWithFormat:@"扫码: %@ (#%u) | 拍照 #%u", val, self.qrHits, self.photosSaved]
                    : [NSString stringWithFormat:@"扫码成功 (#%u): %@", self.qrHits, val];
                if (self.qrHits <= 3) {
                    fprintf(stderr, "ZIZHAN_DEMOAPP: QR_DECODED hit=%u payload=%s\n",
                            self.qrHits, [val UTF8String]);
                    fflush(stderr);
                }
            }
        }
    }
}

@end

@interface ZizhanDemoAppDelegate : UIResponder <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@end

@implementation ZizhanDemoAppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    (void)application; (void)launchOptions;
    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    UINavigationController *nav = [[UINavigationController alloc] initWithRootViewController:[[ZizhanDemoController alloc] init]];
    nav.navigationBar.barStyle = UIBarStyleBlack;
    self.window.rootViewController = nav;
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([ZizhanDemoAppDelegate class]));
    }
}
