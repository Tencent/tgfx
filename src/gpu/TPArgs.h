/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "gpu/proxies/TextureProxy.h"

namespace tgfx {
/**
 * Arguments passed to create a TextureProxy.
 */
class TPArgs {
 public:
  TPArgs() = default;

  TPArgs(Context* context, uint32_t renderFlags, bool mipmapped,
         BackingFit backingFit = BackingFit::Approximate)
      : context(context), renderFlags(renderFlags), mipmapped(mipmapped), backingFit(backingFit) {
  }

  /**
   * The context to create the texture proxy.
   */
  Context* context = nullptr;

  /**
   * The render flags to create the texture proxy.
   */
  uint32_t renderFlags = 0;

  /**
   * Specifies whether the texture proxy should be mipmapped. This may be ignored if the associated
   * image already has preset mipmaps.
   */
  bool mipmapped = false;

  /**
   * Specifies whether the texture size should be approximated based on the width and height.
   */
  BackingFit backingFit = BackingFit::Approximate;
};
}  // namespace tgfx
