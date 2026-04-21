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

#include "BlendImageFilter.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/XfermodeFragmentProcessor.h"

namespace tgfx {

std::shared_ptr<ImageFilter> ImageFilter::Blend(BlendMode blendMode,
                                                std::shared_ptr<Shader> shader) {
  if (shader == nullptr) {
    return nullptr;
  }
  return std::make_shared<BlendImageFilter>(blendMode, std::move(shader));
}

// When source is null (dst = transparent black), Porter-Duff reduces to srcFactor * src. For modes
// whose srcFactor collapses to zero in that condition, the result is transparent black and the
// whole filter can be skipped. Otherwise the result is simply the shader itself.
static bool DstIsRequired(BlendMode mode) {
  switch (mode) {
    case BlendMode::Src:
    case BlendMode::SrcOver:
    case BlendMode::DstOver:
    case BlendMode::SrcOut:
    case BlendMode::DstATop:
    case BlendMode::Xor:
    case BlendMode::PlusLighter:
    case BlendMode::Screen:
      return false;
    default:
      return true;
  }
}

PlacementPtr<FragmentProcessor> BlendImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  auto allocator = args.context->drawingAllocator();
  auto shaderFP = FragmentProcessor::Make(shader, args, uvMatrix);
  if (shaderFP == nullptr) {
    return nullptr;
  }
  if (source == nullptr) {
    return DstIsRequired(blendMode) ? nullptr : std::move(shaderFP);
  }
  auto sourceFP = FragmentProcessor::Make(source, args, sampling, constraint, uvMatrix);
  if (sourceFP == nullptr) {
    return DstIsRequired(blendMode) ? nullptr : std::move(shaderFP);
  }
  // The shader acts as the src operand and the source image as the dst operand of the blend.
  return XfermodeFragmentProcessor::MakeFromTwoProcessors(allocator, std::move(shaderFP),
                                                          std::move(sourceFP), blendMode);
}

}  // namespace tgfx
