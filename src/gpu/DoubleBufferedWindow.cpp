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

#include "tgfx/gpu/DoubleBufferedWindow.h"
#include "gpu/Texture.h"

namespace tgfx {
class DoubleBufferedWindowOffscreen : public DoubleBufferedWindow {
 public:
  DoubleBufferedWindowOffscreen(std::shared_ptr<Device> device, int width, int height,
                                bool tryHardware)
      : DoubleBufferedWindow(std::move(device)), width(width), height(height),
        tryHardware(tryHardware) {
  }

 private:
  std::shared_ptr<Surface> makeSurface(Context* context) const {
    if (tryHardware) {
      auto hardwareBuffer = tgfx::HardwareBufferAllocate(width, height);
      auto surface = tgfx::Surface::MakeFrom(context, hardwareBuffer);
      tgfx::HardwareBufferRelease(hardwareBuffer);
      if (surface != nullptr) {
        return surface;
      }
    }
#ifdef __APPLE__
    return tgfx::Surface::Make(context, width, height, tgfx::ColorType::BGRA_8888);
#else
    return tgfx::Surface::Make(context, width, height);
#endif
  }

  std::shared_ptr<Surface> onCreateSurface(Context* context) override {
    if (frontSurface == nullptr) {
      frontSurface = makeSurface(context);
    }
    if (backSurface == nullptr) {
      backSurface = makeSurface(context);
    }
    if (frontSurface == nullptr || backSurface == nullptr) {
      return nullptr;
    }
    return backSurface;
  }

  int width;
  int height;
  bool tryHardware;
};

std::shared_ptr<DoubleBufferedWindow> DoubleBufferedWindow::Make(std::shared_ptr<Device> device,
                                                                 int width, int height,
                                                                 bool tryHardware) {
  if (device == nullptr || width <= 0 || height <= 0) {
    return nullptr;
  }
  return std::shared_ptr<DoubleBufferedWindow>(
      new DoubleBufferedWindowOffscreen(std::move(device), width, height, tryHardware));
}

DoubleBufferedWindow::DoubleBufferedWindow(std::shared_ptr<Device> device)
    : Window(std::move(device)) {
}

DoubleBufferedWindow::~DoubleBufferedWindow() {
  frontSurface = nullptr;
  backSurface = nullptr;
}

void DoubleBufferedWindow::onPresent(Context* context, int64_t) {
  if (frontSurface == nullptr) {
    return;
  }
  std::swap(frontSurface, backSurface);
  onSwapSurfaces(context);
}
}  // namespace tgfx
