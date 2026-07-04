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
 * A RuntimeEffect that performs glass refraction by computing the surface normal and UV offset
 * directly in the shader, without any intermediate displacement map texture. Supports optional
 * chromatic dispersion (RGB channel splitting).
 */
class GlassRefractionEffect : public RuntimeEffect {
 public:
  /**
   * Creates a GlassRefractionEffect.
   * @param glassWidth The glass layer width in pixels.
   * @param glassHeight The glass layer height in pixels.
   * @param halfWidth Half of the glass width in pixels.
   * @param halfHeight Half of the glass height in pixels.
   * @param cornerRadius The corner radius of the rounded rectangle shape in pixels.
   * @param minHalf The minimum of halfWidth and halfHeight.
   * @param innerHalfWidth Half width of the inner (flat region) shape in pixels.
   * @param innerHalfHeight Half height of the inner (flat region) shape in pixels.
   * @param innerRadius The corner radius of the inner shape in pixels.
   * @param glassThickness The glass thickness used to scale the displacement magnitude.
   * @param refractionFactor The refraction factor [0, 1] scaling the displacement.
   * @param dispersion The chromatic aberration coefficient. Zero means no dispersion.
   */
  GlassRefractionEffect(float glassWidth, float glassHeight, float halfWidth, float halfHeight,
                        float cornerRadius, float minHalf, float innerHalfWidth,
                        float innerHalfHeight, float innerRadius, float glassThickness,
                        float refractionFactor, float dispersion);

 protected:
  bool onDraw(CommandEncoder* encoder, const std::vector<std::shared_ptr<Texture>>& inputTextures,
              std::shared_ptr<Texture> outputTexture, const Point& offset) const override;

 private:
  std::shared_ptr<RenderPipeline> createPipeline(GPU* gpu) const;
  void collectVertices(const Texture* source, const Texture* target, const Point& offset,
                       float* vertices) const;

  float _glassWidth = 0.0f;
  float _glassHeight = 0.0f;
  float _halfWidth = 0.0f;
  float _halfHeight = 0.0f;
  float _cornerRadius = 0.0f;
  float _minHalf = 0.0f;
  float _innerHalfWidth = 0.0f;
  float _innerHalfHeight = 0.0f;
  float _innerRadius = 0.0f;
  float _glassThickness = 0.0f;
  float _refractionFactor = 0.0f;
  float _dispersion = 0.0f;
  // Pipeline requires GPU pointer only available in onDraw (which is const). Caching here avoids
  // re-creating the pipeline on every frame.
  mutable std::shared_ptr<RenderPipeline> cachedPipeline = nullptr;
};

}  // namespace tgfx
