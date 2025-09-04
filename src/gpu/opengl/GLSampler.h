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

#include "gpu/GPUSampler.h"

namespace tgfx {
class GLSampler : public GPUSampler {
 public:
  explicit GLSampler(const GPUSamplerDescriptor& descriptor) : descriptor(descriptor) {
  }

  AddressMode addressModeX() const {
    return descriptor.addressModeX;
  }

  AddressMode addressModeY() const {
    return descriptor.addressModeY;
  }

  FilterMode minFilter() const {
    return descriptor.minFilter;
  }

  FilterMode magFilter() const {
    return descriptor.magFilter;
  }

  MipmapMode mipmapMode() const {
    return descriptor.mipmapMode;
  }

  void release(GPU*) override {
  }

 private:
  GPUSamplerDescriptor descriptor = {};
};
}  // namespace tgfx
