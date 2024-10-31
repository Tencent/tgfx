/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/layers/PathProvider.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {
std::shared_ptr<PathProvider> PathProvider::Wrap(const Path& path) {
  if (path.isEmpty()) {
    return nullptr;
  }
  return std::shared_ptr<PathProvider>(new PathProvider(path));
}

PathProvider::PathProvider(Path path) : path(std::move(path)) {
}

Path PathProvider::getPath() {
  if (dirty) {
    path = onGeneratePath();
    dirty = false;
  }
  return path;
}

Path PathProvider::onGeneratePath() {
  return path;
}

void PathProvider::invalidatePath() {
  dirty = true;
  invalidate();
}

}  // namespace tgfx
