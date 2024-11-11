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

#include "Rasterizer.h"

namespace tgfx {
class GlyphRasterizer : public Rasterizer {
 public:
  GlyphRasterizer(std::shared_ptr<GlyphRunList> glyphRunList, const ISize& clipSize,
                  const Matrix& matrix, bool antiAlias, const Stroke* stroke)
      : Rasterizer(clipSize, matrix, antiAlias, stroke), glyphRunList(std::move(glyphRunList)) {
  }

 protected:
  std::shared_ptr<ImageBuffer> onMakeBuffer(bool tryHardware) const override {
    auto mask = Mask::Make(width(), height(), tryHardware);
    if (!mask) {
      return nullptr;
    }
    mask->setAntiAlias(antiAlias);
    mask->setMatrix(matrix);
    mask->fillText(glyphRunList.get(), stroke);
    return mask->makeBuffer();
  }

 private:
  std::shared_ptr<GlyphRunList> glyphRunList = nullptr;
};

std::shared_ptr<Rasterizer> Rasterizer::MakeFrom(std::shared_ptr<GlyphRunList> glyphRunList,
                                                 const ISize& clipSize, const Matrix& matrix,
                                                 bool antiAlias, const Stroke* stroke) {
  if (glyphRunList == nullptr || clipSize.isEmpty()) {
    return nullptr;
  }
  return std::make_shared<GlyphRasterizer>(std::move(glyphRunList), clipSize, matrix, antiAlias,
                                           stroke);
}

Rasterizer::Rasterizer(const ISize& clipSize, const Matrix& matrix, bool antiAlias, const Stroke* s)
    : ImageGenerator(clipSize.width, clipSize.height), matrix(matrix), antiAlias(antiAlias) {
  if (s != nullptr) {
    stroke = new Stroke(*s);
  }
}

Rasterizer::~Rasterizer() {
  delete stroke;
}

bool Rasterizer::asyncSupport() const {
#if defined(TGFX_BUILD_FOR_WEB) && !defined(TGFX_USE_FREETYPE)
  return false;
#else
  return true;
#endif
}
}  // namespace tgfx
