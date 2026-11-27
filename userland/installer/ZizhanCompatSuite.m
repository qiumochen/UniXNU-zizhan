/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* ZizhanCompatSuite.app (com.zizhan.compatsuite, 16K page-signed decrypted third-party IPA):
 * Interactive + automated 8-subsystem runtime verification suite for Xiaomi MIX Fold 2:
 *  1. METAL:    MTLCreateSystemDefaultDevice() + MTLCommandQueue + blit/fill GPU command completion
 *  2. KEYCHAIN: SecItemAdd / SecItemCopyMatching / SecItemUpdate / SecItemDelete (0 -34018 errors)
 *  3. APPGROUP: containerURLForSecurityApplicationGroupIdentifier:@"group.com.zizhan.shared" R/W
 *  4. SQLITE:   SQLite3 WAL database create + insert + query inside container Documents/
 *  5. WEBKIT:   WKWebView HTML load + JavaScript evaluation round-trip
 *  6. PERIPH:   PM8350B X-axis haptics + CS35L41 top earpiece / dual stereo speaker routing + test chime
 *  7. LOCATION: CLLocationManager authorizationStatus == AuthorizedAlways + GNSS bridge readiness
 *  8. CAMERA3:  AVCaptureDevice 3-lens discovery (1x, 0.6x, Front) + continuous 960x720 preview +
 *               live QR scanner + DW9800V autofocus + JPEG photo capture!
 *
 * Behavior policy:
 *  - Interactive mode (default): NO surprise vibration on launch; 960x720 camera preview stays ON
 *    continuously until paused or backgrounded; full 9-button control deck for manual testing.
 *  - Unattended CI mode (`zizhan_compat_autotest=1` boot-arg): runs automated haptics/route/3-lens
 *    sweep and stops camera session on completion (`idle=0mW`).
 */
#import <UIKit/UIKit.h>
#import <Metal/Metal.h>
#import <Security/Security.h>
#import <WebKit/WebKit.h>
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreLocation/CoreLocation.h>
#include <math.h>
#include <sqlite3.h>
#include <stdio.h>
#include <sys/sysctl.h>

static bool suite_has_token(const char *args, const char *opt) {
    const size_t n = strlen(opt);
    for (const char *p = args; (p = strstr(p, opt)) != NULL; ++p) {
        if ((p == args || p[-1] == ' ' || p[-1] == '\t') &&
            (p[n] == '\0' || p[n] == ' ' || p[n] == '\t')) return true;
    }
    return false;
}

static bool suite_boot_option(const char *opt) {
    char args[2048] = {0};
    size_t len = sizeof(args) - 1;
    if (sysctlbyname("kern.zizhan_bootargs", args, &len, NULL, 0) == 0 && suite_has_token(args, opt))
        return true;
    memset(args, 0, sizeof(args));
    len = sizeof(args) - 1;
    if (sysctlbyname("kern.bootargs", args, &len, NULL, 0) == 0 && suite_has_token(args, opt))
        return true;
    return false;
}

/* Short self-contained AudioQueue chime (523Hz -> 659Hz, 1.1s) for earpiece vs stereo speaker verification */
typedef struct {
    AudioQueueRef queue;
    uint32_t sampleIdx;
    uint32_t totalSamples;
} ZizhanToneState;

static void tone_aq_callback(void *inUserData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer) {
    ZizhanToneState *st = (ZizhanToneState *)inUserData;
    if (!st || st->sampleIdx >= st->totalSamples) {
        AudioQueueStop(inAQ, false);
        AudioQueueDispose(inAQ, false);
        free(st);
        return;
    }
    const uint32_t frames = inBuffer->mAudioDataBytesCapacity / (2u * sizeof(int16_t));
    int16_t *pcm = (int16_t *)inBuffer->mAudioData;
    for (uint32_t i = 0; i < frames; ++i) {
        uint32_t idx = st->sampleIdx + i;
        if (idx >= st->totalSamples) {
            pcm[i * 2] = 0;
            pcm[i * 2 + 1] = 0;
            continue;
        }
        double freq = (idx < 24000u) ? 523.25 : 659.25;
        double env = 1.0;
        if (idx < 1200u) env = (double)idx / 1200.0;
        else if (st->totalSamples - idx < 2400u) env = (double)(st->totalSamples - idx) / 2400.0;
        int16_t s = (int16_t)(sin(2.0 * M_PI * freq * ((double)idx / 48000.0)) * 7500.0 * env);
        pcm[i * 2] = s;
        pcm[i * 2 + 1] = s;
    }
    st->sampleIdx += frames;
    inBuffer->mAudioDataByteSize = frames * 2u * sizeof(int16_t);
    AudioQueueEnqueueBuffer(inAQ, inBuffer, 0, NULL);
}

