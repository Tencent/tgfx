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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <optional>
#include "gpu/shaders/Family.h"

namespace tgfx {

struct DrawShape;
struct AOTTextureParameters;
struct AOTGradientParameters;
struct AOTOpaqueKernelParameters;

/// One declaration-driven family's match: the variant the draw maps to and the module data
/// sources the writers read (the tiled leaf for the TiledTexture slot, the mask leaf for the
/// MaskTexture slot and the color graph's gradient node for the Gradient slot, null when absent).
struct FamilyMatchResult {
  Variant variant;
  const AOTTextureParameters* tiledLeaf = nullptr;
  const AOTTextureParameters* maskLeaf = nullptr;
  const AOTGradientParameters* gradient = nullptr;
  const AOTOpaqueKernelParameters* blurKernel = nullptr;
  const AOTTextureParameters* blurChild = nullptr;
  const AOTOpaqueKernelParameters* glassKernel = nullptr;
};

/// Matches a draw against the declaration-driven family table (Families(), in table order).
/// Returns nullopt when no assembled family serves the shape; the caller then falls back to the
/// legacy kernel table (the transitional route until P3 removes it).
std::optional<FamilyMatchResult> MatchAssembledFamily(const DrawShape& shape);

}  // namespace tgfx
