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

#include "tgfx/core/Image.h"
#include "tgfx/layers/layerstyles/LayerStyle.h"

namespace tgfx {

enum class GlassShapeType {
  Auto,
  RoundedRect,
  Ellipse,
  AlphaMask,
};

/**
 * GlassStyle simulates the physical behavior of light passing through a glass surface, producing
 * refraction, chromatic dispersion, frosted blur, and specular highlights. It captures the
 * background content below the layer and renders it with optical distortion driven by a
 * displacement map derived from the layer's rounded-rectangle shape.
 */
class GlassStyle : public LayerStyle {
 public:
  /**
   * Creates a new GlassStyle with the specified parameters.
   * @param refraction The amount of optical distortion along curved edges, range [0, 100].
   * @param depth The inward extent of the refraction region from edges, range [0, 100].
   * @param frost The amount of background blur (frosted glass), range [0, 100].
   * @param dispersion The intensity of chromatic aberration (rainbow prism effect), range [0, 100].
   * @param splay The spread of projected light on the glass surface, range [0, 100].
   * @param lightAngle The direction of the light source in degrees, range [0, 360].
   * @param lightIntensity The brightness of edge highlights, range [0, 100].
   */
  static std::shared_ptr<GlassStyle> Make(float refraction = 50.0f, float depth = 15.0f,
                                          float frost = 10.0f, float dispersion = 0.0f,
                                          float splay = 50.0f, float lightAngle = 135.0f,
                                          float lightIntensity = 50.0f);

  LayerStyleType Type() const override {
    return LayerStyleType::Glass;
  }

  /** Optical distortion strength along curved edges. Range [0, 100]. */
  float refraction() const {
    return _refraction;
  }

  /** Sets the optical distortion strength. */
  void setRefraction(float value);

  /** Inward extent of refraction region from edges in percentage. Range [0, 100]. */
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
   * The spread of projected light across the glass surface. Range [0, 100].
   * Controls how wide the edge highlights diffuse. Higher values produce softer, broader
   * highlights; lower values produce sharper, more concentrated specular reflections.
   */
  float splay() const {
    return _splay;
  }

  /** Sets the light spread amount. */
  void setSplay(float value);

  /** Light source direction in degrees. Range [0, 360]. */
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

  /** The corner radius used for the glass surface shape. */
  float cornerRadius() const {
    return _cornerRadius;
  }

  /** Sets the corner radius for the glass surface shape. */
  void setCornerRadius(float radius);

  /** The shape type of the glass surface. */
  GlassShapeType shapeType() const {
    return _shapeType;
  }

  /** Sets the shape type of the glass surface. */
  void setShapeType(GlassShapeType type);

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

  void invalidateFilter();

  void invalidateRefractionFilter();

  void invalidateFrostFilter();

  void invalidateMaskFilter();

  std::shared_ptr<ImageFilter> getRefractionFilter(
      int layerWidth, int layerHeight, float contentScale, GlassShapeType shapeType,
      float cornerRadius, float halfWidth, float halfHeight, float minHalf, float glassThickness,
      float refractionFactor, float dispersion, float splay, float depthRatio, float lightAngle,
      float lightIntensity, float origMinHalf, float udfPixelToLayerPixel,
      std::shared_ptr<Image> maskImage, std::shared_ptr<Image> coarseMaskImage);

  float _refraction = 50.0f;
  float _depth = 15.0f;
  float _frost = 10.0f;
  float _dispersion = 0.0f;
  float _splay = 50.0f;
  float _lightAngle = 135.0f;
  float _lightIntensity = 50.0f;
  float _cornerRadius = 0.0f;
  GlassShapeType _shapeType = GlassShapeType::Auto;

  std::shared_ptr<ImageFilter> frostFilter = nullptr;
  float currentFrostScale = 0.0f;

  std::shared_ptr<ImageFilter> refractionFilter = nullptr;
  std::shared_ptr<ImageFilter> maskBlurFilter = nullptr;
  std::shared_ptr<ImageFilter> coarseMaskBlurFilter = nullptr;

  int cachedLayerWidth = 0;
  int cachedLayerHeight = 0;
  float cachedContentScale = 0.0f;
  GlassShapeType cachedShapeType = GlassShapeType::RoundedRect;
  float cachedCornerRadius = 0.0f;
  float cachedMaskBlurRadiusX = 0.0f;
  float cachedMaskBlurRadiusY = 0.0f;
  float cachedCoarseMaskBlurRadiusX = 0.0f;
  float cachedCoarseMaskBlurRadiusY = 0.0f;
};

}  // namespace tgfx
