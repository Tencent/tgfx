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

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <variant>
#include <vector>
#include "gpu/SamplerState.h"
#include "gpu/proxies/TextureProxy.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {

class AOTNodeID {
 public:
  constexpr AOTNodeID() = default;

  explicit constexpr AOTNodeID(uint32_t value) : value(value) {
  }

  static constexpr AOTNodeID Invalid() {
    return AOTNodeID();
  }

  constexpr bool isValid() const {
    return value != std::numeric_limits<uint32_t>::max();
  }

  constexpr uint32_t index() const {
    return value;
  }

  constexpr bool operator==(const AOTNodeID& other) const {
    return value == other.value;
  }

  constexpr bool operator!=(const AOTNodeID& other) const {
    return !(*this == other);
  }

 private:
  uint32_t value = std::numeric_limits<uint32_t>::max();
};

enum class AOTEffectKind {
  GeometryColor,
  TextureSource,
  ColorMatrix,
  Luma,
};

enum class EffectDomain {
  Pointwise,
  Neighborhood,
  Composite,
  External,
};

enum class EffectInputUsage {
  Ignore,
  ColorRGBA,
  ColorAlpha,
  SameCoordinateChild,
  RepeatedCoordinateChild,
};

struct EffectTraits {
  EffectDomain domain = EffectDomain::Pointwise;
  EffectInputUsage inputUsage = EffectInputUsage::Ignore;
  bool isSelfContained = false;
  bool preservesAlphaRepresentation = false;
  bool preservesColorSpace = false;
};

struct AOTTextureParameters {
  std::shared_ptr<TextureProxy> textureProxy = nullptr;
  SamplerState samplerState = {};
  SrcRectConstraint constraint = SrcRectConstraint::Fast;
  Matrix uvMatrix = {};
  std::optional<Rect> subset = std::nullopt;
  Point alphaStart = {};
  bool isYUV = false;
  bool isAlphaOnly = false;
  bool hasRGBAAA = false;
  bool hasSubset = false;
  bool hasPerspective = false;
};

struct AOTColorMatrixParameters {
  std::array<float, 20> matrix = {};
};

struct AOTLumaParameters {
  float kr = 0.2126f;
  float kg = 0.7152f;
  float kb = 0.0722f;
};

using AOTEffectParameters =
    std::variant<std::monostate, AOTTextureParameters, AOTColorMatrixParameters, AOTLumaParameters>;

struct AOTEffectNode {
  AOTEffectKind kind = AOTEffectKind::GeometryColor;
  std::vector<AOTNodeID> inputs = {};
  EffectTraits traits = {};
  AOTEffectParameters parameters = {};
};

class AOTEffectGraph {
 public:
  size_t nodeCount() const {
    return nodes.size();
  }

  const AOTEffectNode* nodeAt(AOTNodeID nodeID) const;

  AOTNodeID root() const {
    return rootNode;
  }

 private:
  friend class AOTNodeBuilder;

  std::vector<AOTEffectNode> nodes = {};
  AOTNodeID rootNode = AOTNodeID::Invalid();
};

class AOTNodeBuilder {
 public:
  bool addGeometryColor(AOTNodeID* output);

  bool addTextureSource(AOTNodeID input, const AOTTextureParameters& parameters, AOTNodeID* output);

  bool addColorMatrix(AOTNodeID input, const AOTColorMatrixParameters& parameters,
                      AOTNodeID* output);

  bool addLuma(AOTNodeID input, const AOTLumaParameters& parameters, AOTNodeID* output);

  bool finish(AOTNodeID root, AOTEffectGraph* graph) const;

  size_t nodeCount() const {
    return nodes.size();
  }

 private:
  bool addUnaryNode(AOTEffectKind kind, AOTNodeID input, EffectTraits traits,
                    AOTEffectParameters parameters, AOTNodeID* output);

  bool contains(AOTNodeID nodeID) const;

  std::vector<AOTEffectNode> nodes = {};
};

}  // namespace tgfx
