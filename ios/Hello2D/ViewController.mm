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
@property(nonatomic) CGFloat currentZoom;
@property(nonatomic) CGPoint currentOffset;
@property(nonatomic) CGPoint pinchCenter;
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
  self.currentOffset = CGPointZero;
  self.pinchCenter = CGPointZero;

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
  if (gesture.state == UIGestureRecognizerStateBegan) {
    self.currentOffset = self.contentOffset;
  }
  CGPoint translation = [gesture translationInView:self.tgfxView];
  self.contentOffset =
      CGPointMake(self.currentOffset.x + (translation.x * self.tgfxView.contentScaleFactor),
                  self.currentOffset.y + (translation.y * self.tgfxView.contentScaleFactor));
  [self.tgfxView draw:self.drawCount zoom:self.zoomScale offset:self.contentOffset];
}

- (void)handlePinch:(UIPinchGestureRecognizer*)gesture {
  if (gesture.numberOfTouches < 2) {
    return;
  }
  if (gesture.state == UIGestureRecognizerStateBegan) {
    self.currentZoom = self.zoomScale;
    self.currentOffset = self.contentOffset;
    CGPoint location = [gesture locationInView:self.tgfxView];
    self.pinchCenter = CGPointMake(location.x * self.tgfxView.contentScaleFactor,
                                   location.y * self.tgfxView.contentScaleFactor);
  }
  if (gesture.state == UIGestureRecognizerStateChanged) {
    self.zoomScale = MAX(MinZoom, MIN(MaxZoom, self.currentZoom * gesture.scale));
    self.contentOffset = CGPointMake(
        (self.currentOffset.x - self.pinchCenter.x) * self.zoomScale / self.currentZoom +
            self.pinchCenter.x,
        (self.currentOffset.y - self.pinchCenter.y) * self.zoomScale / self.currentZoom +
            self.pinchCenter.y);
    [self.tgfxView draw:self.drawCount zoom:self.zoomScale offset:self.contentOffset];
  }
}

@end
