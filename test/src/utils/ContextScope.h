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

#include "DevicePool.h"

namespace tgfx {
class ContextScope {
 public:
  explicit ContextScope() : device(DevicePool::Make()) {
    if (device != nullptr) {
      context = device->lockContext();
    }
  }

  ~ContextScope() {
    if (context) {
      device->unlock();
    }
  }

  Context* getContext() const {
    return context;
  }

 private:
  std::shared_ptr<Device> device = nullptr;
  Context* context = nullptr;
};
}  // namespace tgfx
