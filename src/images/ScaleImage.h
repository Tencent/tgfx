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
#include "TransformImage.h"

namespace tgfx {
/*
* RasterImage takes an Image and rasterizes it to a new Image with a different scale and sampling
* options.
*/
class ScaleImage : public TransformImage {
 public:
  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<Image> source, float scaleX, float scaleY);

  int width() const override;

  int height() const override;

 protected:
  std::shared_ptr<Image> onCloneWith(std::shared_ptr<Image> newSource) const override;

  std::shared_ptr<Image> onMakeScale(float scaleX, float scaleY) const override;

  std::unique_ptr<FragmentProcessor> asFragmentProcessor(const FPArgs& args, TileMode tileModeX,
                                                         TileMode tileModeY,
                                                         const SamplingOptions& sampling,
                                                         const Matrix* uvMatrix) const override;

 private:
  float scaleX = 1.0f;
  float scaleY = 1.0f;

  ScaleImage(std::shared_ptr<Image> source, float scaleX, float scaleY);
};
}  // namespace tgfx
