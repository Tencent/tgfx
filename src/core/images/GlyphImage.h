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

#include "GeneratorImage.h"
#include "tgfx/core/GlyphFace.h"
#include "tgfx/core/Image.h"

namespace tgfx {
class GlyphImage final : public GeneratorImage {
 public:
  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<GlyphFace> glyphFace, GlyphID glyphId,
                                         Matrix* matrix);

 protected:
  Type type() const override {
    return Type::Glyph;
  }

 private:
  explicit GlyphImage(std::shared_ptr<ImageGenerator> generator);
};
}  //namespace tgfx