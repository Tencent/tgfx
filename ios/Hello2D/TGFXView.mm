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

#import "TGFXView.h"
#include <cmath>
#include "drawers/Drawer.h"

@implementation TGFXView {
  std::shared_ptr<tgfx::EAGLWindow> tgfxWindow;
  std::unique_ptr<drawers::AppHost> appHost;
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
    appHost = std::make_unique<drawers::AppHost>();
    NSString* imagePath = [[NSBundle mainBundle] pathForResource:@"bridge" ofType:@"jpg"];
    auto image = tgfx::Image::MakeFromFile(imagePath.UTF8String);
    appHost->addImage("bridge", image);
    auto typeface = tgfx::Typeface::MakeFromName("PingFang SC", "");
    appHost->addTypeface("default", typeface);
    typeface = tgfx::Typeface::MakeFromName("Apple Color Emoji", "");
    appHost->addTypeface("emoji", typeface);
  }
  auto sizeChanged = appHost->updateScreen(width, height, self.layer.contentsScale);
  if (sizeChanged && tgfxWindow != nullptr) {
    tgfxWindow->invalidSize();
  }
}

- (void)draw:(int)index zoom:(float)zoom offset:(CGPoint)offset {
  if (self.window == nil) {
    return;
  }
  if (appHost->width() <= 0 || appHost->height() <= 0) {
    return;
  }
  if (tgfxWindow == nullptr) {
    tgfxWindow = tgfx::EAGLWindow::MakeFrom((CAEAGLLayer*)[self layer]);
  }
  if (tgfxWindow == nullptr) {
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
  appHost->updateZoomAndOffset(zoom, tgfx::Point(offset.x, offset.y));
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto numDrawers = drawers::Drawer::Count() - 1;
  index = (index % numDrawers) + 1;
  auto drawer = drawers::Drawer::GetByName("GridBackground");
  drawer->draw(canvas, appHost.get());
  drawer = drawers::Drawer::GetByIndex(index);
  drawer->draw(canvas, appHost.get());
  context->flushAndSubmit();
  tgfxWindow->present(context);
  device->unlock();
}

@end