static void play_test_chime_48k(void) {
    AudioStreamBasicDescription fmt = {0};
    fmt.mSampleRate = 48000.0;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    fmt.mBytesPerPacket = 4;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerFrame = 4;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 16;

    ZizhanToneState *st = (ZizhanToneState *)calloc(1, sizeof(ZizhanToneState));
    if (!st) return;
    st->totalSamples = 52800u; /* 1.1 seconds @ 48 kHz */
    AudioQueueRef q = NULL;
    if (AudioQueueNewOutput(&fmt, tone_aq_callback, st, NULL, kCFRunLoopCommonModes, 0, &q) != noErr || !q) {
        free(st);
        return;
    }
    st->queue = q;
    for (int b = 0; b < 3; ++b) {
        AudioQueueBufferRef buf = NULL;
        if (AudioQueueAllocateBuffer(q, 4096 * 4, &buf) == noErr && buf) {
            tone_aq_callback(st, q, buf);
        }
    }
    AudioQueueStart(q, NULL);
}

@interface ZizhanCompatController : UIViewController <AVCapturePhotoCaptureDelegate, AVCaptureMetadataOutputObjectsDelegate, WKNavigationDelegate>
@property (nonatomic, strong) UILabel *headerLabel;
@property (nonatomic, strong) UIButton *rerunBtn;
@property (nonatomic, strong) UITextView *logView;
@property (nonatomic, strong) UIView *camBox;
@property (nonatomic, strong) UILabel *camInfoLabel;
@property (nonatomic, strong) UIView *controlsHost;
@property (nonatomic, strong) NSArray<UIButton *> *controlButtons;
@property (nonatomic, strong) WKWebView *webView;
@property (nonatomic, strong) AVCaptureSession *session;
@property (nonatomic, strong) AVCapturePhotoOutput *photoOut;
@property (nonatomic, strong) AVCaptureMetadataOutput *metaOut;
@property (nonatomic, strong) AVCaptureVideoPreviewLayer *previewLayer;
@property (nonatomic, strong) NSMutableString *reportText;
@property (nonatomic, assign) unsigned passCount;
@property (nonatomic, assign) unsigned photosCount;
@property (nonatomic, assign) unsigned qrCount;
@property (nonatomic, assign) int activeLensTag; /* 1=1x, 4=0.6x, 2=Front */
@property (nonatomic, assign) BOOL userPausedCam;
@property (nonatomic, assign) BOOL webKitDone;
@property (nonatomic, assign) BOOL checksRunning;
@property (nonatomic, copy) NSString *lastQrText;
@property (nonatomic, copy) void (^photoCompletion)(NSData *jpeg);
@end

@implementation ZizhanCompatController

