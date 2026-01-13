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
 * Defines the type of text selector.
 */
enum class TextSelectorType {
  /**
   * Range selector: selects characters based on a range.
   */
  Range,
  /**
   * Wiggly selector: adds randomness to the selection.
   */
  Wiggly
};

/**
 * Defines the unit type for text selector ranges.
 */
enum class SelectorUnit {
  /**
   * The range values are character indices.
   */
  Index,
  /**
   * The range values are percentages (0.0-1.0).
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
 * Defines what the selector is based on.
 */
enum class SelectorBasis {
  /**
   * Selection is based on individual characters.
   */
  Characters,
  /**
   * Selection is based on characters excluding spaces.
   */
  CharactersExcludingSpaces,
  /**
   * Selection is based on words.
   */
  Words,
  /**
   * Selection is based on lines.
   */
  Lines
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
   * Smooth shape: very smooth falloff using smoothstep.
   */
  Smooth
};

/**
 * Defines the dimension for wiggly selector.
 */
enum class WigglyDimension {
  /**
   * Wiggle affects both dimensions.
   */
  Both,
  /**
   * Wiggle affects only the first dimension (e.g., x or start).
   */
  First,
  /**
   * Wiggle affects only the second dimension (e.g., y or end).
   */
  Second
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
   * Returns the type of this text selector.
   */
  virtual TextSelectorType type() const = 0;

  /**
   * Calculates the selection factor for a given character index.
   * @param index The index of the character.
   * @param totalCount The total number of characters.
   * @return The selection factor for this character.
   */
  virtual float calculateFactor(size_t index, size_t totalCount) const = 0;

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
   * Returns the amount of influence this selector has (0.0 to 1.0).
   */
  float amount() const {
    return _amount;
  }

  /**
   * Sets the amount of influence this selector has.
   */
  void setAmount(float value);

 protected:
  TextSelector() = default;

 private:
  SelectorMode _mode = SelectorMode::Add;
  float _amount = 1.0f;

  friend class TextModifier;
};

/**
 * RangeSelector selects characters based on a range with various shape options.
 */
class RangeSelector : public TextSelector {
 public:
  RangeSelector() = default;

  TextSelectorType type() const override {
    return TextSelectorType::Range;
  }

  float calculateFactor(size_t index, size_t totalCount) const override;

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
   * Returns what the selector is based on.
   */
  SelectorBasis basedOn() const {
    return _basedOn;
  }

  /**
   * Sets what the selector is based on.
   */
  void setBasedOn(SelectorBasis value);

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
   * Returns the smoothness of the selection edges (0.0 to 1.0).
   */
  float smoothness() const {
    return _smoothness;
  }

  /**
   * Sets the smoothness of the selection edges.
   */
  void setSmoothness(float value);

  /**
   * Returns the ease applied to the high end of the selection (0.0 to 1.0).
   */
  float easeHigh() const {
    return _easeHigh;
  }

  /**
   * Sets the ease applied to the high end of the selection.
   */
  void setEaseHigh(float value);

  /**
   * Returns the ease applied to the low end of the selection (0.0 to 1.0).
   */
  float easeLow() const {
    return _easeLow;
  }

  /**
   * Sets the ease applied to the low end of the selection.
   */
  void setEaseLow(float value);

  /**
   * Returns whether to randomize the order of character selection.
   */
  bool randomizeOrder() const {
    return _randomizeOrder;
  }

  /**
   * Sets whether to randomize the order of character selection.
   */
  void setRandomizeOrder(bool value);

  /**
   * Returns the random seed used when randomizeOrder is true.
   */
  uint32_t randomSeed() const {
    return _randomSeed;
  }

  /**
   * Sets the random seed used when randomizeOrder is true.
   */
  void setRandomSeed(uint32_t value);

 private:
  float _start = 0.0f;
  float _end = 1.0f;
  float _offset = 0.0f;
  SelectorUnit _unit = SelectorUnit::Percentage;
  SelectorBasis _basedOn = SelectorBasis::Characters;
  SelectorShape _shape = SelectorShape::Square;
  float _smoothness = 1.0f;
  float _easeHigh = 0.0f;
  float _easeLow = 0.0f;
  bool _randomizeOrder = false;
  uint32_t _randomSeed = 0;
};

/**
 * WigglySelector adds randomness/wiggle to the selection values.
 */
class WigglySelector : public TextSelector {
 public:
  WigglySelector() = default;

  TextSelectorType type() const override {
    return TextSelectorType::Wiggly;
  }

  float calculateFactor(size_t index, size_t totalCount) const override;

  /**
   * Returns the maximum deviation for the wiggle effect (0.0 to 1.0).
   */
  float maxAmount() const {
    return _maxAmount;
  }

  /**
   * Sets the maximum deviation for the wiggle effect.
   */
  void setMaxAmount(float value);

  /**
   * Returns the minimum deviation for the wiggle effect (0.0 to 1.0).
   */
  float minAmount() const {
    return _minAmount;
  }

  /**
   * Sets the minimum deviation for the wiggle effect.
   */
  void setMinAmount(float value);

  /**
   * Returns what the selector is based on.
   */
  SelectorBasis basedOn() const {
    return _basedOn;
  }

  /**
   * Sets what the selector is based on.
   */
  void setBasedOn(SelectorBasis value);

  /**
   * Returns the number of wiggles per second.
   */
  float wigglesPerSecond() const {
    return _wigglesPerSecond;
  }

  /**
   * Sets the number of wiggles per second.
   */
  void setWigglesPerSecond(float value);

  /**
   * Returns the correlation between adjacent characters (0.0 to 1.0). Higher values mean adjacent
   * characters move more similarly.
   */
  float correlation() const {
    return _correlation;
  }

  /**
   * Sets the correlation between adjacent characters.
   */
  void setCorrelation(float value);

  /**
   * Returns the temporal phase offset in degrees.
   */
  float temporalPhase() const {
    return _temporalPhase;
  }

  /**
   * Sets the temporal phase offset in degrees.
   */
  void setTemporalPhase(float value);

  /**
   * Returns the spatial phase offset in degrees.
   */
  float spatialPhase() const {
    return _spatialPhase;
  }

  /**
   * Sets the spatial phase offset in degrees.
   */
  void setSpatialPhase(float value);

  /**
   * Returns whether to lock the random dimensions together.
   */
  bool lockDimensions() const {
    return _lockDimensions;
  }

  /**
   * Sets whether to lock the random dimensions together.
   */
  void setLockDimensions(bool value);

  /**
   * Returns which dimensions the wiggle affects.
   */
  WigglyDimension dimension() const {
    return _dimension;
  }

  /**
   * Sets which dimensions the wiggle affects.
   */
  void setDimension(WigglyDimension value);

  /**
   * Returns the random seed for the wiggle effect.
   */
  uint32_t randomSeed() const {
    return _randomSeed;
  }

  /**
   * Sets the random seed for the wiggle effect.
   */
  void setRandomSeed(uint32_t value);

 private:
  float _maxAmount = 1.0f;
  float _minAmount = 0.0f;
  SelectorBasis _basedOn = SelectorBasis::Characters;
  float _wigglesPerSecond = 2.0f;
  float _correlation = 0.5f;
  float _temporalPhase = 0.0f;
  float _spatialPhase = 0.0f;
  bool _lockDimensions = false;
  WigglyDimension _dimension = WigglyDimension::Both;
  uint32_t _randomSeed = 0;
};

}  // namespace tgfx
