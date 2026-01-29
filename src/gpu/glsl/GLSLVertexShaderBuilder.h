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

#include "gpu/VertexShaderBuilder.h"

namespace tgfx {
class GLSLVertexShaderBuilder : public VertexShaderBuilder {
 public:
  explicit GLSLVertexShaderBuilder(ProgramBuilder* program);

  void emitNormalizedPosition(const std::string& devPos) override;

  void emitTransformedPoint(const std::string& dstPointName, const std::string& srcPointName,
                            const std::string& transformName, bool hasPerspective) override;
};
}  // namespace tgfx
