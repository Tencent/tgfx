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

#include "gpu/shaders/kernels/MaskFillShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/shaders/modules/ParamWriter.h"
#include "gpu/shaders/modules/PorterDuffXPModule.h"

namespace tgfx {

std::optional<MaskFillShader::Inputs> MaskFillShader::Extract(const ProgramInfo* programInfo) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  auto gp = programInfo->getGeometryProcessor();
  if (!IsProcessor<DefaultGeometryProcessor>(gp)) {
    return std::nullopt;
  }
  // Solid color (from the DefaultGP Color uniform) masked by a single alpha-only coverage
  // TextureEffect (a rasterized shape mask from ShapeDrawOp). No color fragment processors.
  if (programInfo->numColorFragmentProcessors() != 0) {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (!IsProcessor<TextureEffect>(fp)) {
    return std::nullopt;
  }
  auto* te = static_cast<const TextureEffect*>(fp);
  // Only the simple mask form is supported: a plain alpha-only texture with no subset, no packed
  // alpha, no YUV. Anything else falls back to ProgramBuilder.
  if (te->numTextureSamplers() == 0 || te->isYUV() || !te->isAlphaOnly() || te->hasRGBAAA() ||
      te->hasSubset()) {
    return std::nullopt;
  }
  // The mask coverage feeds the XferProcessor's coverage lerp, so Porter-Duff blends (DST_TEX and
  // framebuffer-fetch) are supported via the HAS_XP fragment dimension. Only a genuinely
  // unsupported transfer processor (xpType < 0) falls back to ProgramBuilder.
  auto xpType = GetXPType(programInfo);
  if (xpType < XP_NONE) {
    return std::nullopt;
  }
  return Inputs{xpType};
}

XPBindFn MaskFillShader::XPBindingFor(const ProgramInfo* programInfo) {
  auto* xp = programInfo->getXferProcessor();
  if (!IsProcessor<PorterDuffXferProcessor>(xp)) {
    return nullptr;
  }
  return &PorterDuffXPModule::Bind;
}

}  // namespace tgfx
