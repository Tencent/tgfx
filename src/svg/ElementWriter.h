/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "ResourceStore.h"
#include "core/filters/DropShadowImageFilter.h"
#include "core/filters/GaussianBlurImageFilter.h"
#include "core/filters/InnerShadowImageFilter.h"
#include "core/filters/MatrixColorFilter.h"
#include "core/filters/ModeColorFilter.h"
#include "core/images/PictureImage.h"
#include "core/shaders/ColorShader.h"
#include "core/shaders/GradientShader.h"
#include "core/shaders/ImageShader.h"
#include "tgfx/core/Brush.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/svg/SVGPathParser.h"
#include "xml/XMLWriter.h"

namespace tgfx {

class ElementWriter {
 public:
  ElementWriter(const std::string& name, XMLWriter* writer);
  ElementWriter(const std::string& name, const std::unique_ptr<XMLWriter>& writer);
  ElementWriter(const std::string& name, const std::unique_ptr<XMLWriter>& writer,
                ResourceStore* bucket);
  ElementWriter(const std::string& name, Context* context, SVGExportContext* svgContext,
                XMLWriter* writer, ResourceStore* bucket, bool disableWarning, const MCState& state,
                const Brush& brush, const Stroke* stroke = nullptr);
  ~ElementWriter();

  void addAttribute(const std::string& name, const std::string& val);
  void addAttribute(const std::string& name, int32_t val);
  void addAttribute(const std::string& name, float val);

  void addText(const std::string& text);
  void addFontAttributes(const Font& font);
  void addRectAttributes(const Rect& rect);
  void addRoundRectAttributes(const RRect& roundRect);
  void addCircleAttributes(const Rect& bound);
  void addEllipseAttributes(const Rect& bound);
  void addPathAttributes(const Path& path, SVGPathParser::PathEncoding encoding);

  Resources addImageFilterResource(const std::shared_ptr<ImageFilter>& imageFilter, Rect bound);

 private:
  Resources addResources(const Brush& brush, Context* context, SVGExportContext* svgContext);

  void addShaderResources(const std::shared_ptr<Shader>& shader, Context* context,
                          Resources* resources);
  void addColorShaderResources(const ColorShader* shader, Resources* resources);
  void addGradientShaderResources(const GradientShader* shader, const Matrix& matrix,
                                  Resources* resources);
  void addImageShaderResources(const ImageShader* shader, const Matrix& matrix, Context* context,
                               Resources* resources);

  void addBlendColorFilterResources(const ModeColorFilter* modeColorFilter, Resources* resources);

  void addMatrixColorFilterResources(const MatrixColorFilter* matrixColorFilter,
                                     Resources* resources);

  void addMaskResources(const std::shared_ptr<MaskFilter>& maskFilter, Resources* resources,
                        Context* context, SVGExportContext* svgContext);

  void addImageMaskResources(const ImageShader* imageShader, const std::string& filterID,
                             Context* context, SVGExportContext* svgContext);

  void addPictureImageMaskResources(const PictureImage* pictureImage, const std::string& filterID,
                                    SVGExportContext* svgContext);

  void addRenderImageMaskResources(const ImageShader* imageShaders, const std::string& filterID,
                                   Context* context);

  void addShaderMaskResources(const std::shared_ptr<Shader>& shader, const std::string& filterID,
                              Context* context);

  void addFillAndStroke(const Brush& brush, const Stroke* stroke, const Resources& resources);

  void addGradientColors(const GradientInfo& info);
  std::string addLinearGradientDef(const GradientInfo& info, const Matrix& matrix);
  std::string addRadialGradientDef(const GradientInfo& info, const Matrix& matrix);
  std::string addUnsupportedGradientDef(const GradientInfo& info, const Matrix& matrix);

  std::string addImageFilter(const std::shared_ptr<ImageFilter>& imageFilter, Rect bound);
  void addBlurImageFilter(const GaussianBlurImageFilter* filter);
  void addDropShadowImageFilter(const DropShadowImageFilter* filter);
  void addInnerShadowImageFilter(const InnerShadowImageFilter* filter);

  void reportUnsupportedElement(const char* message) const;

  XMLWriter* writer = nullptr;
  ResourceStore* resourceStore = nullptr;
  bool disableWarning = false;
};

}  // namespace tgfx
