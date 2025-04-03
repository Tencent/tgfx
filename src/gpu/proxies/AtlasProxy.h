/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <memory>
#include "gpu/proxies/GpuBufferProxy.h"

namespace tgfx {
class AtlasProxy {
 public:
  explicit AtlasProxy(std::shared_ptr<GpuBufferProxy> triangles) : triangles(std::move(triangles)) {
  }

  Context* getContext() const {
    return triangles->getContext();
  }

  std::shared_ptr<GpuBufferProxy> getTriangles() const {
    return triangles;
  }

 private:
  std::shared_ptr<GpuBufferProxy> triangles = nullptr;
};
}  // namespace tgfx