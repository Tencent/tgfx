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

@implementation TGFXView {
  int _width;
  int _height;
  std::shared_ptr<tgfx::CGLWindow> window;
  std::shared_ptr<tgfx::Surface> surface;
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
  [self drawBackground:canvas];
  if (index % 2 == 0) {
    [self drawShape:canvas];
  } else {
    [self drawImage:canvas];
  }
  canvas->restore();
  surface->flush();
  context->submit();
  window->present(context);
  device->unlock();
}

- (void)drawShape:(tgfx::Canvas*)canvas {
  auto scale = [self.window backingScaleFactor];
  tgfx::Color cyan = {0.0f, 1.0f, 1.0f, 1.0f};
  tgfx::Color magenta = {1.0f, 0.0f, 1.0f, 1.0f};
  tgfx::Color yellow = {1.0f, 1.0f, 0.0f, 1.0f};
  auto shader = tgfx::Shader::MakeSweepGradient(tgfx::Point::Make(_width / 2, _height / 2), 0, 360,
                                                {cyan, magenta, yellow, cyan}, {});
  tgfx::Paint paint = {};
  paint.setShader(shader);
  auto size = static_cast<int>(384 * scale);
  auto rect = tgfx::Rect::MakeXYWH((_width - size) / 2, (_height - size) / 2, size, size);
  tgfx::Path path = {};
  path.addRoundRect(rect, 20 * scale, 20 * scale);
  canvas->drawPath(path, paint);
};

- (void)drawImage:(tgfx::Canvas*)canvas {
  auto scale = [self.window backingScaleFactor];
  auto filter = tgfx::ImageFilter::DropShadow(5 * scale, 5 * scale, 50 * scale, 50 * scale,
                                              tgfx::Color::Black());
  tgfx::Paint paint = {};
  paint.setImageFilter(filter);
  NSString* imagePath = [[NSBundle mainBundle] pathForResource:@"bridge" ofType:@"jpg"];
  auto image = tgfx::Image::MakeFromFile(imagePath.UTF8String);
  canvas->drawImage(image, (_width - image->width()) / 2, (_height - image->height()) / 2, &paint);
};

- (void)drawBackground:(tgfx::Canvas*)canvas {
  tgfx::Paint paint;
  paint.setColor(tgfx::Color{0.8f, 0.8f, 0.8f, 1.f});
  int tileSize = 8 * [self.window backingScaleFactor];
  for (int y = 0; y < _height; y += tileSize) {
    bool draw = (y / tileSize) % 2 == 1;
    for (int x = 0; x < _width; x += tileSize) {
      if (draw) {
        auto rect =
            tgfx::Rect::MakeXYWH(static_cast<float>(x), static_cast<float>(y),
                                 static_cast<float>(tileSize), static_cast<float>(tileSize));
        canvas->drawRect(rect, paint);
      }
      draw = !draw;
    }
  }
}
@end
