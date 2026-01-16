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

#include <unordered_map>
#include "tgfx/core/Data.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Typeface.h"

namespace hello2d {
/**
 * AppHost provides resources for building layer trees.
 */
class AppHost {
 public:
  virtual ~AppHost() = default;

  /**
   * Returns an image with the given name.
   */
  std::shared_ptr<tgfx::Image> getImage(const std::string& name) const;

  /**
   * Returns a typeface with the given name.
   */
  std::shared_ptr<tgfx::Typeface> getTypeface(const std::string& name) const;

  /**
   * Add an image for the given resource name.
   */
  void addImage(const std::string& name, std::shared_ptr<tgfx::Image> image);

  /**
   * Adds a typeface for the given resource name.
   */
  void addTypeface(const std::string& name, std::shared_ptr<tgfx::Typeface> typeface);

 private:
  std::unordered_map<std::string, std::shared_ptr<tgfx::Image>> images = {};
  std::unordered_map<std::string, std::shared_ptr<tgfx::Typeface>> typefaces = {};
};
}  // namespace hello2d
