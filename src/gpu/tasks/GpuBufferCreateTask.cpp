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

#include "GpuBufferCreateTask.h"
#include "tgfx/utils/Task.h"

namespace tgfx {
class GpuBufferCreator : public GpuBufferCreateTask {
 public:
  GpuBufferCreator(ResourceKey resourceKey, BufferType bufferType,
                   std::shared_ptr<DataProvider> provider)
      : GpuBufferCreateTask(std::move(resourceKey), bufferType), provider(std::move(provider)) {
  }

 protected:
  std::shared_ptr<Data> getData() override {
    if (provider == nullptr) {
      return nullptr;
    }
    return provider->getData();
  }

 private:
  std::shared_ptr<DataProvider> provider = nullptr;
};

struct DataHolder {
  std::shared_ptr<Data> data;
};

class AsyncGpuBufferCreator : public GpuBufferCreateTask {
 public:
  AsyncGpuBufferCreator(ResourceKey resourceKey, BufferType bufferType,
                        std::shared_ptr<DataProvider> dataProvider)
      : GpuBufferCreateTask(std::move(resourceKey), bufferType), provider(std::move(dataProvider)) {
    holder = std::make_shared<DataHolder>();
    task = Task::Run(
        [result = holder, dataProvider = provider]() { result->data = dataProvider->getData(); });
  }

  ~AsyncGpuBufferCreator() override {
    task->cancel();
  }

 protected:
  std::shared_ptr<Data> getData() override {
    if (provider == nullptr) {
      return nullptr;
    }
    task->wait();
    auto data = holder->data;
    if (data != nullptr) {
      // Free the data provider immediately to reduce memory pressure.
      provider = nullptr;
      holder = nullptr;
    }
    return data;
  }

 private:
  std::shared_ptr<DataProvider> provider = nullptr;
  std::shared_ptr<DataHolder> holder = nullptr;
  std::shared_ptr<Task> task = nullptr;
};

std::shared_ptr<GpuBufferCreateTask> GpuBufferCreateTask::MakeFrom(
    ResourceKey resourceKey, BufferType bufferType, std::shared_ptr<DataProvider> provider,
    bool async) {
  if (provider == nullptr) {
    return nullptr;
  }
  if (async) {
    return std::make_shared<AsyncGpuBufferCreator>(std::move(resourceKey), bufferType,
                                                   std::move(provider));
  }
  return std::make_shared<GpuBufferCreator>(std::move(resourceKey), bufferType,
                                            std::move(provider));
}

GpuBufferCreateTask::GpuBufferCreateTask(ResourceKey resourceKey, BufferType bufferType)
    : ResourceTask(std::move(resourceKey)), bufferType(bufferType) {
}

std::shared_ptr<Resource> GpuBufferCreateTask::onMakeResource(Context* context) {
  auto data = getData();
  if (data == nullptr || data->empty()) {
    // We don't need to log an error here because the getData() should have already logged an error.
    return nullptr;
  }
  auto gpuBuffer = GpuBuffer::Make(context, data->data(), data->size(), bufferType);
  if (gpuBuffer == nullptr) {
    LOGE("GpuBufferCreateTask::onMakeResource failed to create GpuBuffer");
  }
  return gpuBuffer;
}
}  // namespace tgfx
