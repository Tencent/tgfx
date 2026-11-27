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

#include <memory>
#include "gpu/FragmentShaderBuilder.h"
#include "gpu/UniformData.h"
#include "gpu/UniformHandler.h"
#include "gpu/ir/ProgramIR.h"
#include "gpu/ir/XferFacts.h"
#include "gpu/processors/Processor.h"
#include "gpu/proxies/TextureProxy.h"

namespace tgfx {
class ProgramIRBuilder;

class XferProcessor : public Processor {
 public:
  struct EmitArgs {
    EmitArgs(FragmentShaderBuilder* fragBuilder, UniformHandler* uniformHandler,
             std::string inputColor, std::string inputCoverage, std::string outputColor,
             const SamplerHandle dstTextureSamplerHandle, ProgramIRBuilder* irBuilder = nullptr,
             ValueID inputColorValue = INVALID_VALUE_ID,
             ValueID inputCoverageValue = INVALID_VALUE_ID, ValueID outputValue = INVALID_VALUE_ID)
        : fragBuilder(fragBuilder), uniformHandler(uniformHandler),
          inputColor(std::move(inputColor)), inputCoverage(std::move(inputCoverage)),
          outputColor(std::move(outputColor)), dstTextureSamplerHandle(dstTextureSamplerHandle),
          irBuilder(irBuilder), inputColorValue(inputColorValue),
          inputCoverageValue(inputCoverageValue), outputValue(outputValue) {
    }
    ValueID samplerValue(SamplerHandle handle) const {
      if (samplerValues == nullptr || handle.toIndex() >= samplerValues->size()) {
        return INVALID_VALUE_ID;
      }
      return (*samplerValues)[handle.toIndex()];
    }
    FragmentShaderBuilder* fragBuilder;
    UniformHandler* uniformHandler;
    const std::string inputColor;
    const std::string inputCoverage;
    const std::string outputColor;
    const SamplerHandle dstTextureSamplerHandle;
    ProgramIRBuilder* irBuilder = nullptr;
    ValueID inputColorValue = INVALID_VALUE_ID;
    ValueID inputCoverageValue = INVALID_VALUE_ID;
    ValueID outputValue = INVALID_VALUE_ID;
    const std::vector<ValueID>* samplerValues = nullptr;
  };

  virtual const TextureView* dstTextureView() const {
    return nullptr;
  }

  virtual void emitCode(const EmitArgs& args) const = 0;

  virtual void setData(UniformData* vertexUniformData, UniformData* fragmentUniformData) const = 0;

 protected:
  explicit XferProcessor(uint32_t classID) : Processor(classID) {
  }
};
}  // namespace tgfx
