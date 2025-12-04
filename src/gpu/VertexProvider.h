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

#include <vector>
#include "core/DataSource.h"
#include "core/utils/BlockAllocator.h"
#include "core/utils/PlacementPtr.h"
#include "tgfx/core/Data.h"

namespace tgfx {
/**
 * A provider for vertices.
 */
class VertexProvider {
 public:
  explicit VertexProvider(std::shared_ptr<BlockAllocator> reference)
      : reference(std::move(reference)) {
  }

  virtual ~VertexProvider() = default;

  /**
   * Returns the number of vertices in the provider.
   */
  virtual size_t vertexCount() const = 0;

  /**
   * Returns the vertices in the provider.
   */
  virtual void getVertices(float* vertices) const = 0;

 private:
  std::shared_ptr<BlockAllocator> reference = nullptr;
};

class VertexProviderTask : public Task {
 public:
  VertexProviderTask(PlacementPtr<VertexProvider> provider, float* vertices)
      : provider(std::move(provider)), vertices(vertices) {
  }

 protected:
  void onExecute() override;

  void onCancel() override;

 private:
  PlacementPtr<VertexProvider> provider = nullptr;
  float* vertices = nullptr;
};

class AsyncVertexSource : public DataSource<Data> {
 public:
  AsyncVertexSource(std::shared_ptr<Data> data, std::vector<std::shared_ptr<Task>> tasks)
      : data(std::move(data)), tasks(std::move(tasks)) {
  }

  ~AsyncVertexSource() override;

  std::shared_ptr<Data> getData() const override;

 private:
  std::shared_ptr<Data> data = nullptr;
  std::vector<std::shared_ptr<Task>> tasks = {};
};
}  // namespace tgfx
