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
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_set>
#include "core/FillStyle.h"
#include "core/MCState.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/ShaderType.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/svg/SVGParse.h"

namespace tgfx {

namespace {
std::string svg_transform(const Matrix& matrix) {
  ASSERT(!matrix.isIdentity());

  std::stringstream strStream;
  // http://www.w3.org/TR/SVG/coords.html#TransformMatrixDefined
  //    | a c e |
  //    | b d f |
  //    | 0 0 1 |
  //matrix(scaleX skewY skewX scaleY transX transY)
  strStream << "matrix(" << matrix.getScaleX() << " " << matrix.getSkewY() << " "
            << matrix.getSkewX() << " " << matrix.getScaleY() << " " << matrix.getTranslateX()
            << " " << matrix.getTranslateY() << ")";
  return strStream.str();
}

// For maximum compatibility, do not convert colors to named colors, convert them to hex strings.
static std::string svg_color(Color color) {
  auto r = static_cast<uint8_t>(color.red * 255);
  auto g = static_cast<uint8_t>(color.green * 255);
  auto b = static_cast<uint8_t>(color.blue * 255);

  // Some users care about every byte here, so we'll use hex colors with single-digit channels
  // when possible.
  uint8_t rh = r >> 4;
  uint8_t rl = r & 0xf;
  uint8_t gh = g >> 4;
  uint8_t gl = g & 0xf;
  uint8_t bh = b >> 4;
  uint8_t bl = b & 0xf;
  if ((rh == rl) && (gh == gl) && (bh == bl)) {
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "#%1X%1X%1X", rh, gh, bh);
    return std::string(buffer);
  }
  char buffer[8];
  snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", r, g, b);
  return std::string(buffer);
}

std::string svg_cap(LineCap cap) {
  static const std::array<std::string, 3> capMap = {
      "",       // kButt_Cap (default)
      "round",  // kRound_Cap
      "square"  // kSquare_Cap
  };

  auto index = static_cast<size_t>(cap);
  ASSERT(index < capMap.size());
  return capMap[index];
}

std::string svg_join(LineJoin join) {
  static const std::array<std::string, 3> join_map = {
      "",       // kMiter_Join (default)
      "round",  // kRound_Join
      "bevel"   // kBevel_Join
  };

  auto index = static_cast<size_t>(join);
  ASSERT(index < join_map.size());
  return join_map[index];
}

void base64_encode(unsigned char const* bytes_to_encode, size_t in_len, char* ret) {
  static const std::string base64_chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz"
      "0123456789+/";
  int i = 0;
  int j = 0;
  unsigned char char_array_3[3];
  unsigned char char_array_4[4];

  while (in_len--) {
    char_array_3[i++] = *(bytes_to_encode++);
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] = static_cast<unsigned char>(((char_array_3[0] & 0x03) << 4) +
                                                   ((char_array_3[1] & 0xf0) >> 4));
      char_array_4[2] = static_cast<unsigned char>(((char_array_3[1] & 0x0f) << 2) +
                                                   ((char_array_3[2] & 0xc0) >> 6));
      char_array_4[3] = char_array_3[2] & 0x3f;

      for (i = 0; (i < 4); i++) {
        *ret++ = base64_chars[char_array_4[i]];
      }
      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 3; j++) char_array_3[j] = '\0';

    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] = static_cast<unsigned char>(((char_array_3[0] & 0x03) << 4) +
                                                 ((char_array_3[1] & 0xf0) >> 4));
    char_array_4[2] = static_cast<unsigned char>(((char_array_3[1] & 0x0f) << 2) +
                                                 ((char_array_3[2] & 0xc0) >> 6));
    char_array_4[3] = char_array_3[2] & 0x3f;

    for (j = 0; (j < i + 1); j++) ret += base64_chars[char_array_4[j]];

    while ((i++ < 3)) {
      *ret++ = '=';
    }
  }
}

