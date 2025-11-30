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

@implementation TGFXView {
  std::shared_ptr<tgfx::EAGLWindow> tgfxWindow;
  std::unique_ptr<hello2d::AppHost> appHost;
  tgfx::DisplayList displayList;
  int lastDrawIndex;
  bool needsRedraw;
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
  auto width = static_cast<int>(roundf(self.layer.bounds.size.width * self.layer.contentsScale));
  auto height = static_cast<int>(roundf(self.layer.bounds.size.height * self.layer.contentsScale));
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
    needsRedraw = true;
    displayList.setRenderMode(tgfx::RenderMode::Tiled);
    displayList.setAllowZoomBlur(true);
    displayList.setMaxTileCount(512);
  }
  auto sizeChanged = appHost->updateScreen(width, height, self.layer.contentsScale);
  if (sizeChanged && tgfxWindow != nullptr) {
    tgfxWindow->invalidSize();
    needsRedraw = true;
  }
}
- (void)markDirty {
  needsRedraw = true;
}

- (BOOL)draw:(int)drawIndex zoom:(float)zoom offset:(CGPoint)offset {
  if (!needsRedraw) {
    return false;
  }

  if (self.window == nil) {
    return false;
  }
  if (appHost->width() <= 0 || appHost->height() <= 0) {
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
  auto numBuilders = hello2d::GetLayerBuilderCount();
  auto index = (drawIndex % numBuilders);
  if (index != lastDrawIndex) {
    auto layer = hello2d::BuildAndCenterLayer(index, appHost.get());
    if (layer) {
      displayList.root()->removeChildren();
      displayList.root()->addChild(layer);
    }
    lastDrawIndex = index;
  }

  // Directly set zoom and offset on DisplayList
  displayList.setZoomScale(zoom);
  displayList.setContentOffset(static_cast<float>(offset.x), static_cast<float>(offset.y));

  // Draw background and render DisplayList
  auto canvas = surface->getCanvas();
  canvas->clear();
  hello2d::DrawSampleBackground(canvas, appHost.get());
  displayList.render(surface.get(), false);

  context->flushAndSubmit();
  tgfxWindow->present(context);
  device->unlock();

  needsRedraw = false;
  return true;
}

@end
