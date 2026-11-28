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

struct Std140Layout {
  uint32_t size = 0;
  uint32_t alignment = 0;
  uint32_t arrayStride = 0;
};

Std140Layout LayoutOf(SLType type) {
  switch (type) {
    case SLType::Float:
      return {4, 4, 16};
    case SLType::Float2:
      return {8, 8, 16};
    case SLType::Float3:
      return {12, 16, 16};
    case SLType::Float4:
      return {16, 16, 16};
    case SLType::Float2x2:
      return {32, 16, 32};
    case SLType::Float3x3:
      return {48, 16, 48};
    case SLType::Float4x4:
      return {64, 16, 64};
    case SLType::Int:
      return {4, 4, 16};
    case SLType::Int2:
      return {8, 8, 16};
    case SLType::Int3:
      return {12, 16, 16};
    case SLType::Int4:
      return {16, 16, 16};
    default:
      return {};
  }
}

uint32_t AlignUp(uint32_t value, uint32_t alignment) {
  if (alignment == 0) {
    return value;
  }
  return (value + alignment - 1) / alignment * alignment;
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
  value.debugName = debugName;
  const auto id = value.id;
  ir.values.push_back(std::move(value));
  if (!debugName.empty()) {
    valuesByName.emplace(debugName, id);
  }
  return id;
}

ValueID ProgramIRBuilder::InternValue(const std::string& name, SLType type, Precision precision,
                                      IRValueKind kind) {
  if (name.empty()) {
    return CreateValue(type, precision, kind);
  }
  const auto found = valuesByName.find(name);
  if (found != valuesByName.end()) {
    auto& value = ir.values[found->second];
    if (value.type == SLType::Void && type != SLType::Void) {
      value.type = type;
    }
    return found->second;
  }
  return CreateValue(type, precision, kind, name);
}

const IRValue* ProgramIRBuilder::FindValue(const std::string& name) const {
  const auto found = valuesByName.find(name);
  if (found == valuesByName.end()) {
    return nullptr;
  }
  return &ir.values[found->second];
}

ValueID ProgramIRBuilder::AddAttribute(AttributeIR attribute) {
  for (const auto& existing : ir.interface.attributes) {
    if (existing.name == attribute.name) {
      return InternValue(existing.name, existing.type, existing.precision, IRValueKind::Attribute);
    }
  }
  const auto precision = Precision::High;
  attribute.location = nextAttributeLocation++;
  const auto id = InternValue(attribute.name, attribute.type, precision, IRValueKind::Attribute);
  ResourceBinding binding = {};
  binding.kind = BindingKind::Attribute;
  binding.location = attribute.location;
  binding.stage = ShaderStage::Vertex;
  binding.type = attribute.type;
  binding.precision = precision;
  ir.interface.bindings.push_back(binding);
  ir.interface.attributes.push_back(std::move(attribute));
  return id;
}

ValueID ProgramIRBuilder::AddVarying(VaryingIR varying) {
  for (const auto& existing : ir.interface.varyings) {
    if (existing.name == varying.name) {
      return InternValue(existing.name, existing.type, existing.precision, IRValueKind::Varying);
    }
  }
  const auto precision = Precision::High;
  varying.location = nextVaryingLocation++;
  const auto id = InternValue(varying.name, varying.type, precision, IRValueKind::Varying);
  for (const auto stage : {ShaderStage::Vertex, ShaderStage::Fragment}) {
    ResourceBinding binding = {};
    binding.kind = BindingKind::Varying;
    binding.location = varying.location;
    binding.stage = stage;
    binding.type = varying.type;
    binding.precision = precision;
    ir.interface.bindings.push_back(binding);
  }
  ir.interface.varyings.push_back(std::move(varying));
  return id;
}

bool ProgramIRBuilder::HasUniformBinding(ShaderStage stage) const {
  for (const auto& binding : ir.interface.bindings) {
    if (binding.kind == BindingKind::UniformBuffer && binding.stage == stage) {
      return true;
    }
  }
  return false;
}