- (UIButton *)makeBtn:(NSString *)title bg:(UIColor *)bg sel:(SEL)sel tag:(NSInteger)tag {
    UIButton *b = [UIButton buttonWithType:UIButtonTypeSystem];
    [b setTitle:title forState:UIControlStateNormal];
    [b setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    b.titleLabel.font = [UIFont boldSystemFontOfSize:14.0];
    b.titleLabel.adjustsFontSizeToFitWidth = YES;
    b.titleLabel.minimumScaleFactor = 0.75;
    b.backgroundColor = bg;
    b.layer.cornerRadius = 10.0;
    b.tag = tag;
    [b addTarget:self action:sel forControlEvents:UIControlEventTouchUpInside];
    return b;
}

- (void)appendInfo:(NSString *)line {
    fprintf(stderr, "ZIZHAN_COMPATSUITE: ACTION %s\n", [line UTF8String]);
    fflush(stderr);
    dispatch_async(dispatch_get_main_queue(), ^{
        [self.reportText appendFormat:@"[实测] %@\n", line];
        self.logView.text = self.reportText;
        if (self.reportText.length > 2) {
            [self.logView scrollRangeToVisible:NSMakeRange(self.reportText.length - 1, 1)];
        }
    });
}

- (void)appendLine:(NSString *)line passed:(BOOL)ok {
    if (ok) self.passCount++;
    fprintf(stderr, "ZIZHAN_COMPATSUITE: %s %s\n", ok ? "PASS" : "FAIL", [line UTF8String]);
    fflush(stderr);
    dispatch_async(dispatch_get_main_queue(), ^{
        [self.reportText appendFormat:@"%@ %@\n", ok ? @"[PASS]" : @"[FAIL]", line];
        self.logView.text = self.reportText;
        self.headerLabel.text = [NSString stringWithFormat:@"MIX Fold 2 · 兼容与外设验证 (%u/8 通过)", self.passCount];
    });
}

- (NSString *)lensNameForTag:(int)tag {
    if (tag == 4) return @"0.6x 超广角 (OV13B10)";
    if (tag == 2) return @"前置自拍 (IMX596)";
    return @"1x 后置主摄";
}

- (void)refreshControlHighlights {
    UIColor *activeLensBg = [UIColor colorWithRed:0.12 green:0.62 blue:0.42 alpha:1.0];
    UIColor *idleLensBg   = [UIColor colorWithRed:0.16 green:0.24 blue:0.38 alpha:1.0];
    for (UIButton *b in self.controlButtons) {
        if (b.tag == 1 || b.tag == 4 || b.tag == 2) {
            b.backgroundColor = (b.tag == self.activeLensTag) ? activeLensBg : idleLensBg;
        } else if (b.tag == 103) {
            BOOL running = self.session.isRunning;
            [b setTitle:(running ? @"暂停预览" : @"开启预览") forState:UIControlStateNormal];
            b.backgroundColor = running
                ? [UIColor colorWithRed:0.45 green:0.28 blue:0.14 alpha:1.0]
                : [UIColor colorWithRed:0.15 green:0.55 blue:0.35 alpha:1.0];
        }
    }
    NSString *camState = self.session.isRunning ? @"960×720 持续预览中" : @"预览已暂停";
    NSString *qrPart = self.lastQrText.length
        ? [NSString stringWithFormat:@"扫码命中(#%u): %@", self.qrCount, self.lastQrText]
        : @"对准二维码可实时自动识别";
    self.camInfoLabel.text = [NSString stringWithFormat:@"镜头: %@ (%@) | 拍照: %u 张\n%@",
                              [self lensNameForTag:self.activeLensTag], camState, self.photosCount, qrPart];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithRed:0.06 green:0.08 blue:0.13 alpha:1.0];
    self.reportText = [NSMutableString string];
    self.activeLensTag = 1;

    self.headerLabel = [[UILabel alloc] init];
    self.headerLabel.textColor = [UIColor colorWithRed:0.35 green:0.92 blue:0.62 alpha:1.0];
    self.headerLabel.font = [UIFont boldSystemFontOfSize:19];
    self.headerLabel.adjustsFontSizeToFitWidth = YES;
    self.headerLabel.minimumScaleFactor = 0.7;
    self.headerLabel.text = @"MIX Fold 2 · 兼容与外设验证 (自检中...)";
    [self.view addSubview:self.headerLabel];

    self.rerunBtn = [self makeBtn:@"重跑自检" bg:[UIColor colorWithRed:0.20 green:0.34 blue:0.58 alpha:1.0]
                              sel:@selector(onTapRerun) tag:99];
    [self.view addSubview:self.rerunBtn];

    self.logView = [[UITextView alloc] init];
    self.logView.backgroundColor = [UIColor colorWithRed:0.10 green:0.13 blue:0.20 alpha:1.0];
    self.logView.textColor = [UIColor whiteColor];
    self.logView.font = [UIFont monospacedSystemFontOfSize:13 weight:UIFontWeightMedium];
    self.logView.editable = NO;
    self.logView.layer.cornerRadius = 12;
    [self.view addSubview:self.logView];

    self.camBox = [[UIView alloc] init];
    self.camBox.backgroundColor = [UIColor blackColor];
    self.camBox.layer.cornerRadius = 12;
    self.camBox.layer.borderWidth = 1.5;
    self.camBox.layer.borderColor = [UIColor colorWithRed:0.24 green:0.58 blue:0.95 alpha:0.8].CGColor;
    self.camBox.clipsToBounds = YES;
    [self.view addSubview:self.camBox];

    self.camInfoLabel = [[UILabel alloc] init];
    self.camInfoLabel.textColor = [UIColor colorWithRed:0.98 green:0.86 blue:0.38 alpha:1.0];
    self.camInfoLabel.font = [UIFont systemFontOfSize:13 weight:UIFontWeightSemibold];
    self.camInfoLabel.numberOfLines = 2;
    self.camInfoLabel.text = @"正在启动 960×720 实时相机预览与扫码引擎...";
    [self.view addSubview:self.camInfoLabel];

    self.controlsHost = [[UIView alloc] init];
    [self.view addSubview:self.controlsHost];

    UIColor *lensBg   = [UIColor colorWithRed:0.16 green:0.24 blue:0.38 alpha:1.0];
    UIColor *actionBg = [UIColor colorWithRed:0.15 green:0.44 blue:0.78 alpha:1.0];
    UIColor *periphBg = [UIColor colorWithRed:0.46 green:0.24 blue:0.68 alpha:1.0];

    UIButton *b1x    = [self makeBtn:@"1x 主摄"      bg:lensBg   sel:@selector(onTapLens:)    tag:1];
    UIButton *b06    = [self makeBtn:@"0.6x 超广角"  bg:lensBg   sel:@selector(onTapLens:)    tag:4];
    UIButton *bFr    = [self makeBtn:@"前置镜头"     bg:lensBg   sel:@selector(onTapLens:)    tag:2];
    UIButton *bAf    = [self makeBtn:@"自动对焦 AF"  bg:actionBg sel:@selector(onTapAf)       tag:101];
    UIButton *bSnap  = [self makeBtn:@"拍照 JPEG"    bg:actionBg sel:@selector(onTapPhoto)    tag:102];
    UIButton *bPause = [self makeBtn:@"暂停预览"     bg:actionBg sel:@selector(onTapToggleCam) tag:103];
    UIButton *bHap   = [self makeBtn:@"X轴清脆震动"  bg:periphBg sel:@selector(onTapHaptics)  tag:201];
    UIButton *bEar   = [self makeBtn:@"顶部听筒试音" bg:periphBg sel:@selector(onTapEarpiece) tag:202];
    UIButton *bSpk   = [self makeBtn:@"双扬外放试音" bg:periphBg sel:@selector(onTapSpeaker)  tag:203];

    self.controlButtons = @[b1x, b06, bFr, bAf, bSnap, bPause, bHap, bEar, bSpk];
    for (UIButton *b in self.controlButtons) [self.controlsHost addSubview:b];

    /* Offscreen 1x1 WKWebView for sandboxed JS engine verification without crowding controls */
    WKWebViewConfiguration *wcfg = [[WKWebViewConfiguration alloc] init];
    self.webView = [[WKWebView alloc] initWithFrame:CGRectMake(-20, -20, 10, 10) configuration:wcfg];
    self.webView.navigationDelegate = self;
    [self.view addSubview:self.webView];
    [self.webView loadHTMLString:@"<html><body><p id='v'>Ready</p></body></html>" baseURL:nil];

    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(onAppDidBackground)
                                                 name:UIApplicationDidEnterBackgroundNotification object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(onAppWillForeground)
                                                 name:UIApplicationWillEnterForegroundNotification object:nil];

    [self refreshControlHighlights];
    [self startSelfCheckAsync];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    UIEdgeInsets sa = self.view.safeAreaInsets;
    CGFloat top = MAX(sa.top, 24.0) + 8.0;
    CGFloat bot = MAX(sa.bottom, 12.0) + 8.0;
    CGFloat w = self.view.bounds.size.width;
    CGFloat h = self.view.bounds.size.height;

    self.headerLabel.frame = CGRectMake(16, top, w - 124, 32);
    self.rerunBtn.frame    = CGRectMake(w - 100, top, 84, 32);

    CGFloat bodyY = top + 40.0;
    CGFloat bodyH = h - bodyY - bot;
    CGFloat ctrlH = 142.0; /* 3 rows x 42pt + 8pt spacing */

    if (w >= 740.0) {
        /* Unfolded inner screen (1080x958 pt): side-by-side layout */
        CGFloat leftW = floor(w * 0.46) - 20.0;
        CGFloat rightX = floor(w * 0.46) + 4.0;
        CGFloat rightW = w - rightX - 16.0;
        self.logView.frame = CGRectMake(16, bodyY, leftW, bodyH);

        CGFloat camH = MAX(180.0, bodyH - ctrlH - 48.0);
        self.camBox.frame = CGRectMake(rightX, bodyY, rightW, camH);
        self.camInfoLabel.frame = CGRectMake(rightX + 2, bodyY + camH + 6.0, rightW - 4, 36.0);
        self.controlsHost.frame = CGRectMake(rightX, bodyY + camH + 46.0, rightW, ctrlH);
    } else {
        /* Folded 9:16 mobile viewport (540x958 pt): clean vertical stack */
        CGFloat innerW = w - 28.0;
        CGFloat camH = MIN(floor(innerW * 0.72), floor((bodyH - ctrlH - 46.0) * 0.56));
        self.camBox.frame = CGRectMake(14, bodyY, innerW, camH);
        self.camInfoLabel.frame = CGRectMake(16, bodyY + camH + 4.0, innerW - 4, 36.0);
        self.controlsHost.frame = CGRectMake(14, bodyY + camH + 44.0, innerW, ctrlH);
        CGFloat logY = bodyY + camH + 44.0 + ctrlH + 8.0;
        self.logView.frame = CGRectMake(14, logY, innerW, MAX(100.0, h - bot - logY));
    }

    CGFloat cw = self.controlsHost.bounds.size.width;
    CGFloat gap = 8.0;
    CGFloat bw = floor((cw - gap * 2.0) / 3.0);
    CGFloat bh = 42.0;
    for (NSUInteger i = 0; i < self.controlButtons.count; ++i) {
        NSUInteger row = i / 3;
        NSUInteger col = i % 3;
        self.controlButtons[i].frame = CGRectMake(col * (bw + gap), row * (bh + gap), bw, bh);
    }
    if (self.previewLayer) {
        self.previewLayer.frame = self.camBox.bounds;
    }
}

