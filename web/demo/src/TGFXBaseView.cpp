/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "TGFXBaseView.h"
#include <random>

using namespace emscripten;

namespace hello2d {
TGFXBaseView::TGFXBaseView(const std::string& canvasID) : canvasID(canvasID) {
  appHost = std::make_shared<drawers::AppHost>();
  redPaint.setColor(tgfx::Color::Red());
  redPaint.setAntiAlias(false);
  greenPaint.setColor(tgfx::Color::Green());
  greenPaint.setAntiAlias(false);
  bluePaint.setColor(tgfx::Color::Blue());
  bluePaint.setAntiAlias(false);
}

void TGFXBaseView::setDrawItemsCount(size_t count) {
  itemCount = count;
}

void TGFXBaseView::updateSize(float devicePixelRatio) {
  if (!canvasID.empty()) {
    emscripten_get_canvas_element_size(canvasID.c_str(), &width, &height);
    auto sizeChanged = appHost->updateScreen(width, height, devicePixelRatio);
    if (items.empty() || sizeChanged) {
      initItemData();
    }
    if (sizeChanged && window) {
      window->invalidSize();
    }
  }
}

void TGFXBaseView::setImagePath(const std::string& imagePath) {
  auto image = tgfx::Image::MakeFromFile(imagePath.c_str());
  if (image) {
    appHost->addImage("bridge", std::move(image));
  }
}

void TGFXBaseView::initItemData() {
  items.resize(itemCount);
  std::mt19937 rng(18);
  std::mt19937 rngSpeed(36);
  std::uniform_real_distribution<float> distribution(0, 1);
  std::uniform_real_distribution<float> speedDistribution(1, 2);
  for (size_t i = 0; i < itemCount; i++) {
    items[i].x = static_cast<float>(width) * distribution(rng);
    items[i].y = static_cast<float>(height) * distribution(rng);
    items[i].width = 10.0f + distribution(rng) * 40.0f;
    items[i].speed = speedDistribution(rngSpeed);
  }
}

void TGFXBaseView::updateItemData() {
  for (size_t i = 0; i < itemCount; i++) {
    items[i].x -= items[i].speed;
    if (items[i].x + items[i].width < 0) {
      items[i].x = static_cast<float>(width);
    }
  }
}

void TGFXBaseView::drawContents(tgfx::Canvas* canvas) {
  for (size_t i = 0; i < items.size(); i++) {
    auto item = items[i];
    auto rect = tgfx::Rect::MakeXYWH(item.x, item.y, item.width, item.width);
    if (i % 3 == 0) {
      canvas->drawRect(rect, redPaint);
    } else if (i % 3 == 1) {
      canvas->drawRect(rect, greenPaint);
    } else {
      canvas->drawRect(rect, bluePaint);
    }
  }
}

uint64_t GetCurrentTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto time = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  return static_cast<uint64_t>(time);
}

void TGFXBaseView::draw(int) {
  if (appHost->width() <= 0 || appHost->height() <= 0) {
    return;
  }
  if (window == nullptr) {
    window = tgfx::WebGLWindow::MakeFrom(canvasID);
  }
  if (window == nullptr) {
    return;
  }
  auto device = window->getDevice();
  auto context = device->lockContext();
  if (context == nullptr) {
    return;
  }
  auto surface = window->getSurface(context);
  if (surface == nullptr) {
    device->unlock();
    return;
  }
  auto canvas = surface->getCanvas();
  canvas->clear();

  drawContents(canvas);

  // auto numDrawers = drawers::Drawer::Count() - 1;
  // auto index = (drawIndex % numDrawers) + 1;
  // auto drawer = drawers::Drawer::GetByName("GridBackground");
  // drawer->draw(canvas, appHost.get());
  // drawer = drawers::Drawer::GetByIndex(index);
  // drawer->draw(canvas, appHost.get());
  context->flushAndSubmit();
  window->present(context);
  device->unlock();
  updateItemData();
}
}  // namespace hello2d

int main(int, const char*[]) {
  return 0;
}
