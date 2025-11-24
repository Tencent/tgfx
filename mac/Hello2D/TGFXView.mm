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

#import "TGFXView.h"
#import <QuartzCore/CADisplayLink.h>
#include <cmath>
#include "hello2d/SampleBuilder.h"
#include "tgfx/core/Point.h"

static CVReturn OnDisplayLinkCallback(CVDisplayLinkRef, const CVTimeStamp*, const CVTimeStamp*,
                                      CVOptionFlags, CVOptionFlags*, void* userInfo) {
  TGFXView* view = (__bridge TGFXView*)userInfo;
  [view draw];
  return kCVReturnSuccess;
}

@implementation TGFXView {
  std::shared_ptr<tgfx::CGLWindow> tgfxWindow;
  std::unique_ptr<hello2d::AppHost> appHost;
}

- (BOOL)acceptsFirstResponder {
  return YES;
}

- (void)setBounds:(CGRect)bounds {
  CGRect oldBounds = self.bounds;
  [super setBounds:bounds];
  if (oldBounds.size.width != bounds.size.width || oldBounds.size.height != bounds.size.height) {
    [self updateSize];
  }
}

- (void)setFrame:(CGRect)frame {
  CGRect oldRect = self.frame;
  [super setFrame:frame];
  if (oldRect.size.width != frame.size.width || oldRect.size.height != frame.size.height) {
    [self updateSize];
  }
}

- (void)updateSize {
  CGSize size = [self convertSizeToBacking:self.bounds.size];
  auto width = static_cast<int>(roundf(size.width));
  auto height = static_cast<int>(roundf(size.height));
  if (appHost == nullptr) {
    appHost = std::make_unique<hello2d::AppHost>();
    NSString* imagePath = [[NSBundle mainBundle] pathForResource:@"bridge" ofType:@"jpg"];
    auto image = tgfx::Image::MakeFromFile(imagePath.UTF8String);
    appHost->addImage("bridge", image);
    imagePath = [[NSBundle mainBundle] pathForResource:@"tgfx" ofType:@"png"];
    image = tgfx::Image::MakeFromFile(imagePath.UTF8String);
    appHost->addImage("TGFX", image);
    auto typeface = tgfx::Typeface::MakeFromName("PingFang SC", "");
    appHost->addTypeface("default", typeface);
    typeface = tgfx::Typeface::MakeFromName("Apple Color Emoji", "");
    appHost->addTypeface("emoji", typeface);
  }
  auto contentScale = size.height / self.bounds.size.height;
  auto sizeChanged = appHost->updateScreen(width, height, contentScale);
  if (sizeChanged && tgfxWindow != nullptr) {
    tgfxWindow->invalidSize();
    [self draw];
  }
}

- (void)viewDidMoveToWindow {
  [super viewDidMoveToWindow];
  self.drawIndex = 0;
  self.zoomScale = 1.0f;
  self.contentOffset = CGPointZero;
  [self.window makeFirstResponder:self];

  if (@available(macOS 14, *)) {
    self.caDisplayLink = [self displayLinkWithTarget:self selector:@selector(draw)];
  } else {
    CVDisplayLinkCreateWithActiveCGDisplays(&_cvDisplayLink);
    CVDisplayLinkSetOutputCallback(_cvDisplayLink, &OnDisplayLinkCallback, (__bridge void*)self);
  }

  [self updateSize];
}

- (void)startDisplayLink {
  if (@available(macOS 14, *)) {
    if (self.caDisplayLink) {
      [self.caDisplayLink addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSRunLoopCommonModes];
    }
  } else {
    if (self.cvDisplayLink) {
      CVDisplayLinkStart(self.cvDisplayLink);
    }
  }
}

- (void)stopDisplayLink {
  if (@available(macOS 14, *)) {
    if (self.caDisplayLink) {
      [self.caDisplayLink removeFromRunLoop:[NSRunLoop currentRunLoop]
                                    forMode:NSRunLoopCommonModes];
    }
  } else {
    if (self.cvDisplayLink) {
      CVDisplayLinkStop(self.cvDisplayLink);
    }
  }
}

- (void)dealloc {
  if (@available(macOS 14, *)) {
    if (self.caDisplayLink) {
      [self.caDisplayLink invalidate];
      self.caDisplayLink = nil;
    }
  } else {
    if (self.cvDisplayLink) {
      CVDisplayLinkStop(self.cvDisplayLink);
      CVDisplayLinkRelease(self.cvDisplayLink);
    }
  }
}
- (void)markDirty {
  appHost->markDirty();
}

- (BOOL)draw {
  if (!appHost->isDirty()) {
    return false;
  }
  appHost->resetDirty();
  if (self.window == nil) {
    return false;
  }
  if (appHost->width() <= 0 || appHost->height() <= 0) {
    return false;
  }
  if (tgfxWindow == nullptr) {
    tgfxWindow = tgfx::CGLWindow::MakeFrom(self);
  }
  if (tgfxWindow == nullptr) {
    return false;
  }
  auto device = tgfxWindow->getDevice();
  auto context = device->lockContext();
  if (context == nullptr) {
    return false;
  }
  auto surface = tgfxWindow->getSurface(context);
  if (surface == nullptr) {
    device->unlock();
    return false;
  }

  appHost->updateZoomAndOffset(self.zoomScale,
                               tgfx::Point(static_cast<float>(self.contentOffset.x),
                                           static_cast<float>(self.contentOffset.y)));
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto numBuilders = hello2d::GetSampleCount();
  auto index = (self.drawIndex % numBuilders);
  appHost->draw(canvas, index, true);
  context->flushAndSubmit();
  tgfxWindow->present(context);
  device->unlock();

  return true;
}

@end
