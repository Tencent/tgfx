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

#include "hello2d/LayerBuilder.h"

namespace hello2d {
#define DEFINE_LAYER_BUILDER(BuilderName)                                               \
  class BuilderName : public hello2d::LayerBuilder {                                    \
   public:                                                                              \
    BuilderName() : hello2d::LayerBuilder(#BuilderName) {                               \
    }                                                                                   \
                                                                                        \
    std::shared_ptr<tgfx::Layer> buildLayerTree(const hello2d::AppHost* host) override; \
  }

// For backward compatibility
#define DEFINE_SAMPLE DEFINE_LAYER_BUILDER

DEFINE_LAYER_BUILDER(ConicGradient);
DEFINE_LAYER_BUILDER(ImageWithMipmap);
DEFINE_LAYER_BUILDER(ImageWithShadow);
DEFINE_LAYER_BUILDER(RichText);
DEFINE_LAYER_BUILDER(SimpleLayerTree);
}  // namespace hello2d