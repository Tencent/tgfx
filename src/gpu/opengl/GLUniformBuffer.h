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

#include "GLBuffer.h"

namespace tgfx {
class GLUniformBuffer : public GLBuffer {
 public:
  explicit GLUniformBuffer(size_t size);

  ~GLUniformBuffer() override;

  void* map() override;

  void unmap() override;

  const void* data() const {
    return mappedAddress;
  }

 private:
  uint32_t uniqueID = 0;
  void* mappedAddress = nullptr;

  void releaseInternal();
};
}  // namespace tgfx