UniformBlockIR& ProgramIRBuilder::EnsureUniformBlock(ShaderStage stage, uint32_t binding) {
  for (auto& block : ir.interface.uniformBlocks) {
    if (block.stage == stage) {
      return block;
    }
  }
  UniformBlockIR block = {};
  block.stage = stage;
  block.binding = binding;
  block.name = stage == ShaderStage::Vertex ? "VertexUniforms" : "FragmentUniforms";
  ir.interface.uniformBlocks.push_back(block);
  return ir.interface.uniformBlocks.back();
}

ValueID ProgramIRBuilder::AddUniform(UniformIR uniform) {
  for (const auto& existing : ir.interface.uniforms) {
    if (existing.name == uniform.name && existing.stage == uniform.stage) {
      return InternValue(existing.name, existing.type, existing.precision,
                         IRValueKind::Uniform);
    }
  }
  uniform.binding = uniform.stage == ShaderStage::Vertex ? 0 : 1;
  const auto layout = LayoutOf(uniform.type);
  auto& cursor = uniform.stage == ShaderStage::Vertex ? vertexUniformCursor : fragmentUniformCursor;
  if (layout.size != 0) {
    uniform.offset = AlignUp(cursor, layout.alignment);
    uniform.arrayStride = layout.arrayStride;
    uniform.size = uniform.count > 1 ? layout.arrayStride * uniform.count : layout.size;
    cursor = uniform.offset + uniform.size;
  }
  const auto id = InternValue(uniform.name, uniform.type, uniform.precision, IRValueKind::Uniform);
  if (!HasUniformBinding(uniform.stage)) {
    ResourceBinding binding = {};
    binding.kind = BindingKind::UniformBuffer;
    binding.binding = uniform.binding;
    binding.stage = uniform.stage;
    binding.type = SLType::Void;
    binding.precision = Precision::Default;
    ir.interface.bindings.push_back(binding);
  }
  auto& block = EnsureUniformBlock(uniform.stage, uniform.binding);
  block.size = AlignUp(cursor, 16);
  ir.interface.uniforms.push_back(std::move(uniform));
  return id;
}

ValueID ProgramIRBuilder::AddSampler(SamplerIR sampler) {
  for (const auto& existing : ir.interface.samplers) {
    if (existing.name == sampler.name) {
      return InternValue(existing.name, existing.type, Precision::Default, IRValueKind::Sampler);
    }
  }
  sampler.binding = nextSamplerBinding++;
  const auto id = InternValue(sampler.name, sampler.type, Precision::Default, IRValueKind::Sampler);
  ResourceBinding binding = {};
  binding.kind = BindingKind::Sampler;
  binding.binding = sampler.binding;
  binding.stage = ShaderStage::Fragment;
  binding.type = sampler.type;
  binding.precision = Precision::Default;
  ir.interface.bindings.push_back(binding);
  ir.interface.samplers.push_back(std::move(sampler));
  return id;
}

