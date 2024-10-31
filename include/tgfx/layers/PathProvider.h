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

#pragma once

#include "tgfx/core/Path.h"
#include "tgfx/layers/LayerProperty.h"

namespace tgfx {
/**
 * PathProvider is an interface for classes that generates a Path. It defers the acquisition of the
 * Path until it is actually required, allowing the Path to be invalidated and regenerate if
 * necessary. Note: PathProvider is not thread-safe and should be accessed from a single thread.
 */
class PathProvider : public LayerProperty {
 public:
  /**
   * Creates a new PathProvider that wraps the given Path.
   */
  static std::shared_ptr<PathProvider> Wrap(const Path& path);

  /**
   * Returns the Path provided by this object.
   */
  Path getPath();

 protected:
  PathProvider() = default;

  /**
   * Creates a new PathProvider with an initial path.
   */
  explicit PathProvider(Path path);

  virtual Path onGeneratePath();

  /**
   * Marks the path as dirty and invalidates the cached path.
   */
  void invalidatePath();

 private:
  Path path = {};

  bool dirty = true;
};
}  // namespace tgfx
