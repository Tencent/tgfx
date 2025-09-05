#import "ViewController.h"
#import "TGFXView.h"

@interface ViewController () <UIGestureRecognizerDelegate>
@property(weak, nonatomic) IBOutlet TGFXView* tgfxView;
@property(nonatomic) int drawIndex;
@property(nonatomic) CGFloat zoomScale;
@property(nonatomic) CGPoint contentOffset;

@property(nonatomic) CGFloat currentZoom;
@property(nonatomic) CGPoint currentPanOffset;
@property(nonatomic) CGPoint currentPinchOffset;
@property(nonatomic) CGPoint pinchCenter;
@property(nonatomic) BOOL isTapEnabled;
@property(nonatomic, strong) CADisplayLink* displayLink;
@end

@implementation ViewController

static const float MinZoom = 0.001f;
static const float MaxZoom = 1000.0f;

- (void)viewDidLoad {
  [super viewDidLoad];
  self.tgfxView.contentScaleFactor = [UIScreen mainScreen].scale;
  self.zoomScale = 1.0f;
  self.contentOffset = CGPointZero;
  self.currentZoom = 1.0f;
  self.currentPanOffset = CGPointZero;
  self.currentPinchOffset = CGPointZero;
  self.pinchCenter = CGPointZero;
  self.isTapEnabled = true;

  UITapGestureRecognizer* tap =
      [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(tgfxViewClicked)];
  [self.tgfxView addGestureRecognizer:tap];

  UIPanGestureRecognizer* pan =
      [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handlePan:)];
  pan.delegate = self;
  [self.tgfxView addGestureRecognizer:pan];

  UIPinchGestureRecognizer* pinch =
      [[UIPinchGestureRecognizer alloc] initWithTarget:self action:@selector(handlePinch:)];
  pinch.delegate = self;
  [self.tgfxView addGestureRecognizer:pinch];

  self.displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(update:)];
  [self.displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];

  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(appDidEnterBackground:)
                                               name:UIApplicationDidEnterBackgroundNotification
                                             object:nil];
  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(appWillEnterForeground:)
                                               name:UIApplicationWillEnterForegroundNotification
                                             object:nil];
}

- (void)appDidEnterBackground:(NSNotification*)notification {
  [self.displayLink setPaused:YES];
}

- (void)appWillEnterForeground:(NSNotification*)notification {
  [self.displayLink setPaused:NO];
  [self requestDraw];
}

- (void)dealloc {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];
  [self requestDraw];
}

- (void)tgfxViewClicked {
  if (!self.isTapEnabled) {
    return;
  }
  self.drawIndex++;
  self.zoomScale = 1.0f;
  self.contentOffset = CGPointZero;
  self.currentZoom = 1.0f;
  self.currentPanOffset = CGPointZero;
  self.currentPinchOffset = CGPointZero;
  [self requestDraw];
}

- (void)handlePan:(UIPanGestureRecognizer*)gesture {
  CGPoint translation = [gesture translationInView:self.tgfxView];
  if (gesture.state == UIGestureRecognizerStateBegan) {
    self.currentPanOffset = translation;
    self.isTapEnabled = false;
  }
  if (gesture.state == UIGestureRecognizerStateEnded) {
    self.isTapEnabled = true;
    return;
  }
  self.contentOffset =
      CGPointMake(self.contentOffset.x +
                      (translation.x - self.currentPanOffset.x) * self.tgfxView.contentScaleFactor,
                  self.contentOffset.y +
                      (translation.y - self.currentPanOffset.y) * self.tgfxView.contentScaleFactor);
  self.currentPanOffset = translation;
  [self requestDraw];
}

- (void)handlePinch:(UIPinchGestureRecognizer*)gesture {
  self.isTapEnabled = false;

  CGPoint center = [gesture locationInView:self.tgfxView];
  center.x *= self.tgfxView.contentScaleFactor;
  center.y *= self.tgfxView.contentScaleFactor;

  if (gesture.state == UIGestureRecognizerStateBegan) {
    self.currentZoom = self.zoomScale;
    self.currentPinchOffset = self.contentOffset;
    self.pinchCenter = center;
  }
  if (gesture.state == UIGestureRecognizerStateEnded) {
    self.isTapEnabled = true;
    return;
  }
  if (gesture.numberOfTouches != 2) {
    return;
  }
  CGFloat scale = MAX(MinZoom, MIN(MaxZoom, self.currentZoom * gesture.scale));
  CGPoint offset;
  offset.x = (self.currentPinchOffset.x - self.pinchCenter.x) * scale / self.currentZoom + center.x;
  offset.y = (self.currentPinchOffset.y - self.pinchCenter.y) * scale / self.currentZoom + center.y;
  self.zoomScale = scale;
  self.contentOffset = offset;
  [self requestDraw];
}

- (BOOL)gestureRecognizer:(UIGestureRecognizer*)gestureRecognizer
    shouldRecognizeSimultaneouslyWithGestureRecognizer:
        (UIGestureRecognizer*)otherGestureRecognizer {
  return YES;
}

- (void)update:(CADisplayLink*)displayLink {
  if (![self.tgfxView draw:self.drawIndex zoom:self.zoomScale offset:self.contentOffset]) {
    [displayLink setPaused:YES];
  }
}

- (void)requestDraw {
  [self.tgfxView markDirty];
  [self.displayLink setPaused:NO];
}

@end
