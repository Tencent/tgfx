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

#include "tgfx/core/Size.h"
#include "tgfx/svg/SVGTypes.h"

namespace tgfx {

/**
 * SVGLengthContext is a utility class for converting SVGLength values to pixel values.
 * An SVGLength represents a numeric value paired with a unit. This class provides the context 
 * needed to resolve these lengths, taking into account factors such as viewport size, DPI, and
 * object bounding box units. It supports resolving various length types, including percentages,
 * absolute units, centimeters, and more.
 */
class SVGLengthContext {
 public:
  /**
   * Construct a new SVGLengthContext object
   */
  explicit SVGLengthContext(const Size& viewport, float dpi = 90) : _viewPort(viewport), dpi(dpi) {
  }
  /**
   * When resolving a length, you need to specify its type. For example, width is a horizontal type,
   * height is a vertical type, and lengths such as stroke width that are not direction-specific use
   * the other type.
   */
  enum class LengthType {
    Horizontal,
    Vertical,
    Other,
  };

  /**
   *  Returns the viewport size.
   */
  const Size& viewPort() const {
    return _viewPort;
  }

  /**
   * Set the view port size
   */
  void setViewPort(const Size& viewPort) {
    _viewPort = viewPort;
  }

  /**
   *  Resolves an SVGLength to a float value using the current context.
   */
  float resolve(const SVGLength& length, LengthType type) const;

  /**
   *  Resolves a rectangle using the given SVGLength values for x, y, width, and height.
   */
  Rect resolveRect(const SVGLength& x, const SVGLength& y, const SVGLength& w,
                   const SVGLength& h) const;

  /**
   * Resolves radii for an ellipse or rounded rectangle.
   */
  std::tuple<float, float> resolveOptionalRadii(const std::optional<SVGLength>& optionalRx,
                                                const std::optional<SVGLength>& optionalRy) const;

  /**
   * Sets the bounding box units. This means a value of 1 represents the full length or width.
   */
  void setBoundingBoxUnits(SVGObjectBoundingBoxUnits inputUnit) {
    unit = inputUnit;
  }

  /**
   * Clears the bounding box units.
   */
  void clearBoundingBoxUnits() {
    unit.reset();
  }

  /**
   * Returns the current bounding box units, if set. If not set, returns std::nullopt.
   */
  std::optional<SVGObjectBoundingBoxUnits> getBoundingBoxUnits() const {
    return unit;
  }

 private:
  Size _viewPort;
  float dpi;
  std::optional<SVGObjectBoundingBoxUnits> unit;
};

}  // namespace tgfx
