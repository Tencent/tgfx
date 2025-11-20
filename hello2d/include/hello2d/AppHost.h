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
#include "tgfx/layers/DisplayList.h"

namespace hello2d {
/**
 * AppHost provides information about the current app context.
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
   * Returns the current scale factor applied to the view. The default value is 1.0, which means no
   * zooming is applied.
   */
  float zoomScale() const {
    return _zoomScale;
  }

  /**
   * Returns the current content offset of the view after applying the zoomScale. The default value
   * is (0, 0).
   */
  const tgfx::Point& contentOffset() const {
    return _contentOffset;
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
   * Updates the zoom scale and content offset.
   */
  bool updateZoomAndOffset(float zoomScale, const tgfx::Point& contentOffset);

  /**
   * Add an image for the given resource name.
   */
  void addImage(const std::string& name, std::shared_ptr<tgfx::Image> image);

  /**
   * Adds a typeface for the given resource name.
   */
  void addTypeface(const std::string& name, std::shared_ptr<tgfx::Typeface> typeface);

  bool isDirty() const;
  void markDirty();
  void resetDirty();
  void setRenderMode(tgfx::RenderMode renderMode);
  /**
   * Draws the content of the corresponding SampleBuilder based on the index.
   */
  void draw(tgfx::Canvas* canvas, int drawIndex, bool isNeedBackground);
  /**
   * Calculates and sets the transformation matrix for the current root layer
   * (displayList.root()->firstChild()), centering it in the window, scaling it proportionally, and
   * leaving a 30px padding on all sides.
   */
  void updateRootMatrix();

  /**
   * Returns all layers hit at the specified logical coordinates (sorted by depth then by level).
   */
  std::vector<std::shared_ptr<tgfx::Layer>> getLayersUnderPoint(float x, float y) const;

  mutable tgfx::DisplayList displayList = {};

 private:
  int _width = 1280;
  int _height = 720;
  float _density = 1.0f;
  float _zoomScale = 1.0f;
  tgfx::Point _contentOffset = {};
  std::unordered_map<std::string, std::shared_ptr<tgfx::Image>> images = {};
  std::unordered_map<std::string, std::shared_ptr<tgfx::Typeface>> typefaces = {};
  bool _dirty = true;
  bool _isTileMode = true;

  int lastDrawIndex = -1;
  std::shared_ptr<tgfx::Layer> root = nullptr;
};
}  // namespace hello2d