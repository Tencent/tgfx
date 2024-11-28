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

#include <string>
#include "core/FillStyle.h"
#include "core/shaders/ShaderBase.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/svg/SVGParse.h"
#include "xml/XMLWriter.h"

namespace tgfx {

struct Resources {
  explicit Resources(const FillStyle& fill);
  std::string _paintServer;
  std::string _colorFilter;
};

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

  std::string addColorFilter() {
    return "cfilter_" + std::to_string(_colorFilterCount++);
  }

  std::string addPattern() {
    return "pattern_" + std::to_string(_patternCount++);
  }

 private:
  uint32_t _gradientCount = 0;
  uint32_t _pathCount = 0;
  uint32_t _imageCount = 0;
  uint32_t _patternCount = 0;
  uint32_t _colorFilterCount = 0;
};

class ElementWriter {
 public:
  ElementWriter(const std::string& name, Context* GPUContext, XMLWriter* writer);
  ElementWriter(const std::string& name, Context* GPUContext,
                const std::unique_ptr<XMLWriter>& writer);
  ElementWriter(const std::string& name, Context* GPUContext, SVGContext* svgContext,
                ResourceStore* bucket, const MCState& state, const FillStyle& fill,
                const Stroke* stroke = nullptr);
  ~ElementWriter();

  void addAttribute(const std::string& name, const std::string& val);
  void addAttribute(const std::string& name, int32_t val);
  void addAttribute(const std::string& name, float val);
  void addText(const std::string& text);

  void addRectAttributes(const Rect&);
  void addRoundRectAttributes(const RRect&);
  void addCircleAttributes(const Rect&);
  void addEllipseAttributes(const Rect&);
  void addPathAttributes(const Path&, PathParse::PathEncoding);
  void addTextAttributes(const Font&);

 private:
  Resources addResources(const FillStyle& fill);
  void addShaderResources(const std::shared_ptr<Shader>& shader, Resources* resources);
  void addGradientShaderResources(const std::shared_ptr<Shader>& shader, Resources* resources);
  void addColorFilterResources(const ColorFilter& colorFilter, Resources* resources);
  void addImageShaderResources(const std::shared_ptr<Shader>&, Resources* resources);

  void addFillAndStroke(const FillStyle& fill, const Stroke* stroke, const Resources& resources);

  void addGradientColors(const GradientInfo& info);
  std::string addLinearGradientDef(const GradientInfo& info);
  std::string addRadialGradientDef(const GradientInfo& info);
  std::string addUnsupportedGradientDef(const GradientInfo& info);

  Context* _context = nullptr;
  XMLWriter* _writer = nullptr;
  ResourceStore* _resourceStore = nullptr;
};

}  // namespace tgfx