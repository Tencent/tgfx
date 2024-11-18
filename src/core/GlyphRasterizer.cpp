/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "GlyphRasterizer.h"
#include "tgfx/core/Mask.h"
#include "profileClient/Profile.h"

namespace tgfx {
GlyphRasterizer::GlyphRasterizer(int width, int height, std::shared_ptr<GlyphRunList> glyphRunList,
                                 bool antiAlias, const Matrix& matrix, const Stroke* s)
    : Rasterizer(width, height), glyphRunList(std::move(glyphRunList)), antiAlias(antiAlias),
      matrix(matrix) {
  if (s != nullptr) {
    stroke = new Stroke(*s);
  }
}

GlyphRasterizer::~GlyphRasterizer() {
  delete stroke;
}

std::shared_ptr<ImageBuffer> GlyphRasterizer::onMakeBuffer(bool tryHardware) const {
  TGFX_PROFILE_ZONE_SCOPPE_NAME("GlyphRasterizer::onMakeBuffer");
  auto mask = Mask::Make(width(), height(), tryHardware);
  if (!mask) {
    return nullptr;
  }
  mask->setAntiAlias(antiAlias);
  mask->setMatrix(matrix);
  mask->fillText(glyphRunList.get(), stroke);
  return mask->makeBuffer();
}
}  // namespace tgfx
