/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
#include "tgfx/core/Data.h"

namespace tgfx {
enum class BufferType {
  Index,
  Vertex,
};

class GPUBufferUploadTask : public ResourceTask {
 public:
  GPUBufferUploadTask(std::shared_ptr<ResourceProxy> proxy, BufferType bufferType,
                      std::unique_ptr<DataSource<Data>> source);

 protected:
  std::shared_ptr<Resource> onMakeResource(Context* context) override;

 private:
  BufferType bufferType = BufferType::Vertex;
  std::unique_ptr<DataSource<Data>> source = nullptr;
};
}  // namespace tgfx
