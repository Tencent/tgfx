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

#include "gpu/ir/ProgramIRBuilder.h"

namespace tgfx {
namespace {

bool CanAppend(size_t count) {
  return count < static_cast<size_t>(INVALID_VALUE_ID);
}

}  // namespace

ValueID ProgramIRBuilder::CreateValue(SLType type, Precision precision, IRValueKind kind,
                                      std::string debugName) {
  if (!CanAppend(ir.values.size())) {
    return INVALID_VALUE_ID;
  }
  IRValue value = {};
  value.id = static_cast<ValueID>(ir.values.size());
  value.type = type;
  value.precision = precision;
  value.kind = kind;
  value.debugName = std::move(debugName);
  ir.values.push_back(std::move(value));
  return ir.values.back().id;
}

ValueID ProgramIRBuilder::AddExpr(IRExpr expr) {
  if (!CanAppend(ir.exprs.size())) {
    return INVALID_VALUE_ID;
  }
  const auto result = CreateValue(expr.type, Precision::High, IRValueKind::Expr);
  if (result == INVALID_VALUE_ID) {
    return INVALID_VALUE_ID;
  }
  expr.id = static_cast<ExprID>(ir.exprs.size());
  expr.result = result;
  ir.values[result].definingExpr = expr.id;
  ir.exprs.push_back(std::move(expr));
  return result;
}

ValueID ProgramIRBuilder::CreateConstant(float value, Precision precision) {
  IRExpr expr = {};
  expr.kind = IRExprKind::ConstantFloat;
  expr.type = SLType::Float;
  expr.constantBits = FloatBits(value);
  const auto result = AddExpr(std::move(expr));
  if (result != INVALID_VALUE_ID) {
    ir.values[result].kind = IRValueKind::Constant;
    ir.values[result].precision = precision;
  }
  return result;
}

ValueID ProgramIRBuilder::CreateIntConstant(int32_t value) {
  IRExpr expr = {};
  expr.kind = IRExprKind::ConstantInt;
  expr.type = SLType::Int;
  expr.constantBits = static_cast<uint32_t>(value);
  const auto result = AddExpr(std::move(expr));
  if (result != INVALID_VALUE_ID) {
    ir.values[result].kind = IRValueKind::Constant;
    ir.values[result].precision = Precision::High;
  }
  return result;
}

ValueID ProgramIRBuilder::CreateLoad(ValueID source, SLType type, Precision precision) {
  IRExpr expr = {};
  expr.kind = IRExprKind::Load;
  expr.type = type;
  expr.args = {source};
  const auto result = AddExpr(std::move(expr));
  if (result != INVALID_VALUE_ID) {
    ir.values[result].precision = precision;
  }
  return result;
}

ValueID ProgramIRBuilder::CreateSwizzle(ValueID source, SLType type, std::string swizzle) {
  IRExpr expr = {};
  expr.kind = IRExprKind::Swizzle;
  expr.type = type;
  expr.args = {source};
  expr.swizzle = std::move(swizzle);
  return AddExpr(std::move(expr));
}

ValueID ProgramIRBuilder::CreateConstruct(SLType type, const std::vector<ValueID>& args) {
  IRExpr expr = {};
  expr.kind = IRExprKind::Construct;
  expr.type = type;
  expr.args = args;
  return AddExpr(std::move(expr));
}

ValueID ProgramIRBuilder::CreateBinary(IRBinaryOp op, SLType type, ValueID left, ValueID right) {
  IRExpr expr = {};
  expr.kind = IRExprKind::Binary;
  expr.type = type;
  expr.binaryOp = op;
  expr.args = {left, right};
  return AddExpr(std::move(expr));
}

ValueID ProgramIRBuilder::CreateCall(OperatorID op, SLType type, const std::vector<ValueID>& args) {
  IRExpr expr = {};
  expr.kind = IRExprKind::Call;
  expr.type = type;
  expr.op = op;
  expr.args = args;
  return AddExpr(std::move(expr));
}

ValueID ProgramIRBuilder::CreateTextureSample(ValueID sampler, ValueID coordinate, SLType type) {
  IRExpr expr = {};
  expr.kind = IRExprKind::TextureSample;
  expr.type = type;
  expr.args = {sampler, coordinate};
  return AddExpr(std::move(expr));
}

void ProgramIRBuilder::Assign(LValue dst, ValueID src) {
  IRStatement statement = {};
  statement.kind = IRStatementKind::Assign;
  statement.dst = std::move(dst);
  statement.src = src;
  ir.statements.push_back(std::move(statement));
}

void ProgramIRBuilder::StoreOutput(ValueID dst, ValueID src) {
  IRStatement statement = {};
  statement.kind = IRStatementKind::StoreOutput;
  statement.dst.base = dst;
  statement.src = src;
  ir.statements.push_back(std::move(statement));
}

}  // namespace tgfx
