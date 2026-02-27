/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include <string>
#include <vector>
#include "tgfx/gpu/Backend.h"

namespace tgfx {
/**
 * GPUInfo contains descriptive information about the GPU.
 */
class GPUInfo {
 public:
  /**
   * Returns the backend API used by the GPU.
   */
  Backend backend = Backend::Unknown;

  /**
   * Returns the version string of the GPU.
   */
  std::string version;

  /**
   * Returns the renderer string of the GPU.
   */
  std::string renderer;

  /**
   * Returns the vendor string of the GPU.
   */
  std::string vendor;

  /**
   * Returns a list of supported extensions by the GPU.
   */
  std::vector<std::string> extensions = {};
};
}  // namespace tgfx
