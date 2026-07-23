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

#include <algorithm>
#include "tgfx/core/Image.h"
#include "tgfx/layers/layerstyles/LayerStyle.h"

namespace tgfx {

/**
 * GlassStyle simulates the physical behavior of light passing through a glass surface, producing
 * refraction, chromatic dispersion, frosted blur, and specular highlights. It captures the
 * background content below the layer and renders it with optical distortion driven by a
 * displacement map derived from the layer content's alpha mask.
 */
class GlassStyle : public LayerStyle {
 public:
  /**
   * Creates a new GlassStyle with the specified parameters.
   * @param refraction The amount of optical distortion along curved edges, range [0, 100].
   * @param depth The inward extent of the refraction region from edges, range [1, 100].
   * @param frost The amount of background blur (frosted glass), range [0, 100].
   * @param dispersion The intensity of chromatic aberration (rainbow prism effect), range [0, 100].
   * @param splay The blend factor between the UDF gradient direction and the radial direction used for refraction, range [0, 100].
   * @param lightAngle The direction of the light source in degrees, range [-179, 180].
   * @param lightIntensity The brightness of edge highlights, range [0, 100].
   */
  static std::shared_ptr<GlassStyle> Make(float refraction, float depth, float frost,
                                          float dispersion, float splay, float lightAngle,
                                          float lightIntensity);

  LayerStyleType Type() const override {
    return LayerStyleType::Glass;
  }

  /** Optical distortion strength along curved edges. Range [0, 100]. */
  float refraction() const {
    return _refraction;
  }

  /** Sets the optical distortion strength. */
  void setRefraction(float value);

  /** Inward extent of refraction region from edges in percentage. Range [1, 100]. */
  float depth() const {
    return _depth;
  }

  /** Sets the depth of the refraction region. */
  void setDepth(float value);

  /** Background blur amount (frosted glass). Range [0, 100]. */
  float frost() const {
    return _frost;
  }

  /** Sets the frosted glass blur amount. */
  void setFrost(float value);

  /** Chromatic aberration intensity. Range [0, 100]. */
  float dispersion() const {
    return _dispersion;
  }

  /** Sets the chromatic aberration intensity. */
  void setDispersion(float value);

  /**
   * The blend factor between the UDF gradient direction and the radial (center-pointing)
   * direction used for refraction. Range [0, 100]. At 0, refraction follows the surface
   * curvature (UDF gradient); at 100, refraction points toward the shape center.
   */
  float splay() const {
    return _splay;
  }

  /** Sets the refraction direction blend factor. */
  void setSplay(float value);

  /** Light source direction in degrees. Range [-179, 180]. */
  float lightAngle() const {
    return _lightAngle;
  }

  /** Sets the light source direction. */
  void setLightAngle(float degrees);

  /** Edge highlight brightness. Range [0, 100]. */
  float lightIntensity() const {
    return _lightIntensity;
  }

  /** Sets the edge highlight brightness. */
  void setLightIntensity(float value);

  LayerStylePosition position() const override {
    return LayerStylePosition::Below;
  }

  Rect filterBounds(const Rect& srcRect, float) override {
    return srcRect;
  }

  Rect filterBackground(const Rect& srcRect, float contentScale) override;

  LayerStyleExtraSourceType extraSourceType() const override {
    return LayerStyleExtraSourceType::Background;
  }

 protected:
  void onDraw(Canvas* canvas, const LayerStyleInput& input, float alpha,
              BlendMode blendMode) override;

 private:
  GlassStyle(float refraction, float depth, float frost, float dispersion, float splay,
             float lightAngle, float lightIntensity);

  std::shared_ptr<ImageFilter> getFrostFilter(float contentScale);

  void invalidateFrostFilter();

  std::shared_ptr<ImageFilter> getRefractionFilter(int layerWidth, int layerHeight, float halfWidth,
                                                   float halfHeight, float udfPixelToLayerPixel,
                                                   std::shared_ptr<Image> maskImage,
                                                   std::shared_ptr<Image> coarseMaskImage);

  float getRefractionFactor() const {
    return std::clamp(_refraction / 100.0f, 0.0f, 1.0f);
  }

  float getDepthRatio() const {
    return std::clamp(_depth / 100.0f, 0.0f, 1.0f);
  }

  float _refraction = 80.0f;
  float _depth = 20.0f;
  float _frost = 5.0f;
  float _dispersion = 50.0f;
  float _splay = 0.0f;
  float _lightAngle = 45.0f;
  float _lightIntensity = 80.0f;

  std::shared_ptr<ImageFilter> frostFilter = nullptr;
  float currentFrostScale = 0.0f;
};

}  // namespace tgfx
