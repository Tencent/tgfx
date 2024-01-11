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

#include "PathProxy.h"
#include "tgfx/core/Shape.h"

namespace tgfx {
class PathShape : public Shape {
 public:
  explicit PathShape(std::shared_ptr<PathProxy> proxy, float resolutionScale);

  Rect getBounds() const override {
    return bounds;
  }

 protected:
  Rect bounds = Rect::MakeEmpty();

  Path getFillPath() const;

 private:
  std::shared_ptr<PathProxy> proxy = nullptr;
};
}  // namespace tgfx
