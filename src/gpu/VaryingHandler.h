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

#include <vector>
#include "gpu/ShaderVar.h"
#include "tgfx/gpu/ShaderStage.h"

namespace tgfx {
class ProgramBuilder;
class GeometryProcessor;

class Varying {
 public:
  const std::string& vsOut() const {
    return _name;
  }

  const std::string& fsIn() const {
    return _name;
  }

  const std::string& name() const {
    return _name;
  }

  SLType type() const {
    return _type;
  }

 private:
  SLType _type = SLType::Void;
  std::string _name;
  bool _isFlat = false;

  friend class VaryingHandler;
};

class VaryingHandler {
 public:
  explicit VaryingHandler(ProgramBuilder* program) : programBuilder(program) {
  }

  virtual ~VaryingHandler() = default;

  Varying addVarying(const std::string& name, SLType type, bool isFlat = false);

  void emitAttributes(const GeometryProcessor& processor);

  /**
   * This should be called once all attributes and varyings have been added to the VaryingHandler
   * and before getting/adding any of the declarations to the shaders.
   */
  void finalize();

  void getDeclarations(std::string* inputDecls, std::string* outputDecls, ShaderStage stage) const;

 private:
  void appendDecls(const std::vector<ShaderVar>& vars, std::string* out, ShaderStage stage) const;

  void addAttribute(const ShaderVar& var);

  std::vector<Varying> varyings;
  std::vector<ShaderVar> vertexInputs;
  std::vector<ShaderVar> vertexOutputs;
  std::vector<ShaderVar> fragInputs;

  // This is not owned by the class
  ProgramBuilder* programBuilder;
};
}  // namespace tgfx
