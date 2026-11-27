/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* ZizhanDemoApp: Real third-party containerized UIKit app packaged as a 16 KiB page-signed
 * decrypted .ipa (com.zizhan.demoapp, "扫码相机验证") to validate end-to-end IPA sideloading,
 * CoreLocation GNSS, and 3-camera 960x720 AVCaptureSession (1x / 0.6x / Front live switching,
 * DW9800V autofocus, JPEG photo capture, and real-time QR code recognition). */
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
@property (nonatomic, strong) NSArray<UIButton *> *lensButtons;
@property (nonatomic, strong) AVCaptureSession *session;
@property (nonatomic, strong) AVCaptureVideoPreviewLayer *previewLayer;
@property (nonatomic, strong) AVCapturePhotoOutput *photoOutput;
@property (nonatomic, strong) AVCaptureMetadataOutput *metaOutput;
@property (nonatomic, strong) CLLocationManager *locMgr;
@property (nonatomic, assign) int currentLensTag; /* 1=1x, 4=0.6x, 2=Front */
@property (nonatomic, assign) unsigned qrHits;
@property (nonatomic, assign) unsigned photosSaved;
@property (nonatomic, copy) NSString *lastQrPayload;
@end

@implementation ZizhanDemoController

- (UIButton *)makeBtn:(NSString *)title bg:(UIColor *)bg sel:(SEL)sel tag:(NSInteger)tag {
    UIButton *b = [UIButton buttonWithType:UIButtonTypeSystem];
    b.translatesAutoresizingMaskIntoConstraints = NO;
    [b setTitle:title forState:UIControlStateNormal];
    b.titleLabel.font = [UIFont boldSystemFontOfSize:15.0];
    b.titleLabel.adjustsFontSizeToFitWidth = YES;
    b.titleLabel.minimumScaleFactor = 0.75;
    b.backgroundColor = bg;
    [b setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    b.layer.cornerRadius = 10.0;
    b.tag = tag;
    [b addTarget:self action:sel forControlEvents:UIControlEventTouchUpInside];
    return b;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithRed:0.06 green:0.08 blue:0.12 alpha:1.0];
    self.title = @"MIX Fold 2 · 三摄与扫码相机";
    self.currentLensTag = 1;

    UILabel *banner = [[UILabel alloc] init];
    banner.translatesAutoresizingMaskIntoConstraints = NO;
    banner.text = @"960×720 三摄实时取景 · 二维码秒级识别 · 自动对焦拍照";
    banner.font = [UIFont boldSystemFontOfSize:18.0];
    banner.textColor = [UIColor colorWithRed:0.35 green:0.92 blue:0.62 alpha:1.0];
    banner.textAlignment = NSTextAlignmentCenter;
    banner.numberOfLines = 0;

    self.statusLabel = [[UILabel alloc] init];
    self.statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    self.statusLabel.text = @"正在初始化 960×720 三摄 AVCaptureSession 与 CoreLocation GNSS...";
    self.statusLabel.font = [UIFont monospacedSystemFontOfSize:12.5 weight:UIFontWeightRegular];
    self.statusLabel.textColor = [UIColor colorWithWhite:0.88 alpha:1.0];
    self.statusLabel.textAlignment = NSTextAlignmentCenter;
    self.statusLabel.numberOfLines = 2;

    self.previewHost = [[UIView alloc] init];
    self.previewHost.translatesAutoresizingMaskIntoConstraints = NO;
    self.previewHost.backgroundColor = [UIColor blackColor];
    self.previewHost.layer.cornerRadius = 14.0;
    self.previewHost.layer.masksToBounds = YES;
    self.previewHost.layer.borderWidth = 2.0;
    self.previewHost.layer.borderColor = [UIColor colorWithRed:0.25 green:0.65 blue:1.0 alpha:0.8].CGColor;

    self.qrLabel = [[UILabel alloc] init];
    self.qrLabel.translatesAutoresizingMaskIntoConstraints = NO;
    self.qrLabel.text = @"二维码识别就绪：对准任意二维码即可实时读取内容";
    self.qrLabel.font = [UIFont boldSystemFontOfSize:15.5];
    self.qrLabel.textColor = [UIColor colorWithRed:1.0 green:0.84 blue:0.30 alpha:1.0];
    self.qrLabel.textAlignment = NSTextAlignmentCenter;
    self.qrLabel.numberOfLines = 2;

    UIColor *idleBg = [UIColor colorWithRed:0.16 green:0.26 blue:0.42 alpha:1.0];
    UIButton *b1x   = [self makeBtn:@"1x 主摄"     bg:idleBg sel:@selector(onSelectLens:) tag:1];
    UIButton *b06   = [self makeBtn:@"0.6x 超广角" bg:idleBg sel:@selector(onSelectLens:) tag:4];
    UIButton *bFr   = [self makeBtn:@"前置镜头"    bg:idleBg sel:@selector(onSelectLens:) tag:2];
    UIButton *bSnap = [self makeBtn:@"对焦 + 拍照" bg:[UIColor colorWithRed:0.15 green:0.62 blue:0.42 alpha:1.0]
                                sel:@selector(takePhoto) tag:10];
    self.lensButtons = @[b1x, b06, bFr];

    UIStackView *btnRow = [[UIStackView alloc] initWithArrangedSubviews:@[b1x, b06, bFr, bSnap]];
    btnRow.translatesAutoresizingMaskIntoConstraints = NO;
    btnRow.axis = UILayoutConstraintAxisHorizontal;
    btnRow.spacing = 10.0;
    btnRow.distribution = UIStackViewDistributionFillEqually;

    [self.view addSubview:banner];
    [self.view addSubview:self.statusLabel];
    [self.view addSubview:self.previewHost];
    [self.view addSubview:self.qrLabel];
    [self.view addSubview:btnRow];

    UILayoutGuide *g = self.view.safeAreaLayoutGuide;
    NSLayoutConstraint *prefW = [self.previewHost.widthAnchor constraintEqualToConstant:560.0];
    prefW.priority = UILayoutPriorityDefaultHigh;
    NSLayoutConstraint *prefH = [self.previewHost.heightAnchor constraintEqualToConstant:420.0];
    prefH.priority = UILayoutPriorityDefaultHigh;

    [NSLayoutConstraint activateConstraints:@[
        [banner.topAnchor constraintEqualToAnchor:g.topAnchor constant:10.0],
        [banner.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:14.0],
        [banner.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-14.0],

        [self.statusLabel.topAnchor constraintEqualToAnchor:banner.bottomAnchor constant:6.0],
        [self.statusLabel.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:14.0],
        [self.statusLabel.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-14.0],

        [self.previewHost.topAnchor constraintEqualToAnchor:self.statusLabel.bottomAnchor constant:10.0],
        [self.previewHost.centerXAnchor constraintEqualToAnchor:g.centerXAnchor],
        [self.previewHost.widthAnchor constraintLessThanOrEqualToAnchor:g.widthAnchor constant:-28.0],
        prefW,
        prefH,
        [self.previewHost.heightAnchor constraintLessThanOrEqualToAnchor:g.heightAnchor multiplier:0.62],

        [self.qrLabel.topAnchor constraintEqualToAnchor:self.previewHost.bottomAnchor constant:10.0],
        [self.qrLabel.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:14.0],
        [self.qrLabel.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-14.0],

        [btnRow.topAnchor constraintEqualToAnchor:self.qrLabel.bottomAnchor constant:12.0],
        [btnRow.centerXAnchor constraintEqualToAnchor:g.centerXAnchor],
        [btnRow.leadingAnchor constraintGreaterThanOrEqualToAnchor:g.leadingAnchor constant:14.0],
        [btnRow.trailingAnchor constraintLessThanOrEqualToAnchor:g.trailingAnchor constant:-14.0],
        [btnRow.widthAnchor constraintLessThanOrEqualToConstant:560.0],
        [btnRow.heightAnchor constraintEqualToConstant:44.0],
    ]];

    fprintf(stderr, "ZIZHAN_DEMOAPP: LAUNCHED bundle=%s path=%s\n",
            [[[NSBundle mainBundle] bundleIdentifier] UTF8String],
            [[[NSBundle mainBundle] bundlePath] UTF8String]);
    fflush(stderr);

    self.locMgr = [[CLLocationManager alloc] init];
    self.locMgr.delegate = self;
    [self.locMgr startUpdatingLocation];

    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(onDidBackground)
                                                 name:UIApplicationDidEnterBackgroundNotification object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(onWillForeground)
                                                 name:UIApplicationWillEnterForegroundNotification object:nil];

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.35 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [self selectLensTag:1];
    });
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    if (self.previewLayer) {
        self.previewLayer.frame = self.previewHost.bounds;
    }
}

