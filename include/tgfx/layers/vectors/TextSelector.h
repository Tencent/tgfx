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

#include <memory>
#include <vector>
#include "tgfx/layers/LayerProperty.h"

namespace tgfx {

/**
 * Defines the unit type for text selector ranges.
 */
enum class SelectorUnit {
  /**
   * The range values are character indices (0 to totalCount).
   */
  Index,
  /**
   * The range values are normalized percentages (0.0 to 1.0).
   */
  Percentage
};

/**
 * Defines how multiple selectors are combined.
 */
enum class SelectorMode {
  /**
   * Add to the current selection.
   */
  Add,
  /**
   * Subtract from the current selection.
   */
  Subtract,
  /**
   * Intersect with the current selection.
   */
  Intersect,
  /**
   * Use the minimum value of the current and new selection.
   */
  Min,
  /**
   * Use the maximum value of the current and new selection.
   */
  Max,
  /**
   * Difference with the current selection.
   */
  Difference
};

/**
 * Defines the shape of the selection range.
 */
enum class SelectorShape {
  /**
   * Square shape: uniform selection within the range.
   */
  Square,
  /**
   * Ramp up shape: selection increases from start to end.
   */
  RampUp,
  /**
   * Ramp down shape: selection decreases from start to end.
   */
  RampDown,
  /**
   * Triangle shape: selection peaks in the middle.
   */
  Triangle,
  /**
   * Round shape: smooth circular falloff.
   */
  Round,
  /**
   * Smooth shape: very smooth falloff using cubic bezier easing.
   */
  Smooth
};

/**
 * TextSelector is the base class for text selection modes.
 */
class TextSelector : public LayerProperty {
 public:
  /**
   * Calculates the combined factor from multiple selectors for a given character.
   * @param selectors The list of selectors to combine.
   * @param index The index of the character.
   * @param totalCount The total number of characters.
   * @return The combined selection factor, clamped to [-1.0, 1.0].
   */
  static float CalculateCombinedFactor(const std::vector<std::shared_ptr<TextSelector>>& selectors,
                                       size_t index, size_t totalCount);

  /**
   * Calculates the selection factor for a given character index.
   * @param index The index of the character.
   * @param totalCount The total number of characters.
   * @return The selection factor for this character.
   */
  virtual float calculateFactor(size_t index, size_t totalCount) = 0;

  /**
   * Returns how this selector combines with previous selectors.
   */
  SelectorMode mode() const {
    return _mode;
  }

  /**
   * Sets how this selector combines with previous selectors.
   */
  void setMode(SelectorMode value);

  /**
   * Returns the weight multiplier for this selector's influence.
   */
  float weight() const {
    return _weight;
  }

  /**
   * Sets the weight multiplier for this selector's influence.
   */
  void setWeight(float value);

 protected:
  TextSelector() = default;

 private:
  SelectorMode _mode = SelectorMode::Add;
  float _weight = 1.0f;

  friend class TextModifier;
};

/**
 * RangeSelector selects characters based on a range with various shape options.
 */
class RangeSelector : public TextSelector {
 public:
  RangeSelector() = default;

  float calculateFactor(size_t index, size_t totalCount) override;

  /**
   * Returns the start of the selection range.
   */
  float start() const {
    return _start;
  }

  /**
   * Sets the start of the selection range.
   */
  void setStart(float value);

  /**
   * Returns the end of the selection range.
   */
  float end() const {
    return _end;
  }

  /**
   * Sets the end of the selection range.
   */
  void setEnd(float value);

  /**
   * Returns the offset applied to the selection range.
   */
  float offset() const {
    return _offset;
  }

  /**
   * Sets the offset applied to the selection range.
   */
  void setOffset(float value);

  /**
   * Returns the unit type for the range values.
   */
  SelectorUnit unit() const {
    return _unit;
  }

  /**
   * Sets the unit type for the range values.
   */
  void setUnit(SelectorUnit value);

  /**
   * Returns the shape of the selection range.
   */
  SelectorShape shape() const {
    return _shape;
  }

  /**
   * Sets the shape of the selection range.
   */
  void setShape(SelectorShape value);

  /**
   * Returns the ease-out value applied when the selection factor approaches 1.0 (0.0 to 1.0).
   */
  float easeOut() const {
    return _easeOut;
  }

  /**
   * Sets the ease-out value applied when the selection factor approaches 1.0.
   */
  void setEaseOut(float value);

  /**
   * Returns the ease-in value applied when the selection factor starts from 0.0 (0.0 to 1.0).
   */
  float easeIn() const {
    return _easeIn;
  }

  /**
   * Sets the ease-in value applied when the selection factor starts from 0.0.
   */
  void setEaseIn(float value);

  /**
   * Returns whether to randomize the order of character selection.
   */
  bool randomOrder() const {
    return _randomOrder;
  }

  /**
   * Sets whether to randomize the order of character selection.
   */
  void setRandomOrder(bool value);

  /**
   * Returns the random seed used when randomOrder is true.
   */
  uint16_t randomSeed() const {
    return _randomSeed;
  }

  /**
   * Sets the random seed used when randomOrder is true.
   */
  void setRandomSeed(uint16_t value);

 private:
  float _start = 0.0f;
  float _end = 1.0f;
  float _offset = 0.0f;
  SelectorUnit _unit = SelectorUnit::Percentage;
  SelectorShape _shape = SelectorShape::Square;
  float _easeOut = 0.0f;
  float _easeIn = 0.0f;
  bool _randomOrder = false;
  uint16_t _randomSeed = 0;

  std::vector<size_t> _randomIndicesCache = {};
};

}  // namespace tgfx
