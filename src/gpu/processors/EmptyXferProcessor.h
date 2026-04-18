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

#include "XferProcessor.h"

namespace tgfx {
class EmptyXferProcessor : public XferProcessor {
 public:
  static const EmptyXferProcessor* GetInstance();

  std::string name() const override {
    return "EmptyXferProcessor";
  }

  void setData(UniformData*, UniformData*) const override {
  }

  void computeProcessorKey(Context* context, BytesKey* bytesKey) const override;

 protected:
  EmptyXferProcessor() : XferProcessor(ClassID()) {
  }

  std::string shaderFunctionFile() const override {
    return "xfer/empty_xfer.frag";
  }

  ShaderCallManifest buildXferCallStatement(const std::string& colorInVar,
                                            const std::string& coverageInVar,
                                            const std::string& outputVar,
                                            const std::string& /*dstColorExpr*/,
                                            const MangledUniforms& /*uniforms*/,
                                            const MangledSamplers& /*samplers*/) const override {
    // XP functions write to an out-parameter (outputVar) instead of returning a value, so we
    // use declareOutput=false and let RenderManifest emit `TGFX_EmptyXP_FS(args..., outputVar);`.
    ShaderCallManifest manifest;
    manifest.functionName = "TGFX_EmptyXP_FS";
    manifest.outputVarName = outputVar;
    manifest.declareOutput = false;
    manifest.argExpressions = {colorInVar, coverageInVar};
    return manifest;
  }

 private:
  DEFINE_PROCESSOR_CLASS_ID
};
}  // namespace tgfx
