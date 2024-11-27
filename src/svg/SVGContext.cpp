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
#include <QtGui/qopenglversionfunctions.h>
#include <sys/_types/_u_int32_t.h>
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
SVGContext::SVGContext(Context* GPUContext, const ISize& size, std::unique_ptr<XMLWriter> writer,
                       uint32_t flags)
    : _size(size), _context(GPUContext), _writer(std::move(writer)),
      _resourceBucket(new ResourceStore), _flags(flags) {
  ASSERT(_writer);
  _writer->writeHeader();

  if (_flags || size.width == 0) {
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
  ElementWriter rrectElement("rect", _context, this, _resourceBucket.get(), state, fill);
  rrectElement.addRoundRectAttributes(roundRect);
}

void SVGContext::drawPath(const Path& path, const MCState& state, const FillStyle& style,
                          const Stroke* stroke) {
  std::unique_ptr<ElementWriter> element;
  if (Rect rect; path.isRect(&rect)) {
    element = std::make_unique<ElementWriter>("rect", _context, this, _resourceBucket.get(), state,
                                              style, stroke);
    element->addRectAttributes(rect);
  } else if (RRect rrect; path.isRRect(&rrect)) {
    element = std::make_unique<ElementWriter>("rect", _context, this, _resourceBucket.get(), state,
                                              style, stroke);
    element->addRoundRectAttributes(rrect);
  } else if (Rect bound; path.isOval(&bound)) {
    if (FloatNearlyEqual(bound.width(), bound.height())) {
      element = std::make_unique<ElementWriter>("circle", _context, this, _resourceBucket.get(),
                                                state, style, stroke);
      element->addCircleAttributes(bound);
    } else {
      element = std::make_unique<ElementWriter>("ellipse", _context, this, _resourceBucket.get(),
                                                state, style, stroke);
      element->addEllipseAttributes(bound);
    }
  } else {
    element = std::make_unique<ElementWriter>("path", _context, this, _resourceBucket.get(), state,
                                              style, stroke);
    element->addPathAttributes(path, this->pathEncoding());
    if (path.getFillType() == PathFillType::EvenOdd) {
      element->addAttribute("fill-rule", "evenodd");
    }
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
  if (Path path; glyphRunList->getPath(&path, Matrix::I(), stroke)) {
    SVGContext::drawPath(path, state, style, nullptr);
  }
}

void SVGContext::drawColorGlyphs(const std::shared_ptr<GlyphRunList>& glyphRunList,
                                 const MCState& state, const FillStyle& style) {
  auto bound = glyphRunList->getBounds(Matrix::I());
  SVGContext::drawRect(bound, state, style);

  //TODO (YGAurora): Implement emoji export

  // auto viewMatrix = state.matrix;
  // auto scale = viewMatrix.getMaxScale();
  // viewMatrix.preScale(1.0f / scale, 1.0f / scale);
  // for (const auto& glyphRun : glyphRunList->glyphRuns()) {
  //   auto font = glyphRun.font;
  //   font = font.makeWithSize(font.getSize() * scale);
  //   const auto& glyphIDs = glyphRun.glyphs;
  //   auto glyphCount = glyphIDs.size();
  //   const auto& positions = glyphRun.positions;
  //   auto glyphState = state;
  //   for (size_t i = 0; i < glyphCount; ++i) {
  //     const auto& glyphID = glyphIDs[i];
  //     const auto& position = positions[i];
  //     auto glyphImage = font.getImage(glyphID, &glyphState.matrix);
  //     if (glyphImage == nullptr) {
  //       continue;
  //     }
  //     glyphState.matrix.postTranslate(position.x * scale, position.y * scale);
  //     glyphState.matrix.postConcat(viewMatrix);
  //     auto rect = Rect::MakeWH(glyphImage->width(), glyphImage->height());
  //     SVGContext::drawImageRect(std::move(glyphImage), rect, {}, glyphState, style);
  //   }
  // }
}

void SVGContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  if (picture != nullptr) {
    picture->playback(this, state);
  }
}

bool SVGContext::RequiresViewportReset(const FillStyle& fill) {
  auto shader = fill.shader;
  if (!shader) {
    return false;
  }
  auto [image, tileX, tileY] = shader->asImage();
  if (!image) {
    return false;
  }
  return tileX == TileMode::Repeat || tileY == TileMode::Repeat;
}

void SVGContext::syncClipStack(const MCState& mc) {
  uint32_t equalIndex = 0;
  if (!_clipStack.empty()) {
    for (auto i = _clipStack.size() - 1; i >= 0; i--) {
      if (_clipStack[i].clip == &mc) {
        break;
      }
      equalIndex++;
    }
  }

  //the clip is in the stack, pop all the clips above it
  if (equalIndex != _clipStack.size()) {
    for (uint32_t i = 0; i < equalIndex; i++) {
      _clipStack.pop_back();
    }
    return;
  }

  auto defineClip = [this](const MCState& mc) -> std::string {
    if (mc.clip.isEmpty()) {
      return "";
    }

    std::string clipID = "clip_" + std::to_string(_clipAttributeCount++);
    ElementWriter clipPath("clipPath", _context, _writer);
    clipPath.addAttribute("id", clipID);
    {
      auto clipPath = mc.clip;
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
        element->addPathAttributes(clipPath, this->pathEncoding());
        if (clipPath.getFillType() == PathFillType::EvenOdd) {
          element->addAttribute("clip-rule", "evenodd");
        }
      }
    }
    return clipID;
  };

  auto clipID = defineClip(mc);
  if (clipID.empty()) {
    _clipStack.push_back({&mc, nullptr});
  } else {
    auto clipElement = std::make_unique<ElementWriter>("g", _context, _writer);
    clipElement->addAttribute("clip-path", "url(#" + clipID + ")");
    _clipStack.push_back({&mc, std::move(clipElement)});
  }
}

}  // namespace tgfx