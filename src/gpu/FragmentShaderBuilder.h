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

namespace tgfx {
class FragmentProcessor;

class FragmentShaderBuilder : public ShaderBuilder {
 public:
  explicit FragmentShaderBuilder(ProgramBuilder* program);

  virtual std::string dstColor() = 0;

  void onBeforeChildProcEmitCode(const FragmentProcessor* child);

  void onAfterChildProcEmitCode();

  void declareCustomOutputColor();

 protected:
  virtual std::string colorOutputName() = 0;

  static std::string CustomColorOutputName() {
    return "tgfx_FragColor";
  }

  void onFinalize() override;

  friend class ProgramBuilder;
};
}  // namespace tgfx
