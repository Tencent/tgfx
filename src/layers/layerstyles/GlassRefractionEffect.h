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

/**
 * A RuntimeEffect that performs glass refraction by sampling the source texture with UV offsets
 * driven by a displacement map. Supports optional chromatic dispersion (RGB channel splitting).
 */
class GlassRefractionEffect : public RuntimeEffect {
 public:
  /**
   * Creates a GlassRefractionEffect.
   * @param displacementMap The displacement map image (R=dx, G=dy, 128=zero offset).
   * @param displacementScale The maximum displacement in pixels.
   * @param dispersion The chromatic aberration coefficient [0, 0.15]. Zero means no dispersion.
   * @param glassWidth The glass layer width in pixels (same as displacement map width).
   * @param glassHeight The glass layer height in pixels (same as displacement map height).
   */
  GlassRefractionEffect(std::shared_ptr<Image> displacementMap, float displacementScale,
                        float dispersion, float glassWidth, float glassHeight);

 protected:
  bool onDraw(CommandEncoder* encoder, const std::vector<std::shared_ptr<Texture>>& inputTextures,
              std::shared_ptr<Texture> outputTexture, const Point& offset) const override;

 private:
  std::shared_ptr<RenderPipeline> createPipeline(GPU* gpu) const;
  void collectVertices(const Texture* source, const Texture* target, const Point& offset,
                       float* vertices) const;

  float _displacementScale = 0.0f;
  float _dispersion = 0.0f;
  float _glassWidth = 0.0f;
  float _glassHeight = 0.0f;
  // Pipeline requires GPU pointer only available in onDraw (which is const). Caching here avoids
  // re-creating the pipeline on every frame.
  mutable std::shared_ptr<RenderPipeline> cachedPipeline = nullptr;
};

}  // namespace tgfx
