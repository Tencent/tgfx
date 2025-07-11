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

#pragma once

#include <array>
#include "core/PixelRef.h"
#include "tgfx/core/Mask.h"

namespace tgfx {
class PixelRefMask : public Mask {
 public:
  explicit PixelRefMask(std::shared_ptr<PixelRef> pixelRef);

  int width() const override {
    return pixelRef->width();
  }

  int height() const override {
    return pixelRef->height();
  }

  bool isHardwareBacked() const override {
    return pixelRef->isHardwareBacked();
  }

  void clear() override {
    return pixelRef->clear();
  }

  std::shared_ptr<ImageBuffer> makeBuffer() const override {
    return pixelRef->makeBuffer();
  }

 protected:
  std::shared_ptr<PixelRef> pixelRef = nullptr;

  void markContentDirty(const Rect& bounds, bool flipY);

  static const std::array<uint8_t, 256>& GammaTable();

  std::shared_ptr<ImageStream> getImageStream() const override {
    return pixelRef;
  }
};
}  // namespace tgfx
