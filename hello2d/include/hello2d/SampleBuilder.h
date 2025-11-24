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

// Base class for individual samples
class Sample {
 public:
  explicit Sample(const std::string& name);
  virtual ~Sample() = default;

  std::string name() const {
    return _name;
  }

  /**
   * Build the contents.
   */
  void build(const hello2d::AppHost* host);

  std::vector<std::shared_ptr<tgfx::Layer>> getLayersUnderPoint(float x, float y);

  virtual std::shared_ptr<tgfx::Layer> buildLayerTree(const hello2d::AppHost* host) = 0;

 protected:
  float padding = 30.f;
  std::shared_ptr<tgfx::Layer> _root = nullptr;

 private:
  std::string _name = "";
};

/**
 * Returns the number of samples.
 */
int GetSampleCount();

/**
 * Returns the names of all samples.
 */
std::vector<std::string> GetSampleNames();

/**
 * Returns the sample with the given index.
 */
Sample* GetSampleByIndex(int index);

/**
 * Returns the sample with the given name.
 */
Sample* GetSampleByName(const std::string& name);

/**
 * Draws the background for samples.
 */
void DrawSampleBackground(tgfx::Canvas* canvas, const hello2d::AppHost* host);

// For backward compatibility, alias SampleBuilder to Sample
using SampleBuilder = Sample;

}  // namespace hello2d