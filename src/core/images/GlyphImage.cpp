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

#include "GlyphImage.h"

namespace tgfx {
class GlyphImageGenerator final : public ImageGenerator {
 public:
  GlyphImageGenerator(int width, int height, std::shared_ptr<GlyphFace> glyphFace, GlyphID glyphID)
      : ImageGenerator(width, height), glyphFace(std::move(glyphFace)), glyphID(glyphID) {
  }

  bool isAlphaOnly() const override {
    return !glyphFace->hasColor();
  }

 protected:
  std::shared_ptr<ImageBuffer> onMakeBuffer(bool tryHardware) const override {
    return glyphFace->getImage(glyphID, tryHardware);
  }

 private:
  std::shared_ptr<GlyphFace> glyphFace = nullptr;
  GlyphID glyphID = 0;
};

std::shared_ptr<Image> GlyphImage::MakeFrom(std::shared_ptr<GlyphFace> glyphFace, GlyphID glyphID,
                                            Matrix* matrix) {
  if (glyphFace == nullptr) {
    return nullptr;
  }
  auto bounds = glyphFace->getImageTransform(glyphID, matrix);
  if (bounds.isEmpty()) {
    return nullptr;
  }
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  auto generator =
      std::make_shared<GlyphImageGenerator>(width, height, std::move(glyphFace), glyphID);
  auto image = std::shared_ptr<GlyphImage>(new GlyphImage(std::move(generator)));
  image->weakThis = image;
  return image;
}

GlyphImage::GlyphImage(std::shared_ptr<ImageGenerator> generator)
    : GeneratorImage(UniqueKey::Make(), std::move(generator)) {
}

}  //namespace tgfx