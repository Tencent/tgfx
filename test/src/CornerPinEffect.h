/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include "utils/EffectCache.h"

namespace tgfx {
class CornerPinEffect : public RuntimeEffect {
 public:
  static std::shared_ptr<CornerPinEffect> Make(EffectCache* cache, const Point& upperLeft,
                                               const Point& upperRight, const Point& lowerRight,
                                               const Point& lowerLeft);

  Rect filterBounds(const Rect& srcRect, MapDirection) const override;

  bool onDraw(CommandEncoder* encoder, const std::vector<std::shared_ptr<Texture>>& inputTextures,
              std::shared_ptr<Texture> outputTexture, const Point& offset) const override;

 private:
  EffectCache* cache = nullptr;
  Point cornerPoints[4] = {};
  float vertexQs[4] = {1.0f};

  CornerPinEffect(EffectCache* cache, const Point& upperLeft, const Point& upperRight,
                  const Point& lowerRight, const Point& lowerLeft);

  std::shared_ptr<RenderPipeline> createPipeline(GPU* gpu) const;

  void collectVertices(const Texture* source, const Texture* target, const Point& offset,
                       float* vertices) const;

  void calculateVertexQs();
};
}  // namespace tgfx
