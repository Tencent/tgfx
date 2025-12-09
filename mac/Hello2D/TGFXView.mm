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
  // Cached surface size for calculating base scale without locking device
  int lastSurfaceWidth;
  int lastSurfaceHeight;
  // Flag to force render when size changes
  bool sizeInvalidated;
  // Last applied zoom/offset to DisplayList
  float lastZoomScale;
  CGPoint lastContentOffset;
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
    sizeInvalidated = false;
    lastZoomScale = 1.0f;
    lastContentOffset = {0, 0};
    displayList.setRenderMode(tgfx::RenderMode::Tiled);
    displayList.setAllowZoomBlur(true);
    displayList.setMaxTileCount(512);
  }
  if (tgfxWindow != nullptr) {
    tgfxWindow->invalidSize();
    // Clear lastRecording when size changes, as it was created for the old surface size
    lastRecording = nullptr;
    // Mark size as invalidated to force render next frame
    sizeInvalidated = true;
    // Start display link to trigger redraw after size change
    [self startDisplayLink];
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
    self.caDisplayLink.paused = YES;
    [self.caDisplayLink addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSRunLoopCommonModes];
  } else {
    CVDisplayLinkCreateWithActiveCGDisplays(&_cvDisplayLink);
    CVDisplayLinkSetOutputCallback(_cvDisplayLink, &OnDisplayLinkCallback, (__bridge void*)self);
    CVDisplayLinkStart(self.cvDisplayLink);
  }

  [self updateSize];
}

- (void)startDisplayLink {
  if (@available(macOS 14, *)) {
    if (self.caDisplayLink) {
      self.caDisplayLink.paused = NO;
    }
  }
}

- (void)stopDisplayLink {
  if (@available(macOS 14, *)) {
    if (self.caDisplayLink) {
      self.caDisplayLink.paused = YES;
    }
  }
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
  if (![self draw]) {
    displayLink.paused = YES;
  }
}

