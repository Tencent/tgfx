/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "core/FillStyle.h"
#include "core/filters/ImageFilterBase.h"
#include "core/shaders/ShaderBase.h"
#include "svg/SVGUtils.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/gpu/Context.h"
#include "xml/XMLWriter.h"

namespace tgfx {

struct Resources {
  Resources() = default;
  explicit Resources(const FillStyle& fill);
  std::string _paintColor;
  std::string _filter;
};

// TODO(YGAurora)  The resource store implements the feature to reuse resources
class ResourceStore {
 public:
  ResourceStore() = default;

  std::string addGradient() {
    return "gradient_" + std::to_string(_gradientCount++);
  }

  std::string addPath() {
    return "path_" + std::to_string(_pathCount++);
  }

  std::string addImage() {
    return "img_" + std::to_string(_imageCount++);
  }

  std::string addFilter() {
    return "filter_" + std::to_string(_filterCount++);
  }

  std::string addPattern() {
    return "pattern_" + std::to_string(_patternCount++);
  }

  std::string addClip() {
    return "clip_" + std::to_string(_clipCount++);
  }

 private:
  uint32_t _gradientCount = 0;
  uint32_t _pathCount = 0;
  uint32_t _imageCount = 0;
  uint32_t _patternCount = 0;
  uint32_t _filterCount = 0;
  uint32_t _clipCount = 0;
};

class ElementWriter {
 public:
  ElementWriter(const std::string& name, Context* GPUContext, XMLWriter* writer);
  ElementWriter(const std::string& name, Context* GPUContext,
                const std::unique_ptr<XMLWriter>& writer);
  ElementWriter(const std::string& name, Context* GPUContext,
                const std::unique_ptr<XMLWriter>& writer, ResourceStore* bucket);
  ElementWriter(const std::string& name, Context* GPUContext, SVGContext* svgContext,
                ResourceStore* bucket, const MCState& state, const FillStyle& fill,
                const Stroke* stroke = nullptr);
  ~ElementWriter();

  void addAttribute(const std::string& name, const std::string& val);
  void addAttribute(const std::string& name, int32_t val);
  void addAttribute(const std::string& name, float val);

  void addText(const std::string& text);
  void addRectAttributes(const Rect& rect);
  void addRoundRectAttributes(const RRect& roundRect);
  void addCircleAttributes(const Rect& bound);
  void addEllipseAttributes(const Rect& bound);
  void addPathAttributes(const Path& path, PathEncoding encoding);

  Resources addImageFilterResource(const std::shared_ptr<ImageFilter>& imageFilter,
                                   const Rect& bound);

 private:
  Resources addResources(const FillStyle& fill);
  void addShaderResources(const std::shared_ptr<Shader>& shader, Resources* resources);
  void addColorShaderResources(const std::shared_ptr<Shader>& shader, Resources* resources);
  void addGradientShaderResources(const std::shared_ptr<Shader>& shader, Resources* resources);
  void addImageShaderResources(const std::shared_ptr<Shader>&, Resources* resources);
  void addColorFilterResources(const ColorFilter& colorFilter, Resources* resources);

  void addFillAndStroke(const FillStyle& fill, const Stroke* stroke, const Resources& resources);

  void addGradientColors(const GradientInfo& info);
  std::string addLinearGradientDef(const GradientInfo& info);
  std::string addRadialGradientDef(const GradientInfo& info);
  std::string addUnsupportedGradientDef(const GradientInfo& info);

  void addBlurImageFilter(const ImageFilterInfo& info);
  void addDropShadowImageFilter(const ImageFilterInfo& info);
  void addInnerShadowImageFilter(const ImageFilterInfo& info);

  Context* _context = nullptr;
  XMLWriter* _writer = nullptr;
  ResourceStore* _resourceStore = nullptr;
};

}  // namespace tgfx