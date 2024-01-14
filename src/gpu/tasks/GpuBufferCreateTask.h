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
class GpuBufferCreateTask : public ResourceTask {
 public:
  /**
   * Create a new GpuBufferCreateTask to generate a GpuBuffer with the given data provider. If async
   * is true, the data provider will be read from an asynchronous thread immediately. Otherwise, the
   * data provider will be read from the current thread when the GpuBufferCreateTask is executed.
   */
  static std::shared_ptr<GpuBufferCreateTask> MakeFrom(ResourceKey strongkey, BufferType bufferType,
                                                       std::shared_ptr<DataProvider> provider,
                                                       bool async);

 protected:
  BufferType bufferType = BufferType::Vertex;

  GpuBufferCreateTask(ResourceKey strongKey, BufferType bufferType);

  std::shared_ptr<Resource> onMakeResource(Context* context) override;

  virtual std::shared_ptr<Data> getData() = 0;
};
}  // namespace tgfx
