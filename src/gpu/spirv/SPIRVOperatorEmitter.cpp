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

#include "SPIRVOperatorEmitter.h"

namespace tgfx {
namespace {

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

}  // namespace

bool SPIRVOperatorEmitter::EmitCall(const ProgramIR& program, ValueID result,
                                    const OperatorBundle& bundle, SPIRVShaderBuilder* builder,
                                    std::string* error) {
  if (builder == nullptr) {
    return Fail(error, "the SPIR-V shader builder is null");
  }
  if (result == INVALID_VALUE_ID || result >= program.values.size()) {
    return Fail(error, "the call result has an invalid value");
  }
  const auto& value = program.values[result];
  if (value.definingExpr == INVALID_EXPR_ID || value.definingExpr >= program.exprs.size()) {
    return Fail(error, "the call result has no defining expression");
  }
  const auto& call = program.exprs[value.definingExpr];
  if (call.kind != IRExprKind::Call) {
    return Fail(error, "the value is not defined by an operator call");
  }
  const auto* desc = FindOperator(call.op);
  if (desc == nullptr) {
    return Fail(error, "the call references an unknown operator");
  }
  if (call.type != desc->returnType) {
    return Fail(error, "the call result type does not match the operator manifest");
  }
  if (call.args.size() != desc->params.size()) {
    return Fail(error, "the call argument count does not match the operator manifest");
  }
  for (size_t index = 0; index < call.args.size(); ++index) {
    const auto& argument = program.values[call.args[index]];
    if (argument.type != desc->params[index].type) {
      return Fail(error, "a call argument type does not match the operator manifest");
    }
  }
  const auto* artifact = bundle.Find(call.op, OperatorBackend::SPIRV);
  if (artifact == nullptr || artifact->symbol.empty() || artifact->spirv.empty()) {
    return Fail(error, "the operator bundle has no SPIR-V function for this operator");
  }

  const auto functionId = builder->ImportFunction(*desc, *artifact, error);
  if (functionId == 0) {
    return false;
  }
  std::vector<uint32_t> args = {};
  for (const auto argument : call.args) {
    uint32_t id = 0;
    if (!builder->EmitValue(program, argument, &id, error)) {
      return false;
    }
    args.push_back(id);
  }
  return builder->EmitFunctionCall(result, call.type, args, functionId, error) != 0;
}

}  // namespace tgfx
