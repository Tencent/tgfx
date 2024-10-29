/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/gpu/Context.h"

namespace tgfx {
/**
 * DrawArgs represents the arguments passed to the draw method of a Layer.
 */
class DrawArgs {
 public:
  DrawArgs() = default;

  DrawArgs(Context* context, uint32_t renderFlags, bool cleanDirtyFlags = false)
      : context(context), renderFlags(renderFlags), cleanDirtyFlags(cleanDirtyFlags) {
  }

  // The GPU context to be used during the drawing process. Note: this could be nullptr.
  Context* context = nullptr;
  // Render flags to be used during the drawing process.
  uint32_t renderFlags = 0;
  // Whether to clean the dirty flags of the associated Layer during the drawing process.
  bool cleanDirtyFlags = false;
};
}  // namespace tgfx
