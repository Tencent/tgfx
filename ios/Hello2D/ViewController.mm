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

  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.3 * NSEC_PER_SEC)),
                 dispatch_get_main_queue(), ^{
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
  CGPoint translation = [gesture translationInView:self.tgfxView];
  self.contentOffset = CGPointMake(lastOffset.x + translation.x,
                                   lastOffset.y + translation.y);
  [self.tgfxView draw:self.drawCount zoom:self.zoomScale offset:self.contentOffset];
}

- (void)handlePinch:(UIPinchGestureRecognizer*)gesture {
    CGFloat oldZoom = self.zoomScale;
    CGFloat newZoom = oldZoom * gesture.scale;
    if (newZoom < 0.001f) {
        newZoom = 0.001f;
    }
    if (newZoom > 1000.0f) {
        newZoom = 1000.0f;
    }
  CGPoint pinchCenter = [gesture locationInView:self.tgfxView];
  CGFloat contentX = (pinchCenter.x - self.contentOffset.x) / oldZoom;
  CGFloat contentY = (pinchCenter.y - self.contentOffset.y) / oldZoom;
    self.contentOffset = CGPointMake(pinchCenter.x - contentX * newZoom,
                                  pinchCenter.y - contentY * newZoom);
  self.zoomScale = newZoom;
  [self.tgfxView draw:self.drawCount zoom:self.zoomScale offset:self.contentOffset];
}

@end
