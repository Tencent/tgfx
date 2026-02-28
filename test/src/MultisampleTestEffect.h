/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "tgfx/gpu/RuntimeEffect.h"

namespace tgfx {

struct MultisampleConfig {
  int sampleCount = 1;
  uint32_t sampleMask = 0xFFFFFFFF;
  bool alphaToCoverage = false;
  Color outputColor = Color::Red();
};

class MultisampleTestEffect : public RuntimeEffect {
 public:
  static std::shared_ptr<MultisampleTestEffect> Make(const MultisampleConfig& config);

  Rect filterBounds(const Rect& srcRect, MapDirection mapDirection) const override;

  bool onDraw(CommandEncoder* encoder, const std::vector<std::shared_ptr<Texture>>& inputTextures,
              std::shared_ptr<Texture> outputTexture, const Point& offset) const override;

 private:
  MultisampleConfig config = {};

  explicit MultisampleTestEffect(const MultisampleConfig& config);

  std::shared_ptr<RenderPipeline> createPipeline(GPU* gpu) const;
};
}  // namespace tgfx
