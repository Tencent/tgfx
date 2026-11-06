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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/spec/SpecInjector.h"
#include "gpu/aot/spec/SpecInject.h"

namespace tgfx {

namespace {

/// Driver source text (GLSL today): rewrites the generated TGFX_SPEC_<id> declarations, so the
/// driver's compiler folds the accessors before it ever sees the code.
class SourceRewriteInjector final : public SpecInjector {
 public:
  bool Apply(const std::string& code, const std::vector<SpecFieldInfo>& fields,
             const SpecValues& values, SpecInjection* injection) const override {
    auto specialized = InjectSpecGLSL(code, fields, values);
    if (specialized.empty()) {
      return false;
    }
    injection->code = std::move(specialized);
    return true;
  }
};

/// A precompiled container resolved with function/pipeline constants (Metal's metallib today,
/// Vulkan's SPIR-V specialization map later): the values join the pipeline descriptor.
class PipelineConstantInjector final : public SpecInjector {
 public:
  bool Apply(const std::string& /*code*/, const std::vector<SpecFieldInfo>& fields,
             const SpecValues& values, SpecInjection* injection) const override {
    AppendSpecConstants(fields, values, &injection->constants);
    return !injection->constants.empty();
  }
};

}  // namespace

const SpecInjector* FindSpecInjector(ShaderCodeFormat format) {
  static const SourceRewriteInjector sourceRewrite;
  static const PipelineConstantInjector pipelineConstants;
  switch (format) {
    case ShaderCodeFormat::GLSL:
      return &sourceRewrite;
    case ShaderCodeFormat::METALLIB:
      return &pipelineConstants;
    default:
      // MSL/DXBC/HLSL/WGSL/SPIRV: the channels exist but are not wired to a consumer yet, so the
      // draw serves the generic form instead of a specialization nothing would read.
      return nullptr;
  }
}

}  // namespace tgfx
