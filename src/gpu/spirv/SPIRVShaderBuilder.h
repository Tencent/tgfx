/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except
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

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "gpu/ir/OperatorBundle.h"
#include "gpu/ir/ProgramIR.h"
#include "gpu/spirv/SPIRVWriter.h"

namespace tgfx {

/// The SPIR-V counterpart of FragmentShaderBuilder. It owns types, constants, the entry point,
/// the output variable and the emitted value ids. It does not own operator bodies; those come
/// from OperatorBundle in a later phase.
class SPIRVShaderBuilder {
 public:
  explicit SPIRVShaderBuilder(ShaderStage stage);
  ~SPIRVShaderBuilder() = default;

  void SetOperatorBundle(const OperatorBundle* value) {
    operatorBundle = value;
  }

  bool Begin(std::string* error = nullptr);

  /// Declares function-local variables for values that are assigned in the IR. Must be called
  /// after Begin and before any other value is emitted so the variables stay in the entry block.
  bool DeclareLocals(const ProgramIR& program, std::string* error = nullptr);

  bool Assign(const ProgramIR& program, const LValue& dst, ValueID value,
              std::string* error = nullptr);

  bool StoreVarying(const ProgramIR& program, const LValue& dst, ValueID value,
                    std::string* error = nullptr);

  /// Emits the defining expression of value and returns its SPIR-V result id.
  bool EmitValue(const ProgramIR& program, ValueID value, uint32_t* resultId,
                 std::string* error = nullptr);

  bool StoreOutput(const ProgramIR& program, ValueID value, std::string* error = nullptr);

  bool StoreOutput(const ProgramIR& program, ValueID dst, ValueID value,
                   std::string* error = nullptr);

  /// Imports a bundle function and returns its module-local id. The declaration is appended after
  /// the main function in Finish so a function is never declared inside another function.
  uint32_t ImportFunction(const OperatorDesc& desc, const OperatorArtifact& artifact,
                          std::string* error = nullptr);

  /// Emits OpFunctionCall for an imported function and records the result value.
  uint32_t EmitFunctionCall(ValueID result, SLType returnType, const std::vector<uint32_t>& args,
                            uint32_t functionId, std::string* error = nullptr);

  std::vector<uint32_t> Finish(std::string* error = nullptr);

 private:
  uint32_t TypeId(SLType type, std::string* error);
  uint32_t BoolTypeId(std::string* error);
  uint32_t BoolVectorTypeId(uint32_t components, std::string* error);
  uint32_t ConstantId(const IRExpr& expr, std::string* error);
  uint32_t IntConstantId(int32_t value, std::string* error);
  uint32_t ArrayTypeId(SLType elementType, uint32_t count, uint32_t stride, bool decorateStride,
                       std::string* error);
  uint32_t FunctionParamTypeId(const OperatorParam& param, std::string* error);
  void EnsureLinkageCapability();
  uint32_t EnsureAttribute(const ProgramIR& program, const IRValue& value, std::string* error);
  uint32_t EnsureVarying(const ProgramIR& program, const IRValue& value, std::string* error);
  uint32_t EmitValueAsValue(const ProgramIR& program, ValueID value, std::string* error);
  void AddEntryPointInterface(uint32_t id);
  uint32_t LoadSampler(const ProgramIR& program, ValueID sampler, std::string* error);
  /// Declares the sampler's descriptor variable (UniformConstant storage) and returns its id —
  /// the form a linked operator call passes as its sampler parameter. LoadSampler loads this.
  uint32_t EnsureSamplerVariable(const ProgramIR& program, const IRValue& value,
                                 std::string* error);  bool EnsureUniformBlock(const ProgramIR& program, std::string* error);
  uint32_t EmitUniformValue(const ProgramIR& program, const IRValue& value, std::string* error);
  uint32_t EmitUniformArrayValue(const ProgramIR& program, const IRValue& value,
                                 std::string* error);
  uint32_t EmitExpr(const ProgramIR& program, const IRExpr& expr, std::string* error);
  uint32_t PointerTypeId(SLType type, uint32_t storageClass, std::string* error);
  uint32_t PointerTypeIdFor(uint32_t pointeeType, uint32_t storageClass, std::string* error);

  ShaderStage stage = ShaderStage::Fragment;
  spirv::Writer writer = {};
  std::unordered_map<uint32_t, uint32_t> typeIds = {};
  std::unordered_map<uint64_t, uint32_t> constantIds = {};
  std::unordered_map<uint64_t, uint32_t> pointerTypeIds = {};
  std::unordered_map<uint32_t, uint32_t> valueIds = {};
  struct ImportedFunction {
    uint32_t id = 0;
    uint32_t functionType = 0;
    uint32_t returnTypeId = 0;
    std::vector<uint32_t> paramTypeIds = {};
    std::string symbol = {};
  };

  uint32_t mainId = 0;
  uint32_t labelId = 0;
  uint32_t outputPointerId = 0;
  uint32_t outputId = 0;
  uint32_t fragCoordId = 0;
  uint32_t boolTypeId = 0;
  std::unordered_map<uint32_t, uint32_t> boolVectorTypeIds = {};
  uint32_t imageTypeId = 0;
  uint32_t samplerTypeId = 0;
  uint32_t sampledImageTypeId = 0;
  uint32_t uniformBlockTypeId = 0;
  uint32_t uniformBlockPointerId = 0;
  uint32_t uniformBlockVariableId = 0;
  bool uniformBlockDeclared = false;
  bool linkageCapabilityAdded = false;
  std::unordered_map<uint32_t, uint32_t> samplerValues = {};
  std::unordered_map<std::string, uint32_t> samplerVariables = {};
  /// The SPIR-V ids passed as linked-call arguments that are already pointers (the sampler
  /// descriptor variables): EmitFunctionCall forwards them instead of staging them into a
  /// Function-storage local.
  std::unordered_set<uint32_t> samplerPointerArgs = {};
  std::unordered_map<uint32_t, uint32_t> varyingVariables = {};
  std::unordered_map<std::string, uint32_t> uniformMemberIndices = {};
  std::unordered_map<std::string, uint32_t> uniformArrayTypeIds = {};
  std::unordered_map<std::string, uint32_t> uniformArrayPointerIds = {};
  std::vector<ImportedFunction> importedFunctions = {};
  std::vector<std::vector<uint32_t>> importedModules = {};
  const OperatorBundle* operatorBundle = nullptr;
  bool begun = false;
  bool finished = false;
};

class SPIRVFragmentShaderBuilder : public SPIRVShaderBuilder {
 public:
  SPIRVFragmentShaderBuilder();
};

class SPIRVVertexShaderBuilder : public SPIRVShaderBuilder {
 public:
  SPIRVVertexShaderBuilder();
};

}  // namespace tgfx
