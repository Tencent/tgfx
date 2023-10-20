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

#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shader.h"

namespace tgfx {
class LocalMatrixShader final : public Shader {
 public:
  LocalMatrixShader(std::shared_ptr<Shader> proxy, const Matrix& preLocalMatrix,
                    const Matrix& postLocalMatrix)
      : proxyShader(std::move(proxy)), _preLocalMatrix(preLocalMatrix),
        _postLocalMatrix(postLocalMatrix) {
  }

  bool isOpaque() const override {
    return proxyShader->isOpaque();
  }

  std::shared_ptr<Shader> makeWithLocalMatrix(const Matrix& matrix, bool isPre) const override;

  std::unique_ptr<FragmentProcessor> asFragmentProcessor(const FPArgs& args) const override;

 private:
  std::shared_ptr<Shader> proxyShader;
  const Matrix _preLocalMatrix;
  const Matrix _postLocalMatrix;
};
}  // namespace tgfx
