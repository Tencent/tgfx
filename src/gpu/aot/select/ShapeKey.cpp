/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/select/ShapeKey.h"
#include <variant>
#include "gpu/ProgramInfo.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/aot/ir/GeometryLayout.h"
#include "gpu/aot/ir/XferFacts.h"
#include "gpu/aot/select/DrawFacts.h"

namespace tgfx {
namespace {

/// One operator's structural fields: the ints and flags that change the code path, never the
/// per-draw values (colours, matrices, rects). A type that carries only values contributes nothing.
struct StructureVisitor {
  BytesKey* key = nullptr;

  void operator()(const std::monostate&) const {
  }

  void operator()(const AOTTextureParameters& params) const {
    HashTextureStructure(params, key);
  }

  void operator()(const AOTColorMatrixParameters&) const {
    // The matrix is a value: it rides a uniform, not the program's identity.
  }

  void operator()(const ChainISA::LumaData&) const {
    // The three weights are values.
  }

  void operator()(const ChainISA::AlphaThresholdData&) const {
    // The threshold is a value.
  }

  void operator()(const AOTColorSpaceXformParameters& params) const {
    key->write(static_cast<uint32_t>(params.steps != nullptr ? 1u : 0u));
  }

  void operator()(const AOTConstColorParameters& params) const {
    // The colour is a value; the input mode selects the executor's branch.
    key->write(static_cast<int>(params.inputMode));
  }

  void operator()(const AOTBlendParameters& params) const {
    key->write(static_cast<int>(params.blendMode));
    key->write(static_cast<int>(params.childType));
    key->write(static_cast<uint32_t>(params.multiplyInputAlpha ? 1u : 0u));
  }

  void operator()(const AOTPerlinNoiseParameters& params) const {
    key->write(static_cast<int>(params.noiseType));
    key->write(static_cast<int>(params.numOctaves));
    key->write(static_cast<uint32_t>(params.stitchTiles ? 1u : 0u));
    key->write(static_cast<int>(params.stitchWidth));
    key->write(static_cast<int>(params.stitchHeight));
    key->write(static_cast<uint32_t>(params.lookupData != nullptr ? 1u : 0u));
  }

  void operator()(const AOTRectCoverageParameters&) const {
    // The rect and its device-to-local matrix are values.
  }

  void operator()(const AOTGradientParameters& params) const {
    key->write(static_cast<int>(params.layoutType));
    key->write(static_cast<int>(params.colorizerKind));
    key->write(static_cast<int>(params.intervalCount));
    key->write(static_cast<uint32_t>(params.lutProxy != nullptr ? 1u : 0u));
  }

  void operator()(const AOTRRectCoverageParameters&) const {
    // The rect and the radii are values.
  }

