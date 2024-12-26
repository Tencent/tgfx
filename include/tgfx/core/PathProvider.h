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
   * Note: This method may be called multiple times. If constructing the Path object is
   * time-consuming, consider caching the Path object after it is created.
   */
  virtual Path getPath() = 0;
};
}  // namespace tgfx
