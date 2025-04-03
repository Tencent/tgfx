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

#pragma once
#include <memory>
#include "ResourceTask.h"
#include "core/AtlasBuffer.h"
#include "core/DataSource.h"
#include "gpu/Resource.h"
#include "gpu/ResourceKey.h"

namespace tgfx {
class AtlasBufferUploadTask : public ResourceTask {
 public:
  AtlasBufferUploadTask(UniqueKey atlasKey, std::unique_ptr<DataSource<AtlasBuffer>> source);

  bool execute(Context* context) override;

 protected:
  std::shared_ptr<Resource> onMakeResource(Context*) override {
    return nullptr;
  }

 private:
  std::unique_ptr<DataSource<AtlasBuffer>> source = nullptr;
};
}  // namespace tgfx