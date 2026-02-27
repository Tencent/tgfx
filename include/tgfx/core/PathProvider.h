/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/Rect.h"

namespace tgfx {
/**
 * PathProvider defines interfaces for creating a Path object lazily, which is useful when the path
 * is expensive to compute and not needed immediately. It can be used to create a Shape object.
 * See Shape::MakeFrom(std::shared_ptr<PathProvider> pathProvider) for more details. Note that
 * PathProvider is thread-safe and immutable once created.
 */
class PathProvider {
 public:
  static std::shared_ptr<PathProvider> Wrap(Path path);

  virtual ~PathProvider() = default;

  /**
   * Returns the computed path of the PathProvider. Note: The path is recalculated each time this
   * method is called, as it is not cached.
   */
  virtual Path getPath() const = 0;

  /**
   * Returns the bounding box of the path. The bounds might be larger than the actual path because
   * the exact bounds can't be determined until the path is computed.
   */
  virtual Rect getBounds() const = 0;

  /**
   * Returns the fill type of the path. The default implementation returns PathFillType::Winding.
   */
  virtual PathFillType fillType() const {
    return PathFillType::Winding;
  }
};
}  // namespace tgfx
