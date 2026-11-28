/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include <cstdint>
#include <string>
#include <vector>
#include "gpu/SLType.h"
#include "gpu/ir/ValueRef.h"
#include "tgfx/gpu/ShaderStage.h"

namespace tgfx {

using AttributeID = uint16_t;
using UniformID = uint16_t;
using VaryingID = uint16_t;
using SamplerID = uint16_t;
constexpr VaryingID INVALID_VARYING = static_cast<VaryingID>(-1);

struct AttributeDesc {
  std::string name = {};
  SLType type = SLType::Void;
  AttributeID id = 0;
};

struct VaryingDesc {
  std::string name = {};
  SLType type = SLType::Void;
  VaryingID id = 0;
  uint8_t location = 0;
  bool produced = false;
  bool flat = false;
};

struct UniformDesc {
  std::string name = {};
  SLType type = SLType::Void;
  ShaderStage stage = ShaderStage::Fragment;
  uint32_t count = 1;
  UniformID id = 0;
};

struct SamplerDesc {
  std::string name = {};
  SLType type = SLType::Void;
  SamplerID id = 0;
};

/// The stage interface the emitter declared: attributes, varyings, uniforms and samplers, in
/// declaration order. The GL printer reads the same handlers for now; the description is the
/// single place later backends will read.

}  // namespace tgfx
