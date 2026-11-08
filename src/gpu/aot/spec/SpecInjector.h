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

#pragma once

#include <string>
#include <utility>
#include <vector>
#include "gpu/aot/spec/SpecValues.h"
#include "tgfx/gpu/ShaderModule.h"

namespace tgfx {

/// What one injection produced: the formats that rewrite driver source return the specialized
/// text (the caller swaps the module's code), the pipeline-constant formats append their table to
/// the descriptor (the caller mirrors it into the pipeline cache key).
struct SpecInjection {
  std::string code = {};
  std::vector<std::pair<std::string, double>> constants = {};
};

/// One backend format's specialization channel. The two channels differ in where the values land,
/// not in what they mean: source formats rewrite the generated declaration text before module
/// creation, pipeline-constant formats resolve the constants when the pipeline is created. A
/// format without an injector cannot specialize — the caller serves the generic form.
class SpecInjector {
 public:
  virtual ~SpecInjector() = default;

  /// Applies the captured values to one fragment artifact. Returns false when the artifact does
  /// not carry what this format expects (a lost declaration, an empty table), which the caller
  /// answers with the generic form — never with a dropped draw.
  virtual bool Apply(const std::string& code, const std::vector<SpecFieldInfo>& fields,
                     const SpecValues& values, SpecInjection* injection) const = 0;
};

/// The injector registered for one code format, or nullptr when the format cannot specialize.
/// This switch is the only place a format joins the specialization channel: GLSL and HLSL text
/// take source macros, metallib, SPIR-V and WGSL take the pipeline-constant table, and the two
/// precompiled containers that cannot take values (MSL text and DXBC) keep the generic form.
const SpecInjector* FindSpecInjector(ShaderCodeFormat format);

}  // namespace tgfx
