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

#include <cstdint>
#include <string>
#include <vector>
#include "gpu/SLType.h"
#include "gpu/ir/ValueRef.h"
#include "tgfx/gpu/ShaderStage.h"

namespace tgfx {

using ExprID = uint32_t;
using BindingID = uint32_t;
using OperatorID = uint32_t;

constexpr ExprID INVALID_EXPR_ID = static_cast<ExprID>(-1);
constexpr BindingID INVALID_BINDING_ID = static_cast<BindingID>(-1);
constexpr OperatorID INVALID_OPERATOR_ID = static_cast<OperatorID>(-1);

/// Temporary stable IDs for the dual-write phase; the generated operator table replaces this.
enum class BuiltinOperator : OperatorID {
  Luma = 1,
  ConstColor = 2,
  ColorMatrix = 3,
  AlphaThreshold = 4,
  YUVToRGB = 5,
  Clamp = 6,
  DeviceCoord = 7,
  MulAlpha = 8,
  AlphaModulate = 9,
};

enum class Precision : uint8_t {
  Default,
  Low,
  Medium,
  High,
};

enum class IRValueKind : uint8_t {
  Constant,
  Attribute,
  Varying,
  Uniform,
  Sampler,
  Local,
  Expr,
  Field,
};

/// One SSA value. The debug name is only a printing aid; the ID and type are the semantics.
struct IRValue {
  ValueID id = INVALID_VALUE_ID;
  SLType type = SLType::Void;
  Precision precision = Precision::Default;
  IRValueKind kind = IRValueKind::Local;
  ExprID definingExpr = INVALID_EXPR_ID;
  std::string debugName = {};
};

enum class IRBinaryOp : uint8_t {
  Add,
  Sub,
  Mul,
  Div,
};

enum class IRExprKind : uint8_t {
  ConstantFloat,
  ConstantInt,
  Load,
  Swizzle,
  Construct,
  Binary,
  Call,
  TextureSample,
  Builtin,
};

/// One typed expression. Values are always referenced by ValueID; the constant payload is kept as
/// IEEE-754 bits so text backends cannot lose precision before they format it.
struct IRExpr {
  ExprID id = INVALID_EXPR_ID;
  IRExprKind kind = IRExprKind::Call;
  ValueID result = INVALID_VALUE_ID;
  std::vector<ValueID> args = {};
  SLType type = SLType::Void;
  uint32_t constantBits = 0;
  OperatorID op = INVALID_OPERATOR_ID;
  IRBinaryOp binaryOp = IRBinaryOp::Add;
  std::string swizzle = {};
  std::string builtin = {};
  SLType samplerType = SLType::Void;
  int32_t samplerSwizzleCode = -1;
  bool subsetClamped = false;
  ValueID strictSubset = INVALID_VALUE_ID;
  ValueID drawSubset = INVALID_VALUE_ID;
};

/// A destination value, optionally narrowed to a swizzle such as yuv.x.
struct LValue {
  ValueID base = INVALID_VALUE_ID;
  std::string swizzle = {};
};

enum class IRStatementKind : uint8_t {
  Assign,
  Call,
  StoreOutput,
  StoreVarying,
  Return,
};

struct IRStatement {
  IRStatementKind kind = IRStatementKind::Assign;
  LValue dst = {};
  ValueID src = INVALID_VALUE_ID;
  ExprID expr = INVALID_EXPR_ID;
};

enum class BindingKind : uint8_t {
  Attribute,
  Varying,
  UniformBuffer,
  Sampler,
  PushConstant,
};

struct ResourceBinding {
  BindingKind kind = BindingKind::UniformBuffer;
  uint32_t set = 0;
  uint32_t binding = 0;
  uint32_t location = 0;
  ShaderStage stage = ShaderStage::Fragment;
  SLType type = SLType::Void;
  Precision precision = Precision::Default;
};

struct AttributeIR {
  std::string name = {};
  SLType type = SLType::Void;
  Precision precision = Precision::Default;
  uint32_t location = 0;
};

struct VaryingIR {
  std::string name = {};
  SLType type = SLType::Void;
  Precision precision = Precision::Default;
  uint32_t location = 0;
  bool flat = false;
};

struct UniformIR {
  std::string name = {};
  SLType type = SLType::Void;
  Precision precision = Precision::Default;
  ShaderStage stage = ShaderStage::Fragment;
  uint32_t count = 1;
  uint32_t binding = 0;
  /// std140 offsets and sizes, matching the CPU-side UniformData layout.
  uint32_t offset = 0;
  uint32_t size = 0;
  uint32_t arrayStride = 0;
};

struct UniformBlockIR {
  ShaderStage stage = ShaderStage::Fragment;
  uint32_t binding = 0;
  uint32_t size = 0;
};

struct SamplerIR {
  std::string name = {};
  SLType type = SLType::Void;
  uint32_t binding = 0;
};

struct InterfaceIR {
  std::vector<AttributeIR> attributes = {};
  std::vector<VaryingIR> varyings = {};
  std::vector<UniformIR> uniforms = {};
  std::vector<UniformBlockIR> uniformBlocks = {};
  std::vector<SamplerIR> samplers = {};
  std::vector<ResourceBinding> bindings = {};
};

/// The backend-neutral program representation. Values are SSA, expressions only refer to earlier
/// values, and statements preserve the FP tree's emission order.
struct ProgramIR {
  InterfaceIR interface = {};
  std::vector<IRValue> values = {};
  std::vector<IRExpr> exprs = {};
  std::vector<IRStatement> statements = {};
};

/// Returns the IEEE-754 binary32 bits of a float, without a double round-trip.
uint32_t FloatBits(float value);

}  // namespace tgfx