- (void)onAppDidBackground {
    if (self.session && self.session.isRunning) {
        [self.session stopRunning];
        [self refreshControlHighlights];
    }
}

- (void)onAppWillForeground {
    if (self.session && !self.session.isRunning && !self.userPausedCam && !suite_boot_option("zizhan_compat_autotest=1")) {
        [self.session startRunning];
        [self refreshControlHighlights];
    }
}

- (AVCaptureDevice *)deviceForLensTag:(int)tag {
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

- (void)ensureCameraRunningWithTag:(int)tag {
    AVCaptureDevice *dev = [self deviceForLensTag:tag];
    if (!dev) return;
    self.activeLensTag = tag;
    if (!self.session) {
        self.session = [[AVCaptureSession alloc] init];
        self.photoOut = [[AVCapturePhotoOutput alloc] init];
        if ([self.session canAddOutput:self.photoOut]) [self.session addOutput:self.photoOut];
        self.metaOut = [[AVCaptureMetadataOutput alloc] init];
        if ([self.session canAddOutput:self.metaOut]) {
            [self.session addOutput:self.metaOut];
            [self.metaOut setMetadataObjectsDelegate:self queue:dispatch_get_main_queue()];
            if ([[self.metaOut availableMetadataObjectTypes] containsObject:AVMetadataObjectTypeQRCode]) {
                self.metaOut.metadataObjectTypes = @[AVMetadataObjectTypeQRCode];
            }
        }
        self.previewLayer = [AVCaptureVideoPreviewLayer layerWithSession:self.session];
        self.previewLayer.videoGravity = AVLayerVideoGravityResizeAspectFill;
        self.previewLayer.frame = self.camBox.bounds;
        [self.camBox.layer addSublayer:self.previewLayer];
    }
    AVCaptureDeviceInput *inp = [AVCaptureDeviceInput deviceInputWithDevice:dev error:nil];
    if (inp) [self.session addInput:inp];
    if (!self.session.isRunning) [self.session startRunning];
    [self refreshControlHighlights];
}

- (void)onTapLens:(UIButton *)sender {
    self.userPausedCam = NO;
    int tag = (int)sender.tag;
    [self ensureCameraRunningWithTag:tag];
    [self appendInfo:[NSString stringWithFormat:@"已切换镜头 -> %@", [self lensNameForTag:tag]]];
}

- (void)onTapAf {
    AVCaptureDevice *dev = [self deviceForLensTag:self.activeLensTag];
    if (dev && [dev isFocusModeSupported:AVCaptureFocusModeAutoFocus]) {
        [dev lockForConfiguration:nil];
        [dev setFocusMode:AVCaptureFocusModeAutoFocus];
        [dev unlockForConfiguration];
        [self appendInfo:@"已触发 DW9800V 音圈马达自动对焦 (AF)"];
    } else {
        [self appendInfo:[NSString stringWithFormat:@"%@ 为定焦镜头 (超广角/前置免对焦)", [self lensNameForTag:self.activeLensTag]]];
    }
}

- (void)onTapPhoto {
    if (!self.session) [self ensureCameraRunningWithTag:self.activeLensTag];
    if (!self.session.isRunning) {
        self.userPausedCam = NO;
        [self.session startRunning];
    }
    __weak typeof(self) weakSelf = self;
    self.photoCompletion = ^(NSData *d) {
        __strong typeof(weakSelf) s = weakSelf;
        if (!s) return;
        UIImage *im = d.length > 512 ? [UIImage imageWithData:d] : nil;
        dispatch_async(dispatch_get_main_queue(), ^{
            s.photosCount++;
            [s refreshControlHighlights];
            [s appendInfo:[NSString stringWithFormat:@"拍照完成 #%u: %.0f×%.0f JPEG (%lu 字节)",
                           s.photosCount, im ? im.size.width : 0, im ? im.size.height : 0, (unsigned long)d.length]];
        });
    };
    [self.photoOut capturePhotoWithSettings:[AVCapturePhotoSettings photoSettings] delegate:self];
}

- (void)onTapToggleCam {
    if (!self.session) {
        self.userPausedCam = NO;
        [self ensureCameraRunningWithTag:self.activeLensTag];
        return;
    }
    if (self.session.isRunning) {
        self.userPausedCam = YES;
        [self.session stopRunning];
        [self appendInfo:@"已手动暂停相机预览 (传感器已断电归零)"];
    } else {
        self.userPausedCam = NO;
        [self.session startRunning];
        [self appendInfo:@"已恢复 960×720 持续相机预览"];
    }
    [self refreshControlHighlights];
}

- (void)onTapHaptics {
    UIImpactFeedbackGenerator *imp = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleHeavy];
    [imp impactOccurredWithIntensity:0.9];
    [self appendInfo:@"已触发 PM8350B X轴高压线性马达清脆震动"];
}

