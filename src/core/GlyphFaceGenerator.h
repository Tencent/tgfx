/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/GlyphFace.h"
#include "tgfx/core/ImageGenerator.h"

namespace tgfx {
class GlyphFaceGenerator final : public ImageGenerator {
 public:
  static std::shared_ptr<GlyphFaceGenerator> MakeFrom(std::shared_ptr<GlyphFace> glyphFace,
                                                      GlyphID glyphID, Matrix* matrix);

  bool isAlphaOnly() const override {
    return !glyphFace->hasColor();
  }

 protected:
  std::shared_ptr<ImageBuffer> onMakeBuffer(bool tryHardware) const override {
    return glyphFace->getImage(glyphID, tryHardware);
  }

 private:
  GlyphFaceGenerator(int width, int height, std::shared_ptr<GlyphFace> glyphFace, GlyphID glyphID)
      : ImageGenerator(width, height), glyphFace(std::move(glyphFace)), glyphID(glyphID) {
  }

  std::shared_ptr<GlyphFace> glyphFace = nullptr;
  GlyphID glyphID = 0;
};
}  // namespace tgfx