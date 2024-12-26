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

namespace tgfx {
/**
 * PathProvider is an external path data provider that can be used to create a Shape object.
 * When constructing a Path externally is time-consuming, PathProvider can be used to delay the
 * creation of the Path. See Shape::MakeFrom(std::shared_ptr<PathProvider> pathProvider) for
 * reference.
 */
class PathProvider {
 public:
  virtual ~PathProvider() = default;

  /**
   * This method retrieves a Path object from the PathProvider.
   */
  virtual Path getPath(float resolutionScale = 1.0f) const = 0;

  /**
   * Returns the bounding box of the path. The bounds might be larger than the actual path because
   * the exact bounds can't be determined until the path is computed. Note: Since the path may
   * contain strokes whose outlines can change with different scale factors, it's
   * best to pass the final drawing scale factor in the resolutionScale for computing the bounds to
   * ensure accuracy. However, the resolutionScale is not applied to the returned bounds; it just
   * affects the precision of the bounds.
   * @param resolutionScale The intended resolution for the path. The default value is 1.0. Higher
   * values (res > 1) mean the result should be more precise, as it will be zoomed up and small
   * errors will be magnified. Lower values (0 < res < 1) mean the result can be less precise, as it
   * will be zoomed down and small errors may be invisible.
   * @return The bounding box of the path.
   */
  virtual Rect getBounds(float resolutionScale = 1.0f) const = 0;
};
}  // namespace tgfx
