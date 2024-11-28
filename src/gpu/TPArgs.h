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

#include "gpu/ResourceKey.h"
#include "gpu/proxies/TextureProxy.h"

namespace tgfx {
/**
 * Arguments passed to create a TextureProxy.
 */
class TPArgs {
 public:
  TPArgs() = default;

  TPArgs(Context* context, uint32_t renderFlags, bool mipmapped, bool flattened = false,
         UniqueKey uniqueKey = {})
      : context(context), renderFlags(renderFlags), mipmapped(mipmapped), flattened(flattened),
        uniqueKey(std::move(uniqueKey)) {
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
   * Specifies whether the texture proxy should be flattened.
   */
  bool flattened = false;

  /**
   * The unique key assigned to the texture proxy. This may be ignored if the associated image
   * already has a unique key.
   */
  UniqueKey uniqueKey = {};
};
}  // namespace tgfx
