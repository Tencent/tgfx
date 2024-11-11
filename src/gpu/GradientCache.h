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

#include <list>
#include <unordered_map>
#include "core/PixelBuffer.h"
#include "tgfx/core/BytesKey.h"
#include "tgfx/core/Color.h"

namespace tgfx {
class GradientCache {
 public:
  std::shared_ptr<Texture> getGradient(Context* context, const Color* colors,
                                       const float* positions, int count);

  void releaseAll();

  bool empty() const;

 private:
  std::shared_ptr<Texture> find(const BytesKey& bytesKey);

  void add(const BytesKey& bytesKey, std::shared_ptr<Texture> texture);

  std::list<BytesKey> keys = {};
  BytesKeyMap<std::shared_ptr<Texture>> textures = {};
};
}  // namespace tgfx