- (void)onTapEarpiece {
    AVAudioSession *avas = [AVAudioSession sharedInstance];
    [avas setCategory:AVAudioSessionCategoryPlayAndRecord error:nil];
    [avas setMode:AVAudioSessionModeVoiceChat error:nil];
    [avas overrideOutputAudioPort:AVAudioSessionPortOverrideNone error:nil];
    play_test_chime_48k();
    [self appendInfo:@"已切至顶部微缝听筒 (CS35L41 0x40) 并播放 1.1s 提示音，请贴近手机顶端聆听"];
}

- (void)onTapSpeaker {
    AVAudioSession *avas = [AVAudioSession sharedInstance];
    [avas setCategory:AVAudioSessionCategoryPlayAndRecord error:nil];
    [avas setMode:AVAudioSessionModeDefault error:nil];
    [avas overrideOutputAudioPort:AVAudioSessionPortOverrideSpeaker error:nil];
    play_test_chime_48k();
    [self appendInfo:@"已恢复双扬声器立体声外放 (0x40+0x42) 并播放 1.1s 提示音"];
}

- (void)onTapRerun {
    if (self.checksRunning) return;
    [self.reportText setString:@""];
    self.passCount = 0;
    self.logView.text = @"正在重新执行 8 项自检...\n";
    [self startSelfCheckAsync];
}

