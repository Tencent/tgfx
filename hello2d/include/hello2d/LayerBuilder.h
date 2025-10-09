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
#include "tgfx/layers/DisplayList.h"

namespace hello2d {
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

  static void DrawBackground(tgfx::Canvas* canvas, const AppHost* host);

  explicit LayerBuilder(std::string name);

  virtual ~LayerBuilder() = default;
  std::vector<std::shared_ptr<tgfx::Layer>> getLayersUnderPoint(float x, float y);

  std::string name() const {
    return _name;
  }

  /**
   * Build the contents.
   */
  void build(const AppHost* host);
  virtual std::shared_ptr<tgfx::Layer> buildLayerTree(const AppHost* host) = 0;

 protected:
  float padding = 30.f;
  std::shared_ptr<tgfx::Layer> _root = nullptr;

 private:
  std::string _name;
};
}  // namespace hello2d