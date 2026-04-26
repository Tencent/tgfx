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
#include "tgfx/layers/filters/LayerFilter.h"

namespace tgfx {

class MonoNoiseFilter;
class DuoNoiseFilter;
class MultiNoiseFilter;

/**
 * NoiseFilter overlays procedural Perlin noise on its input image using the specified blend mode.
 * All three modes (Mono / Duo / Multi) apply a band-pass mask on the noise signal centered at
 * luma (or noise alpha for Multi) = 0.25, with the `density` parameter controlling the band width.
 * The mask is then filled with a color (Mono), two mirrored colors (Duo), or the noise's own
 * contrast-enhanced RGB (Multi). Higher density -> wider band -> more noise pixels painted.
 *
 * The noise pattern is anchored at the center of the content region, so it stays stable when the
 * layer is resized or replayed from a picture. Only pixels with non-zero alpha in the source
 * image are affected.
 */
class NoiseFilter : public LayerFilter {
 public:
  /**
   * Creates a single-color noise filter. A band-pass mask is applied on the luminance of the noise
   * centered at 0.25, with width controlled by density. Matching pixels are filled with color.
   * @param size      The noise grain size. Larger values produce coarser grains. Must be positive.
   * @param density   The band width in [0, 1]. Larger values widen the band so more noise pixels
   *                  pass. UI-driven clients that need a non-linear density curve (for example to
   *                  match a specific design tool) should apply the mapping at their own layer.
   * @param color     The noise color. The alpha component controls the noise opacity.
   * @param seed      The random seed for the noise pattern.
   * @param blendMode The blend mode used to composite the noise with the source image.
   */
  static std::shared_ptr<MonoNoiseFilter> MakeMono(float size, float density, const Color& color,
                                                   float seed = 0.0f,
                                                   BlendMode blendMode = BlendMode::SrcOver);

  /**
   * Creates a dual-color noise filter. Two mirrored band-pass masks are applied on the luminance
   * of the noise: one centered at 0.25 filled with firstColor, one centered at 0.75 filled with
   * secondColor. Both bands share the same width controlled by density.
   * @param size         The noise grain size. Larger values produce coarser grains. Must be
   *                     positive.
   * @param density      The band width in [0, 1]. Larger values widen both bands so more noise
   *                     pixels pass.
   * @param firstColor   The color filled into the first band (luma around 0.25). The alpha
   *                     component controls its opacity.
   * @param secondColor  The color filled into the second band (luma around 0.75). The alpha
   *                     component controls its opacity.
   * @param seed         The random seed for the noise pattern.
   * @param blendMode    The blend mode used to composite the noise with the source image.
   */
  static std::shared_ptr<DuoNoiseFilter> MakeDuo(float size, float density, const Color& firstColor,
                                                 const Color& secondColor, float seed = 0.0f,
                                                 BlendMode blendMode = BlendMode::SrcOver);

  /**
   * Creates a multi-color noise filter that preserves the Perlin noise RGB (with a 2x contrast
   * enhancement). A band-pass mask is applied on the noise's own alpha channel centered at 0.25.
   * @param size      The noise grain size. Larger values produce coarser grains. Must be positive.
   * @param density   The band width in [0, 1]. Larger values widen the band so more noise pixels
   *                  pass.
   * @param opacity   The overall noise opacity in [0, 1].
   * @param seed      The random seed for the noise pattern.
   * @param blendMode The blend mode used to composite the noise with the source image.
   */
  static std::shared_ptr<MultiNoiseFilter> MakeMulti(float size, float density, float opacity,
                                                     float seed = 0.0f,
                                                     BlendMode blendMode = BlendMode::SrcOver);

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
   * The noise density in [0, 1].
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

  /**
   * The blend mode used to composite the noise with the source image.
   */
  BlendMode blendMode() const {
    return _blendMode;
  }

  /**
   * Sets the blend mode.
   */
  void setBlendMode(BlendMode blendMode);

 protected:
  NoiseFilter(float size, float density, float seed, BlendMode blendMode);

  Type type() const override {
    return Type::NoiseFilter;
  }

  std::shared_ptr<Image> onFilterImage(std::shared_ptr<Image> input, const Rect& contentBounds,
                                       float scale, Point* offset) override;

  /**
   * Builds the colored noise shader used by this filter, already anchored at the content center.
   * The shader is composited with the input image via the filter's blend mode and clipped to the
   * input image alpha.
   */
  virtual std::shared_ptr<Shader> onBuildNoiseShader(float scale) const = 0;

  float _size = 4.0f;
  float _density = 0.5f;
  float _seed = 0.0f;
  BlendMode _blendMode = BlendMode::SrcOver;
};

/**
 * MonoNoiseFilter adds a single-color noise overlay to the input image.
 */
class MonoNoiseFilter : public NoiseFilter {
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
  std::shared_ptr<Shader> onBuildNoiseShader(float scale) const override;

 private:
  MonoNoiseFilter(float size, float density, const Color& color, float seed, BlendMode blendMode);

  Color _color = Color::Black();

  friend class NoiseFilter;
};

/**
 * DuoNoiseFilter adds a dual-color noise overlay to the input image. The noise source is split into
 * two complementary regions, each filled with a different color.
 */
class DuoNoiseFilter : public NoiseFilter {
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
  std::shared_ptr<Shader> onBuildNoiseShader(float scale) const override;

 private:
  DuoNoiseFilter(float size, float density, const Color& firstColor, const Color& secondColor,
                 float seed, BlendMode blendMode);

  Color _firstColor = Color::Black();
  Color _secondColor = Color::White();

  friend class NoiseFilter;
};

/**
 * MultiNoiseFilter adds a multi-color noise overlay to the input image. The original Perlin noise
 * RGB values are preserved with enhanced contrast.
 */
class MultiNoiseFilter : public NoiseFilter {
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
  std::shared_ptr<Shader> onBuildNoiseShader(float scale) const override;

 private:
  MultiNoiseFilter(float size, float density, float opacity, float seed, BlendMode blendMode);

  float _opacity = 0.15f;

  friend class NoiseFilter;
};

}  // namespace tgfx
