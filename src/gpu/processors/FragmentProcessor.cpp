/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "gpu/processors/FragmentProcessor.h"
#include "ComposeFragmentProcessor.h"
#include "core/utils/Log.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/processors/XfermodeFragmentProcessor.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Shader.h"

namespace tgfx {
PlacementPtr<FragmentProcessor> FragmentProcessor::Make(std::shared_ptr<Image> image,
                                                        const FPArgs& args,
                                                        const SamplingOptions& sampling,
                                                        SrcRectConstraint constraint,
                                                        const Matrix* uvMatrix) {
  DEBUG_ASSERT(image != nullptr);
  SamplingArgs samplingArgs = SamplingArgs(TileMode::Clamp, TileMode::Clamp, sampling, constraint);
  return image->asFragmentProcessor(args, samplingArgs, uvMatrix);
}

PlacementPtr<FragmentProcessor> FragmentProcessor::Make(
    std::shared_ptr<Image> image, const FPArgs& args, TileMode tileModeX, TileMode tileModeY,
    const SamplingOptions& sampling, SrcRectConstraint constraint, const Matrix* uvMatrix) {
  DEBUG_ASSERT(image != nullptr);
  SamplingArgs samplingArgs = SamplingArgs(tileModeX, tileModeY, sampling, constraint);
  return image->asFragmentProcessor(args, samplingArgs, uvMatrix);
}

PlacementPtr<FragmentProcessor> FragmentProcessor::Make(std::shared_ptr<Image> image,
                                                        const FPArgs& args,
                                                        const SamplingArgs& samplingArgs,
                                                        const Matrix* uvMatrix) {
  DEBUG_ASSERT(image != nullptr);
  return image->asFragmentProcessor(args, samplingArgs, uvMatrix);
}

PlacementPtr<FragmentProcessor> FragmentProcessor::Make(
    std::shared_ptr<Shader> shader, const FPArgs& args, const Matrix* uvMatrix,
    const std::shared_ptr<ColorSpace>& dstColorSpace) {
  DEBUG_ASSERT(shader != nullptr);
  return shader->asFragmentProcessor(args, uvMatrix, dstColorSpace);
}

PlacementPtr<FragmentProcessor> FragmentProcessor::MulChildByInputAlpha(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> child) {
  if (child == nullptr) {
    return nullptr;
  }
  return XfermodeFragmentProcessor::MakeFromDstProcessor(allocator, std::move(child),
                                                         BlendMode::DstIn);
}

PlacementPtr<FragmentProcessor> FragmentProcessor::MulInputByChildAlpha(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> child, bool inverted) {
  if (child == nullptr) {
    return nullptr;
  }
  return XfermodeFragmentProcessor::MakeFromDstProcessor(
      allocator, std::move(child), inverted ? BlendMode::SrcOut : BlendMode::SrcIn);
}

PlacementPtr<FragmentProcessor> FragmentProcessor::Compose(BlockAllocator* allocator,
                                                           PlacementPtr<FragmentProcessor> first,
                                                           PlacementPtr<FragmentProcessor> second) {
  return ComposeFragmentProcessor::Make(allocator, std::move(first), std::move(second));
}

void FragmentProcessor::computeProcessorKey(Context* context, BytesKey* bytesKey) const {
  bytesKey->write(classID());
  onComputeProcessorKey(bytesKey);
  auto textureSamplerCount = onCountTextureSamplers();
  for (size_t i = 0; i < textureSamplerCount; ++i) {
    auto texture = textureAt(i);
    if (texture != nullptr) {
      TextureView::ComputeTextureKey(texture, bytesKey);
    }
  }
  for (const auto& childProcessor : childProcessors) {
    childProcessor->computeProcessorKey(context, bytesKey);
  }
}

bool FragmentProcessor::lowerToAOT(AOTNodeBuilder* builder, AOTNodeID, AOTNodeID*) const {
  if (builder != nullptr) {
    builder->recordMissingLowering(this);
  }
  return false;
}

size_t FragmentProcessor::registerChildProcessor(PlacementPtr<FragmentProcessor> child) {
  auto index = childProcessors.size();
  childProcessors.push_back(std::move(child));
  return index;
}

const FragmentProcessor* FragmentProcessor::Iter::next() {
  if (fpStack.empty()) {
    return nullptr;
  }
  const FragmentProcessor* back = fpStack.back();
  fpStack.pop_back();
  for (size_t i = 0; i < back->numChildProcessors(); ++i) {
    fpStack.push_back(back->childProcessor(back->numChildProcessors() - i - 1));
  }
  return back;
}

FragmentProcessor::CoordTransformIter::CoordTransformIter(
    const std::vector<CoordTransform>* transformList)
    : transforms(transformList) {
}

const CoordTransform* FragmentProcessor::CoordTransformIter::next() {
  if (transforms == nullptr || transformIndex >= transforms->size()) {
    return nullptr;
  }
  return &(*transforms)[transformIndex++];
}

}  // namespace tgfx
