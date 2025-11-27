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

#include "core/utils/PlacementPtr.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {

class Context3DCompositor {
 public:
  Context3DCompositor(const Context& context, int width, int height);

  ~Context3DCompositor();

  void drawImage(std::shared_ptr<Image> image, const Matrix3D& matrix, float x, float y);

  std::shared_ptr<Image> finish();

 private:
  int width;
  int height;
  std::shared_ptr<RenderTargetProxy> targetColorProxy = nullptr;
  std::shared_ptr<RenderTargetProxy> targetDepthStencilProxy = nullptr;
  std::vector<PlacementPtr<DrawOp>> drawOps = {};
};

}  // namespace tgfx