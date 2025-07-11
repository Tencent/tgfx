/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include "core/ShapeBuffer.h"

namespace tgfx {
class ShapeBufferUploadTask : public ResourceTask {
 public:
  ShapeBufferUploadTask(UniqueKey trianglesKey, UniqueKey textureKey,
                        std::unique_ptr<DataSource<ShapeBuffer>> source);

  bool execute(Context* context) override;

 protected:
  std::shared_ptr<Resource> onMakeResource(Context*) override {
    // The execute() method is already overridden, so this method should never be called.
    return nullptr;
  }

 private:
  UniqueKey textureKey = {};
  std::unique_ptr<DataSource<ShapeBuffer>> source = nullptr;
};
}  // namespace tgfx