// Returns data uri from bytes.
// it will use any cached data if available, otherwise will
// encode as png.
std::shared_ptr<Data> AsDataUri(Context* GPUContext, const std::shared_ptr<Image>& image) {
  static constexpr auto pngPrefix = "data:image/png;base64,";
  size_t prefixLength = strlen(pngPrefix);

  auto surface = Surface::Make(GPUContext, image->width(), image->height());
  auto* canvas = surface->getCanvas();
  canvas->clear();
  canvas->drawImage(image, 0, 0);
  tgfx::Bitmap bitmap = {};
  bitmap.allocPixels(surface->width(), surface->height());
  auto pixels = bitmap.lockPixels();
  auto success = surface->readPixels(bitmap.info(), pixels);
  bitmap.unlockPixels();
  if (!success) {
    return nullptr;
  }
  auto imageData = bitmap.encode();

  size_t base64Size = ((imageData->size() + 2) / 3) * 4;
  auto bufferSize = prefixLength + base64Size;
  auto dest = static_cast<char*>(malloc(bufferSize));
  memcpy(dest, pngPrefix, prefixLength);
  base64_encode(imageData->bytes(), base64Size, dest + prefixLength - 1);
  dest[bufferSize - 1] = 0;
  auto dataUri = Data::MakeWithoutCopy(dest, bufferSize);
  return dataUri;
}

}  // namespace

Resources::Resources(const FillStyle& fill) : fPaintServer(svg_color(fill.color)) {
}

AutoElement::AutoElement(const std::string& name, Context* GPUContext, XMLWriter* writer)
    : _context(GPUContext), fWriter(writer), fResourceBucket(nullptr) {
  fWriter->startElement(name);
}

AutoElement::AutoElement(const std::string& name, Context* GPUContext,
                         const std::unique_ptr<XMLWriter>& writer)
    : AutoElement(name, GPUContext, writer.get()) {
}

AutoElement::AutoElement(const std::string& name, Context* GPUContext, SVGContext* svgContext,
                         ResourceBucket* bucket, const MCState& mc, const FillStyle& fill,
                         const Stroke* stroke)
    : _context(GPUContext), fWriter(svgContext->getWriter()), fResourceBucket(bucket) {

  svgContext->syncClipStack(mc);
  Resources res = this->addResources(fill);

  fWriter->startElement(name);

  this->addFillAndStroke(fill, stroke, res);

  if (!mc.matrix.isIdentity()) {
    this->addAttribute("transform", svg_transform(mc.matrix));
  }
}

AutoElement::~AutoElement() {
  fWriter->endElement();
  /////////////////////////////////////////////////
  fResourceBucket = nullptr;
}

void AutoElement::AutoElement::addFillAndStroke(const FillStyle& fill, const Stroke* stroke,
                                                const Resources& resources) {
  // static const std::string defaultFill = "black";
  // if (resources.fPaintServer != defaultFill) {
  //   this->addAttribute("fill", resources.fPaintServer);
  // }
  // if (!fill.isOpaque()) {
  //   this->addAttribute("fill-opacity", fill.color.alpha);
  // }
  // if (!resources.fColorFilter.empty()) {
  //   this->addAttribute("filter", resources.fColorFilter);
  // }

  if (!stroke) {  //fill draw
    static const std::string defaultFill = "black";
    if (resources.fPaintServer != defaultFill) {
      this->addAttribute("fill", resources.fPaintServer);
    }
    if (!fill.isOpaque()) {
      this->addAttribute("fill-opacity", fill.color.alpha);
    }
  } else {  //stroke draw
    this->addAttribute("fill", "none");

    this->addAttribute("stroke", resources.fPaintServer);

    float strokeWidth = stroke->width;
    if (strokeWidth == 0) {
      // Hairline stroke
      strokeWidth = 1;
      this->addAttribute("vector-effect", "non-scaling-stroke");
    }
    this->addAttribute("stroke-width", strokeWidth);

    if (auto cap = svg_cap(stroke->cap); !cap.empty()) {
      this->addAttribute("stroke-linecap", cap);
    }

    if (auto join = svg_join(stroke->join); !join.empty()) {
      this->addAttribute("stroke-linejoin", join);
    }

    if (stroke->join == LineJoin::Miter) {
      this->addAttribute("stroke-miterlimit", stroke->miterLimit);
    }

    if (!fill.isOpaque()) {
      this->addAttribute("stroke-opacity", fill.color.alpha);
    }
  }

  if (!resources.fColorFilter.empty()) {
    this->addAttribute("filter", resources.fColorFilter);
  }
}

void AutoElement::addAttribute(const std::string& name, const std::string& val) {
  fWriter->addAttribute(name, val);
}

void AutoElement::addAttribute(const std::string& name, int32_t val) {
  fWriter->addS32Attribute(name, val);
}

void AutoElement::addAttribute(const std::string& name, float val) {
  fWriter->addScalarAttribute(name, val);
}

