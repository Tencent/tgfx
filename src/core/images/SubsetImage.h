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

#include <optional>
#include "core/images/TransformImage.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
/**
 * An image that is a subset of another image.
 */
class SubsetImage : public TransformImage {
 public:
  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<Image> source, const Rect& bounds);

  int width() const override {
    return static_cast<int>(bounds.width());
  }

  int height() const override {
    return static_cast<int>(bounds.height());
  }

  Rect bounds = Rect::MakeEmpty();

 protected:
  Type type() const override {
    return Type::Subset;
  }

  std::shared_ptr<Image> onCloneWith(std::shared_ptr<Image> newSource) const override;

  std::shared_ptr<Image> onMakeSubset(const Rect& subset) const override;

  std::unique_ptr<FragmentProcessor> asFragmentProcessor(const FPArgs& args, TileMode tileModeX,
                                                         TileMode tileModeY,
                                                         const SamplingOptions& sampling,
                                                         const Matrix* uvMatrix) const override;

  std::optional<Matrix> concatUVMatrix(const Matrix* uvMatrix) const;

  SubsetImage(std::shared_ptr<Image> source, const Rect& bounds);
};
}  // namespace tgfx