  void operator()(const AOTOpaqueKernelParameters& params) const {
    key->write(static_cast<uint32_t>(params.kernel));
    key->write(static_cast<int>(params.sigma));
    key->write(static_cast<int>(params.maxRadius));
    key->write(static_cast<uint32_t>(
        params.geometryKind.has_value() ? static_cast<uint32_t>(*params.geometryKind) + 1u : 0u));
    key->write(static_cast<uint32_t>(params.dispersionOn ? 1u : 0u));
    key->write(static_cast<uint32_t>(params.hasPerspective ? 1u : 0u));
    key->write(static_cast<int>(params.kernelRadius));
    key->write(static_cast<uint32_t>(params.vertical ? 1u : 0u));
    key->write(static_cast<int>(params.glassShapeType));
    key->write(static_cast<int>(params.tentField));
    key->write(static_cast<int>(params.tentInputIsPacked));
  }
};

}  // namespace

void HashTextureStructure(const AOTTextureParameters& params, BytesKey* key) {
  key->write(static_cast<uint32_t>(params.samplingKind));
  key->write(static_cast<uint32_t>(params.isYUV ? 1u : 0u));
  key->write(static_cast<int>(params.yuvFormat));
  key->write(static_cast<uint32_t>(params.isAlphaOnly ? 1u : 0u));
  key->write(static_cast<uint32_t>(params.hasRGBAAA ? 1u : 0u));
  key->write(static_cast<uint32_t>(params.hasPerspective ? 1u : 0u));
  key->write(static_cast<uint32_t>(params.isIntermediate ? 1u : 0u));
  key->write(static_cast<uint32_t>(params.subset.has_value() ? 1u : 0u));
  key->write(static_cast<uint32_t>(params.tiledRecipe.has_value() ? 1u : 0u));
  key->write(static_cast<uint32_t>(params.constraint));
}

void HashGraphStructure(const AOTEffectGraph& graph, BytesKey* key) {
  key->write(static_cast<uint32_t>(graph.nodeCount()));
  for (uint32_t index = 0; index < graph.nodeCount(); ++index) {
    const auto* node = graph.nodeAt(AOTNodeID(index));
    if (node == nullptr) {
      key->write(static_cast<uint32_t>(0xFFFFFFFFu));
      continue;
    }
    key->write(static_cast<uint32_t>(node->kind));
    // The edges are what makes two graphs with the same node kinds different expressions: a role
    // resolved by position (a coverage subtree's mask leaf) must never answer for another graph's
    // node with the same kind.
    key->write(static_cast<uint32_t>(node->inputs.size()));
    for (const auto& input : node->inputs) {
      key->write(static_cast<uint32_t>(input.isValid() ? input.index() : 0xFFFFFFFFu));
    }
    std::visit(StructureVisitor{key}, node->parameters);
  }
}

ShapeKey MakeShapeKey(const DrawShape& shape, const ProgramInfo& info) {
  BytesKey key = {};
  // The classification the rules read.
  key.write(static_cast<uint32_t>(
      shape.coverage.has_value() ? static_cast<uint32_t>(*shape.coverage) + 1u : 0u));
  key.write(shape.requiredTextureReads);
  key.write(static_cast<uint32_t>(shape.colorFPCount));
  key.write(static_cast<uint32_t>(shape.fragmentCount));
  key.write(static_cast<uint32_t>(shape.alphaOnlyTarget ? 1u : 0u));
  key.write(static_cast<uint32_t>(shape.hasGradientSource ? 1u : 0u));
  key.write(static_cast<uint32_t>(shape.xpType()));
  // The geometry's structural facts: the kind, the perspective flag and the live attribute
  // formats. Values (matrices, colours) stay out.
  const auto& geometry = info.getGeometryLayout();
  key.write(static_cast<uint32_t>(geometry.parameters.kind));
  key.write(static_cast<uint32_t>(geometry.parameters.hasUVPerspective ? 1u : 0u));
  for (const auto& attribute : geometry.vertexAttributes) {
    if (!attribute.empty()) {
      key.write(static_cast<uint32_t>(attribute.format()));
    }
  }
  for (const auto& attribute : geometry.instanceAttributes) {
    if (!attribute.empty()) {
      key.write(static_cast<uint32_t>(attribute.format()));
    }
  }
  key.write(static_cast<uint32_t>(shape.xfer != nullptr && shape.xfer->shaderBlend() ? 1u : 0u));
  // The expression: the colour graph and every coverage term, structure only.
  if (shape.ir == nullptr) {
    key.write(static_cast<uint32_t>(0xFFFFFFFFu));
  } else {
    const auto& ir = *shape.ir;
    HashGraphStructure(ir.color, &key);
    key.write(static_cast<uint32_t>(ir.coverage.size()));
    for (const auto& term : ir.coverage) {
      key.write(static_cast<uint32_t>(term.kind));
      key.write(static_cast<uint32_t>(term.root.isValid() ? term.root.index() : 0xFFFFFFFFu));
      HashGraphStructure(term.graph, &key);
      HashTextureStructure(term.mask, &key);
    }
  }
  // The pipeline state the admission gates and the program identity both read.
  info.writePipelineStateKey(&key);
  return key;
}

}  // namespace tgfx