void AutoElement::addText(const std::string& text) {
  fWriter->addText(text);
}

void AutoElement::addRectAttributes(const Rect& rect) {
  // x, y default to 0
  if (rect.x() != 0) {
    this->addAttribute("x", rect.x());
  }
  if (rect.y() != 0) {
    this->addAttribute("y", rect.y());
  }

  this->addAttribute("width", rect.width());
  this->addAttribute("height", rect.height());
}

void AutoElement::addPathAttributes(const Path& path, PathParse::PathEncoding encoding) {
  this->addAttribute("d", PathParse::ToSVGString(path, encoding));
}

void AutoElement::addTextAttributes(const Font& font) {
  //TODO(YGAurora): add font attributes
  this->addAttribute("font-size", font.getSize());

  std::string familyName;
  std::unordered_set<std::string> familySet;
  auto typeFace = font.getTypeface();

  ASSERT(typeFace);
  // auto style = typeFace->fontStyle();
  this->addAttribute("font-style", font.isFauxItalic() ? "italic" : "oblique");

  this->addAttribute("font-weight", font.isFauxBold() ? "normal" : "bold");
  // int weightIndex = (SkTPin(style.weight(), 100, 900) - 50) / 100;
  // if (weightIndex != 3) {
  //   static constexpr const char* weights[] = {"100", "200", "300",  "normal", "400",
  //                                             "500", "600", "bold", "800",    "900"};
  //   this->addAttribute("font-weight", weights[weightIndex]);
  // }

  // int stretchIndex = style.width() - 1;
  // if (stretchIndex != 4) {
  //   static constexpr const char* stretches[] = {
  //       "ultra-condensed", "extra-condensed", "condensed",      "semi-condensed", "normal",
  //       "semi-expanded",   "expanded",        "extra-expanded", "ultra-expanded"};
  //   this->addAttribute("font-stretch", stretches[stretchIndex]);
  // }

  // sk_sp<SkTypeface::LocalizedStrings> familyNameIter(tface->createFamilyNameIterator());
  // SkTypeface::LocalizedString familyString;
  // if (familyNameIter) {
  //   while (familyNameIter->next(&familyString)) {
  //     if (familySet.contains(familyString.fString)) {
  //       continue;
  //     }
  //     familySet.add(familyString.fString);
  //     familyName.appendf((familyName.isEmpty() ? "%s" : ", %s"), familyString.fString.c_str());
  //   }
  // }
  // if (!familyName.isEmpty()) {
  //   this->addAttribute("font-family", familyName);
  // }
}

Resources AutoElement::addResources(const FillStyle& fill) {
  Resources resources(fill);

  if (auto shader = fill.shader) {
    AutoElement defs("defs", _context, fWriter);
    this->addShaderResources(shader, &resources);
  }

  if (auto colorFilter = fill.colorFilter) {
    // TODO(YGAurora): Implement skia color filters for blend modes other than SrcIn
    BlendMode mode;
    if (colorFilter->asColorMode(nullptr, &mode) && mode == BlendMode::SrcIn) {
      this->addColorFilterResources(*colorFilter, &resources);
    }
  }

  return resources;
}

void AutoElement::addShaderResources(const std::shared_ptr<Shader>& shader, Resources* resources) {

  auto shaderType = shader->type();
  if (shaderType == ShaderType::Color || shaderType == ShaderType::Gradient) {
    this->addGradientShaderResources(shader, resources);
  } else if (shaderType == ShaderType::Image) {
    this->addImageShaderResources(shader, resources);
  }
  // TODO(YGAurora): other shader types?
}

void AutoElement::addGradientShaderResources(const std::shared_ptr<Shader>& shader,
                                             Resources* resources) {
  if (shader->type() == ShaderType::Color) {
    Color color;
    if (shader->asColor(&color)) {
      resources->fPaintServer = svg_color(color);
      return;
    }
  }

  GradientInfo info;
  auto type = shader->asGradient(&info);

  if (type != GradientType::Linear) {
    // TODO(YGAurora): other gradient support
    return;
  }

  ASSERT(info.colors.size() != info.positions.size());

  resources->fPaintServer = "url(#" + addLinearGradientDef(info) + ")";
}

