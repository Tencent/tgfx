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
#include <vector>
#include "gpu/AOTEffect.h"

namespace tgfx {
class FragmentProcessor;

enum class AOTDecompositionMode {
  PreferFusion,
  Standard,
};

enum class AOTKernelKind {
  TextureFill,
  TextureColorMatrix,
  TexturedColorMatrix,
  TexturedLuma,
};

struct AOTPassDescriptor {
  AOTKernelKind kernel = AOTKernelKind::TextureFill;
  std::vector<AOTNodeID> nodes = {};
  std::vector<uint32_t> dependencies = {};
  AOTNodeID output = AOTNodeID::Invalid();
  bool materializesOutput = false;
};

struct AOTEffectPlan {
  std::vector<AOTPassDescriptor> passes = {};
  AOTNodeID output = AOTNodeID::Invalid();
};

class AOTEffectDecomposer {
 public:
  static bool Lower(const std::vector<const FragmentProcessor*>& processors, AOTEffectGraph* graph);

  static bool Decompose(const AOTEffectGraph& graph, AOTDecompositionMode mode,
                        AOTEffectPlan* plan);
};

}  // namespace tgfx
