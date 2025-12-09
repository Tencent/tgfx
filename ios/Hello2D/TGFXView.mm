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
    displayList.setRenderMode(tgfx::RenderMode::Tiled);
    displayList.setAllowZoomBlur(true);
    displayList.setMaxTileCount(512);
  }
  if (tgfxWindow != nullptr) {
    tgfxWindow->invalidSize();
  }
}

- (BOOL)draw:(int)drawIndex zoom:(float)zoom offset:(CGPoint)offset {
  if (self.window == nil) {
    return false;
  }
  if (tgfxWindow == nullptr) {
    tgfxWindow = tgfx::EAGLWindow::MakeFrom((CAEAGLLayer*)[self layer]);
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

  // Switch sample when drawIndex changes
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

  // Calculate base scale and offset to fit 720x720 design size to window
  static constexpr float DESIGN_SIZE = 720.0f;
  auto scaleX = static_cast<float>(surface->width()) / DESIGN_SIZE;
  auto scaleY = static_cast<float>(surface->height()) / DESIGN_SIZE;
  auto baseScale = std::min(scaleX, scaleY);
  auto scaledSize = DESIGN_SIZE * baseScale;
  auto baseOffsetX = (static_cast<float>(surface->width()) - scaledSize) * 0.5f;
  auto baseOffsetY = (static_cast<float>(surface->height()) - scaledSize) * 0.5f;

  // Apply user zoom and offset on top of the base scale/offset
  displayList.setZoomScale(zoom * baseScale);
  displayList.setContentOffset(baseOffsetX + static_cast<float>(offset.x),
                               baseOffsetY + static_cast<float>(offset.y));

  // Check if content has changed before rendering
  bool needsRender = displayList.hasContentChanged();

  // In delayed one-frame present mode:
  // - If no content changed AND no last recording to submit -> skip rendering
  if (!needsRender && lastRecording == nullptr) {
    device->unlock();
    return false;
  }

  // If no new content but have last recording, only submit it without new rendering
  if (!needsRender) {
    context->submit(std::move(lastRecording));
    tgfxWindow->present(context);
    device->unlock();
    return false;
  }

  // Draw background
  auto canvas = surface->getCanvas();
  canvas->clear();
  hello2d::DrawBackground(canvas, surface->width(), surface->height(), self.layer.contentsScale);

  // Render DisplayList
  displayList.render(surface.get(), false);

  // Delayed one-frame present mode: flush + submit
  auto recording = context->flush();
  if (lastRecording) {
    context->submit(std::move(lastRecording));
    if (recording) {
      tgfxWindow->present(context);
    }
  }
  lastRecording = std::move(recording);

  device->unlock();

  // In delayed one-frame mode, if we have a pending recording, we need another frame to present it
  return displayList.hasContentChanged() || lastRecording != nullptr;
}

@end