std::string AutoElement::addLinearGradientDef(const GradientInfo& info) {
  ASSERT(fResourceBucket);
  auto id = fResourceBucket->addLinearGradient();

  {
    AutoElement gradient("linearGradient", _context, fWriter);

    gradient.addAttribute("id", id);
    gradient.addAttribute("gradientUnits", "userSpaceOnUse");
    gradient.addAttribute("x1", info.points[0].x);
    gradient.addAttribute("y1", info.points[0].y);
    gradient.addAttribute("x2", info.points[1].x);
    gradient.addAttribute("y2", info.points[1].y);

    ASSERT(info.colors.size() >= 2);
    for (uint32_t i = 0; i < info.colors.size(); ++i) {
      auto color = info.colors[i];
      auto colorStr = svg_color(color);

      AutoElement stop("stop", _context, fWriter);
      stop.addAttribute("offset", info.positions[i]);
      stop.addAttribute("stop-color", colorStr);

      if (!color.isOpaque()) {
        stop.addAttribute("stop-opacity", color.alpha);
      }
    }
  }
  return id;
}

void AutoElement::addImageShaderResources(const std::shared_ptr<Shader>& shader,
                                          Resources* resources) {
  auto [image, x, y] = shader->asImage();
  ASSERT(image);

  auto dataUri = AsDataUri(this->_context, image);
  if (!dataUri) {
    return;
  }

  auto imageWidth = image->width();
  auto imageHeight = image->height();
  auto transDimension = [](TileMode mode, int length) -> std::string {
    if (mode == TileMode::Repeat) {
      return std::to_string(length);
    } else {
      // TODO(YGAurora): other tile modes
      return "100%";
    }
  };
  std::string widthValue = transDimension(x, imageWidth);
  std::string heightValue = transDimension(y, imageHeight);

  std::string patternID = fResourceBucket->addPattern();
  {
    AutoElement pattern("pattern", _context, fWriter);
    pattern.addAttribute("id", patternID);
    pattern.addAttribute("patternUnits", "userSpaceOnUse");
    pattern.addAttribute("patternContentUnits", "userSpaceOnUse");
    pattern.addAttribute("width", widthValue);
    pattern.addAttribute("height", heightValue);
    pattern.addAttribute("x", 0);
    pattern.addAttribute("y", 0);

    {
      std::string imageID = fResourceBucket->addImage();
      AutoElement imageTag("image", _context, fWriter);
      imageTag.addAttribute("id", imageID);
      imageTag.addAttribute("x", 0);
      imageTag.addAttribute("y", 0);
      imageTag.addAttribute("width", image->width());
      imageTag.addAttribute("height", image->height());
      imageTag.addAttribute("xlink:href", static_cast<const char*>(dataUri->data()));
    }
  }
  resources->fPaintServer = "url(#" + patternID + ")";
}

void AutoElement::addColorFilterResources(const ColorFilter& colorFilter, Resources* resources) {
  std::string colorFilterID = fResourceBucket->addColorFilter();
  {
    AutoElement filterElement("filter", _context, fWriter);
    filterElement.addAttribute("id", colorFilterID);
    filterElement.addAttribute("x", "0%");
    filterElement.addAttribute("y", "0%");
    filterElement.addAttribute("width", "100%");
    filterElement.addAttribute("height", "100%");

    Color filterColor;
    BlendMode mode;
    colorFilter.asColorMode(&filterColor, &mode);
    ASSERT(mode == BlendMode::SrcIn);

    {
      // first flood with filter color
      AutoElement floodElement("feFlood", _context, fWriter);
      floodElement.addAttribute("flood-color", svg_color(filterColor));
      floodElement.addAttribute("flood-opacity", filterColor.alpha);
      floodElement.addAttribute("result", "flood");
    }

    {
      // apply the transform to filter color
      AutoElement compositeElement("feComposite", _context, fWriter);
      compositeElement.addAttribute("in", "flood");
      compositeElement.addAttribute("operator", "in");
    }
  }
  resources->fColorFilter = "url(#" + colorFilterID + ")";
}

/////////////////////////////////////////////////////////////////////////////////////////////////
SVGContext::SVGContext(Context* GPUContext, const ISize& size, std::unique_ptr<XMLWriter> writer,
                       uint32_t flags)
    : _size(size), _context(GPUContext), _writer(std::move(writer)),
      _resourceBucket(new ResourceBucket), _flags(flags) {
  ASSERT(_writer);
  _writer->writeHeader();

  if (_flags || size.width == 0) return;

  // The root <svg> tag gets closed by the destructor.
  _rootElement = std::make_unique<AutoElement>("svg", _context, _writer);

  _rootElement->addAttribute("xmlns", "http://www.w3.org/2000/svg");
  _rootElement->addAttribute("xmlns:xlink", "http://www.w3.org/1999/xlink");
  _rootElement->addAttribute("width", _size.width);
  _rootElement->addAttribute("height", _size.height);
}

