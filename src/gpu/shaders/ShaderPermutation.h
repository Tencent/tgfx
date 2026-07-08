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

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace tgfx {

/**
 * A boolean permutation dimension. Always has exactly 2 possible values (0 or 1).
 */
class PermutationBool {
 public:
  constexpr explicit PermutationBool(const char* defineName) : defineName(defineName) {
  }

  static constexpr int valueCount() {
    return 2;
  }

  const char* defineName;
};

/**
 * An enumeration permutation dimension. The number of possible values equals the number of
 * value names provided.
 */
class PermutationEnum {
 public:
  PermutationEnum(const char* defineName, std::initializer_list<const char*> valueNames)
      : defineName(defineName), valueNames(valueNames) {
  }

  int valueCount() const {
    return static_cast<int>(valueNames.size());
  }

  const char* defineName;
  std::vector<const char*> valueNames;
};

/**
 * An integer permutation dimension with values in the range [0, count).
 */
class PermutationInt {
 public:
  constexpr PermutationInt(const char* defineName, int count)
      : defineName(defineName), count(count) {
  }

  int valueCount() const {
    return count;
  }

  const char* defineName;
  int count;
};

using PermutationDimension = std::variant<PermutationBool, PermutationEnum, PermutationInt>;

/**
 * Represents the full permutation space formed by a list of dimensions. Provides encoding and
 * decoding between a flat variant index and per-dimension value arrays using mixed-radix
 * LSB-first layout.
 */
class PermutationDomain {
 public:
  explicit PermutationDomain(std::vector<PermutationDimension> dimensions);

  /**
   * Creates a domain consisting entirely of boolean dimensions parsed from a comma-separated
   * string of define names (e.g. "HAS_YUV, ALPHA_ONLY").
   */
  static PermutationDomain FromBoolNames(const char* commaSeparatedNames);

  uint32_t totalCount() const;

  size_t dimensionCount() const {
    return dimensions.size();
  }

  /**
   * Decodes a flat variant index into per-dimension values using mixed-radix decomposition.
   */
  std::vector<int> decode(uint32_t index) const;

  /**
   * Encodes per-dimension values into a flat variant index. The size of values must equal
   * dimensionCount().
   */
  uint32_t encode(const std::vector<int>& values) const;

  /**
   * Returns a list of "DEFINE_NAME=value" strings for the given variant index. Every dimension
   * is always included, even when the value is 0.
   */
  std::vector<std::string> defineListFor(uint32_t index) const;

  const std::vector<PermutationDimension>& getDimensions() const {
    return dimensions;
  }

 private:
  std::vector<PermutationDimension> dimensions;
};

/**
 * Declares a nested Dims struct containing an enum of dimension indices and a static domain()
 * method. Usage:
 *   TGFX_DEFINE_DIMS(HAS_YUV, ALPHA_ONLY, HAS_RGBAAA)
 * Expands to enum values HAS_YUV=0, ALPHA_ONLY=1, HAS_RGBAAA=2, COUNT=3 and a domain()
 * returning the corresponding PermutationDomain.
 */
#define TGFX_DEFINE_DIMS(...)                                \
  struct Dims {                                              \
    enum : uint32_t { __VA_ARGS__, COUNT };                  \
    static PermutationDomain domain() {                      \
      return PermutationDomain::FromBoolNames(#__VA_ARGS__); \
    }                                                        \
  }

}  // namespace tgfx
