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

#include "tgfx/layers/vectors/TextSelector.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>

namespace tgfx {

// ==================== Helper Functions ====================

static float OverlayFactor(float oldFactor, float factor, SelectorMode mode) {
  switch (mode) {
    case SelectorMode::Add:
      return oldFactor + factor;
    case SelectorMode::Subtract:
      return factor >= 0.0f ? oldFactor * (1.0f - factor) : oldFactor * (-1.0f - factor);
    case SelectorMode::Intersect:
      return oldFactor * factor;
    case SelectorMode::Min:
      return std::min(oldFactor, factor);
    case SelectorMode::Max:
      return std::max(oldFactor, factor);
    case SelectorMode::Difference:
      return std::abs(oldFactor - factor);
  }
  return factor;
}

// Square shape factor calculation
static float CalculateSquareFactor(float textStart, float textEnd, float rangeStart,
                                   float rangeEnd) {
  if (textStart >= rangeEnd || textEnd <= rangeStart) {
    return 0.0f;
  }
  auto textRange = textEnd - textStart;
  if (textStart < rangeStart) {
    textStart = rangeStart;
  }
  if (textEnd > rangeEnd) {
    textEnd = rangeEnd;
  }
  return (textEnd - textStart) / textRange;
}

// Ramp up shape factor calculation
static float CalculateRampUpFactor(float textStart, float textEnd, float rangeStart,
                                   float rangeEnd) {
  auto textCenter = (textStart + textEnd) * 0.5f;
  if (textCenter <= rangeStart) {
    return 0.0f;
  }
  if (textCenter >= rangeEnd) {
    return 1.0f;
  }
  return (textCenter - rangeStart) / (rangeEnd - rangeStart);
}

// Ramp down shape factor calculation
static float CalculateRampDownFactor(float textStart, float textEnd, float rangeStart,
                                     float rangeEnd) {
  auto textCenter = (textStart + textEnd) * 0.5f;
  if (textCenter <= rangeStart) {
    return 1.0f;
  }
  if (textCenter >= rangeEnd) {
    return 0.0f;
  }
  return (rangeEnd - textCenter) / (rangeEnd - rangeStart);
}

// Triangle shape factor calculation
static float CalculateTriangleFactor(float textStart, float textEnd, float rangeStart,
                                     float rangeEnd) {
  auto textCenter = (textStart + textEnd) * 0.5f;
  auto rangeCenter = (rangeStart + rangeEnd) * 0.5f;

  if (textCenter < rangeStart || textCenter > rangeEnd) {
    return 0.0f;
  }

  if (textCenter <= rangeCenter) {
    return (textCenter - rangeStart) / (rangeCenter - rangeStart);
  } else {
    return (rangeEnd - textCenter) / (rangeEnd - rangeCenter);
  }
}

// Round shape factor calculation (circular falloff)
static float CalculateRoundFactor(float textStart, float textEnd, float rangeStart,
                                  float rangeEnd) {
  auto textCenter = (textStart + textEnd) * 0.5f;
  if (textCenter >= rangeEnd || textCenter <= rangeStart) {
    return 0.0f;
  }
  auto rangeCenter = (rangeStart + rangeEnd) * 0.5f;
  auto radius = (rangeEnd - rangeStart) * 0.5f;
  auto xWidth = rangeCenter - textCenter;
  auto yHeight = std::sqrt(radius * radius - xWidth * xWidth);
  return yHeight / radius;
}

// Smooth shape factor calculation (smoothstep-like curve)
static float CalculateSmoothFactor(float textStart, float textEnd, float rangeStart,
                                   float rangeEnd) {
  auto textCenter = (textStart + textEnd) * 0.5f;
  if (textCenter >= rangeEnd || textCenter <= rangeStart) {
    return 0.0f;
  }
  auto rangeCenter = (rangeStart + rangeEnd) * 0.5f;
  float x = 0.0f;
  if (textCenter < rangeCenter) {
    x = (textCenter - rangeStart) / (rangeCenter - rangeStart);
  } else {
    x = (rangeEnd - textCenter) / (rangeEnd - rangeCenter);
  }
  // Smoothstep: 3x^2 - 2x^3
  return x * x * (3.0f - 2.0f * x);
}

// Generate randomized index for a given seed and total count
static size_t GetRandomizedIndex(size_t index, size_t totalCount, uint32_t seed) {
  // Generate a deterministic random permutation
  std::vector<size_t> indices(totalCount);
  std::iota(indices.begin(), indices.end(), 0);

  // Simple Fisher-Yates shuffle with the given seed
  std::srand(seed);
  for (size_t i = totalCount - 1; i > 0; --i) {
    size_t j = static_cast<size_t>(std::rand()) % (i + 1);
    std::swap(indices[i], indices[j]);
  }

  return indices[index];
}

// ==================== TextSelector ====================

float TextSelector::CalculateCombinedFactor(
    const std::vector<std::shared_ptr<TextSelector>>& selectors, size_t index, size_t totalCount) {
  if (selectors.empty()) {
    return 1.0f;
  }

  float totalFactor = 0.0f;
  bool isFirst = true;

  for (const auto& selector : selectors) {
    if (selector == nullptr) {
      continue;
    }
    float factor = selector->calculateFactor(index, totalCount);

    if (isFirst && selector->mode() != SelectorMode::Subtract) {
      totalFactor = factor;
    } else {
      totalFactor = OverlayFactor(totalFactor, factor, selector->mode());
    }
    isFirst = false;
  }

  return std::clamp(totalFactor, -1.0f, 1.0f);
}

void TextSelector::setMode(SelectorMode value) {
  if (_mode == value) {
    return;
  }
  _mode = value;
  invalidateContent();
}

void TextSelector::setAmount(float value) {
  if (_amount == value) {
    return;
  }
  _amount = value;
  invalidateContent();
}

// ==================== RangeSelector ====================

void RangeSelector::setStart(float value) {
  if (_start == value) {
    return;
  }
  _start = value;
  invalidateContent();
}

void RangeSelector::setEnd(float value) {
  if (_end == value) {
    return;
  }
  _end = value;
  invalidateContent();
}

void RangeSelector::setOffset(float value) {
  if (_offset == value) {
    return;
  }
  _offset = value;
  invalidateContent();
}

void RangeSelector::setUnit(SelectorUnit value) {
  if (_unit == value) {
    return;
  }
  _unit = value;
  invalidateContent();
}

void RangeSelector::setBasedOn(SelectorBasis value) {
  if (_basedOn == value) {
    return;
  }
  _basedOn = value;
  invalidateContent();
}

void RangeSelector::setShape(SelectorShape value) {
  if (_shape == value) {
    return;
  }
  _shape = value;
  invalidateContent();
}

void RangeSelector::setSmoothness(float value) {
  if (_smoothness == value) {
    return;
  }
  _smoothness = value;
  invalidateContent();
}

void RangeSelector::setEaseHigh(float value) {
  if (_easeHigh == value) {
    return;
  }
  _easeHigh = value;
  invalidateContent();
}

void RangeSelector::setEaseLow(float value) {
  if (_easeLow == value) {
    return;
  }
  _easeLow = value;
  invalidateContent();
}

void RangeSelector::setRandomizeOrder(bool value) {
  if (_randomizeOrder == value) {
    return;
  }
  _randomizeOrder = value;
  invalidateContent();
}

void RangeSelector::setRandomSeed(uint32_t value) {
  if (_randomSeed == value) {
    return;
  }
  _randomSeed = value;
  invalidateContent();
}

// ==================== WigglySelector ====================

void WigglySelector::setMaxAmount(float value) {
  if (_maxAmount == value) {
    return;
  }
  _maxAmount = value;
  invalidateContent();
}

void WigglySelector::setMinAmount(float value) {
  if (_minAmount == value) {
    return;
  }
  _minAmount = value;
  invalidateContent();
}

void WigglySelector::setBasedOn(SelectorBasis value) {
  if (_basedOn == value) {
    return;
  }
  _basedOn = value;
  invalidateContent();
}

void WigglySelector::setWigglesPerSecond(float value) {
  if (_wigglesPerSecond == value) {
    return;
  }
  _wigglesPerSecond = value;
  invalidateContent();
}

void WigglySelector::setCorrelation(float value) {
  if (_correlation == value) {
    return;
  }
  _correlation = value;
  invalidateContent();
}

void WigglySelector::setTemporalPhase(float value) {
  if (_temporalPhase == value) {
    return;
  }
  _temporalPhase = value;
  invalidateContent();
}

void WigglySelector::setSpatialPhase(float value) {
  if (_spatialPhase == value) {
    return;
  }
  _spatialPhase = value;
  invalidateContent();
}

void WigglySelector::setLockDimensions(bool value) {
  if (_lockDimensions == value) {
    return;
  }
  _lockDimensions = value;
  invalidateContent();
}

void WigglySelector::setDimension(WigglyDimension value) {
  if (_dimension == value) {
    return;
  }
  _dimension = value;
  invalidateContent();
}

void WigglySelector::setRandomSeed(uint32_t value) {
  if (_randomSeed == value) {
    return;
  }
  _randomSeed = value;
  invalidateContent();
}

}  // namespace tgfx