- (BOOL)draw {
  if (self.window == nil) {
    return false;
  }
  if (tgfxWindow == nullptr) {
    tgfxWindow = tgfx::CGLWindow::MakeFrom(self);
  }
  if (tgfxWindow == nullptr) {
    return false;
  }

  // ========== All DisplayList updates BEFORE locking device ==========

  auto numBuilders = hello2d::LayerBuilder::Count();
  auto index = (self.drawIndex % numBuilders);
  if (index != lastDrawIndex || !contentLayer) {
    auto builder = hello2d::LayerBuilder::GetByIndex(index);
    if (builder) {
      contentLayer = builder->buildLayerTree(appHost.get());
      if (contentLayer) {
        displayList.root()->removeChildren();
        displayList.root()->addChild(contentLayer);
      }
    }
    lastDrawIndex = index;
  }

  // Calculate base scale and offset using cached surface size
  if (lastSurfaceWidth > 0 && lastSurfaceHeight > 0) {
    bool zoomChanged = (self.zoomScale != lastZoomScale);
    bool offsetChanged = (self.contentOffset.x != lastContentOffset.x ||
                          self.contentOffset.y != lastContentOffset.y);
    if (zoomChanged || offsetChanged) {
      static constexpr float DESIGN_SIZE = 720.0f;
      auto scaleX = static_cast<float>(lastSurfaceWidth) / DESIGN_SIZE;
      auto scaleY = static_cast<float>(lastSurfaceHeight) / DESIGN_SIZE;
      auto baseScale = std::min(scaleX, scaleY);
      auto scaledSize = DESIGN_SIZE * baseScale;
      auto baseOffsetX = (static_cast<float>(lastSurfaceWidth) - scaledSize) * 0.5f;
      auto baseOffsetY = (static_cast<float>(lastSurfaceHeight) - scaledSize) * 0.5f;

      displayList.setZoomScale(self.zoomScale * baseScale);
      displayList.setContentOffset(baseOffsetX + static_cast<float>(self.contentOffset.x),
                                   baseOffsetY + static_cast<float>(self.contentOffset.y));
      lastZoomScale = self.zoomScale;
      lastContentOffset = self.contentOffset;
    }
  }

  // Check if content has changed AFTER setting all properties, BEFORE locking device
  bool needsRender = displayList.hasContentChanged() || sizeInvalidated;

  // If no content change AND no pending lastRecording -> skip everything, don't lock device
  if (!needsRender && lastRecording == nullptr) {
    return false;
  }

  // ========== Now lock device for rendering/submitting ==========

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

  // Update cached surface size for next frame's calculations
  int newSurfaceWidth = surface->width();
  int newSurfaceHeight = surface->height();
  bool sizeChanged = (newSurfaceWidth != lastSurfaceWidth || newSurfaceHeight != lastSurfaceHeight);
  lastSurfaceWidth = newSurfaceWidth;
  lastSurfaceHeight = newSurfaceHeight;

  // If surface size just changed, update zoomScale/contentOffset now
  if (sizeChanged && lastSurfaceWidth > 0 && lastSurfaceHeight > 0) {
    static constexpr float DESIGN_SIZE = 720.0f;
    auto scaleX = static_cast<float>(lastSurfaceWidth) / DESIGN_SIZE;
    auto scaleY = static_cast<float>(lastSurfaceHeight) / DESIGN_SIZE;
    auto baseScale = std::min(scaleX, scaleY);
    auto scaledSize = DESIGN_SIZE * baseScale;
    auto baseOffsetX = (static_cast<float>(lastSurfaceWidth) - scaledSize) * 0.5f;
    auto baseOffsetY = (static_cast<float>(lastSurfaceHeight) - scaledSize) * 0.5f;
    displayList.setZoomScale(self.zoomScale * baseScale);
    displayList.setContentOffset(baseOffsetX + static_cast<float>(self.contentOffset.x),
                                 baseOffsetY + static_cast<float>(self.contentOffset.y));
    lastZoomScale = self.zoomScale;
    lastContentOffset = self.contentOffset;
    needsRender = true;
  }

  // Clear sizeInvalidated flag
  sizeInvalidated = false;

  // Track if we submitted anything this frame
  bool didSubmit = false;

  // Case 1: No content change BUT have pending lastRecording -> only submit lastRecording
  if (!needsRender) {
    context->submit(std::move(lastRecording));
    tgfxWindow->present(context);
    didSubmit = true;
    device->unlock();
    return didSubmit;
  }

  // Case 2: Content changed -> render new content
  auto canvas = surface->getCanvas();
  canvas->clear();
  hello2d::DrawBackground(canvas, surface->width(), surface->height(), self.layer.contentsScale);

  displayList.render(surface.get(), false);

  // Delayed one-frame present mode
  auto recording = context->flush();
  if (lastRecording) {
    // Normal delayed mode: submit last frame, save current for next
    context->submit(std::move(lastRecording));
    tgfxWindow->present(context);
    didSubmit = true;
    lastRecording = std::move(recording);
  } else if (recording) {
    // No lastRecording (first frame or after size change): submit current directly
    context->submit(std::move(recording));
    tgfxWindow->present(context);
    didSubmit = true;
  }

  device->unlock();

  // Return true if we submitted or have pending recording
  return didSubmit || lastRecording != nullptr;
}

- (void)mouseDown:(NSEvent*)event {
  self.drawIndex++;
  self.zoomScale = 1.0f;
  self.contentOffset = CGPointZero;
  [self startDisplayLink];
}

- (void)scrollWheel:(NSEvent*)event {
  BOOL isCommandPressed = (event.modifierFlags & NSEventModifierFlagCommand) != 0;
  BOOL isShiftPressed = (event.modifierFlags & NSEventModifierFlagShift) != 0;

  CGSize backingSize = [self convertSizeToBacking:CGSizeMake(1.0, 1.0)];
  CGFloat backingScale = backingSize.height;

  if (isCommandPressed) {
    CGFloat zoomStep = std::exp(event.deltaY / 400.0f);
    CGPoint mousePoint = [self convertPoint:event.locationInWindow fromView:nil];
    CGPoint backingPoint = [self convertPointToBacking:mousePoint];
    float oldZoom = self.zoomScale;
    self.zoomScale = std::clamp(self.zoomScale * static_cast<float>(zoomStep), 0.001f, 1000.0f);
    CGPoint offset = self.contentOffset;
    offset.x = backingPoint.x - ((backingPoint.x - offset.x) / oldZoom) * self.zoomScale;
    offset.y = backingPoint.y - ((backingPoint.y - offset.y) / oldZoom) * self.zoomScale;
    self.contentOffset = offset;
  } else {
    CGPoint offset = self.contentOffset;
    if (isShiftPressed) {
      offset.x += static_cast<float>(event.deltaX * backingScale);
    } else {
      offset.y -= static_cast<float>(event.deltaY * backingScale);
    }
    self.contentOffset = offset;
  }
  [self startDisplayLink];
}

@end
