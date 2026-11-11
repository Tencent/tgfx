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
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/Family.h"

namespace tgfx {

class ProgramInfo;
struct AOTTextureParameters;
struct AOTGradientParameters;
struct AOTOpaqueKernelParameters;

/// One declaration-driven family's match: the variant the draw maps to and the module data
/// sources the writers read (the tiled leaf for the TiledTexture slot, the mask leaf for the
/// MaskTexture slot and the color graph's gradient node for the Gradient slot, null when absent).
/// It lives in its own header because the resolved selection rides the per-draw ProgramInfo,
/// which must not pull in the match rules that produce it.
struct FamilyMatchResult {
  Variant variant;
  const AOTTextureParameters* tiledLeaf = nullptr;
  const AOTTextureParameters* maskLeaf = nullptr;
  const AOTGradientParameters* gradient = nullptr;
  const AOTOpaqueKernelParameters* blurKernel = nullptr;
  const AOTTextureParameters* blurChild = nullptr;
  const AOTOpaqueKernelParameters* glassKernel = nullptr;
};

}  // namespace tgfx
