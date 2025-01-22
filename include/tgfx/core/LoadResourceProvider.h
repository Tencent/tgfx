/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <memory>
#include <string>
#include "tgfx/core/Data.h"
#include "tgfx/core/Image.h"

namespace tgfx {

/**
 * LoadResourceProvider is an interface for loading resources (e.g. images, font) from
 * external sources.
 */
class LoadResourceProvider {
 public:
  /**
   * Data is a wrapper for raw data.
   */
  virtual ~LoadResourceProvider() = default;

  /**
 * Creates an empty resource provider.
 */
  static std::shared_ptr<LoadResourceProvider> MakeEmpty();

  /**
 * Creates a file resource provider with the given base path.
 */
static std::shared_ptr<LoadResourceProvider> MakeFileProvider(const std::string& basePath);

/**
   * Load a generic resource specified by |path| + |name|, and return as an Data object.
   */
virtual std::shared_ptr<Data> load(const std::string& /*resourcePath*/,
                                   const std::string& /*resourceName*/) const {
  return nullptr;
  }

  /**
   * Load an image asset specified by |path| + |name|, and returns the Image object.
   */
  virtual std::shared_ptr<Image> loadImage(const std::string& /*resourcePath*/,
                                           const std::string& /*resourceName*/) const {
    return nullptr;
  }
};

}  // namespace tgfx