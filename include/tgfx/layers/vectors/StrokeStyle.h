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

#include <vector>
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/layers/StrokeAlign.h"
#include "tgfx/layers/vectors/ColorSource.h"
#include "tgfx/layers/vectors/VectorElement.h"

namespace tgfx {

/**
 * StrokeStyle applies a stroke to shapes in the same group using a ColorSource.
 */
class StrokeStyle : public VectorElement {
 public:
  StrokeStyle() = default;

  /**
   * Returns the color source used for the stroke.
   */
  std::shared_ptr<ColorSource> colorSource() const {
    return _colorSource;
  }

  /**
   * Sets the color source used for the stroke.
   */
  void setColorSource(std::shared_ptr<ColorSource> value);

  /**
   * Returns the alpha value applied to the stroke. Ranges from 0.0 (fully transparent) to 1.0
   * (fully opaque). The default value is 1.0.
   */
  float alpha() const {
    return _alpha;
  }

  /**
   * Sets the alpha value applied to the stroke.
   */
  void setAlpha(float value);

  /**
   * Returns the blend mode used when drawing the stroke. The default value is BlendMode::SrcOver.
   */
  BlendMode blendMode() const {
    return _blendMode;
  }

  /**
   * Sets the blend mode used when drawing the stroke.
   */
  void setBlendMode(BlendMode value);

  /**
   * Returns the width of the stroke.
   */
  float strokeWidth() const {
    return _stroke.width;
  }

  /**
   * Sets the width of the stroke.
   */
  void setStrokeWidth(float value);

  /**
   * Returns the line cap style for the stroke.
   */
  LineCap lineCap() const {
    return _stroke.cap;
  }

  /**
   * Sets the line cap style for the stroke.
   */
  void setLineCap(LineCap value);

  /**
   * Returns the line join style for the stroke.
   */
  LineJoin lineJoin() const {
    return _stroke.join;
  }

  /**
   * Sets the line join style for the stroke.
   */
  void setLineJoin(LineJoin value);

  /**
   * Returns the miter limit for the stroke.
   */
  float miterLimit() const {
    return _stroke.miterLimit;
  }

  /**
   * Sets the miter limit for the stroke.
   */
  void setMiterLimit(float value);

  /**
   * Returns the dash pattern for the stroke. An empty vector means a solid line.
   */
  const std::vector<float>& dashes() const {
    return _dashes;
  }

  /**
   * Sets the dash pattern for the stroke.
   */
  void setDashes(std::vector<float> value);

  /**
   * Returns the offset into the dash pattern at which to start the dash.
   */
  float dashOffset() const {
    return _dashOffset;
  }

  /**
   * Sets the offset into the dash pattern at which to start the dash.
   */
  void setDashOffset(float value);

  /**
   * Returns the stroke alignment relative to the shape boundary. The default value is
   * StrokeAlign::Center.
   */
  StrokeAlign strokeAlign() const {
    return _strokeAlign;
  }

  /**
   * Sets the stroke alignment relative to the shape boundary.
   */
  void setStrokeAlign(StrokeAlign value);

 protected:
  Type type() const override {
    return Type::StrokeStyle;
  }

  void attachToLayer(Layer* layer) override;

  void detachFromLayer(Layer* layer) override;

  void apply(VectorContext* context) override;

 private:
  std::shared_ptr<ColorSource> _colorSource = nullptr;
  float _alpha = 1.0f;
  BlendMode _blendMode = BlendMode::SrcOver;
  Stroke _stroke = Stroke(1.0f);
  std::vector<float> _dashes = {};
  float _dashOffset = 0.0f;
  StrokeAlign _strokeAlign = StrokeAlign::Center;
  std::shared_ptr<PathEffect> _cachedDashEffect = nullptr;
};

}  // namespace tgfx
