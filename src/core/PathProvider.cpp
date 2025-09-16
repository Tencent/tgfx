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

#include "tgfx/core/PathProvider.h"

namespace tgfx {

class PathProviderWrapper final : public PathProvider {
 public:
  explicit PathProviderWrapper(Path path) : _path(std::move(path)) {
  }

  Path getPath() const override {
    return _path;
  }

  Rect getBounds() const override {
    return _path.getBounds();
  }

 private:
  Path _path;
};

std::shared_ptr<PathProvider> PathProvider::Wrap(Path path) {
  return std::make_shared<PathProviderWrapper>(std::move(path));
}
}  // namespace tgfx
