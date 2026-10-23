/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//       https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/level1/SolidColorFillShader.h"
#include <optional>
#include <vector>
#include "gpu/PermutationMatcher.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"

namespace tgfx {

// The whole runtime-side knowledge of this family in one place: which draws it serves and how the
// draw's shape maps onto the permutation indices. The pass-through shape (one HAS_XP axis fed by
// the XferProcessor type) needs no Inputs struct and no Compose function.
std::optional<PermutationMatchResult> SolidColorFillShader::Match(const ProgramInfo* programInfo) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != GpClassName) {
    return std::nullopt;
  }
  // The fill color comes from the DefaultGeometryProcessor Color uniform, so a color fragment
  // processor would be silently dropped and must reject. The only accepted FP is one direct
  // analytic clip coverage FP (AA rect or rrect), which this shader evaluates from the runtime
  // clip contract uniforms.
  if (!HasNoProcessorsOrDirectClip(programInfo)) {
    return std::nullopt;
  }
  auto xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fragIndex = FD::domain().encode(std::vector<int>{xpType});
  return PermutationMatchResult{"SolidColorFillShader", 0, fragIndex};
}

}  // namespace tgfx
