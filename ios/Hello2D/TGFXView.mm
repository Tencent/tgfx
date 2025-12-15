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
#include <cmath>
#include "hello2d/LayerBuilder.h"
#include "tgfx/gpu/Recording.h"

@implementation TGFXView {
  std::shared_ptr<tgfx::EAGLWindow> tgfxWindow;
  std::unique_ptr<hello2d::AppHost> appHost;
  tgfx::DisplayList displayList;
  std::shared_ptr<tgfx::Layer> contentLayer;
  int lastDrawIndex;
  std::unique_ptr<tgfx::Recording> lastRecording;
  int lastSurfaceWidth;
  int lastSurfaceHeight;
  bool sizeInvalidated;
}

+ (Class)layerClass {
  return [CAEAGLLayer class];
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

- (void)setContentScaleFactor:(CGFloat)scaleFactor {
  CGFloat oldScaleFactor = self.contentScaleFactor;
  [super setContentScaleFactor:scaleFactor];
  if (oldScaleFactor != scaleFactor) {
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
    displayList.setRenderMode(tgfx::RenderMode::Tiled);
    displayList.setAllowZoomBlur(true);
    displayList.setMaxTileCount(512);
  }
  lastSurfaceWidth = static_cast<int>(self.bounds.size.width * self.contentScaleFactor);
  lastSurfaceHeight = static_cast<int>(self.bounds.size.height * self.contentScaleFactor);
  if (contentLayer && lastSurfaceWidth > 0 && lastSurfaceHeight > 0) {
    hello2d::LayerBuilder::ApplyCenteringTransform(
        contentLayer, static_cast<float>(lastSurfaceWidth), static_cast<float>(lastSurfaceHeight));
  }
  if (tgfxWindow != nullptr) {
    tgfxWindow->invalidSize();
    lastRecording = nullptr;
    sizeInvalidated = true;
  }
}

- (void)updateDisplayListWithDrawIndex:(int)drawIndex zoom:(float)zoom offset:(CGPoint)offset {
  if (appHost == nullptr) {
    [self updateSize];
  }
  auto numBuilders = hello2d::LayerBuilder::Count();
  auto index = (drawIndex % numBuilders);
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

  [self applyTransformWithZoom:zoom offset:offset];
}

- (void)applyTransformWithZoom:(float)zoom offset:(CGPoint)offset {
  if (lastSurfaceWidth > 0 && lastSurfaceHeight > 0) {
    if (contentLayer) {
      hello2d::LayerBuilder::ApplyCenteringTransform(contentLayer,
                                                     static_cast<float>(lastSurfaceWidth),
                                                     static_cast<float>(lastSurfaceHeight));
    }
    displayList.setZoomScale(zoom);
    displayList.setContentOffset(static_cast<float>(offset.x), static_cast<float>(offset.y));
  }
}

- (void)draw:(int)drawIndex zoom:(float)zoom offset:(CGPoint)offset {
  if (self.window == nil) {
    return;
  }
  if (tgfxWindow == nullptr) {
    tgfxWindow = tgfx::EAGLWindow::MakeFrom((CAEAGLLayer*)[self layer]);
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

  // Sync surface size for DPI changes.
  bool surfaceResized = sizeInvalidated;
  sizeInvalidated = false;

  if (surface->width() != lastSurfaceWidth || surface->height() != lastSurfaceHeight) {
    lastSurfaceWidth = surface->width();
    lastSurfaceHeight = surface->height();
    [self applyTransformWithZoom:zoom offset:offset];
    surfaceResized = true;
  }

  auto canvas = surface->getCanvas();
  canvas->clear();
  hello2d::DrawBackground(canvas, surface->width(), surface->height(), self.layer.contentsScale);

  displayList.render(surface.get(), false);

  auto recording = context->flush();

  if (surfaceResized) {
    // When resized, submit current frame immediately (no delay)
    if (recording) {
      context->submit(std::move(recording));
      tgfxWindow->present(context);
    }
    lastRecording = nullptr;
  } else {
    // Delayed one-frame present
    std::swap(lastRecording, recording);

    if (recording) {
      context->submit(std::move(recording));
      tgfxWindow->present(context);
    }
  }

  device->unlock();
}

@end
