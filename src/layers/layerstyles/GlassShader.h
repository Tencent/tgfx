/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/SamplingOptions.h"
#include "tgfx/core/Shader.h"

namespace tgfx {

class GlassRefractionImageFilter;

/**
 * Shader that renders a glass refraction filter directly at draw (screen) resolution. Unlike
 * FilterImage, it never falls back to the offscreen texture path, so the analytical SDF edge
 * light is evaluated per output pixel instead of being baked into a downscaled texture.
 */
class GlassShader : public Shader {
 public:
  static std::shared_ptr<GlassShader> Make(std::shared_ptr<GlassRefractionImageFilter> filter,
                                           std::shared_ptr<Image> source, const Matrix& matrix,
                                           const SamplingOptions& sampling);

 protected:
  Type type() const override {
    return Type::Glass;
  }

  bool isEqual(const Shader* shader) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      const FPArgs& args, const Matrix* uvMatrix,
      const std::shared_ptr<ColorSpace>& dstColorSpace) const override;

 private:
  GlassShader(std::shared_ptr<GlassRefractionImageFilter> filter, std::shared_ptr<Image> source,
              const Matrix& matrix, const SamplingOptions& sampling);

  std::shared_ptr<GlassRefractionImageFilter> filter = nullptr;
  std::shared_ptr<Image> source = nullptr;
  Matrix matrix = Matrix::I();
  SamplingOptions sampling = {};
};

}  // namespace tgfx