- (void)onDidBackground {
    if (self.session && self.session.isRunning) [self.session stopRunning];
}

- (void)onWillForeground {
    if (self.session && !self.session.isRunning && !demo_boot_option("zizhan_cam_autotest=1")) {
        [self.session startRunning];
    }
}

- (AVCaptureDevice *)deviceForTag:(int)tag {
    if (tag == 4) {
        return [AVCaptureDevice defaultDeviceWithDeviceType:AVCaptureDeviceTypeBuiltInUltraWideCamera
                                                  mediaType:AVMediaTypeVideo
                                                   position:AVCaptureDevicePositionBack];
    }
    if (tag == 2) {
        return [AVCaptureDevice defaultDeviceWithDeviceType:AVCaptureDeviceTypeBuiltInWideAngleCamera
                                                  mediaType:AVMediaTypeVideo
                                                   position:AVCaptureDevicePositionFront];
    }
    return [AVCaptureDevice defaultDeviceWithDeviceType:AVCaptureDeviceTypeBuiltInWideAngleCamera
                                              mediaType:AVMediaTypeVideo
                                               position:AVCaptureDevicePositionBack];
}

- (void)selectLensTag:(int)tag {
    self.currentLensTag = tag;
    AVCaptureDevice *dev = [self deviceForTag:tag];
    if (!dev) dev = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    if (!self.session) {
        AVCaptureSession *sess = [[AVCaptureSession alloc] init];
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
    }

    AVCaptureDeviceInput *inp = dev ? [AVCaptureDeviceInput deviceInputWithDevice:dev error:nil] : nil;
    if (inp) [self.session addInput:inp];
    if (!self.session.isRunning) [self.session startRunning];

    for (UIButton *b in self.lensButtons) {
        b.backgroundColor = (b.tag == tag)
            ? [UIColor colorWithRed:0.16 green:0.52 blue:0.88 alpha:1.0]
            : [UIColor colorWithRed:0.16 green:0.26 blue:0.42 alpha:1.0];
    }

    AVAuthorizationStatus auth = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    CLLocation *loc = self.locMgr.location;
    self.statusLabel.text = [NSString stringWithFormat:
        @"当前镜头: %@ (960×720 持续预览中)\n权限=%ld | GPS定位=%@ (精度 %.1fm)",
        dev ? dev.localizedName : @"nil", (long)auth,
        loc ? @"已就绪" : @"搜星中", loc ? loc.horizontalAccuracy : -1.0];
    fprintf(stderr, "ZIZHAN_DEMOAPP: CAMERA_START tag=%d auth=%ld dev=%s running=%d gps_ok=%d\n",
            tag, (long)auth, dev ? [[dev uniqueID] UTF8String] : "nil",
            (int)self.session.isRunning, loc != nil);
    fflush(stderr);

    if (demo_boot_option("zizhan_cam_autotest=1") && self.photosSaved == 0 && tag == 1) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 4 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            [self takePhoto];
        });
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 8 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            [self selectLensTag:4];
        });
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 12 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            [self selectLensTag:2];
        });
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
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

- (void)onSelectLens:(UIButton *)sender {
    [self selectLensTag:(int)sender.tag];
}

- (void)takePhoto {
    if (!self.photoOutput) return;
    AVCaptureDevice *dev = [self deviceForTag:self.currentLensTag];
    if (dev && [dev isFocusModeSupported:AVCaptureFocusModeAutoFocus]) {
        [dev lockForConfiguration:nil];
        [dev setFocusMode:AVCaptureFocusModeAutoFocus];
        [dev unlockForConfiguration];
    }
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
        ? [NSString stringWithFormat:@"扫码: %@ | 拍照 #%u (960×720, %lu B)",
           self.lastQrPayload, self.photosSaved, (unsigned long)(jpeg ? jpeg.length : 0)]
        : [NSString stringWithFormat:@"拍照成功 #%u: 960×720 JPEG (%lu 字节) | 二维码命中=%u",
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
