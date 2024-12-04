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

#include "tgfx/core/Color.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Point.h"

namespace tgfx {

enum class ImageFilterType {
  None,
  Blur,
  DropShadow,
  InnerShadow,
  Color,
  Compose,
  Runtime,
};

struct ImageFilterInfo {
  bool onlyShadow = true;
  float blurrinessX = 0.f;
  float blurrinessY = 0.f;
  Point offset = Point::Zero();
  Color color = Color::Black();
};

class ImageFilterBase : public ImageFilter {
 public:
  virtual ImageFilterType asImageFilterInfo(ImageFilterInfo*) const {
    return ImageFilterType::None;
  };
};

inline const ImageFilterBase* asImageFilterBase(const std::shared_ptr<ImageFilter>& filter) {
  return static_cast<ImageFilterBase*>(filter.get());
}

}  // namespace tgfx