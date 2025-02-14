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

#include "ResourceTask.h"
#include "core/DataProvider.h"
#include "gpu/GpuBuffer.h"

namespace tgfx {
class GpuBufferUploadTask : public ResourceTask {
 public:
  /**
   * Create a new GpuBufferUploadTask to generate a GpuBuffer with the given data provider.
   */
  static std::unique_ptr<GpuBufferUploadTask> MakeFrom(UniqueKey uniqueKey, BufferType bufferType,
                                                       std::unique_ptr<DataProvider> provider);

 protected:
  std::shared_ptr<Resource> onMakeResource(Context* context) override;

 private:
  BufferType bufferType = BufferType::Vertex;
  std::unique_ptr<DataProvider> provider = nullptr;

  GpuBufferUploadTask(UniqueKey uniqueKey, BufferType bufferType,
                      std::unique_ptr<DataProvider> provider);
};
}  // namespace tgfx
