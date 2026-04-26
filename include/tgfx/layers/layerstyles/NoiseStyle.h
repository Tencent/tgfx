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

#include "tgfx/core/Color.h"
#include "tgfx/core/Shader.h"
#include "tgfx/layers/layerstyles/LayerStyle.h"

namespace tgfx {

class MonoNoiseStyle;
class DuoNoiseStyle;
class MultiNoiseStyle;

/**
 * NoiseStyle adds a procedural Perlin noise overlay above the layer content, clipped to the
 * content's alpha. All three modes (Mono / Duo / Multi) apply a band-pass mask on the noise
 * signal centered at luma (or noise alpha for Multi) = 0.25, with the `density` parameter
 * controlling the band width. The noise pattern is anchored at the center of the content region
 * so it stays stable across layer resizes and picture replays.
 */
class NoiseStyle : public LayerStyle {
 public:
  /**
   * Creates a single-color noise style. A band-pass mask is applied on the luminance of the noise
   * centered at 0.25, with width controlled by density. Matching pixels are filled with color.
   * @param size     The noise grain size. Larger values produce coarser grains. Must be positive.
   * @param density  The band width in [0, 1]. Larger values widen the band so more noise pixels
   *                 pass. UI-driven clients that need a non-linear density curve (for example to
   *                 match a specific design tool) should apply the mapping at their own layer.
   * @param color    The noise color. The alpha component controls the noise opacity.
   * @param seed     The random seed for the noise pattern.
   */
  static std::shared_ptr<MonoNoiseStyle> MakeMono(float size, float density, const Color& color,
                                                  float seed = 0.0f);

  /**
   * Creates a dual-color noise style. Two mirrored band-pass masks are applied on the luminance
   * of the noise: one centered at 0.25 filled with firstColor, one centered at 0.75 filled with
   * secondColor. Both bands share the same width controlled by density.
   * @param size         The noise grain size. Larger values produce coarser grains. Must be
   *                     positive.
   * @param density      The band width in [0, 1]. Larger values widen both bands so more noise
   *                     pixels pass.
   * @param firstColor   The color filled into the first band (luma around 0.25).
   * @param secondColor  The color filled into the second band (luma around 0.75).
   * @param seed         The random seed for the noise pattern.
   */
  static std::shared_ptr<DuoNoiseStyle> MakeDuo(float size, float density, const Color& firstColor,
                                                const Color& secondColor, float seed = 0.0f);

  /**
   * Creates a multi-color noise style that preserves the Perlin noise RGB (with 2x contrast
   * enhancement). A band-pass mask is applied on the noise's own alpha channel centered at 0.25.
   * @param size     The noise grain size. Larger values produce coarser grains. Must be positive.
   * @param density  The band width in [0, 1]. Larger values widen the band so more noise pixels
   *                 pass.
   * @param opacity  The overall noise opacity in [0, 1].
   * @param seed     The random seed for the noise pattern.
   */
  static std::shared_ptr<MultiNoiseStyle> MakeMulti(float size, float density, float opacity,
                                                    float seed = 0.0f);

  LayerStyleType Type() const override {
    return LayerStyleType::Noise;
  }

  /**
   * The noise grain size. Larger values produce coarser grains.
   */
  float size() const {
    return _size;
  }

  /**
   * Sets the noise grain size. Must be positive.
   */
  void setSize(float size);

  /**
   * The noise density in [0, 1]. Controls the proportion of visible noise pixels.
   */
  float density() const {
    return _density;
  }

  /**
   * Sets the noise density. Values are clamped to [0, 1].
   */
  void setDensity(float density);

  /**
   * The random seed for the noise pattern.
   */
  float seed() const {
    return _seed;
  }

  /**
   * Sets the random seed.
   */
  void setSeed(float seed);

  LayerStylePosition position() const override {
    return LayerStylePosition::Above;
  }

  Rect filterBounds(const Rect& srcRect, float contentScale) override;

 protected:
  NoiseStyle(float size, float density, float seed);

  void invalidateNoise();

  /**
   * Returns a Perlin noise shader whose origin is at the center of the content. Subclasses apply
   * color filters on top of this shader.
   */
  std::shared_ptr<Shader> getNoiseShader(float contentScale, int contentWidth,
                                         int contentHeight) const;

  float _size = 4.0f;
  float _density = 0.5f;
  float _seed = 0.0f;
};

/**
 * MonoNoiseStyle adds a single-color noise overlay above the layer content.
 */
class MonoNoiseStyle : public NoiseStyle {
 public:
  /**
   * The noise color. The alpha component controls the noise opacity.
   */
  Color color() const {
    return _color;
  }

  /**
   * Sets the noise color.
   */
  void setColor(const Color& color);

 protected:
  void onDraw(Canvas* canvas, std::shared_ptr<Image> content, float contentScale, float alpha,
              BlendMode blendMode) override;

 private:
  MonoNoiseStyle(float size, float density, const Color& color, float seed);

  Color _color = Color::Black();

  friend class NoiseStyle;
};

/**
 * DuoNoiseStyle adds a dual-color noise overlay above the layer content. The noise source is split
 * into two complementary regions, each filled with a different color.
 */
class DuoNoiseStyle : public NoiseStyle {
 public:
  /**
   * The first noise color. The alpha component controls its opacity.
   */
  Color firstColor() const {
    return _firstColor;
  }

  /**
   * Sets the first noise color.
   */
  void setFirstColor(const Color& color);

  /**
   * The second noise color. The alpha component controls its opacity.
   */
  Color secondColor() const {
    return _secondColor;
  }

  /**
   * Sets the second noise color.
   */
  void setSecondColor(const Color& color);

 protected:
  void onDraw(Canvas* canvas, std::shared_ptr<Image> content, float contentScale, float alpha,
              BlendMode blendMode) override;

 private:
  DuoNoiseStyle(float size, float density, const Color& firstColor, const Color& secondColor,
                float seed);

  Color _firstColor = Color::Black();
  Color _secondColor = Color::White();

  friend class NoiseStyle;
};

/**
 * MultiNoiseStyle adds a multi-color noise overlay above the layer content. The original Perlin
 * noise RGB values are preserved with enhanced contrast.
 */
class MultiNoiseStyle : public NoiseStyle {
 public:
  /**
   * The overall noise opacity in [0, 1].
   */
  float opacity() const {
    return _opacity;
  }

  /**
   * Sets the overall noise opacity. Values are clamped to [0, 1].
   */
  void setOpacity(float opacity);

 protected:
  void onDraw(Canvas* canvas, std::shared_ptr<Image> content, float contentScale, float alpha,
              BlendMode blendMode) override;

 private:
  MultiNoiseStyle(float size, float density, float opacity, float seed);

  float _opacity = 0.15f;

  friend class NoiseStyle;
};

}  // namespace tgfx
