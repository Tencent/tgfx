/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <cinttypes>

namespace tgfx {
/**
 * Defines flags that can be passed to the rendering process.
 */
class RenderFlags {
 public:
  /**
   * Skips generating any new caches to the associated Context during the rendering process.
   */
  static constexpr uint32_t DisableCache = 1 << 0;

  /**
   * Performs all CPU-side tasks in the current thread rather than run them in parallel
   * asynchronously.
   */
  static constexpr uint32_t DisableAsyncTask = 1 << 1;
};
}  // namespace tgfx
