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

#include "tgfx/core/Path.h"
#include "tgfx/layers/TextAlign.h"
#include "tgfx/layers/vectors/VectorElement.h"

namespace tgfx {

/**
 * TextPath applies path-based layout to accumulated glyphs in the VectorContext. When applied, it
 * repositions glyphs along the specified path curve.
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
   * Returns the text alignment within the available path region (path length minus margins).
   * TextAlign::Start means text starts from the beginning, TextAlign::Center means text is centered,
   * TextAlign::End means text ends at the end, and TextAlign::Justify means letter spacing is
   * adjusted to fit text within the available region.
   */
  TextAlign textAlign() const {
    return _textAlign;
  }

  /**
   * Sets the text alignment within the available path region.
   */
  void setTextAlign(TextAlign value);

  /**
   * Returns the margin from the path start in pixels. Positive values offset text forward along
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
   * Returns whether characters stand perpendicular to the path. When true, characters rotate to
   * follow the path direction. When false, characters remain upright regardless of path direction.
   */
  bool perpendicular() const {
    return _perpendicular;
  }

  /**
   * Sets whether characters stand perpendicular to the path.
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

 protected:
  Type type() const override {
    return Type::TextPath;
  }

  void apply(VectorContext* context) override;

 protected:
  TextPath() = default;

 private:
  Path _path = {};
  TextAlign _textAlign = TextAlign::Start;
  float _firstMargin = 0.0f;
  float _lastMargin = 0.0f;
  bool _perpendicular = true;
  bool _reversed = false;
};

}  // namespace tgfx
