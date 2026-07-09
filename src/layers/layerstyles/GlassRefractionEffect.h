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
#include "tgfx/layers/layerstyles/GlassStyle.h"

namespace tgfx {

/**
 * A RuntimeEffect that performs glass refraction and edge lighting by computing the displacement
 * directly in the shader. Supports analytical SDF shapes (rounded rect, ellipse, star) and
 * alpha-mask-based shapes (arbitrary paths). Supports optional chromatic dispersion.
 */
/**
 * Pass 1: Generates a packed gradient mask from an alpha mask image.
 * Output: R=alpha, G=gradient.x (packed to [0,1]), B=gradient.y (packed to [0,1])
 */
class GlassMaskEffect : public RuntimeEffect {
 public:
  GlassMaskEffect() : RuntimeEffect({}) {
  }

 protected:
  bool onDraw(CommandEncoder* encoder, const std::vector<std::shared_ptr<Texture>>& inputTextures,
              std::shared_ptr<Texture> outputTexture, const Point& offset) const override;

 private:
  std::shared_ptr<RenderPipeline> createPipeline(GPU* gpu) const;
  void collectVertices(const Texture* source, const Texture* target, const Point& offset,
                       float* vertices) const;
  mutable std::shared_ptr<RenderPipeline> cachedPipeline = nullptr;
};

/**
 * Pass 2: Performs glass refraction and edge lighting using the packed gradient mask.
 */
class GlassRefractionEffect : public RuntimeEffect {
 public:
  GlassRefractionEffect(float glassWidth, float glassHeight, float halfWidth, float halfHeight,
                        float cornerRadius, float minHalf, float innerHalfWidth,
                        float innerHalfHeight, float innerRadius, float glassThickness,
                        float refractionFactor, float dispersion, float splay, float depthRatio,
                        float lightAngle, float lightIntensity, GlassShapeType shapeType);

 protected:
  bool onDraw(CommandEncoder* encoder, const std::vector<std::shared_ptr<Texture>>& inputTextures,
              std::shared_ptr<Texture> outputTexture, const Point& offset) const override;

 private:
  std::string buildFragmentShader(bool isDesktop) const;
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
  float _splay = 0.0f;
  float _depthRatio = 0.0f;
  float _lightAngle = 0.0f;
  float _lightIntensity = 0.0f;
  GlassShapeType _shapeType = GlassShapeType::RoundedRect;
  mutable std::shared_ptr<RenderPipeline> cachedPipeline = nullptr;
};

}  // namespace tgfx
