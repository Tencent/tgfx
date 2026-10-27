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

#include <cassert>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace tgfx {

/**
 * Binding-signature vocabulary for precompiled shader variants.
 *
 * A degree of freedom may become a compile-time permutation dimension only if it changes GPU
 * resource binding: the number or type of sampler bindings, the attachment configuration, or the
 * varying/attribute layout. Everything else must be a runtime uniform. "The generated code would
 * contain an if" is not a justification — a branch over uniform data is coherent and usually
 * cheaper than another variant, though not free (dispatch, parameter reads, code size). Even a
 * binding difference can sometimes be absorbed by a placeholder binding plus a uniform, as
 * ComplexEllipseFillShader does for its mask sampler.
 *
 * Canonical axes (new kernels may only use these):
 *
 *   Binding axes:
 *     TEXTURE_COUNT  (0/1/2/4)            each texture = one sampler binding + one coord varying
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
 * Typed handle to one declared dimension of a family's permutation stage, declared as a static
 * member of that stage's Dims struct (VertDims / FragDims). The handle names the dimension
 * once, and every consumer resolves the position through that name:
 *
 *   Dims struct ──declares──▶ typed handles (one per dimension: name + declared range)
 *        │
 *        └── domain() lists the handles in the bundle's dimension order
 *                                       │
 *   Compose ──set(handle, value)──▶ ComposedDims ──finish()──▶ position-ordered values
 *
 * The handle replaces the index enum that previously mirrored the domain list: an enum position
 * could silently drift from the domain order (a transposed write produced a wrong but plausible
 * variant), while a handle has no position to drift. The Dims template parameter binds the
 * handle to its owning stage, so a vert handle handed to a fragment builder is a compile error.
 */
template <typename Dims>
class PermutationBoolDim {
 public:
  constexpr PermutationBoolDim(const char* defineName) : defineName(defineName) {
  }

  operator PermutationBool() const {
    return PermutationBool(defineName);
  }

  const char* defineName;
};

template <typename Dims>
class PermutationIntDim {
 public:
  PermutationIntDim(const char* defineName, int count) : defineName(defineName), count(count) {
  }

  operator PermutationInt() const {
    return PermutationInt(defineName, count);
  }

  const char* defineName;
  int count;
};

template <typename Dims>
class PermutationEnumDim {
 public:
  PermutationEnumDim(const char* defineName, std::initializer_list<const char*> valueNames)
      : defineName(defineName), valueNames(valueNames) {
  }

  operator PermutationEnum() const {
    return PermutationEnum(defineName, valueNames);
  }

  const char* defineName;
  std::vector<const char*> valueNames;
};

/**
 * The vertex/fragment variant a family's Match resolved a draw to, within the family's declared
 * permutation domains. The family name is filled by the matcher dispatch loop, so a Match
 * implementation cannot misspell it.
 */
struct PermutationVariant {
  uint32_t vertPermutationIndex;
  uint32_t fragPermutationIndex;
};

/**
 * Dimension values a matcher rule produces for both stages, before domain encoding.
 */
struct RuleComposedValues {
  std::vector<int> vertValues;
  std::vector<int> fragValues;
};

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

  /**
   * Returns the position of the dimension with the given define name, or dimensionCount() when
   * no dimension uses that name. Reads of a decoded values vector go through this lookup so a
   * reader never assumes a position.
   */
  size_t indexOf(const char* defineName) const;

  /**
   * Returns the value count (declared range) of the dimension at the given position.
   */
  int valueCountOf(size_t position) const;

  const std::vector<PermutationDimension>& getDimensions() const {
    return dimensions;
  }

 private:
  std::vector<PermutationDimension> dimensions;
};

/**
 * Write side of one stage's dimension values. set() resolves the handle's position in the
 * domain by name — the call order is irrelevant and a transposition against the domain order is
 * impossible — and range-checks the value against the dimension's declared range. finish()
 * returns the position-ordered value vector the encoder consumes, and fails when a declared
 * dimension was never assigned (the old resize-then-index writes silently zero-filled a
 * forgotten dimension). get() reads a value back by handle for validation passes between
 * Compose's stages. The domain is moved in, so a temporary (Dims::domain()) is safe to pass.
 */
template <typename Dims>
class ComposedDims {
 public:
  explicit ComposedDims(PermutationDomain domain) : domain(std::move(domain)) {
    values.resize(this->domain.dimensionCount(), 0);
    assigned.resize(this->domain.dimensionCount(), 0);
  }

  ComposedDims& set(const PermutationBoolDim<Dims>& dim, int value) {
    setAt(dim.defineName, value);
    return *this;
  }

  ComposedDims& set(const PermutationIntDim<Dims>& dim, int value) {
    setAt(dim.defineName, value);
    return *this;
  }

  ComposedDims& set(const PermutationEnumDim<Dims>& dim, int value) {
    setAt(dim.defineName, value);
    return *this;
  }

  int get(const PermutationBoolDim<Dims>& dim) const {
    return getAt(dim.defineName);
  }

  int get(const PermutationIntDim<Dims>& dim) const {
    return getAt(dim.defineName);
  }

  int get(const PermutationEnumDim<Dims>& dim) const {
    return getAt(dim.defineName);
  }

  /**
   * The position-ordered value vector encode() consumes; fails when a declared dimension was
   * never assigned.
   */
  std::vector<int> finish() const {
    for (size_t i = 0; i < assigned.size(); i++) {
      assert(assigned[i] != 0 && "finish() with a declared dimension never assigned");
    }
    return values;
  }

  const PermutationDomain& permutationDomain() const {
    return domain;
  }

 private:
  size_t positionOf(const char* defineName) const {
    auto position = domain.indexOf(defineName);
    assert(position < domain.dimensionCount() &&
           "unknown dimension handle: the name is not in "
           "the domain the builder was built from");
    return position;
  }

  void setAt(const char* defineName, int value) {
    auto position = positionOf(defineName);
    assert(value >= 0 && value < domain.valueCountOf(position) &&
           "dimension value outside the declared range");
    values[position] = value;
    assigned[position] = 1;
  }

  int getAt(const char* defineName) const {
    auto position = positionOf(defineName);
    assert(assigned[position] != 0 && "reading a dimension that was never assigned");
    return values[position];
  }

  PermutationDomain domain;
  std::vector<int> values;
  std::vector<uint8_t> assigned;
};

/**
 * The legacy dst-read dimension every level1 family shares: how the destination color reaches
 * the fragment shader. New kernels should follow the DST_KIND vocabulary above instead of adding
 * another HAS_XP domain.
 */
inline constexpr const char* XP_DEFINE_NAME = "HAS_XP";
inline constexpr int XP_UNSUPPORTED = -1;
inline constexpr int XP_NONE = 0;
inline constexpr int XP_DST_TEXTURE = 1;
inline constexpr int XP_FRAMEBUFFER_FETCH = 2;
inline constexpr int XP_VALUE_COUNT = 3;

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
