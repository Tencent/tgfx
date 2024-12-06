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

#include "SVGContext.h"
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include "ElementWriter.h"
#include "SVGUtils.h"
#include "core/FillStyle.h"
#include "core/MCState.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "svg/Caster.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

/////////////////////////////////////////////////////////////////////////////////////////////////
SVGContext::SVGContext(Context* GPUContext, const ISize& size, std::unique_ptr<XMLWriter> writer)
    : _size(size), _context(GPUContext), _writer(std::move(writer)),
      _resourceBucket(new ResourceStore) {
  ASSERT(_writer);
  _writer->writeHeader();

  if (size.width == 0 || size.height == 0) {
    return;
  }

  // The root <svg> tag gets closed by the destructor.
  _rootElement = std::make_unique<ElementWriter>("svg", _context, _writer);

  _rootElement->addAttribute("xmlns", "http://www.w3.org/2000/svg");
  _rootElement->addAttribute("xmlns:xlink", "http://www.w3.org/1999/xlink");
  _rootElement->addAttribute("width", _size.width);
  _rootElement->addAttribute("height", _size.height);
}

void SVGContext::clear() {
  _rootElement = nullptr;
  _writer->clear();
  _writer->writeHeader();

  _rootElement = std::make_unique<ElementWriter>("svg", _context, _writer);
  _rootElement->addAttribute("xmlns", "http://www.w3.org/2000/svg");
  _rootElement->addAttribute("xmlns:xlink", "http://www.w3.org/1999/xlink");
  _rootElement->addAttribute("width", _size.width);
  _rootElement->addAttribute("height", _size.height);
};

void SVGContext::drawRect(const Rect& rect, const MCState& mc, const FillStyle& fill) {

  std::unique_ptr<ElementWriter> svg;
  if (RequiresViewportReset(fill)) {
    svg = std::make_unique<ElementWriter>("svg", _context, this, _resourceBucket.get(), mc, fill);
    svg->addRectAttributes(rect);
  }

  ElementWriter rectElement("rect", _context, this, _resourceBucket.get(), mc, fill);

  if (svg) {
    rectElement.addAttribute("x", 0);
    rectElement.addAttribute("y", 0);
    rectElement.addAttribute("width", "100%");
    rectElement.addAttribute("height", "100%");
  } else {
    rectElement.addRectAttributes(rect);
  }
}

void SVGContext::drawRRect(const RRect& roundRect, const MCState& state, const FillStyle& fill) {
  if (roundRect.isOval()) {
    if (roundRect.rect.width() == roundRect.rect.height()) {
      ElementWriter circleElement("circle", _context, this, _resourceBucket.get(), state, fill);
      circleElement.addCircleAttributes(roundRect.rect);
      return;
    } else {
      ElementWriter ovalElement("ellipse", _context, this, _resourceBucket.get(), state, fill);
      ovalElement.addEllipseAttributes(roundRect.rect);
    }
  } else {
    ElementWriter rrectElement("rect", _context, this, _resourceBucket.get(), state, fill);
    rrectElement.addRoundRectAttributes(roundRect);
  }
}

void SVGContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                           const FillStyle& style) {
  auto path = shape->getPath();
  ElementWriter pathElement("path", _context, this, _resourceBucket.get(), state, style);
  pathElement.addPathAttributes(path, tgfx::SVGContext::PathEncoding());
  if (path.getFillType() == PathFillType::EvenOdd) {
    pathElement.addAttribute("fill-rule", "evenodd");
  }
}

void SVGContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                           const MCState& state, const FillStyle& style) {
  if (image == nullptr) {
    return;
  }
  auto rect = Rect::MakeWH(image->width(), image->height());
  return drawImageRect(std::move(image), rect, sampling, state, style);
}

void SVGContext::drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                               const SamplingOptions&, const MCState& state,
                               const FillStyle& style) {
  if (image == nullptr) {
    return;
  }

  auto dataUri = AsDataUri(this->_context, image);
  if (!dataUri) {
    return;
  }

  Rect srcRect = Rect::MakeWH(image->width(), image->height());
  float scaleX = rect.width() / srcRect.width();
  float scaleY = rect.height() / srcRect.height();
  float transX = rect.left - srcRect.left * scaleX;
  float transY = rect.top - srcRect.top * scaleY;

  MCState newState;
  newState.matrix = state.matrix;
  newState.matrix.postScale(scaleX, scaleY);
  newState.matrix.postTranslate(transX, transY);

  std::string imageID = _resourceBucket->addImage();
  {
    ElementWriter defElement("defs", _context, _writer);
    {
      ElementWriter imageElement("image", _context, _writer);
      imageElement.addAttribute("id", imageID);
      imageElement.addAttribute("width", image->width());
      imageElement.addAttribute("height", image->height());
      imageElement.addAttribute("xlink:href", static_cast<const char*>(dataUri->data()));
    }
  }
  {
    ElementWriter imageUse("use", _context, this, _resourceBucket.get(), newState, style);
    imageUse.addAttribute("xlink:href", "#" + imageID);
  }
}

void SVGContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList, const MCState& state,
                                  const FillStyle& style, const Stroke* stroke) {
  if (!glyphRunList) {
    return;
  }
  if (glyphRunList->hasColor()) {
    drawColorGlyphs(std::move(glyphRunList), state, style);
    return;
  }
  if (Path path; glyphRunList->getPath(&path)) {
    ElementWriter pathElement("path", _context, this, _resourceBucket.get(), state, style, stroke);
    pathElement.addPathAttributes(path, tgfx::SVGContext::PathEncoding());
    if (path.getFillType() == PathFillType::EvenOdd) {
      pathElement.addAttribute("fill-rule", "evenodd");
    }
  }
}

void SVGContext::drawColorGlyphs(const std::shared_ptr<GlyphRunList>& glyphRunList,
                                 const MCState& state, const FillStyle& style) {
  auto bound = glyphRunList->getBounds();
  SVGContext::drawRect(bound, state, style);

  //TODO (YGAurora): Implement emoji export
}

void SVGContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  if (picture != nullptr) {
    picture->playback(this, state);
  }
}

void SVGContext::drawLayer(std::shared_ptr<Picture> picture, const MCState& state, const FillStyle&,
                           std::shared_ptr<ImageFilter> imageFilter) {
  if (picture == nullptr) {
    return;
  }

  Resources resources;
  if (imageFilter) {
    ElementWriter defs("defs", _context, _writer, _resourceBucket.get());
    auto bound = picture->getBounds();
    resources = defs.addImageFilterResource(imageFilter, bound);
  }
  {
    auto groupElement =
        std::make_unique<ElementWriter>("g", _context, _writer, _resourceBucket.get());
    if (imageFilter) {
      groupElement->addAttribute("filter", resources._filter);
    }
    picture->playback(this, state);
  }
}

bool SVGContext::RequiresViewportReset(const FillStyle& fill) {
  auto shader = fill.shader;
  if (!shader) {
    return false;
  }

  if (auto imageShader = ShaderCaster::CastToImageShader(shader)) {
    return imageShader->tileModeX == TileMode::Repeat || imageShader->tileModeY == TileMode::Repeat;
  }
  return false;
}

PathEncoding SVGContext::PathEncoding() {
  return PathEncoding::Absolute;
}

void SVGContext::syncMCState(const MCState& state) {
  auto saveCount = _canvas->getSaveCount();
  if (!_stateStack.empty()) {
    if (_stateStack.top().first >= saveCount) {
      while (_stateStack.top().first > saveCount) {
        _stateStack.pop();
      }
      return;
    }
  }

  if (state.clip.isEmpty()) {
    return;
  }

  auto defineClip = [this](const MCState& insertState) -> std::string {
    std::string clipID = "clip_" + _resourceBucket->addClip();
    ElementWriter clipPath("clipPath", _context, _writer);
    clipPath.addAttribute("id", clipID);
    {
      auto clipPath = insertState.clip;
      std::unique_ptr<ElementWriter> element;
      if (Rect rect; clipPath.isRect(&rect)) {
        element = std::make_unique<ElementWriter>("rect", _context, _writer);
        element->addRectAttributes(rect);
      } else if (RRect rrect; clipPath.isRRect(&rrect)) {
        element = std::make_unique<ElementWriter>("rect", _context, _writer);
        element->addRoundRectAttributes(rrect);
      } else if (Rect bound; clipPath.isOval(&bound)) {
        if (FloatNearlyEqual(bound.width(), bound.height())) {
          element = std::make_unique<ElementWriter>("circle", _context, _writer);
          element->addCircleAttributes(bound);
        } else {
          element = std::make_unique<ElementWriter>("ellipse", _context, _writer);
          element->addEllipseAttributes(bound);
        }
      } else {
        element = std::make_unique<ElementWriter>("path", _context, _writer);
        element->addPathAttributes(clipPath, tgfx::SVGContext::PathEncoding());
        if (clipPath.getFillType() == PathFillType::EvenOdd) {
          element->addAttribute("clip-rule", "evenodd");
        }
      }
    }
    return clipID;
  };

  auto clipID = defineClip(state);
  auto clipElement = std::make_unique<ElementWriter>("g", _context, _writer);
  clipElement->addAttribute("clip-path", "url(#" + clipID + ")");
  _stateStack.emplace(saveCount, std::move(clipElement));
}

}  // namespace tgfx