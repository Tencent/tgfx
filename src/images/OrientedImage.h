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

#include "images/NestedImage.h"

namespace tgfx {
/**
 * OrientImage wraps an existing image and applies an orientation transform.
 */
class OrientedImage : public NestedImage {
 public:
  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<Image> source, EncodedOrigin origin);

  int width() const override;

  int height() const override;

 protected:
  EncodedOrigin origin = EncodedOrigin::TopLeft;

  OrientedImage(std::shared_ptr<Image> source, EncodedOrigin origin);

  std::shared_ptr<Image> onCloneWith(std::shared_ptr<Image> newSource) const override;

  std::shared_ptr<Image> onMakeMipMapped() const override;

  std::shared_ptr<Image> onMakeSubset(const Rect& subset) const override;

  std::shared_ptr<Image> onApplyOrigin(EncodedOrigin newOrigin) const override;

  std::unique_ptr<FragmentProcessor> asFragmentProcessor(Context* context, TileMode tileModeX,
                                                         TileMode tileModeY,
                                                         const SamplingOptions& sampling,
                                                         const Matrix* localMatrix,
                                                         uint32_t renderFlags) override;

  virtual Matrix computeLocalMatrix() const;

  EncodedOrigin concatOrigin(EncodedOrigin newOrigin) const;

  ISize getOrientedSize() const;
};
}  // namespace tgfx
