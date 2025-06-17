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

#include "VertexProvider.h"
#include "core/utils/Log.h"

namespace tgfx {
void VertexProviderTask::onExecute() {
  DEBUG_ASSERT(provider != nullptr);
  // Ensure that the reference count of the shared memory (BlockBuffer) is greater than 1 during
  // execution, so that the BlockBuffer does not release the memory used by the Task or its owner.
  if (blockBuffer) {
    blockBuffer->addReference();
  }
  provider->getVertices(vertices);
  provider = nullptr;
  if (blockBuffer) {
    blockBuffer->removeReference();
  }
}

void VertexProviderTask::onCancel() {
  provider = nullptr;
}

AsyncVertexSource::~AsyncVertexSource() {
  // The vertex source might have objects created in shared memory (like BlockBuffer), so we
  // need to wait for the task to finish before destroying it.
  for (auto& task : tasks) {
    task->cancel();
  }
}

std::shared_ptr<Data> AsyncVertexSource::getData() const {
  for (auto& task : tasks) {
    task->wait();
  }
  return data;
}
}  // namespace tgfx