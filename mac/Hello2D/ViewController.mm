/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
#import <CoreVideo/CoreVideo.h>
#import "TGFXView.h"
@interface ViewController ()
@property(strong, nonatomic) TGFXView* tgfxView;
@end

@implementation ViewController

static const float MinZoom = 0.001f;
static const float MaxZoom = 1000.0f;
// Refs https://github.com/microsoft/vscode/blob/main/src/vs/base/browser/mouseEvent.ts
static const float ScrollWheelZoomSensitivity = 120.0f;

- (void)viewDidLoad {
  [super viewDidLoad];
  self.tgfxView = [[TGFXView alloc] initWithFrame:self.view.bounds];
  [self.tgfxView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [self.view addSubview:self.tgfxView];

  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(appDidEnterBackground:)
                                               name:NSApplicationDidResignActiveNotification
                                             object:nil];
  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(appWillEnterForeground:)
                                               name:NSApplicationWillBecomeActiveNotification
                                             object:nil];
}

- (void)viewDidAppear {
  [super viewDidAppear];
  [self requestDraw];
}

- (void)appDidEnterBackground:(NSNotification*)notification {
  [self.tgfxView stopDisplayLink];
}

- (void)appWillEnterForeground:(NSNotification*)notification {
  [self requestDraw];
}

- (void)dealloc {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)mouseUp:(NSEvent*)event {
  self.tgfxView.drawIndex++;
  self.tgfxView.zoomScale = 1.0f;
  self.tgfxView.contentOffset = CGPointZero;
  [self requestDraw];
}

- (void)scrollWheel:(NSEvent*)event {
  BOOL isCtrl = (event.modifierFlags & NSEventModifierFlagControl) != 0;
  BOOL isCmd = (event.modifierFlags & NSEventModifierFlagCommand) != 0;
  if (isCtrl || isCmd) {
    NSPoint mouseInView = [self.tgfxView convertPoint:[event locationInWindow] fromView:nil];
    mouseInView.y = self.tgfxView.bounds.size.height - mouseInView.y;
    mouseInView = [self.tgfxView convertPointToBacking:mouseInView];
    float contentX = (mouseInView.x - self.tgfxView.contentOffset.x) / self.tgfxView.zoomScale;
    float contentY = (mouseInView.y - self.tgfxView.contentOffset.y) / self.tgfxView.zoomScale;
    if (event.hasPreciseScrollingDeltas) {
      self.tgfxView.zoomScale =
          self.tgfxView.zoomScale * (1 + event.scrollingDeltaY / ScrollWheelZoomSensitivity);
    } else {
      self.tgfxView.zoomScale = self.tgfxView.zoomScale * std::pow(1.1, event.scrollingDeltaY);
    }
    if (self.tgfxView.zoomScale < MinZoom) {
      self.tgfxView.zoomScale = MinZoom;
    }
    if (self.tgfxView.zoomScale > MaxZoom) {
      self.tgfxView.zoomScale = MaxZoom;
    }
    self.tgfxView.contentOffset = CGPointMake(mouseInView.x - contentX * self.tgfxView.zoomScale,
                                              mouseInView.y - contentY * self.tgfxView.zoomScale);
  } else {
    if (event.hasPreciseScrollingDeltas) {
      self.tgfxView.contentOffset =
          CGPointMake(self.tgfxView.contentOffset.x + event.scrollingDeltaX,
                      self.tgfxView.contentOffset.y + event.scrollingDeltaY);
    } else {
      self.tgfxView.contentOffset =
          CGPointMake(self.tgfxView.contentOffset.x + event.scrollingDeltaX * 5,
                      self.tgfxView.contentOffset.y + event.scrollingDeltaY * 5);
    }
  }
  [self requestDraw];
}

- (void)magnifyWithEvent:(NSEvent*)event {
  NSPoint mouseInView = [self.tgfxView convertPoint:[event locationInWindow] fromView:nil];
  mouseInView.y = self.tgfxView.bounds.size.height - mouseInView.y;
  mouseInView = [self.tgfxView convertPointToBacking:mouseInView];
  float contentX = (mouseInView.x - self.tgfxView.contentOffset.x) / self.tgfxView.zoomScale;
  float contentY = (mouseInView.y - self.tgfxView.contentOffset.y) / self.tgfxView.zoomScale;
  self.tgfxView.zoomScale = self.tgfxView.zoomScale * (1.0 + event.magnification);
  if (self.tgfxView.zoomScale < MinZoom) {
    self.tgfxView.zoomScale = MinZoom;
  }
  if (self.tgfxView.zoomScale > MaxZoom) {
    self.tgfxView.zoomScale = MaxZoom;
  }
  self.tgfxView.contentOffset = CGPointMake(mouseInView.x - contentX * self.tgfxView.zoomScale,
                                            mouseInView.y - contentY * self.tgfxView.zoomScale);

  [self requestDraw];
}

- (void)requestDraw {
  [self.tgfxView startDisplayLink];
}

@end
