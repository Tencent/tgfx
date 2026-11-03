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
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/kernels/EllipseFillShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/aot/AOTDrawFacts.h"
#include "gpu/processors/PorterDuffXferProcessor.h"

namespace tgfx {

std::optional<EllipseFillShader::Inputs> EllipseFillShader::ExtractFacts(const AOTDrawFacts& facts,
                                                                         PermutationMatchFailure*) {
  // The geometry kind IS the contract: RRectDrawOp's AA form, no processor-tree walk.
  if (facts.geometry.kind != AOTGeometryKind::Ellipse) {
    return std::nullopt;
  }
  if (facts.colorFPCount != 0) {
    return std::nullopt;
  }
  // A clip/mask coverage FP is served through the shared HAS_COVERAGE dimension (device-space
  // sampling in coverage_output.inc). LocalMask needs per-vertex UV the ellipse vertex shader does
  // not provide, so the shared classification rejects it.
  if (!facts.coverage || *facts.coverage == CoverageKind::LocalMask) {
    return std::nullopt;
  }
  const bool hasDeviceMask = *facts.coverage == CoverageKind::DeviceMask;
  const int xpType = facts.xpType();
  if (xpType < XP_NONE) {
    return std::nullopt;
  }
  return Inputs{xpType, hasDeviceMask};
}

}  // namespace tgfx
