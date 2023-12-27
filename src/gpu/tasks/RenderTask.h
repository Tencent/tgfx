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

#include "gpu/Gpu.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/gpu/Surface.h"

namespace tgfx {
class RenderTask {
 public:
  virtual ~RenderTask() = default;

  virtual bool execute(Gpu* gpu) = 0;

  void gatherProxies(std::vector<ProxyBase*>* proxies) const;

  void makeClosed() {
    closed = true;
  }

  bool isClosed() const {
    return closed;
  }

 protected:
  explicit RenderTask(std::shared_ptr<RenderTargetProxy> renderTargetProxy);

  virtual void onGatherProxies(std::vector<ProxyBase*>* proxies) const;

  std::shared_ptr<RenderTargetProxy> renderTargetProxy = nullptr;

 private:
  bool closed = false;
};
}  // namespace tgfx
