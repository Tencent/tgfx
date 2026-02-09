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
#include "tgfx/core/Path.h"
#include "tgfx/layers/vectors/VectorElement.h"

namespace tgfx {

/**
 * TextPath applies path-based layout to accumulated glyphs in the VectorContext. When applied, it
 * repositions glyphs along the specified path curve, preserving their relative positions from the
 * original text layout (kerning, baseline shift, etc.).
 */
class TextPath : public VectorElement {
 public:
  /**
   * Creates a new TextPath instance.
   */
  static std::shared_ptr<TextPath> Make();
  /**
   * Returns the path that text follows.
   */
  const Path& path() const {
    return _path;
  }

  /**
   * Sets the path that text follows.
   */
  void setPath(Path value);

  /**
   * Returns the baseline reference origin as an offset relative to the path start point. Together
   * with baselineAngle, it defines the baseline reference line used for glyph projection. Each
   * glyph's position on the path is determined by projecting the distance between its anchor point
   * and this origin onto the baseline direction. Default is (0, 0), which means the path start
   * point serves as the baseline reference.
   */
  Point baselineOrigin() const {
    return _baselineOrigin;
  }

  /**
   * Sets the baseline reference origin.
   */
  void setBaselineOrigin(Point value);

  /**
   * Returns the angle in degrees of the baseline reference line. Together with baselineOrigin, it
   * defines the baseline reference line in the TextPath's local coordinate space. 0 means horizontal
   * text (baseline along X axis), 90 means vertical text (baseline along Y axis).
   */
  float baselineAngle() const {
    return _baselineAngle;
  }

  /**
   * Sets the angle of the baseline reference line.
   */
  void setBaselineAngle(float value);

  /**
   * Returns the margin from the path start in pixels. Positive values offset glyphs forward along
   * the path.
   */
  float firstMargin() const {
    return _firstMargin;
  }

  /**
   * Sets the margin from the path start.
   */
  void setFirstMargin(float value);

  /**
   * Returns the margin from the path end in pixels. Negative values shrink the available region
   * from the end, while positive values extend beyond the path end.
   */
  float lastMargin() const {
    return _lastMargin;
  }

  /**
   * Sets the margin from the path end.
   */
  void setLastMargin(float value);

  /**
   * Returns whether glyphs stand perpendicular to the path. When true, glyphs rotate to follow the
   * path direction. When false, glyphs remain upright regardless of path direction.
   */
  bool perpendicular() const {
    return _perpendicular;
  }

  /**
   * Sets whether glyphs stand perpendicular to the path.
   */
  void setPerpendicular(bool value);

  /**
   * Returns whether the path direction is reversed.
   */
  bool reversed() const {
    return _reversed;
  }

  /**
   * Sets whether the path direction is reversed.
   */
  void setReversed(bool value);

  /**
   * Returns whether text is stretched to fit the available path length. When enabled, glyphs are
   * laid out consecutively using their advance widths, then spacing is adjusted proportionally to
   * fill the path region between firstMargin and lastMargin.
   */
  bool forceAlignment() const {
    return _forceAlignment;
  }

  /**
   * Sets whether text is stretched to fit the available path length.
   */
  void setForceAlignment(bool value);

 protected:
  Type type() const override {
    return Type::TextPath;
  }

  void apply(VectorContext* context) override;

  TextPath() = default;

 private:
  Path _path = {};
  Point _baselineOrigin = Point::Zero();
  float _firstMargin = 0.0f;
  float _lastMargin = 0.0f;
  bool _perpendicular = true;
  bool _reversed = false;
  float _baselineAngle = 0.0f;
  bool _forceAlignment = false;
};

}  // namespace tgfx
