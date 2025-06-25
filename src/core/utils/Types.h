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

#include "core/utils/Log.h"
#include "layers/contents/RecordedContent.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/Shader.h"
#include "tgfx/layers/ShapeStyle.h"
#include "tgfx/layers/filters/LayerFilter.h"

namespace tgfx {
class Types {
 public:
  using ShaderType = Shader::Type;
  using ImageFilterType = ImageFilter::Type;
  using ColorFilterType = ColorFilter::Type;
  using ImageType = Image::Type;
  using MaskFilterType = MaskFilter::Type;
  using ShapeType = Shape::Type;
  using RecordedContentType = RecordedContent::Type;
  using ShapeStyleType = ShapeStyle::Type;
  using LayerFilterType = LayerFilter::Type;

  static ShaderType Get(const Shader* shader) {
    DEBUG_ASSERT(shader != nullptr);
    return shader->type();
  }

  static ImageFilterType Get(const ImageFilter* imageFilter) {
    DEBUG_ASSERT(imageFilter != nullptr)
    return imageFilter->type();
  }

  static ColorFilterType Get(const ColorFilter* colorFilter) {
    DEBUG_ASSERT(colorFilter != nullptr)
    return colorFilter->type();
  }

  static ImageType Get(const Image* image) {
    DEBUG_ASSERT(image != nullptr)
    return image->type();
  }

  static MaskFilterType Get(const MaskFilter* maskFilter) {
    DEBUG_ASSERT(maskFilter != nullptr)
    return maskFilter->type();
  }

  static ShapeType Get(const Shape* shape) {
    DEBUG_ASSERT(shape != nullptr)
    return shape->type();
  }

  static RecordedContentType Get(const RecordedContent* recordedContent) {
    DEBUG_ASSERT(recordedContent != nullptr)
    return recordedContent->type();
  }

  static ShapeStyleType Get(const ShapeStyle* shapeStyle) {
    DEBUG_ASSERT(shapeStyle != nullptr)
    return shapeStyle->getType();
  }

  static LayerFilterType Get(const LayerFilter* layerFilter) {
    DEBUG_ASSERT(layerFilter != nullptr)
    return layerFilter->type();
  }
};
}  // namespace tgfx