void SVGContext::clear() {
  _rootElement = nullptr;
  _writer->clear();
  _writer->writeHeader();

  _rootElement = std::make_unique<AutoElement>("svg", _context, _writer);
  _rootElement->addAttribute("xmlns", "http://www.w3.org/2000/svg");
  _rootElement->addAttribute("xmlns:xlink", "http://www.w3.org/1999/xlink");
  _rootElement->addAttribute("width", _size.width);
  _rootElement->addAttribute("height", _size.height);
};

void SVGContext::drawRect(const Rect& rect, const MCState& mc, const FillStyle& fill) {

  std::unique_ptr<AutoElement> svg;
  if (RequiresViewportReset(fill)) {
    svg = std::make_unique<AutoElement>("svg", _context, this, _resourceBucket.get(), mc, fill);
    svg->addRectAttributes(rect);
  }

  AutoElement rectElement("rect", _context, this, _resourceBucket.get(), mc, fill);

  if (svg) {
    rectElement.addAttribute("x", 0);
    rectElement.addAttribute("y", 0);
    rectElement.addAttribute("width", "100%");
    rectElement.addAttribute("height", "100%");
  } else {
    rectElement.addRectAttributes(rect);
  }
}

void SVGContext::drawRRect(const RRect& roundRect, const MCState& mc, const FillStyle& fill) {
  AutoElement elem("path", _context, this, _resourceBucket.get(), mc, fill);
  Path path;
  path.addRRect(roundRect);
  elem.addPathAttributes(path, this->pathEncoding());
}

void SVGContext::drawPath(const Path& path, const MCState& state, const FillStyle& style,
                          const Stroke* stroke) {
  // Create path element.
  AutoElement pathElement("path", _context, this, _resourceBucket.get(), state, style, stroke);
  pathElement.addPathAttributes(path, this->pathEncoding());

  // TODO: inverse fill types?
  if (path.getFillType() == PathFillType::EvenOdd) {
    pathElement.addAttribute("clip-rule", "evenodd");
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
  Rect dstRect = rect;
  float scaleX = dstRect.width() / srcRect.width();
  float scaleY = dstRect.height() / srcRect.height();
  float transX = dstRect.left - srcRect.left * scaleX;
  float transY = dstRect.top - srcRect.top * scaleY;

  MCState newState;
  newState.matrix = state.matrix;
  newState.matrix.setScaleX(scaleX);
  newState.matrix.setScaleY(scaleY);
  newState.matrix.setTranslateX(transX);
  newState.matrix.setTranslateY(transY);

  std::string imageID = _resourceBucket->addImage();
  {
    AutoElement defElement("defs", _context, _writer);
    {
      AutoElement imageElement("image", _context, _writer);
      imageElement.addAttribute("id", imageID);
      imageElement.addAttribute("width", image->width());
      imageElement.addAttribute("height", image->height());
      imageElement.addAttribute("xlink:href", static_cast<const char*>(dataUri->data()));
    }
  }
  {
    AutoElement imageUse("use", _context, this, _resourceBucket.get(), newState, style);
    imageUse.addAttribute("xlink:href", "#" + imageID);
  }
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
    AutoElement clipPath("clipPath", _context, _writer);
    clipPath.addAttribute("id", clipID);
    {
      auto clipPath = mc.clip;
      AutoElement pathElement("path", _context, _writer);
      pathElement.addPathAttributes(clipPath, this->pathEncoding());
      if (clipPath.getFillType() == PathFillType::EvenOdd) {
        pathElement.addAttribute("clip-rule", "evenodd");
      }
    }
    return clipID;
  };

  auto clipID = defineClip(mc);
  if (clipID.empty()) {
    _clipStack.push_back({&mc, nullptr});
  } else {
    auto clipElement = std::make_unique<AutoElement>("g", _context, _writer);
    clipElement->addAttribute("clip-path", "url(#" + clipID + ")");
    _clipStack.push_back({&mc, std::move(clipElement)});
  }
}

}  // namespace tgfx