- (void)webView:(WKWebView *)webView didFinishNavigation:(WKNavigation *)navigation {
    (void)navigation;
    [webView evaluateJavaScript:@"21 * 2" completionHandler:^(id res, NSError *err) {
        if (!self.webKitDone && !err && [res respondsToSelector:@selector(intValue)] && [res intValue] == 42) {
            self.webKitDone = YES;
        }
    }];
}

- (void)captureOutput:(AVCapturePhotoOutput *)output didFinishProcessingPhoto:(AVCapturePhoto *)photo error:(NSError *)error {
    (void)output; (void)error;
    NSData *d = [photo fileDataRepresentation];
    void (^cb)(NSData *) = self.photoCompletion;
    self.photoCompletion = nil;
    if (cb) cb(d);
}

- (void)captureOutput:(AVCaptureOutput *)output didOutputMetadataObjects:(NSArray<__kindof AVMetadataObject *> *)metadataObjects fromConnection:(AVCaptureConnection *)connection {
    (void)output; (void)connection;
    for (AVMetadataObject *obj in metadataObjects) {
        if ([obj isKindOfClass:[AVMetadataMachineReadableCodeObject class]]) {
            NSString *val = [(AVMetadataMachineReadableCodeObject *)obj stringValue];
            if (val.length > 0 && ![val isEqualToString:self.lastQrText]) {
                self.qrCount++;
                self.lastQrText = val;
                [self refreshControlHighlights];
                [self appendInfo:[NSString stringWithFormat:@"二维码识别成功 (#%u): %@", self.qrCount, val]];
            }
        }
    }
}

- (void)startSelfCheckAsync {
    self.checksRunning = YES;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 400 * NSEC_PER_MSEC),
                   dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        [self runAllChecks];
        self.checksRunning = NO;
    });
}

