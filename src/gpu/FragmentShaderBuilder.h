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

#include "ShaderBuilder.h"
#include "ShaderVar.h"
#include "gpu/ir/CoordValue.h"

namespace tgfx {
class FragmentProcessor;
class ProgramIRBuilder;

static constexpr char CUSTOM_COLOR_OUTPUT_NAME[] = "tgfx_FragColor";

struct TextureReadResult {
  std::string name = {};
  ValueID value = INVALID_VALUE_ID;
};

class FragmentShaderBuilder : public ShaderBuilder {
 public:
  explicit FragmentShaderBuilder(ProgramBuilder* program);

  ShaderStage shaderStage() const override {
    return ShaderStage::Fragment;
  }

  virtual std::string dstColor() = 0;

  /**
   * Reads one sampler at the named coordinate. The read becomes a typed TextureSample value in the
   * IR; the returned name labels that value for the printers. The subsets travel into the sample
   * itself, so no clamp statement is emitted here.
   */
  TextureReadResult readTexture(SamplerHandle handle, const std::string& coordName,
                                const std::string& strictSubset, const std::string& drawSubset,
                                bool* recorded, ProgramIRBuilder* irBuilder = nullptr,
                                ValueID samplerValue = INVALID_VALUE_ID,
                                SLType coordType = SLType::Void,
                                ValueID strictSubsetValue = INVALID_VALUE_ID,
                                ValueID drawSubsetValue = INVALID_VALUE_ID,
                                bool coverageSample = false);

  void onBeforeChildProcEmitCode(const FragmentProcessor* child) const;

  void onAfterChildProcEmitCode() const;

  void declareCustomOutputColor();

  /**
   * Registers the perspective divide of a projective texture coordinate in the IR and returns the
   * name of the resulting 2D coordinate. A Float2 coordinate needs no division and is returned
   * unchanged.
   */
  std::string emitPerspTextCoord(const CoordValue& coordVar);

  std::string clampToSubsets(const std::string& coordName, const std::string& extraSubset,
                             const std::string& subset) override;

 protected:
  virtual std::string colorOutputName() = 0;

  void recordFragmentInstruction(FragmentInstruction instruction) {
    if (fragmentProgramRecorder != nullptr) {
      fragmentProgramRecorder->instructions.push_back(std::move(instruction));
    }
  }

  friend class ProgramBuilder;
};
}  // namespace tgfx
