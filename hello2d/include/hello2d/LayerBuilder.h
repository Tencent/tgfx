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

// Base class for individual layer builders (factory pattern)
class LayerBuilder {
 public:
  explicit LayerBuilder(const std::string& name);
  virtual ~LayerBuilder() = default;

  std::string name() const {
    return _name;
  }

  /**
   * Builds and returns a layer tree based on the provided AppHost information.
   * This is a pure factory method with no side effects or state caching.
   */
  virtual std::shared_ptr<tgfx::Layer> buildLayerTree(const hello2d::AppHost* host) = 0;

 protected:
  float padding = 30.f;

 private:
  std::string _name = "";
};

/**
 * Returns the number of layer builders.
 */
int GetLayerBuilderCount();

/**
 * Returns the names of all layer builders.
 */
std::vector<std::string> GetLayerBuilderNames();

/**
 * Returns the layer builder with the given index.
 */
LayerBuilder* GetLayerBuilderByIndex(int index);

/**
 * Returns the layer builder with the given name.
 */
LayerBuilder* GetLayerBuilderByName(const std::string& name);

/**
 * Draws the background for samples.
 */
void DrawSampleBackground(tgfx::Canvas* canvas, const hello2d::AppHost* host);

/**
 * Helper function: Builds a layer tree and centers it within the screen.
 * This encapsulates the common logic for centering layers with padding.
 */
std::shared_ptr<tgfx::Layer> BuildAndCenterLayer(int builderIndex, const AppHost* host);

// For backward compatibility
using Sample = LayerBuilder;
using SampleBuilder = LayerBuilder;
int GetSampleCount();
std::vector<std::string> GetSampleNames();
Sample* GetSampleByIndex(int index);
Sample* GetSampleByName(const std::string& name);

}  // namespace hello2d