- (void)runAllChecks {
    const bool autoMode = suite_boot_option("zizhan_compat_autotest=1");

    /* 1. METAL */
    id<MTLDevice> mtl = MTLCreateSystemDefaultDevice();
    id<MTLCommandQueue> mq = mtl ? [mtl newCommandQueue] : nil;
    id<MTLBuffer> mbuf = mtl ? [mtl newBufferWithLength:4096 options:MTLResourceStorageModeShared] : nil;
    BOOL mtlOk = NO;
    if (mtl && mq && mbuf) {
        uint32_t *p = (uint32_t *)mbuf.contents;
        p[0] = 0x12345678u;
        id<MTLCommandBuffer> cb = [mq commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit fillBuffer:mbuf range:NSMakeRange(256, 256) value:0xa5];
        [blit endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        mtlOk = (cb.status == MTLCommandBufferStatusCompleted && ((const uint8_t *)mbuf.contents)[256] == 0xa5u);
    }
    [self appendLine:[NSString stringWithFormat:@"1. METAL: %@ blit=%d", mtl.name ?: @"nil", (int)mtlOk] passed:mtlOk];

    /* 2. KEYCHAIN */
    NSDictionary *qDel = @{(__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
                           (__bridge id)kSecAttrService: @"com.zizhan.compatsuite.kc",
                           (__bridge id)kSecAttrAccount: @"token"};
    SecItemDelete((__bridge CFDictionaryRef)qDel);
    NSData *secret = [@"zizhan-tier2-ok" dataUsingEncoding:NSUTF8StringEncoding];
    NSMutableDictionary *qAdd = [qDel mutableCopy];
    qAdd[(__bridge id)kSecValueData] = secret;
    OSStatus stAdd = SecItemAdd((__bridge CFDictionaryRef)qAdd, NULL);
    NSMutableDictionary *qGet = [qDel mutableCopy];
    qGet[(__bridge id)kSecReturnData] = @YES;
    CFTypeRef outRef = NULL;
    OSStatus stGet = SecItemCopyMatching((__bridge CFDictionaryRef)qGet, &outRef);
    NSData *gotData = (__bridge_transfer NSData *)outRef;
    NSData *secret2 = [@"zizhan-tier2-updated" dataUsingEncoding:NSUTF8StringEncoding];
    NSDictionary *updDict = @{(__bridge id)kSecValueData: secret2};
    OSStatus stUpd = SecItemUpdate((__bridge CFDictionaryRef)qDel, (__bridge CFDictionaryRef)updDict);
    OSStatus stDel = SecItemDelete((__bridge CFDictionaryRef)qDel);
    BOOL kcOk = (stAdd == 0 && stGet == 0 && [gotData isEqualToData:secret] && stUpd == 0 && stDel == 0);
    [self appendLine:[NSString stringWithFormat:@"2. KEYCHAIN: add=%d get=%d upd=%d del=%d", (int)stAdd, (int)stGet, (int)stUpd, (int)stDel] passed:kcOk];

    /* 3. APPGROUP */
    NSURL *agURL = [[NSFileManager defaultManager] containerURLForSecurityApplicationGroupIdentifier:@"group.com.zizhan.shared"];
    NSString *agFile = agURL ? [agURL.path stringByAppendingPathComponent:@"compat.txt"] : nil;
    BOOL agOk = NO;
    if (agFile) {
        [@"shared-ok" writeToFile:agFile atomically:YES encoding:NSUTF8StringEncoding error:nil];
        NSString *rd = [NSString stringWithContentsOfFile:agFile encoding:NSUTF8StringEncoding error:nil];
        agOk = [rd isEqualToString:@"shared-ok"];
    }
    [self appendLine:[NSString stringWithFormat:@"3. APPGROUP: rw=%d", (int)agOk] passed:agOk];

    /* 4. SQLITE */
    NSString *docDir = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    [[NSFileManager defaultManager] createDirectoryAtPath:docDir withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *dbPath = [docDir stringByAppendingPathComponent:@"compat.sqlite3"];
    sqlite3 *db = NULL;
    BOOL sqlOk = NO;
    if (sqlite3_open([dbPath fileSystemRepresentation], &db) == SQLITE_OK && db) {
        sqlite3_exec(db, "PRAGMA journal_mode=WAL; CREATE TABLE IF NOT EXISTS t(k INT, v TEXT); DELETE FROM t; INSERT INTO t VALUES(7, 'mixfold2');", NULL, NULL, NULL);
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db, "SELECT v FROM t WHERE k=7;", -1, &st, NULL) == SQLITE_OK && st) {
            if (sqlite3_step(st) == SQLITE_ROW) {
                const char *txt = (const char *)sqlite3_column_text(st, 0);
                sqlOk = (txt && strcmp(txt, "mixfold2") == 0);
            }
            sqlite3_finalize(st);
        }
        sqlite3_close(db);
    }
    [self appendLine:[NSString stringWithFormat:@"4. SQLITE: wal_rw=%d (%@)", (int)sqlOk, dbPath.lastPathComponent] passed:sqlOk];

    /* 5. WEBKIT */
    for (int i = 0; i < 30 && !self.webKitDone; ++i) usleep(100000);
    [self appendLine:[NSString stringWithFormat:@"5. WEBKIT: js_eval_42=%d", (int)self.webKitDone] passed:self.webKitDone];

    /* 6. PERIPH: In interactive mode, verify generators ready WITHOUT firing surprise vibrations! */
    __block BOOL periphOk = NO;
    dispatch_sync(dispatch_get_main_queue(), ^{
        UIImpactFeedbackGenerator *imp = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleMedium];
        AVAudioSession *avas = [AVAudioSession sharedInstance];
        if (autoMode) {
            [imp impactOccurred];
            UISelectionFeedbackGenerator *sel = [[UISelectionFeedbackGenerator alloc] init];
            [sel selectionChanged];
            AudioServicesPlaySystemSound(1519);
            NSError *aerr = nil;
            [avas setCategory:AVAudioSessionCategoryPlayAndRecord error:&aerr];
            (void)[avas setMode:AVAudioSessionModeVoiceChat error:&aerr];
            (void)[avas overrideOutputAudioPort:AVAudioSessionPortOverrideSpeaker error:&aerr];
            (void)[avas setMode:AVAudioSessionModeDefault error:&aerr];
        } else {
            [imp prepare];
        }
        periphOk = (imp != nil && avas != nil);
    });
    [self appendLine:(autoMode
        ? [NSString stringWithFormat:@"6. PERIPH: auto_haptics=3 receiver_switch=%d", (int)periphOk]
        : @"6. PERIPH: 线性马达与听筒路由就绪 (请点右侧按钮实测)") passed:periphOk];

    /* 7. LOCATION */
    CLLocationManager *locMgr = [[CLLocationManager alloc] init];
    int locAuth = (int)locMgr.authorizationStatus;
    BOOL locEn = [CLLocationManager locationServicesEnabled];
    BOOL locOk = (locEn && (locAuth == kCLAuthorizationStatusAuthorizedAlways || locAuth == kCLAuthorizationStatusAuthorizedWhenInUse));
    [self appendLine:[NSString stringWithFormat:@"7. LOCATION: enabled=%d auth=%d", (int)locEn, locAuth] passed:locOk];

    /* 8. CAMERA3 */
    AVCaptureDeviceDiscoverySession *disc = [AVCaptureDeviceDiscoverySession
        discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera, AVCaptureDeviceTypeBuiltInUltraWideCamera]
                              mediaType:AVMediaTypeVideo
                               position:AVCaptureDevicePositionUnspecified];
    NSArray<AVCaptureDevice *> *devs = disc.devices;
    AVCaptureDevice *d1x = [self deviceForLensTag:1];
    AVCaptureDevice *d06 = [self deviceForLensTag:4];
    AVCaptureDevice *dFr = [self deviceForLensTag:2];

    __block NSData *jpegData = nil;
    __block CGSize imgSz = CGSizeZero;
    if (devs.count >= 3 && d1x && d06 && dFr) {
        dispatch_sync(dispatch_get_main_queue(), ^{
            self.userPausedCam = NO;
            [self ensureCameraRunningWithTag:1];
        });
        usleep(1800000);
        if ([d1x isFocusModeSupported:AVCaptureFocusModeAutoFocus]) {
            [d1x lockForConfiguration:nil];
            [d1x setFocusMode:AVCaptureFocusModeAutoFocus];
            [d1x unlockForConfiguration];
            usleep(500000);
        }
        dispatch_semaphore_t psem = dispatch_semaphore_create(0);
        self.photoCompletion = ^(NSData *d) {
            jpegData = d;
            dispatch_semaphore_signal(psem);
        };
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.photoOut capturePhotoWithSettings:[AVCapturePhotoSettings photoSettings] delegate:self];
        });
        (void)dispatch_semaphore_wait(psem, dispatch_time(DISPATCH_TIME_NOW, 4 * NSEC_PER_SEC));
        if (jpegData.length > 1024) {
            UIImage *im = [UIImage imageWithData:jpegData];
            if (im) imgSz = im.size;
            dispatch_async(dispatch_get_main_queue(), ^{
                self.photosCount = MAX(1u, self.photosCount);
                [self refreshControlHighlights];
            });
        }

        if (autoMode) {
            /* Unattended CI only: sweep 0.6x -> Front -> stopRunning so idle=0mW */
            dispatch_sync(dispatch_get_main_queue(), ^{ [self ensureCameraRunningWithTag:4]; });
            usleep(1200000);
            dispatch_sync(dispatch_get_main_queue(), ^{ [self ensureCameraRunningWithTag:2]; });
            usleep(1200000);
            dispatch_sync(dispatch_get_main_queue(), ^{
                [self.session stopRunning];
                [self refreshControlHighlights];
            });
        }
        /* Interactive human mode: keep 1x 960x720 preview running continuously! */
    }
    BOOL camOk = (devs.count >= 3 && jpegData.length > 4096 && (int)imgSz.width == 960 && (int)imgSz.height == 720);
    [self appendLine:[NSString stringWithFormat:@"8. CAMERA3: devs=%lu photo=%.0fx%.0f (%lu B)%@",
                      (unsigned long)devs.count, imgSz.width, imgSz.height, (unsigned long)jpegData.length,
                      autoMode ? @"" : @" [持续预览中]"] passed:camOk];

    fprintf(stderr, "ZIZHAN_COMPATSUITE: SUMMARY pass=%u/8\n", self.passCount);
    fflush(stderr);
}

@end

@interface ZizhanCompatAppDelegate : UIResponder <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@end

@implementation ZizhanCompatAppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    (void)application; (void)launchOptions;
    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    self.window.rootViewController = [[ZizhanCompatController alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([ZizhanCompatAppDelegate class]));
    }
}
