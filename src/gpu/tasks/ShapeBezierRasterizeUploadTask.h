/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "ResourceTask.h"
#include "core/DataSource.h"
#include "core/ShapeBezierRasterizer.h"

namespace tgfx {
/**
 * ShapeBezierRasterizeUploadTask uploads the bezier vertex stream produced by
 * ShapeBezierRasterizer into a GPU vertex buffer. When the rasterizer reports no drawable
 * geometry, the task resolves to no resource and the bound vertex proxy stays unbacked, which
 * allows the bezier rasterization render path to gracefully fall back to the legacy pipeline.
 */
class ShapeBezierRasterizeUploadTask : public ResourceTask {
 public:
  ShapeBezierRasterizeUploadTask(std::shared_ptr<ResourceProxy> vertexBufferProxy,
                                 std::unique_ptr<DataSource<ShapeBezierBuffer>> source);

 protected:
  std::shared_ptr<Resource> onMakeResource(Context* context) override;

 private:
  std::unique_ptr<DataSource<ShapeBezierBuffer>> source = nullptr;
};
}  // namespace tgfx