ValueID ProgramIRBuilder::AddExpr(IRExpr expr, const std::string& debugName) {
  if (!CanAppend(ir.exprs.size())) {
    return INVALID_VALUE_ID;
  }
  const auto result = CreateValue(expr.type, Precision::High, IRValueKind::Expr, debugName);
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

ValueID ProgramIRBuilder::CreateBinary(IRBinaryOp op, SLType type, ValueID left, ValueID right,
                                       std::string debugName) {
  IRExpr expr = {};
  expr.kind = IRExprKind::Binary;
  expr.type = type;
  expr.binaryOp = op;
  expr.args = {left, right};
  return AddExpr(std::move(expr), debugName);
}

ValueID ProgramIRBuilder::CreateCall(OperatorID op, SLType type, const std::vector<ValueID>& args,
                                     std::string debugName) {
  IRExpr expr = {};
  expr.kind = IRExprKind::Call;
  expr.type = type;
  expr.op = op;
  expr.args = args;
  return AddExpr(std::move(expr), debugName);
}

ValueID ProgramIRBuilder::CreateBuiltin(const std::string& builtin, SLType type) {
  IRExpr expr = {};
  expr.kind = IRExprKind::Builtin;
  expr.type = type;
  expr.builtin = builtin;
  return AddExpr(std::move(expr), builtin);
}

ValueID ProgramIRBuilder::CreateTextureSample(ValueID sampler, ValueID coordinate, SLType type,
                                              std::string debugName, SLType samplerType,
                                              int32_t samplerSwizzleCode, bool subsetClamped,
                                              ValueID strictSubset, ValueID drawSubset,
                                              bool coverageSample) {
  IRExpr expr = {};
  expr.kind = IRExprKind::TextureSample;
  expr.type = type;
  expr.args = {sampler, coordinate};
  expr.samplerType = samplerType;
  expr.samplerSwizzleCode = samplerSwizzleCode;
  expr.coverageSample = coverageSample;
  expr.subsetClamped = subsetClamped;
  expr.strictSubset = strictSubset;
  expr.drawSubset = drawSubset;
  return AddExpr(std::move(expr), debugName);
}

void ProgramIRBuilder::SetStage(ShaderStage stage) {
  currentStage = stage;
}

void ProgramIRBuilder::BeginForLoop(const std::string& varName, ValueID init, ValueID bound,
                                    ValueID step) {
  IRStatement statement = {};
  statement.kind = IRStatementKind::ForLoopBegin;
  statement.loopVarName = varName;
  statement.loopInit = init;
  statement.loopBound = bound;
  statement.loopStep = step;
  (currentStage == ShaderStage::Vertex ? ir.vertexStatements : ir.statements)
      .push_back(std::move(statement));
  // The counter is an IR value the body references by name.
  CreateValue(SLType::Int, Precision::High, IRValueKind::Local, varName);
}

void ProgramIRBuilder::EndForLoop() {
  IRStatement statement = {};
  statement.kind = IRStatementKind::ForLoopEnd;
  (currentStage == ShaderStage::Vertex ? ir.vertexStatements : ir.statements)
      .push_back(std::move(statement));
}

ValueID ProgramIRBuilder::CreateConvert(ValueID source, SLType target) {
  if (source == INVALID_VALUE_ID || source >= ir.values.size()) {
    return INVALID_VALUE_ID;
  }
  if (ir.values[source].type == target) {
    return source;
  }
  IRExpr expr = {};
  expr.kind = IRExprKind::Convert;
  expr.type = target;
  expr.args = {source};
  return AddExpr(std::move(expr), {});
}

void ProgramIRBuilder::Assign(LValue dst, ValueID src) {
  IRStatement statement = {};
  statement.kind = IRStatementKind::Assign;
  statement.dst = std::move(dst);
  statement.src = src;
  (currentStage == ShaderStage::Vertex ? ir.vertexStatements : ir.statements)
      .push_back(std::move(statement));
}

void ProgramIRBuilder::StoreOutput(ValueID dst, ValueID src) {
  IRStatement statement = {};
  statement.kind = IRStatementKind::StoreOutput;
  statement.dst.base = dst;
  statement.src = src;
  (currentStage == ShaderStage::Vertex ? ir.vertexStatements : ir.statements)
      .push_back(std::move(statement));
}

void ProgramIRBuilder::AppendRawText(std::string code, std::string declaredName) {
  IRStatement statement = {};
  statement.kind = IRStatementKind::RawText;
  statement.rawText = std::move(code);
  statement.rawDeclaredName = std::move(declaredName);
  (currentStage == ShaderStage::Vertex ? ir.vertexStatements : ir.statements)
      .push_back(std::move(statement));
}

void ProgramIRBuilder::CreateDiscard(ValueID condition) {
  IRStatement statement = {};
  statement.kind = IRStatementKind::Discard;
  statement.src = condition;
  (currentStage == ShaderStage::Vertex ? ir.vertexStatements : ir.statements)
      .push_back(std::move(statement));
}

void ProgramIRBuilder::StoreVarying(LValue dst, ValueID src) {
  IRStatement statement = {};
  statement.kind = IRStatementKind::StoreVarying;
  statement.dst = std::move(dst);
  statement.src = src;
  (currentStage == ShaderStage::Vertex ? ir.vertexStatements : ir.statements)
      .push_back(std::move(statement));
}

}  // namespace tgfx
