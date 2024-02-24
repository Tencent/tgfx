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

#include "core/DataProvider.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {
class PathAATriangles : public DataProvider {
 public:
  static std::shared_ptr<PathAATriangles> Make(Path path, const Matrix& matrix,
                                               const Stroke* stroke = nullptr);

  ~PathAATriangles() override;

  std::shared_ptr<Data> getData() const override;

 private:
  Path path = {};
  Matrix matrix = Matrix::I();
  Stroke* stroke = nullptr;

  PathAATriangles(Path path, const Matrix& matrix, const Stroke* stroke);
};
}  // namespace tgfx
