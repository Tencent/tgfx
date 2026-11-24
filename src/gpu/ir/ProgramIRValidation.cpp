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

#include "gpu/ir/ProgramIRValidation.h"

namespace tgfx {
namespace {

bool HasValue(const ProgramIR& program, ValueID id) {
  return id != INVALID_VALUE_ID && id < program.values.size();
}

bool ValidateExpr(const ProgramIR& program, const IRExpr& expr, std::string* error) {
  if (expr.id == INVALID_EXPR_ID || expr.id >= program.exprs.size()) {
    *error = "an expression has an invalid id";
    return false;
  }
  if (!HasValue(program, expr.result)) {
    *error = "expression " + std::to_string(expr.id) + " has an invalid result";
    return false;
  }
  const auto& result = program.values[expr.result];
  if (result.definingExpr != expr.id) {
    *error = "expression " + std::to_string(expr.id) + " is not the definition of its result";
    return false;
  }
  for (const auto argument : expr.args) {
    if (!HasValue(program, argument)) {
      *error = "expression " + std::to_string(expr.id) + " references an unknown value";
      return false;
    }
    if (argument >= expr.result) {
      *error = "expression " + std::to_string(expr.id) + " references a later value";
      return false;
    }
  }
  if (expr.kind == IRExprKind::ConstantFloat && expr.type != SLType::Float) {
    *error = "a float constant must have float type";
    return false;
  }
  if (expr.kind == IRExprKind::ConstantInt && expr.type != SLType::Int) {
    *error = "an integer constant must have int type";
    return false;
  }
  if (expr.kind == IRExprKind::Swizzle && (expr.args.size() != 1 || expr.swizzle.empty())) {
    *error = "a swizzle needs one operand and a swizzle spelling";
    return false;
  }
  if (expr.kind == IRExprKind::Binary && expr.args.size() != 2) {
    *error = "a binary expression needs two operands";
    return false;
  }
  if (expr.kind == IRExprKind::Call && expr.op == INVALID_OPERATOR_ID) {
    *error = "a call needs an operator id";
    return false;
  }
  if (expr.kind == IRExprKind::TextureSample && expr.args.size() != 2) {
    *error = "a texture sample needs a sampler and a coordinate";
    return false;
  }
  return true;
}

bool ValidateStatement(const ProgramIR& program, const IRStatement& statement, std::string* error) {
  switch (statement.kind) {
    case IRStatementKind::Assign:
      if (!HasValue(program, statement.dst.base) || !HasValue(program, statement.src)) {
        *error = "an assignment references an unknown value";
        return false;
      }
      break;
    case IRStatementKind::Call:
      if (statement.expr == INVALID_EXPR_ID || statement.expr >= program.exprs.size()) {
        *error = "a call statement references an unknown expression";
        return false;
      }
      break;
    case IRStatementKind::StoreOutput:
      if (!HasValue(program, statement.dst.base) || !HasValue(program, statement.src)) {
        *error = "an output store references an unknown value";
        return false;
      }
      break;
    case IRStatementKind::StoreVarying:
      if (!HasValue(program, statement.dst.base) || !HasValue(program, statement.src)) {
        *error = "a varying store references an unknown value";
        return false;
      }
      break;
    case IRStatementKind::Return:
      break;
  }
  return true;
}

bool ValidateInterface(const InterfaceIR& interface, std::string* error) {
  for (size_t i = 0; i < interface.bindings.size(); ++i) {
    const auto& first = interface.bindings[i];
    if (first.kind != BindingKind::Attribute && first.kind != BindingKind::Varying &&
        first.kind != BindingKind::Sampler) {
      continue;
    }
    for (size_t j = i + 1; j < interface.bindings.size(); ++j) {
      const auto& second = interface.bindings[j];
      if (first.kind != second.kind || first.stage != second.stage) {
        continue;
      }
      const bool duplicate = first.kind == BindingKind::Sampler ? first.binding == second.binding
                                                                : first.location == second.location;
      if (duplicate) {
        *error = "the interface contains a duplicate resource binding";
        return false;
      }
    }
  }
  return true;
}

}  // namespace

bool ValidateProgramIR(const ProgramIR& program, std::string* error) {
  if (error == nullptr) {
    return false;
  }
  for (size_t index = 0; index < program.values.size(); ++index) {
    const auto& value = program.values[index];
    if (value.id != static_cast<ValueID>(index)) {
      *error = "value ids must match their declaration order";
      return false;
    }
    if ((value.kind == IRValueKind::Expr || value.kind == IRValueKind::Constant) &&
        value.definingExpr == INVALID_EXPR_ID) {
      *error = "an expression value has no defining expression";
      return false;
    }
    if (value.kind != IRValueKind::Expr && value.kind != IRValueKind::Constant &&
        value.definingExpr != INVALID_EXPR_ID) {
      *error = "a resource or local value cannot have a defining expression";
      return false;
    }
  }
  for (const auto& expr : program.exprs) {
    if (!ValidateExpr(program, expr, error)) {
      return false;
    }
  }
  for (const auto& statement : program.statements) {
    if (!ValidateStatement(program, statement, error)) {
      return false;
    }
  }
  return ValidateInterface(program.interface, error);
}

}  // namespace tgfx
