/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include "hello2d/LayerBuilder.h"
#include "tgfx/core/Point.h"
#include "tgfx/gpu/Recording.h"

static CVReturn OnDisplayLinkCallback(CVDisplayLinkRef, const CVTimeStamp*, const CVTimeStamp*,
                                      CVOptionFlags, CVOptionFlags*, void* userInfo) {
  TGFXView* view = (__bridge TGFXView*)userInfo;
  [view draw];
  return kCVReturnSuccess;
}

@implementation TGFXView {
  std::shared_ptr<tgfx::CGLWindow> tgfxWindow;
  std::unique_ptr<hello2d::AppHost> appHost;
  tgfx::DisplayList displayList;
  std::shared_ptr<tgfx::Layer> contentLayer;
  int lastDrawIndex;
  std::unique_ptr<tgfx::Recording> lastRecording;
  int lastSurfaceWidth;
  int lastSurfaceHeight;
  bool presentImmediately;
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
    lastDrawIndex = -1;
    lastSurfaceWidth = 0;
    lastSurfaceHeight = 0;
    presentImmediately = true;
    displayList.setRenderMode(tgfx::RenderMode::Tiled);
    displayList.setAllowZoomBlur(true);
    displayList.setMaxTileCount(512);
  }
  CGSize backingSize = [self convertSizeToBacking:self.bounds.size];
  lastSurfaceWidth = static_cast<int>(backingSize.width);
  lastSurfaceHeight = static_cast<int>(backingSize.height);
  [self applyCenteringTransform];
  if (tgfxWindow != nullptr) {
    tgfxWindow->invalidSize();
    presentImmediately = true;
  }
}

- (void)viewDidMoveToWindow {
  [super viewDidMoveToWindow];

  if (self.window == nil) {
    if (@available(macOS 14, *)) {
      if (self.caDisplayLink) {
        self.caDisplayLink.paused = YES;
        [self.caDisplayLink invalidate];
        self.caDisplayLink = nil;
      }
    } else {
      if (self.cvDisplayLink) {
        CVDisplayLinkStop(self.cvDisplayLink);
        CVDisplayLinkRelease(self.cvDisplayLink);
        _cvDisplayLink = NULL;
      }
    }
    return;
  }

  self.drawIndex = 0;
  self.zoomScale = 1.0f;
  self.contentOffset = CGPointZero;
  [self.window makeFirstResponder:self];

  if (@available(macOS 14, *)) {
    self.caDisplayLink = [self displayLinkWithTarget:self selector:@selector(displayLinkCallback:)];
    [self.caDisplayLink addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSRunLoopCommonModes];
  } else {
    CVDisplayLinkCreateWithActiveCGDisplays(&_cvDisplayLink);
    CVDisplayLinkSetOutputCallback(_cvDisplayLink, &OnDisplayLinkCallback, (__bridge void*)self);
    CVDisplayLinkStart(self.cvDisplayLink);
  }

  [self updateSize];
  [self updateZoomScaleAndOffset];
  [self updateLayerTree];
}

- (void)dealloc {
  if (@available(macOS 14, *)) {
    if (self.caDisplayLink) {
      self.caDisplayLink.paused = YES;
      [self.caDisplayLink invalidate];
      self.caDisplayLink = nil;
    }
  } else {
    if (self.cvDisplayLink) {
      CVDisplayLinkStop(self.cvDisplayLink);
      CVDisplayLinkRelease(self.cvDisplayLink);
      _cvDisplayLink = NULL;
    }
  }
}

- (void)displayLinkCallback:(CADisplayLink*)displayLink {
  [self draw];
}

- (void)updateLayerTree {
  auto numBuilders = hello2d::LayerBuilder::Count();
  auto index = (self.drawIndex % numBuilders);
  if (index != lastDrawIndex || !contentLayer) {
    auto builder = hello2d::LayerBuilder::GetByIndex(index);
    if (builder) {
      contentLayer = builder->buildLayerTree(appHost.get());
      if (contentLayer) {
        displayList.root()->removeChildren();
        displayList.root()->addChild(contentLayer);
        [self applyCenteringTransform];
      }
    }
    lastDrawIndex = index;
  }
}

- (void)applyCenteringTransform {
  if (lastSurfaceWidth > 0 && lastSurfaceHeight > 0 && contentLayer) {
    hello2d::LayerBuilder::ApplyCenteringTransform(contentLayer,
                                                   static_cast<float>(lastSurfaceWidth),
                                                   static_cast<float>(lastSurfaceHeight));
  }
}

- (void)updateZoomScaleAndOffset {
  displayList.setZoomScale(self.zoomScale);
  displayList.setContentOffset(static_cast<float>(self.contentOffset.x),
                               static_cast<float>(self.contentOffset.y));
}

