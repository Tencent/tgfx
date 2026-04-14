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
                                                std::shared_ptr<class Shader> shader,
                                                bool clipToSource) {
  if (shader == nullptr) {
    return nullptr;
  }
  return std::make_shared<BlendImageFilter>(blendMode, std::move(shader), clipToSource);
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
    return shaderFP;
  }
  auto sourceFP = FragmentProcessor::Make(source, args, sampling, constraint, uvMatrix);
  if (sourceFP == nullptr) {
    return shaderFP;
  }
  auto blended = XfermodeFragmentProcessor::MakeFromTwoProcessors(allocator, std::move(shaderFP),
                                                                  std::move(sourceFP), blendMode);
  if (!clipToSource) {
    return blended;
  }
  // Clip the blended result to the source image's alpha so the shader doesn't produce visible
  // pixels outside the source content area.
  auto alphaFP = FragmentProcessor::Make(source, args, sampling, constraint, uvMatrix);
  return XfermodeFragmentProcessor::MakeFromTwoProcessors(allocator, std::move(blended),
                                                          std::move(alphaFP), BlendMode::SrcIn);
}

}  // namespace tgfx
