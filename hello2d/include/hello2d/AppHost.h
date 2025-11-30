/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
 * AppHost provides screen information and resources for building layer trees.
 * It is a pure information provider and does not manage rendering or display lists.
 */
class AppHost {
 public:
  /**
   * Creates an AppHost with the given width, height and density. The width and height are in
   * pixels, and the density is the ratio of physical pixels to logical pixels.
   */
  explicit AppHost(int width = 1280, int height = 720, float density = 1.0f);

  virtual ~AppHost() = default;

  /**
   * Returns the width of the screen.
   */
  int width() const {
    return _width;
  }

  /**
   * Returns the height of the screen.
   */
  int height() const {
    return _height;
  }

  /**
   * Returns the density of the screen.
   */
  float density() const {
    return _density;
  }

  /**
   * Returns an image with the given name.
   */
  std::shared_ptr<tgfx::Image> getImage(const std::string& name) const;

  /**
   * Returns a typeface with the given name.
   */
  std::shared_ptr<tgfx::Typeface> getTypeface(const std::string& name) const;

  /**
   * Updates the screen size and density. The default values are 1280x720 and 1.0. The width and
   * height are in pixels, and the density is the ratio of physical pixels to logical pixels.
   * Returns true if the screen size or density has changed.
   */
  bool updateScreen(int width, int height, float density);

  /**
   * Add an image for the given resource name.
   */
  void addImage(const std::string& name, std::shared_ptr<tgfx::Image> image);

  /**
   * Adds a typeface for the given resource name.
   */
  void addTypeface(const std::string& name, std::shared_ptr<tgfx::Typeface> typeface);

 private:
  int _width = 1280;
  int _height = 720;
  float _density = 1.0f;
  std::unordered_map<std::string, std::shared_ptr<tgfx::Image>> images = {};
  std::unordered_map<std::string, std::shared_ptr<tgfx::Typeface>> typefaces = {};
};
}  // namespace hello2d