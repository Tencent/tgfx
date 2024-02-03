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

#include "NestedImage.h"

namespace tgfx {
NestedImage::NestedImage(std::shared_ptr<Image> source) : source(std::move(source)) {
}

std::shared_ptr<Image> NestedImage::onMakeDecoded(Context* context, bool tryHardware) const {
  auto newSource = source->onMakeDecoded(context, tryHardware);
  if (newSource == nullptr) {
    return nullptr;
  }
  return onCloneWith(std::move(newSource));
}

std::shared_ptr<Image> NestedImage::onMakeMipmapped(bool enabled) const {
  auto newSource = source->makeMipmapped(enabled);
  if (newSource == nullptr) {
    return nullptr;
  }
  return onCloneWith(std::move(newSource));
}
}  // namespace tgfx