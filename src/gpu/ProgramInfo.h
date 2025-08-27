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

#pragma once

#include <unordered_map>
#include "RenderTarget.h"
#include "gpu/Blend.h"
#include "gpu/Program.h"
#include "gpu/processors/EmptyXferProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/GeometryProcessor.h"

namespace tgfx {
struct SamplerInfo {
  GPUTexture* texture;
  SamplerState state;
};

/**
 * This immutable object contains information needed to build a shader program and set API state for
 * a draw.
 */
class ProgramInfo {
 public:
  ProgramInfo(RenderTarget* renderTarget, PlacementPtr<GeometryProcessor> geometryProcessor,
              std::vector<PlacementPtr<FragmentProcessor>> fragmentProcessors,
              size_t numColorProcessors, PlacementPtr<XferProcessor> xferProcessor,
              BlendMode blendMode);

  size_t numColorFragmentProcessors() const {
    return numColorProcessors;
  }

  size_t numFragmentProcessors() const {
    return fragmentProcessors.size();
  }

  const GeometryProcessor* getGeometryProcessor() const {
    return geometryProcessor.get();
  }

  const FragmentProcessor* getFragmentProcessor(size_t idx) const {
    return fragmentProcessors[idx].get();
  }

  const XferProcessor* getXferProcessor() const;

  const Swizzle& getOutputSwizzle() const;

  const std::vector<Attribute>& getVertexAttributes() const {
    return geometryProcessor->vertexAttributes();
  }

  std::unique_ptr<BlendFormula> getBlendFormula() const;

  void getUniforms(UniformBuffer* uniformBuffer) const;

  std::vector<SamplerInfo> getSamplers() const;

  /**
   * Returns the index of the processor in the ProgramInfo. Returns -1 if the processor is not in
   * the ProgramInfo.
   */
  int getProcessorIndex(const Processor* processor) const;

  std::string getMangledSuffix(const Processor* processor) const;

  std::shared_ptr<Program> getProgram() const;

 private:
  RenderTarget* renderTarget = nullptr;
  PlacementPtr<GeometryProcessor> geometryProcessor = nullptr;
  std::vector<PlacementPtr<FragmentProcessor>> fragmentProcessors = {};
  std::unordered_map<const Processor*, int> processorIndices = {};
  // This value is also the index in fragmentProcessors where coverage processors begin.
  size_t numColorProcessors = 0;
  PlacementPtr<XferProcessor> xferProcessor = nullptr;
  BlendMode blendMode = BlendMode::SrcOver;

  void updateProcessorIndices();
};
}  // namespace tgfx
