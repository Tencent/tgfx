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

#pragma once

#include "tgfx/core/BytesKey.h"

namespace tgfx {

class ProgramInfo;
class AOTEffectGraph;
struct AOTTextureParameters;
struct DrawShape;

/**
 * The pre-selection structural key: everything the family walk and the admission gates read,
 * nothing they do not. Two draws of the same shape share it however their colours, matrices,
 * subsets and proxies differ; two draws whose expressions differ in a node kind or in an edge never
 * do. It is taken before a variant exists, which is what the program key cannot do.
 *
 * A BytesKey, so a cache keyed by it reuses the existing hasher. Values (colours, matrices, rects,
 * alpha starts), proxies and sampler states stay out: they reach the program through uniforms, not
 * through the program's identity.
 */
using ShapeKey = BytesKey;

/**
 * Hashes one expression graph by structure: each node's kind, its input edges and the structural
 * fields of its parameters. Values inside those parameters are deliberately dropped, so editing a
 * colour or a matrix leaves the key unchanged while adding an edge never does.
 */
void HashGraphStructure(const AOTEffectGraph& graph, BytesKey* key);

/// Hashes one leaf's structural fields, for callers that hold the parameter block without its node
/// (a coverage term's device mask).
void HashTextureStructure(const AOTTextureParameters& params, BytesKey* key);

/**
 * The key of one draw, read off the facts the matcher consumes (see DrawFacts.h) plus the geometry
 * and pipeline state whose admission gates read them. The draw's IR contributes its graph structure
 * (the colour graph and every coverage term); its proxies and values do not.
 */
ShapeKey MakeShapeKey(const DrawShape& shape, const ProgramInfo& info);

}  // namespace tgfx