- (void)draw {
  if (self.window == nil) {
    return;
  }
  if (tgfxWindow == nullptr) {
    tgfxWindow = tgfx::CGLWindow::MakeFrom(self);
  }
  if (tgfxWindow == nullptr) {
    return;
  }

  if (!displayList.hasContentChanged() && lastRecording == nullptr) {
    return;
  }

  auto device = tgfxWindow->getDevice();
  auto context = device->lockContext();
  if (context == nullptr) {
    return;
  }

  auto surface = tgfxWindow->getSurface(context);
  if (surface == nullptr) {
    device->unlock();
    return;
  }

  auto canvas = surface->getCanvas();
  canvas->clear();
  hello2d::DrawBackground(canvas, surface->width(), surface->height(), self.layer.contentsScale);

  displayList.render(surface.get(), false);

  auto recording = context->flush();

  if (presentImmediately) {
    presentImmediately = false;
    if (recording) {
      context->submit(std::move(recording));
      tgfxWindow->present(context);
    }
  } else {
    std::swap(lastRecording, recording);

    if (recording) {
      context->submit(std::move(recording));
      tgfxWindow->present(context);
    }
  }

  device->unlock();
}

- (void)startDisplayLink {
  if (@available(macOS 14, *)) {
    if (self.caDisplayLink) {
      self.caDisplayLink.paused = NO;
    }
  } else {
    if (self.cvDisplayLink && !CVDisplayLinkIsRunning(self.cvDisplayLink)) {
      CVDisplayLinkStart(self.cvDisplayLink);
    }
  }
}

- (void)stopDisplayLink {
  if (@available(macOS 14, *)) {
    if (self.caDisplayLink) {
      self.caDisplayLink.paused = YES;
    }
  } else {
    if (self.cvDisplayLink && CVDisplayLinkIsRunning(self.cvDisplayLink)) {
      CVDisplayLinkStop(self.cvDisplayLink);
    }
  }
}

- (void)mouseDown:(NSEvent*)event {
  self.drawIndex++;
  self.zoomScale = 1.0f;
  self.contentOffset = CGPointZero;
  [self updateZoomScaleAndOffset];
  [self updateLayerTree];
}

- (void)scrollWheel:(NSEvent*)event {
  BOOL isCommandPressed = (event.modifierFlags & NSEventModifierFlagCommand) != 0;

  if (isCommandPressed) {
    CGPoint mousePoint = [self convertPoint:event.locationInWindow fromView:nil];
    mousePoint.y = self.bounds.size.height - mousePoint.y;
    CGPoint backingPoint = [self convertPointToBacking:mousePoint];
    float contentX = (backingPoint.x - self.contentOffset.x) / self.zoomScale;
    float contentY = (backingPoint.y - self.contentOffset.y) / self.zoomScale;
    if (event.hasPreciseScrollingDeltas) {
      self.zoomScale = self.zoomScale * (1.0f + event.scrollingDeltaY / 120.0f);
    } else {
      self.zoomScale = self.zoomScale * std::pow(1.1f, static_cast<float>(event.scrollingDeltaY));
    }
    self.zoomScale = std::clamp(self.zoomScale, 0.001f, 1000.0f);
    self.contentOffset = CGPointMake(backingPoint.x - contentX * self.zoomScale,
                                     backingPoint.y - contentY * self.zoomScale);
  } else {
    if (event.hasPreciseScrollingDeltas) {
      self.contentOffset = CGPointMake(self.contentOffset.x + event.scrollingDeltaX,
                                       self.contentOffset.y + event.scrollingDeltaY);
    } else {
      self.contentOffset = CGPointMake(self.contentOffset.x + event.scrollingDeltaX * 5,
                                       self.contentOffset.y + event.scrollingDeltaY * 5);
    }
  }
  [self updateZoomScaleAndOffset];
}

- (void)magnifyWithEvent:(NSEvent*)event {
  CGPoint mousePoint = [self convertPoint:event.locationInWindow fromView:nil];
  mousePoint.y = self.bounds.size.height - mousePoint.y;
  CGPoint backingPoint = [self convertPointToBacking:mousePoint];
  float contentX = (backingPoint.x - self.contentOffset.x) / self.zoomScale;
  float contentY = (backingPoint.y - self.contentOffset.y) / self.zoomScale;
  self.zoomScale = std::clamp(self.zoomScale * (1.0f + static_cast<float>(event.magnification)),
                              0.001f, 1000.0f);
  self.contentOffset = CGPointMake(backingPoint.x - contentX * self.zoomScale,
                                   backingPoint.y - contentY * self.zoomScale);
  [self updateZoomScaleAndOffset];
}

@end
