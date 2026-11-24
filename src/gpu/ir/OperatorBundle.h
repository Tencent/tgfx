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
#include "gpu/ir/OperatorManifest.h"

namespace tgfx {

enum class OperatorBackend : uint8_t {
  GLSL,
  SPIRV,
  MSL,
  HLSL,
  WGSL,
};

/// One backend function for an operator. Text backends carry source; Vulkan carries the
/// precompiled function module words. The manifest remains the single source of the logical
/// signature; this artifact is the physical function the emitter calls.
struct OperatorArtifact {
  OperatorID id = INVALID_OPERATOR_ID;
  OperatorBackend backend = OperatorBackend::GLSL;
  std::string symbol = {};
  std::string source = {};
  std::vector<uint32_t> spirv = {};
};

class OperatorBundle {
 public:
  explicit OperatorBundle(std::vector<OperatorArtifact> artifacts = {});

  const OperatorArtifact* Find(OperatorID id, OperatorBackend backend) const;

 private:
  std::vector<OperatorArtifact> artifacts = {};
};

}  // namespace tgfx
