/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#import "ViewController.h"
#import "TGFXView.h"

@interface ViewController ()
@property(weak, nonatomic) IBOutlet TGFXView* tgfxView;
@property(nonatomic) int drawCount;
@property(nonatomic) CGFloat zoomScale;
@property(nonatomic) CGPoint contentOffset;
@end

@implementation ViewController

- (void)viewDidLoad {
  [super viewDidLoad];
  self.tgfxView.contentScaleFactor = [UIScreen mainScreen].scale;
  self.zoomScale = 1.0f;
  self.contentOffset = CGPointZero;

  UITapGestureRecognizer* tap =
      [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(tgfxViewClicked)];
  [self.tgfxView addGestureRecognizer:tap];

  UIPanGestureRecognizer* pan =
      [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handlePan:)];
  [self.tgfxView addGestureRecognizer:pan];

  UIPinchGestureRecognizer* pinch =
      [[UIPinchGestureRecognizer alloc] initWithTarget:self action:@selector(handlePinch:)];
  [self.tgfxView addGestureRecognizer:pinch];

  dispatch_after(
      dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [self.tgfxView draw:self.drawCount zoom:self.zoomScale offset:self.contentOffset];
      });
}

- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];
  [self.tgfxView draw:self.drawCount zoom:self.zoomScale offset:self.contentOffset];
}

- (void)tgfxViewClicked {
  self.drawCount++;
  self.zoomScale = 1.0f;
  self.contentOffset = CGPointZero;
  [self.tgfxView draw:self.drawCount zoom:self.zoomScale offset:self.contentOffset];
}

- (void)handlePan:(UIPanGestureRecognizer*)gesture {
  static CGPoint lastOffset;
  if (gesture.state == UIGestureRecognizerStateBegan) {
    lastOffset = self.contentOffset;
  }
  CGFloat scale = self.tgfxView.contentScaleFactor;
  ;
  CGPoint translation = [gesture translationInView:self.tgfxView];
  self.contentOffset =
      CGPointMake(lastOffset.x + (translation.x * scale), lastOffset.y + (translation.y * scale));
  [self.tgfxView draw:self.drawCount zoom:self.zoomScale offset:self.contentOffset];
}

- (void)handlePinch:(UIPinchGestureRecognizer*)gesture {
  if (gesture.numberOfTouches < 2) return;

  static CGFloat beginZoom = 1.0f;
  static CGPoint beginOffset = {0, 0};
  static CGPoint pinchCenter = {0, 0};

  if (gesture.state == UIGestureRecognizerStateBegan) {
    beginZoom = self.zoomScale;
    beginOffset = self.contentOffset;
    CGPoint loc = [gesture locationInView:self.tgfxView];
    CGFloat scale = self.tgfxView.contentScaleFactor;
    pinchCenter = CGPointMake(loc.x * scale, loc.y * scale);
  }

  if (gesture.state == UIGestureRecognizerStateChanged) {
    CGFloat zoomNew = beginZoom * gesture.scale;
    zoomNew = MAX(0.001f, MIN(1000.0f, zoomNew));
    CGFloat scaleChange = zoomNew / beginZoom;
    CGPoint offsetNew;
    offsetNew.x = (beginOffset.x - pinchCenter.x) * scaleChange + pinchCenter.x;
    offsetNew.y = (beginOffset.y - pinchCenter.y) * scaleChange + pinchCenter.y;
    self.zoomScale = zoomNew;
    self.contentOffset = offsetNew;
    [self.tgfxView draw:self.drawCount zoom:self.zoomScale offset:self.contentOffset];
  }
}

@end
