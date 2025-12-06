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

#include "hello2d/AppHost.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/layers/Layer.h"

namespace hello2d {

/**
 * Base class for individual layer builders.
 */
class LayerBuilder {
 public:
  /**
   * Returns the number of layer builders.
   */
  static int Count();

  /**
   * Returns the names of all layer builders.
   */
  static const std::vector<std::string>& Names();

  /**
   * Returns the layer builder with the given index.
   */
  static LayerBuilder* GetByIndex(int index);

  /**
   * Returns the layer builder with the given name.
   */
  static LayerBuilder* GetByName(const std::string& name);

  explicit LayerBuilder(std::string name);

  virtual ~LayerBuilder() = default;

  std::string name() const {
    return _name;
  }

  /**
   * Builds and returns a layer tree.
   */
  std::shared_ptr<tgfx::Layer> buildLayerTree(const hello2d::AppHost* host);

 protected:
  /**
   * Builds and returns the content layer tree.
   */
  virtual std::shared_ptr<tgfx::Layer> onBuildLayerTree(const hello2d::AppHost* host) = 0;

 private:
  std::string _name;

  /**
   * Applies centering and scaling transformation to the layer.
   */
  void applyCenteringTransform(std::shared_ptr<tgfx::Layer> layer);
};

/**
 * Draws the background for samples.
 */
void DrawBackground(tgfx::Canvas* canvas, int width, int height, float density);

}  // namespace hello2d
