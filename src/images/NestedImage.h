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

#pragma once

#include "tgfx/core/Image.h"

namespace tgfx {
/**
 * The base class for all images that have a single source image and apply various transformations.
 */
class NestedImage : public Image {
 public:
  explicit NestedImage(std::shared_ptr<Image> source);

  bool hasMipmaps() const override {
    return source->hasMipmaps();
  }

  bool isLazyGenerated() const override {
    return source->isLazyGenerated();
  }

  bool isAlphaOnly() const override {
    return source->isAlphaOnly();
  }

 protected:
  std::shared_ptr<Image> source = nullptr;

  std::shared_ptr<Image> onMakeDecoded(Context* context) const override;

  std::shared_ptr<Image> onMakeMipMapped() const override;

  virtual std::shared_ptr<Image> onCloneWith(std::shared_ptr<Image> newSource) const = 0;
};
}  // namespace tgfx
