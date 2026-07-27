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

  /**
   * Validates that the effect graph is safe to fuse into a single Pointwise chain, enforcing the
   * semantic guards that the plain node checks do not cover (review #4). Returns false — forcing a
   * fallback to the plain route — when fusing would change pixels, specifically:
   *  - a ColorMatrix whose alpha row carries a non-zero constant bias, which can affect transparent
   *    black (design §6.3): fusing it into the chain would drop the source-alpha constraint;
   *  - any node that does not preserve alpha representation or color space adjacent to another such
   *    node without a materialization edge, since premul/colorspace position is not tracked here.
   * Conservative by design: when the required semantic information is unavailable, it rejects.
   */
  static bool ValidateForFusion(const AOTEffectGraph& graph);
};

}  // namespace tgfx
