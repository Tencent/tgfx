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

#include <cstdint>
#include <memory>
#include <string>
#include "core/DrawContext.h"
#include "core/FillStyle.h"
#include "core/MCState.h"
#include "svg/xml/XMLWriter.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Size.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/svg/SVGParse.h"

namespace tgfx {

class SVGContext;

struct Resources {
  explicit Resources(const FillStyle& fill);
  std::string fPaintServer;
  std::string fColorFilter;
};

class ResourceBucket {
 public:
  ResourceBucket() = default;

  std::string addLinearGradient() {
    return "gradient_" + std::to_string(_gradientCount++);
  }

  std::string addPath() {
    return "path_" + std::to_string(_pathCount++);
  }

  std::string addImage() {
    return "img_" + std::to_string(imageCount++);
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
  uint32_t imageCount = 0;
  uint32_t _patternCount = 0;
  uint32_t _colorFilterCount = 0;
};

class AutoElement {
 public:
  AutoElement(const std::string& name, Context* GPUContext, XMLWriter* writer);
  AutoElement(const std::string& name, Context* GPUContext,
              const std::unique_ptr<XMLWriter>& writer);
  AutoElement(const std::string& name, Context* GPUContext, SVGContext* svgContext,
              ResourceBucket* bucket, const MCState& mc, const FillStyle& fill,
              const Stroke* stroke = nullptr);
  ~AutoElement();

  void addAttribute(const std::string& name, const std::string& val);
  void addAttribute(const std::string& name, int32_t val);
  void addAttribute(const std::string& name, float val);
  void addText(const std::string& text);

  void addRectAttributes(const Rect&);
  void addPathAttributes(const Path&, PathParse::PathEncoding);
  void addTextAttributes(const Font&);

 private:
  Resources addResources(const FillStyle& fill);
  void addShaderResources(const std::shared_ptr<Shader>& shader, Resources* resources);
  void addGradientShaderResources(const std::shared_ptr<Shader>& shader, Resources* resources);
  void addColorFilterResources(const ColorFilter& colorFilter, Resources* resources);
  void addImageShaderResources(const std::shared_ptr<Shader>&, Resources* resources);

  // void addPatternDef(const SkBitmap& bm);

  void addFillAndStroke(const FillStyle& fill, const Stroke* stroke, const Resources& resources);

  std::string addLinearGradientDef(const GradientInfo& info);

  Context* _context = nullptr;
  XMLWriter* fWriter;
  ResourceBucket* fResourceBucket;
};

class SVGContext : public DrawContext {
 public:
  SVGContext(Context* GPUContext, const ISize& size, std::unique_ptr<XMLWriter> writer,
             uint32_t flags);
  ~SVGContext() override = default;

  void clear() override;

  void drawRect(const Rect&, const MCState&, const FillStyle&) override;

  void drawRRect(const RRect&, const MCState&, const FillStyle&) override;

  void drawPath(const Path&, const MCState&, const FillStyle&, const Stroke*) override;

  void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                 const MCState& state, const FillStyle& style) override;

  void drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                     const SamplingOptions& sampling, const MCState& state,
                     const FillStyle& style) override;

  void drawGlyphRunList(std::shared_ptr<GlyphRunList>, const MCState&, const FillStyle&,
                        const Stroke*) override {};

  void drawPicture(std::shared_ptr<Picture>, const MCState&) override;

  void drawLayer(std::shared_ptr<Picture>, const MCState&, const FillStyle&,
                 std::shared_ptr<ImageFilter>) override {};

  void syncClipStack(const MCState& mc);

  XMLWriter* getWriter() const {
    return _writer.get();
  }

 private:
  /**
   * Determine if the paint requires us to reset the viewport.Currently, we do this whenever the
   * paint shader calls for a repeating image.
   */
  bool RequiresViewportReset(const FillStyle& fill);

  PathParse::PathEncoding pathEncoding() const {
    return PathParse::PathEncoding::Relative;
  }

  ISize _size;
  Context* _context = nullptr;
  const std::unique_ptr<XMLWriter> _writer;
  const std::unique_ptr<ResourceBucket> _resourceBucket;
  const uint32_t _flags;
  std::unique_ptr<AutoElement> _rootElement;

  struct ClipRecord {
    const MCState* clip;
    std::unique_ptr<AutoElement> element;
  };

  std::vector<ClipRecord> _clipStack;
  int _clipAttributeCount = 0;
};
}  // namespace tgfx