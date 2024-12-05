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

#include "core/images/OffscreenImage.h"

namespace tgfx {
/**
 * PictureImage is an image that draws a Picture.
 */
class PictureImage : public OffscreenImage {
 public:
  PictureImage(UniqueKey uniqueKey, std::shared_ptr<Picture> picture, int width, int height,
               const Matrix* matrix = nullptr, bool alphaOnly = false);

  ~PictureImage() override;

  int width() const override {
    return _width;
  }

  int height() const override {
    return _height;
  }

  bool isAlphaOnly() const override {
    return alphaOnly;
  }

 protected:
  bool onDraw(std::shared_ptr<RenderTargetProxy> renderTarget, uint32_t renderFlags) const override;

 private:
  std::shared_ptr<Picture> picture = nullptr;
  int _width = 0;
  int _height = 0;
  Matrix* matrix = nullptr;
  bool alphaOnly = false;
};
}  // namespace tgfx
