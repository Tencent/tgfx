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
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/AOTEffect.h"

namespace tgfx {

/**
 * Whether the node kind is one of the four unary pointwise operators every planner and the
 * executor admit between a source and the chain root.
 */
bool IsUnaryPointwiseKind(AOTEffectKind kind);

/**
 * A unary pointwise operator whose single input is the given node.
 */
bool IsUnaryPointwiseOp(const AOTEffectNode* node, AOTNodeID expectedInput);

/**
 * The source of a multi-pass linear plan: an affine RGBA texture fed by the geometry color
 * (node 0), sampled plainly or in device space.
 */
bool IsLinearMultiPassSource(const AOTEffectGraph& graph, AOTNodeID nodeID);

/**
 * A procedural-noise source fed by the geometry color (node 0).
 */
bool IsPerlinNoiseSource(const AOTEffectGraph& graph, AOTNodeID nodeID);

/**
 * A YUV video source fed by the geometry color (node 0) and sampled plainly.
 */
bool IsYUVChainSource(const AOTEffectGraph& graph, AOTNodeID nodeID);

/**
 * A blend whose other operand is an input-independent constant — the const-operand form the
 * slot interpreters carry: a two-child blend is refused (both operands would need evaluation),
 * exactly one side is a ConstColor node, the chained side is the given node, and the constant
 * ignores its own input edge (the slot reads it straight from its uniform record).
 */
bool IsConstOperandBlend(const AOTEffectGraph& graph, const AOTEffectNode* blend,
                         AOTNodeID expectedInput);

/**
 * Whether the kind is an input-environment designator (the geometry color, the coverage unit, and
 * the two xfer-child inputs) that never becomes a chain slot.
 */
bool IsInputDesignator(AOTEffectKind kind);

/**
 * Marks every node reachable from the given root (inclusive) in the reachable vector, sized to the
 * graph. A noise source's inputs stay unvisited: the noise designator input never becomes a slot,
 * and the whole noise node space belongs to the family's shared budgets. Returns false for an
 * invalid root or a dangling node reference.
 */
bool CollectReachableNodes(const AOTEffectGraph& graph, AOTNodeID root,
                           std::vector<bool>* reachable);

}  // namespace tgfx
