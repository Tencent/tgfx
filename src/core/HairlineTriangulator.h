/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "core/DataSource.h"
#include "tgfx/core/Shape.h"

namespace tgfx {

struct HairlineBuffer {
  HairlineBuffer(std::shared_ptr<Data> lineVertices, std::shared_ptr<Data> quadVertices)
      : lineVertices(std::move(lineVertices)), quadVertices(std::move(quadVertices)) {
  }

  std::shared_ptr<Data> lineVertices = nullptr;
  std::shared_ptr<Data> quadVertices = nullptr;
};

class HairlineTriangulator : public DataSource<HairlineBuffer> {
 public:
  HairlineTriangulator(std::shared_ptr<Shape> shape, bool hasCap);

  std::shared_ptr<HairlineBuffer> getData() const override;

 private:
  std::shared_ptr<Shape> shape = nullptr;
  bool hasCap = false;
};

}  // namespace tgfx