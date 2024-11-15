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

#include "ComposeContent.h"
#include "profileClient/Profile.h"

namespace tgfx {
std::unique_ptr<LayerContent> LayerContent::Compose(
    std::vector<std::unique_ptr<LayerContent>> contents) {
  if (contents.empty()) {
    return nullptr;
  }
  if (contents.size() == 1) {
    return std::move(contents[0]);
  }
  return std::make_unique<ComposeContent>(std::move(contents));
}

Rect ComposeContent::getBounds() const {
  TGFX_PROFILE_ZONE_SCOPPE_NAME("ComposeContent::getBounds");
  auto bounds = Rect::MakeEmpty();
  for (const auto& content : contents) {
    bounds.join(content->getBounds());
  }
  return bounds;
}

void ComposeContent::draw(Canvas* canvas, const Paint& paint) const {
  TGFX_PROFILE_ZONE_SCOPPE_NAME("ComposeContent::draw");
  for (const auto& content : contents) {
    content->draw(canvas, paint);
  }
}

bool ComposeContent::hitTestPoint(float localX, float localY, bool pixelHitTest) {
  TGFX_PROFILE_ZONE_SCOPPE_NAME("ComposeContent::hitTestPoint");
  for (const auto& content : contents) {
    if (content->hitTestPoint(localX, localY, pixelHitTest)) {
      return true;
    }
  }
  return false;
}

}  // namespace tgfx
