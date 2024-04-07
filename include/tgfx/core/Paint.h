/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {
/**
 * Defines enumerations for Paint.setStyle().
 */
enum class PaintStyle {
  /**
   * Set to fill geometry.
   */
  Fill,
  /**
   * Set to stroke geometry.
   */
  Stroke
};

/**
 * Paint controls options applied when drawing.
 */
class Paint {
 public:
  /**
   * Sets all Paint contents to their initial values. This is equivalent to replacing Paint with the
   * result of Paint().
   */
  void reset();

  /**
   * Returns true if pixels on the active edges of Path may be drawn with partial transparency. The
   * default value is true.
   */
  bool isAntiAlias() const {
    return antiAlias;
  }

  /**
   * Requests, but does not require, that edge pixels draw opaque or with partial transparency.
   */
  void setAntiAlias(bool aa) {
    antiAlias = aa;
  }

  /**
   * Returns whether the geometry is filled, stroked, or filled and stroked.
   */
  PaintStyle getStyle() const {
    return style;
  }

  /**
   * Sets whether the geometry is filled, stroked, or filled and stroked.
   */
  void setStyle(PaintStyle newStyle) {
    style = newStyle;
  }

  /**
   * Retrieves alpha and RGB, unpremultiplied, as four floating point values.
   */
  Color getColor() const {
    return color;
  }

  /**
   * Sets alpha and RGB used when stroking and filling. The color is four floating point values,
   * unpremultiplied.
   */
  void setColor(Color newColor) {
    color = newColor;
  }

  /**
   * Retrieves alpha from the color used when stroking and filling.
   */
  float getAlpha() const {
    return color.alpha;
  }

  /**
   * Replaces alpha, leaving RGB unchanged.
   */
  void setAlpha(float newAlpha) {
    color.alpha = newAlpha;
  }

  /**
   * Returns the thickness of the pen used by Paint to outline the shape.
   * @return  zero for hairline, greater than zero for pen thickness
   */
  float getStrokeWidth() const {
    return stroke.width;
  }

  /**
   * Sets the thickness of the pen used by the paint to outline the shape. Has no effect if width is
   * less than zero.
   * @param width  zero thickness for hairline; greater than zero for pen thickness
   */
  void setStrokeWidth(float width) {
    stroke.width = width;
  }

  /**
   * Returns the geometry drawn at the beginning and end of strokes.
   */
  LineCap getLineCap() const {
    return stroke.cap;
  }

  /**
   * Sets the geometry drawn at the beginning and end of strokes.
   */
  void setLineCap(LineCap cap) {
    stroke.cap = cap;
  }

  /**
   * Returns the geometry drawn at the corners of strokes.
   */
  LineJoin getLineJoin() const {
    return stroke.join;
  }

  /**
   * Sets the geometry drawn at the corners of strokes.
   */
  void setLineJoin(LineJoin join) {
    stroke.join = join;
  }

  /**
   * Returns the limit at which a sharp corner is drawn beveled.
   */
  float getMiterLimit() const {
    return stroke.miterLimit;
  }

  /**
   * Sets the limit at which a sharp corner is drawn beveled.
   */
  void setMiterLimit(float limit) {
    stroke.miterLimit = limit;
  }

  /**
   * Returns the stroke options if the paint's style is set to PaintStyle::Stroke.
   */
  const Stroke* getStroke() const {
    return style == PaintStyle::Stroke ? &stroke : nullptr;
  }

  /**
   * Sets the stroke options.
   */
  void setStroke(const Stroke& newStroke) {
    stroke = newStroke;
  }

  /**
   * Returns optional colors used when filling a path if previously set, such as a gradient.
   */
  std::shared_ptr<Shader> getShader() const {
    return shader;
  }

  /**
   * Sets optional colors used when filling a path, such as a gradient. If nullptr, color is used
   * instead. The shader remains unaffected by the canvas matrix and always exists in the coordinate
   * space of the associated surface.
   */
  void setShader(std::shared_ptr<Shader> newShader) {
    shader = std::move(newShader);
  }

  /**
   * Returns the mask filter used to modify the alpha channel of the paint when drawing.
   */
  std::shared_ptr<MaskFilter> getMaskFilter() const {
    return maskFilter;
  }

  /**
   * Sets the mask filter used to modify the alpha channel of the paint when drawing.
   */
  void setMaskFilter(std::shared_ptr<MaskFilter> newMaskFilter) {
    maskFilter = std::move(newMaskFilter);
  }

  /**
   * Returns the color filter used to modify the color of the paint when drawing.
   */
  std::shared_ptr<ColorFilter> getColorFilter() const {
    return colorFilter;
  }

  /**
   * Sets the color filter used to modify the color of the paint when drawing.
   */
  void setColorFilter(std::shared_ptr<ColorFilter> newColorFilter) {
    colorFilter = std::move(newColorFilter);
  }

  /**
   * Returns the image filter used to take the input drawings as an offscreen image and alter them
   * before drawing them back to the destination.
   */
  std::shared_ptr<ImageFilter> getImageFilter() const {
    return imageFilter;
  }

  /**
   * Sets the image filter.
   */
  void setImageFilter(std::shared_ptr<ImageFilter> newImageFilter) {
    imageFilter = std::move(newImageFilter);
  }

  /**
   * Returns the blend mode used to combine the paint with the destination pixels.
   */
  BlendMode getBlendMode() const {
    return blendMode;
  }

  /**
   * Sets the blend mode used to combine the paint with the destination pixels.
   */
  void setBlendMode(BlendMode mode) {
    blendMode = mode;
  }

  /**
   * Returns true if the Paint prevents any drawing.
   */
  bool nothingToDraw() const;

 private:
  bool antiAlias = true;
  PaintStyle style = PaintStyle::Fill;
  Color color = Color::White();
  Stroke stroke = {};
  std::shared_ptr<Shader> shader = nullptr;
  std::shared_ptr<MaskFilter> maskFilter = nullptr;
  std::shared_ptr<ColorFilter> colorFilter = nullptr;
  std::shared_ptr<ImageFilter> imageFilter = nullptr;
  BlendMode blendMode = BlendMode::SrcOver;
};
}  // namespace tgfx
