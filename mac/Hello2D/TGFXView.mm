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
#include "tdraw/Drawer.h"

@implementation TGFXView {
  int _width;
  int _height;
  std::shared_ptr<tgfx::CGLWindow> window;
  std::shared_ptr<tgfx::Surface> surface;
  std::unique_ptr<tdraw::AppHost> appHost;
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
  _width = static_cast<int>(roundf(size.width));
  _height = static_cast<int>(roundf(size.height));
  surface = nullptr;
  if (appHost == nullptr) {
    appHost = std::make_unique<tdraw::AppHost>();
    NSString* imagePath = [[NSBundle mainBundle] pathForResource:@"bridge" ofType:@"jpg"];
    auto image = tgfx::Image::MakeFromFile(imagePath.UTF8String);
    appHost->addImage("bridge", image);
    auto typeface = tgfx::Typeface::MakeFromName("PingFang SC", "");
    appHost->addTypeface("default", typeface);
    typeface = tgfx::Typeface::MakeFromName("Apple Color Emoji", "");
    appHost->addTypeface("emoji", typeface);
  }
  auto contentScale = size.height / self.bounds.size.height;
  appHost->updateScreen(_width, _height, contentScale);
}

- (void)viewDidMoveToWindow {
  [super viewDidMoveToWindow];
  [self updateSize];
}

- (void)createSurface {
  if (_width <= 0 || _height <= 0) {
    return;
  }
  if (window == nullptr) {
    window = tgfx::CGLWindow::MakeFrom(self);
  }
  if (window == nullptr) {
    return;
  }
  auto device = window->getDevice();
  auto context = device->lockContext();
  if (context == nullptr) {
    return;
  }
  surface = window->createSurface(context);
  device->unlock();
}

- (void)draw:(int)index {
  if (self.window == nil) {
    return;
  }
  if (surface == nullptr) {
    [self createSurface];
  }
  if (window == nullptr) {
    return;
  }
  auto device = window->getDevice();
  auto context = device->lockContext();
  if (context == nullptr) {
    return;
  }
  auto canvas = surface->getCanvas();
  canvas->clear();
  canvas->save();
  auto numDrawers = tdraw::Drawer::Count() - 1;
  index = (index % numDrawers) + 1;
  auto drawer = tdraw::Drawer::GetByName("GridBackground");
  drawer->draw(canvas, appHost.get());
  drawer = tdraw::Drawer::GetByIndex(index);
  drawer->draw(canvas, appHost.get());
  canvas->restore();
  surface->flush();
  context->submit();
  window->present(context);
  device->unlock();
}

@end
