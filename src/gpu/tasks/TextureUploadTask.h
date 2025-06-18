/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "core/DataSource.h"
#include "core/utils/BlockBuffer.h"
#include "tgfx/core/ImageBuffer.h"

namespace tgfx {
class TextureUploadTask : public ResourceTask {
 public:
  TextureUploadTask(UniqueKey uniqueKey, std::shared_ptr<DataSource<ImageBuffer>> source,
                    bool mipmapped, bool asyncDecoding, ReferenceCounter referenceCounter);

  std::shared_ptr<Resource> onMakeResource(Context* context) override;

 private:
  std::shared_ptr<DataSource<ImageBuffer>> source = nullptr;
  bool mipmapped = false;
};
}  // namespace tgfx
