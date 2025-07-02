/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "gpu/Pipeline.h"
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

PlacementPtr<FragmentProcessor> FragmentProcessor::Make(std::shared_ptr<Shader> shader,
                                                        const FPArgs& args,
                                                        const Matrix* uvMatrix) {
  DEBUG_ASSERT(shader != nullptr);
  return shader->asFragmentProcessor(args, uvMatrix);
}

PlacementPtr<FragmentProcessor> FragmentProcessor::MulChildByInputAlpha(
    BlockBuffer* buffer, PlacementPtr<FragmentProcessor> child) {
  if (child == nullptr) {
    return nullptr;
  }
  return XfermodeFragmentProcessor::MakeFromDstProcessor(buffer, std::move(child),
                                                         BlendMode::DstIn);
}

PlacementPtr<FragmentProcessor> FragmentProcessor::MulInputByChildAlpha(
    BlockBuffer* buffer, PlacementPtr<FragmentProcessor> child, bool inverted) {
  if (child == nullptr) {
    return nullptr;
  }
  return XfermodeFragmentProcessor::MakeFromDstProcessor(
      buffer, std::move(child), inverted ? BlendMode::SrcOut : BlendMode::SrcIn);
}

PlacementPtr<FragmentProcessor> FragmentProcessor::Compose(BlockBuffer* buffer,
                                                           PlacementPtr<FragmentProcessor> f,
                                                           PlacementPtr<FragmentProcessor> g) {
  return ComposeFragmentProcessor::Make(buffer, std::move(f), std::move(g));
}

void FragmentProcessor::computeProcessorKey(Context* context, BytesKey* bytesKey) const {
  bytesKey->write(classID());
  onComputeProcessorKey(bytesKey);
  auto textureSamplerCount = onCountTextureSamplers();
  for (size_t i = 0; i < textureSamplerCount; ++i) {
    textureSampler(i)->computeKey(context, bytesKey);
  }
  for (const auto& childProcessor : childProcessors) {
    childProcessor->computeProcessorKey(context, bytesKey);
  }
}

size_t FragmentProcessor::registerChildProcessor(PlacementPtr<FragmentProcessor> child) {
  auto index = childProcessors.size();
  childProcessors.push_back(std::move(child));
  return index;
}

FragmentProcessor::Iter::Iter(const Pipeline* pipeline) {
  for (auto i = pipeline->numFragmentProcessors(); i >= 1; --i) {
    fpStack.push_back(pipeline->getFragmentProcessor(i - 1));
  }
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

FragmentProcessor::CoordTransformIter::CoordTransformIter(const Pipeline* pipeline)
    : fpIter(pipeline) {
  currFP = fpIter.next();
}

const CoordTransform* FragmentProcessor::CoordTransformIter::next() {
  if (!currFP) {
    return nullptr;
  }
  while (currentIndex == currFP->numCoordTransforms()) {
    currentIndex = 0;
    currFP = fpIter.next();
    if (!currFP) {
      return nullptr;
    }
  }
  return currFP->coordTransform(currentIndex++);
}

void FragmentProcessor::setData(UniformBuffer* uniformBuffer) const {
  onSetData(uniformBuffer);
}

void FragmentProcessor::emitChild(size_t childIndex, const std::string& inputColor,
                                  std::string* outputColor, EmitArgs& args,
                                  std::function<std::string(std::string_view)> coordFunc) const {
  auto* fragBuilder = args.fragBuilder;
  auto pipeline = fragBuilder->getPipeline();
  outputColor->append(pipeline->getMangledSuffix(this));
  fragBuilder->codeAppendf("vec4 %s;", outputColor->c_str());
  internalEmitChild(childIndex, inputColor, *outputColor, args, std::move(coordFunc));
}

void FragmentProcessor::emitChild(size_t childIndex, const std::string& inputColor,
                                  EmitArgs& parentArgs) const {
  internalEmitChild(childIndex, inputColor, parentArgs.outputColor, parentArgs);
}

void FragmentProcessor::internalEmitChild(
    size_t childIndex, const std::string& inputColor, const std::string& outputColor,
    EmitArgs& args, std::function<std::string(std::string_view)> coordFunc) const {
  auto* fragBuilder = args.fragBuilder;
  const auto* childProc = childProcessor(childIndex);
  fragBuilder->onBeforeChildProcEmitCode(childProc);  // call first so mangleString is updated
  auto pipeline = fragBuilder->getPipeline();
  // Prepare a mangled input color variable if the default is not used,
  // inputName remains the empty string if no variable is needed.
  std::string inputName;
  if (!inputColor.empty() && inputColor != "vec4(1.0)") {
    // The input name is based off of the current mangle string, and
    // since this is called after onBeforeChildProcEmitCode(), it will be
    // unique to the child processor (exactly what we want for its input).
    inputName += "_childInput";
    inputName += pipeline->getMangledSuffix(childProc);
    fragBuilder->codeAppendf("vec4 %s = %s;", inputName.c_str(), inputColor.c_str());
  }

  // emit the code for the child in its own scope
  fragBuilder->codeAppend("{\n");
  fragBuilder->codeAppendf("// Processor%d : %s\n", pipeline->getProcessorIndex(childProc),
                           childProc->name().c_str());
  TransformedCoordVars coordVars = args.transformedCoords->childInputs(childIndex);
  TextureSamplers textureSamplers = args.textureSamplers->childInputs(childIndex);

  EmitArgs childArgs(fragBuilder, args.uniformHandler, outputColor,
                     inputName.empty() ? "vec4(1.0)" : inputName, args.inputSubset, &coordVars,
                     &textureSamplers, std::move(coordFunc));
  childProcessor(childIndex)->emitCode(childArgs);
  fragBuilder->codeAppend("}\n");

  fragBuilder->onAfterChildProcEmitCode();
}

template <typename T, size_t (FragmentProcessor::*COUNT)() const>
FragmentProcessor::BuilderInputProvider<T, COUNT>
FragmentProcessor::BuilderInputProvider<T, COUNT>::childInputs(size_t childIndex) const {
  const FragmentProcessor* child = fragmentProcessor->childProcessor(childIndex);
  FragmentProcessor::Iter iter(fragmentProcessor);
  int numToSkip = 0;
  while (true) {
    const FragmentProcessor* processor = iter.next();
    if (processor == child) {
      return BuilderInputProvider(child, t + numToSkip);
    }
    numToSkip += (processor->*COUNT)();
  }
}

}  // namespace tgfx
