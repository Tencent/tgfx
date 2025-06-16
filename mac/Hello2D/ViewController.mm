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
@property(strong, nonatomic) TGFXView* tgfxView;
@property(nonatomic) int drawCount;
@property(nonatomic) float zoomScale;
@property(nonatomic) CGPoint contentOffset;
@end

@implementation ViewController

- (void)viewDidLoad {
  [super viewDidLoad];
  self.tgfxView = [[TGFXView alloc] initWithFrame:self.view.bounds];
  [self.tgfxView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [self.view addSubview:self.tgfxView];
  self.zoomScale = 1.0f;
  self.contentOffset = CGPointZero;
  [self.tgfxView draw:self.drawCount zoom:self.zoomScale offset:self.contentOffset];
}

- (void)viewDidLayout {
  [super viewDidLayout];
  [self.tgfxView draw:self.drawCount zoom:self.zoomScale offset:self.contentOffset];
}

- (void)mouseUp:(NSEvent*)event {
  self.drawCount++;
  self.zoomScale = 1.0f;
  self.contentOffset = CGPointZero;
  [self.tgfxView draw:self.drawCount zoom:self.zoomScale offset:self.contentOffset];
}

- (void)scrollWheel:(NSEvent*)event {
  BOOL isCtrl = (event.modifierFlags & NSEventModifierFlagControl) != 0;
  BOOL isCmd = (event.modifierFlags & NSEventModifierFlagCommand) != 0;
  if (isCtrl || isCmd) {
    float oldZoom = self.zoomScale;
    float zoomStep = 1.0 + event.scrollingDeltaY * 0.05;
    float newZoom = oldZoom * zoomStep;
      if (newZoom < 0.001f) {
          newZoom = 0.001f;
      }
      if (newZoom > 1000.0f) {
          newZoom = 1000.0f;
      }
    NSPoint mouseInView = [self.tgfxView convertPoint:[event locationInWindow] fromView:nil];
    mouseInView.y = self.tgfxView.bounds.size.height - mouseInView.y;
    mouseInView = [self.tgfxView convertPointToBacking:mouseInView];
    float contentX = (mouseInView.x - self.contentOffset.x) / oldZoom;
    float contentY = (mouseInView.y - self.contentOffset.y) / oldZoom;
    self.contentOffset =
        CGPointMake(mouseInView.x - contentX * newZoom, mouseInView.y - contentY * newZoom);
    self.zoomScale = newZoom;
    [self.tgfxView draw:self.drawCount zoom:self.zoomScale offset:self.contentOffset];
  } else {
    self.contentOffset = CGPointMake(self.contentOffset.x + event.scrollingDeltaX,
                                     self.contentOffset.y + event.scrollingDeltaY);
    [self.tgfxView draw:self.drawCount zoom:self.zoomScale offset:self.contentOffset];
  }
}

- (void)magnifyWithEvent:(NSEvent*)event {
  float oldZoom = self.zoomScale;
  float newZoom = oldZoom * (1.0 + event.magnification);
  if (self.zoomScale < 0.001f) {
    self.zoomScale = 0.001f;
  }
  if (self.zoomScale > 1000.0f) {
    self.zoomScale = 1000.0f;
  }
  NSPoint mouseInView = [self.tgfxView convertPoint:[event locationInWindow] fromView:nil];
  mouseInView.y = self.tgfxView.bounds.size.height - mouseInView.y;
  mouseInView = [self.tgfxView convertPointToBacking:mouseInView];
  float contentX = (mouseInView.x - self.contentOffset.x) / oldZoom;
  float contentY = (mouseInView.y - self.contentOffset.y) / oldZoom;
  self.contentOffset =
      CGPointMake(mouseInView.x - contentX * newZoom, mouseInView.y - contentY * newZoom);
  self.zoomScale = newZoom;
  [self.tgfxView draw:self.drawCount zoom:self.zoomScale offset:self.contentOffset];
}

@end
