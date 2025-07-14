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
#import "TGFXView.h"
#import <CoreVideo/CoreVideo.h>
@interface ViewController ()
@property(nonatomic) CVDisplayLinkRef displayLink;
@property(strong, nonatomic) TGFXView* tgfxView;
@property(nonatomic) int drawCount;
@property(nonatomic) float zoomScale;
@property(nonatomic) CGPoint contentOffset;
- (void)updateContentView;
@end

static CVReturn OnAnimationCallback(CVDisplayLinkRef displayLink, const CVTimeStamp* now,
                                      const CVTimeStamp* outputTime, CVOptionFlags flagsIn,
                                      CVOptionFlags* flagsOut, void* displayLinkContext) {
  ViewController* controller = (__bridge ViewController*)displayLinkContext;
  [controller performSelectorOnMainThread:@selector(updateContentView)
                               withObject:nil
                            waitUntilDone:NO];
  return kCVReturnSuccess;
}

@implementation ViewController

static const float MinZoom = 0.001f;
static const float MaxZoom = 1000.0f;
// Refs https://github.com/microsoft/vscode/blob/main/src/vs/base/browser/mouseEvent.ts
static const float ScrollWheelZoomSensitivity = 120.0f;
- (void)setupDisplayLink {
  CVDisplayLinkCreateWithActiveCGDisplays(&_displayLink);
  CVDisplayLinkSetOutputCallback(_displayLink, &OnAnimationCallback, (__bridge void*)self);
  CVDisplayLinkStart(_displayLink);
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.tgfxView = [[TGFXView alloc] initWithFrame:self.view.bounds];
  [self.tgfxView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [self.view addSubview:self.tgfxView];
  self.zoomScale = 1.0f;
  self.contentOffset = CGPointZero;
  [self setupDisplayLink];
}

- (void)updateContentView {
  [self.tgfxView draw:self.drawCount zoom:self.zoomScale offset:self.contentOffset];
}

- (void)dealloc {
  if (_displayLink) {
    CVDisplayLinkStop(_displayLink);
    CVDisplayLinkRelease(_displayLink);
    _displayLink = NULL;
  }
}

- (void)mouseUp:(NSEvent*)event {
  self.drawCount++;
  self.zoomScale = 1.0f;
  self.contentOffset = CGPointZero;
}

- (void)scrollWheel:(NSEvent*)event {
  BOOL isCtrl = (event.modifierFlags & NSEventModifierFlagControl) != 0;
  BOOL isCmd = (event.modifierFlags & NSEventModifierFlagCommand) != 0;
  if (isCtrl || isCmd) {
    NSPoint mouseInView = [self.tgfxView convertPoint:[event locationInWindow] fromView:nil];
    mouseInView.y = self.tgfxView.bounds.size.height - mouseInView.y;
    mouseInView = [self.tgfxView convertPointToBacking:mouseInView];
    float contentX = (mouseInView.x - self.contentOffset.x) / self.zoomScale;
    float contentY = (mouseInView.y - self.contentOffset.y) / self.zoomScale;
    if (event.hasPreciseScrollingDeltas) {
      self.zoomScale = self.zoomScale * (1 + event.scrollingDeltaY / ScrollWheelZoomSensitivity);
    } else {
      self.zoomScale = self.zoomScale * std::pow(1.1, event.scrollingDeltaY);
    }
    if (self.zoomScale < MinZoom) {
      self.zoomScale = MinZoom;
    }
    if (self.zoomScale > MaxZoom) {
      self.zoomScale = MaxZoom;
    }
    self.contentOffset = CGPointMake(mouseInView.x - contentX * self.zoomScale,
                                     mouseInView.y - contentY * self.zoomScale);
  } else {
    if (event.hasPreciseScrollingDeltas) {
      self.contentOffset = CGPointMake(self.contentOffset.x + event.scrollingDeltaX,
                                       self.contentOffset.y + event.scrollingDeltaY);
    } else {
      self.contentOffset = CGPointMake(self.contentOffset.x + event.scrollingDeltaX * 5,
                                       self.contentOffset.y + event.scrollingDeltaY * 5);
    }
  }
}

- (void)magnifyWithEvent:(NSEvent*)event {
  NSPoint mouseInView = [self.tgfxView convertPoint:[event locationInWindow] fromView:nil];
  mouseInView.y = self.tgfxView.bounds.size.height - mouseInView.y;
  mouseInView = [self.tgfxView convertPointToBacking:mouseInView];
  float contentX = (mouseInView.x - self.contentOffset.x) / self.zoomScale;
  float contentY = (mouseInView.y - self.contentOffset.y) / self.zoomScale;
  self.zoomScale = self.zoomScale * (1.0 + event.magnification);
  if (self.zoomScale < MinZoom) {
    self.zoomScale = MinZoom;
  }
  if (self.zoomScale > MaxZoom) {
    self.zoomScale = MaxZoom;
  }
  self.contentOffset = CGPointMake(mouseInView.x - contentX * self.zoomScale,
                                   mouseInView.y - contentY * self.zoomScale);
}

@end
