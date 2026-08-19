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
 * Binding-signature vocabulary for precompiled shader variants.
 *
 * A degree of freedom becomes a compile-time permutation dimension if and only if it changes GPU
 * resource binding: the number or type of sampler bindings, the attachment configuration, or the
 * varying/attribute layout. Everything else must be a runtime uniform. "The generated code would
 * contain an if" is not a justification — branches over uniform data are coherent and free.
 *
 * Canonical axes (new kernels may only use these):
 *
 *   Binding axes:
 *     TEXTURE_COUNT  (0/1/2/4)            each texture = one sampler binding + one coord varying
 *     TEXTURE_KIND   (TwoD/Rect/YUV)      sampler type; YUV binds three planes
 *     DST_KIND       (None/Texture/FBF)   dst-texture binding vs framebuffer-fetch attachment
 *     MASK_KIND      (None/DeviceTexture) a device mask is one sampler binding
 *
 *   Interface axes (varying/attribute layout):
 *     COVERAGE_KIND  (None/Vertex/LocalMask)
 *     COLOR_KIND     (Uniform/Vertex)
 *     GP_INTERFACE   (per-GP interface shape)
 *
 *   Output axis (reserved):
 *     OUTPUT_KIND    (RGBA/AAAA)          currently the OutputAlphaSwizzle runtime uniform; the
 *                                         name is reserved in case a swizzle ever needs compile
 *                                         time handling
 *
 * These must never become dimensions (all are runtime uniforms with production precedent):
 *   - subset rects        -> vec4 Subset uniform + unconditional clamp, full-bounds default
 *                            (QuadTextureFillShader)
 *   - tile modes          -> ShaderModeX/Y uniforms (TiledTextureFillShader)
 *   - blend modes/roles   -> runtime uniforms on the pointwise chain kernel
 *   - pointwise op kinds  -> OpType uniforms (PointwiseChainShader)
 *   - alpha-only/RGBAAA   -> AlphaOnly uniform; swizzles are data, not bindings
 *
 * Naming: cardinalities are *_COUNT, type selectors are *_KIND. Naming a dimension after a
 * processor or effect (a structural name) is prohibited for new kernels; legacy HAS_* dimensions
 * stay until their kernel is otherwise modified, then they align to this vocabulary.
 *
 * Admission test for any proposed new dimension, in order:
 *   1. Does it change the number or type of sampler bindings?        -> dimension
 *   2. Does it change the attachment configuration?                  -> dimension
 *   3. Does it change the varying/attribute layout?                  -> dimension
 *   4. None of the above                                             -> runtime uniform
 */

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
   * dimensionCount(), and each value must be within its dimension's declared range.
   */
  uint32_t encode(const std::vector<int>& values) const;

  /**
   * Returns a list of "DEFINE_NAME=value" strings for the given variant index. Every dimension
   * is always included, even when the value is 0.
   */
  std::vector<std::string> defineListFor(uint32_t index) const;

  /**
   * Returns the decoded value of the dimension with the given define name, or 0 when no dimension
   * uses that name.
   */
  int valueOf(uint32_t index, const char* defineName) const;

  /**
   * Returns true when this domain declares a dimension with the given define name.
   */
  bool hasDimension(const char* defineName) const;

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

/**
 * Returns false when a dimension shared by name between the vertex and fragment domains has
 * differing values in the two stages. Such a dimension gates a varying that both stages must
 * declare identically (the vertex emits it only when the fragment consumes it), so mismatched
 * values would produce an invalid vertex/fragment interface. Callers validating permutations
 * consult this after decoding indices. vertValues/fragValues must match the arity of the
 * respective domains.
 */
bool MirroredDimsAgree(const PermutationDomain& vertDomain, const PermutationDomain& fragDomain,
                       const std::vector<int>& vertValues, const std::vector<int>& fragValues);

}  // namespace tgfx
