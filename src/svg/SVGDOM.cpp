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

#include "tgfx/svg/SVGDOM.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include "core/utils/Log.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Recorder.h"
#include "tgfx/core/Surface.h"
#include "tgfx/svg/SVGAttribute.h"
#include "tgfx/svg/SVGAttributeParser.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/SVGValue.h"
#include "tgfx/svg/node/SVGCircle.h"
#include "tgfx/svg/node/SVGClipPath.h"
#include "tgfx/svg/node/SVGDefs.h"
#include "tgfx/svg/node/SVGEllipse.h"
#include "tgfx/svg/node/SVGFeBlend.h"
#include "tgfx/svg/node/SVGFeColorMatrix.h"
#include "tgfx/svg/node/SVGFeComponentTransfer.h"
#include "tgfx/svg/node/SVGFeComposite.h"
#include "tgfx/svg/node/SVGFeDisplacementMap.h"
#include "tgfx/svg/node/SVGFeFlood.h"
#include "tgfx/svg/node/SVGFeGaussianBlur.h"
#include "tgfx/svg/node/SVGFeImage.h"
#include "tgfx/svg/node/SVGFeLightSource.h"
#include "tgfx/svg/node/SVGFeLighting.h"
#include "tgfx/svg/node/SVGFeMerge.h"
#include "tgfx/svg/node/SVGFeMorphology.h"
#include "tgfx/svg/node/SVGFeOffset.h"
#include "tgfx/svg/node/SVGFeTurbulence.h"
#include "tgfx/svg/node/SVGFilter.h"
#include "tgfx/svg/node/SVGG.h"
#include "tgfx/svg/node/SVGImage.h"
#include "tgfx/svg/node/SVGLine.h"
#include "tgfx/svg/node/SVGLinearGradient.h"
#include "tgfx/svg/node/SVGMask.h"
#include "tgfx/svg/node/SVGNode.h"
#include "tgfx/svg/node/SVGPath.h"
#include "tgfx/svg/node/SVGPattern.h"
#include "tgfx/svg/node/SVGPoly.h"
#include "tgfx/svg/node/SVGRadialGradient.h"
#include "tgfx/svg/node/SVGRect.h"
#include "tgfx/svg/node/SVGSVG.h"
#include "tgfx/svg/node/SVGStop.h"
#include "tgfx/svg/node/SVGText.h"
#include "tgfx/svg/node/SVGUse.h"
#include "tgfx/svg/xml/XMLDOM.h"

namespace tgfx {
namespace {

bool SetIRIAttribute(SVGNode& node, SVGAttribute attr, const char* stringValue) {
  auto parseResult = SVGAttributeParser::parse<SVGIRI>(stringValue);
  if (!parseResult.has_value()) {
    return false;
  }

  node.setAttribute(attr, SVGStringValue(parseResult->iri()));
  return true;
}

bool SetStringAttribute(SVGNode& node, SVGAttribute attr, const char* stringValue) {
  std::string str(stringValue, strlen(stringValue));
  SVGStringType strType = SVGStringType(str);
  node.setAttribute(attr, SVGStringValue(strType));
  return true;
}

bool SetTransformAttribute(SVGNode& node, SVGAttribute attr, const char* stringValue) {
  auto parseResult = SVGAttributeParser::parse<SVGTransformType>(stringValue);
  if (!parseResult.has_value()) {
    return false;
  }

  node.setAttribute(attr, SVGTransformValue(*parseResult));
  return true;
}

bool SetLengthAttribute(SVGNode& node, SVGAttribute attr, const char* stringValue) {
  auto parseResult = SVGAttributeParser::parse<SVGLength>(stringValue);
  if (!parseResult.has_value()) {
    return false;
  }

  node.setAttribute(attr, SVGLengthValue(*parseResult));
  return true;
}

bool SetViewBoxAttribute(SVGNode& node, SVGAttribute attr, const char* stringValue) {
  SVGViewBoxType viewBox;
  SVGAttributeParser parser(stringValue);
  if (!parser.parseViewBox(&viewBox)) {
    return false;
  }

  node.setAttribute(attr, SVGViewBoxValue(viewBox));
  return true;
}

bool SetObjectBoundingBoxUnitsAttribute(SVGNode& node, SVGAttribute attr, const char* stringValue) {
  auto parseResult = SVGAttributeParser::parse<SVGObjectBoundingBoxUnits>(stringValue);
  if (!parseResult.has_value()) {
    return false;
  }

  node.setAttribute(attr, SVGObjectBoundingBoxUnitsValue(*parseResult));
  return true;
}

bool SetPreserveAspectRatioAttribute(SVGNode& node, SVGAttribute attr, const char* stringValue) {
  SVGPreserveAspectRatio par;
  SVGAttributeParser parser(stringValue);
  if (!parser.parsePreserveAspectRatio(&par)) {
    return false;
  }

  node.setAttribute(attr, SVGPreserveAspectRatioValue(par));
  return true;
}

std::string TrimmedString(const char* first, const char* last) {
  ASSERT(first);
  ASSERT(last);
  ASSERT(first <= last);

  while (first <= last && *first <= ' ') {
    first++;
  }
  while (first <= last && *last <= ' ') {
    last--;
  }

  ASSERT(last - first + 1 >= 0);
  return std::string(first, static_cast<size_t>(last - first + 1));
}

// Breaks a "foo: bar; baz: ..." string into key:value pairs.
class StyleIterator {
 public:
  explicit StyleIterator(const char* str) : fPos(str) {
  }

  std::tuple<std::string, std::string> next() {
    std::string name;
    std::string value;

    if (fPos) {
      const char* sep = this->nextSeparator();
      ASSERT(*sep == ';' || *sep == '\0');

      const char* valueSep = strchr(fPos, ':');
      if (valueSep && valueSep < sep) {
        name = TrimmedString(fPos, valueSep - 1);
        value = TrimmedString(valueSep + 1, sep - 1);
      }

      fPos = *sep ? sep + 1 : nullptr;
    }

    return std::make_tuple(name, value);
  }

 private:
  const char* nextSeparator() const {
    const char* sep = fPos;
    while (*sep != ';' && *sep != '\0') {
      sep++;
    }
    return sep;
  }

  const char* fPos;
};

bool set_string_attribute(SVGNode& node, const std::string& name, const std::string& value);

bool SetStyleAttributes(SVGNode& node, SVGAttribute, const char* stringValue) {

  std::string name;
  std::string value;
  StyleIterator iter(stringValue);
  for (;;) {
    std::tie(name, value) = iter.next();
    if (name.empty()) {
      break;
    }
    set_string_attribute(node, name, value);
  }

  return true;
}

template <typename T>
struct SortedDictionaryEntry {
  const std::string fKey;
  const T fValue;
};

using AttributeSetter = std::function<bool(SVGNode&, SVGAttribute, const char*)>;

struct AttrParseInfo {
  SVGAttribute fAttr;
  AttributeSetter fSetter;
};

std::vector<SortedDictionaryEntry<AttrParseInfo>> gAttributeParseInfo = {
    {"cx", {SVGAttribute::Cx, SetLengthAttribute}},
    {"cy", {SVGAttribute::Cy, SetLengthAttribute}},
    {"filterUnits", {SVGAttribute::FilterUnits, SetObjectBoundingBoxUnitsAttribute}},
    // focal point x & y
    {"fx", {SVGAttribute::Fx, SetLengthAttribute}},
    {"fy", {SVGAttribute::Fy, SetLengthAttribute}},
    {"height", {SVGAttribute::Height, SetLengthAttribute}},
    {"preserveAspectRatio", {SVGAttribute::PreserveAspectRatio, SetPreserveAspectRatioAttribute}},
    {"r", {SVGAttribute::R, SetLengthAttribute}},
    {"rx", {SVGAttribute::Rx, SetLengthAttribute}},
    {"ry", {SVGAttribute::Ry, SetLengthAttribute}},
    {"style", {SVGAttribute::Unknown, SetStyleAttributes}},
    {"text", {SVGAttribute::Text, SetStringAttribute}},
    {"transform", {SVGAttribute::Transform, SetTransformAttribute}},
    {"viewBox", {SVGAttribute::ViewBox, SetViewBoxAttribute}},
    {"width", {SVGAttribute::Width, SetLengthAttribute}},
    {"x", {SVGAttribute::X, SetLengthAttribute}},
    {"x1", {SVGAttribute::X1, SetLengthAttribute}},
    {"x2", {SVGAttribute::X2, SetLengthAttribute}},
    {"xlink:href", {SVGAttribute::Href, SetIRIAttribute}},
    {"y", {SVGAttribute::Y, SetLengthAttribute}},
    {"y1", {SVGAttribute::Y1, SetLengthAttribute}},
    {"y2", {SVGAttribute::Y2, SetLengthAttribute}},
};

std::vector<SortedDictionaryEntry<std::shared_ptr<SVGNode> (*)()>> gTagFactories = {
    {"a", []() -> std::shared_ptr<SVGNode> { return SkSVGG::Make(); }},
    {"circle", []() -> std::shared_ptr<SVGNode> { return SVGCircle::Make(); }},
    {"clipPath", []() -> std::shared_ptr<SVGNode> { return SVGClipPath::Make(); }},
    {"defs", []() -> std::shared_ptr<SVGNode> { return SkSVGDefs::Make(); }},
    {"ellipse", []() -> std::shared_ptr<SVGNode> { return SkSVGEllipse::Make(); }},
    {"feBlend", []() -> std::shared_ptr<SVGNode> { return SkSVGFeBlend::Make(); }},
    {"feColorMatrix", []() -> std::shared_ptr<SVGNode> { return SkSVGFeColorMatrix::Make(); }},
    {"feComponentTransfer",
     []() -> std::shared_ptr<SVGNode> { return SkSVGFeComponentTransfer::Make(); }},
    {"feComposite", []() -> std::shared_ptr<SVGNode> { return SkSVGFeComposite::Make(); }},
    {"feDiffuseLighting",
     []() -> std::shared_ptr<SVGNode> { return SkSVGFeDiffuseLighting::Make(); }},
    {"feDisplacementMap",
     []() -> std::shared_ptr<SVGNode> { return SkSVGFeDisplacementMap::Make(); }},
    {"feDistantLight", []() -> std::shared_ptr<SVGNode> { return SkSVGFeDistantLight::Make(); }},
    {"feFlood", []() -> std::shared_ptr<SVGNode> { return SkSVGFeFlood::Make(); }},
    {"feFuncA", []() -> std::shared_ptr<SVGNode> { return SkSVGFeFunc::MakeFuncA(); }},
    {"feFuncB", []() -> std::shared_ptr<SVGNode> { return SkSVGFeFunc::MakeFuncB(); }},
    {"feFuncG", []() -> std::shared_ptr<SVGNode> { return SkSVGFeFunc::MakeFuncG(); }},
    {"feFuncR", []() -> std::shared_ptr<SVGNode> { return SkSVGFeFunc::MakeFuncR(); }},
    {"feGaussianBlur", []() -> std::shared_ptr<SVGNode> { return SkSVGFeGaussianBlur::Make(); }},
    {"feImage", []() -> std::shared_ptr<SVGNode> { return SkSVGFeImage::Make(); }},
    {"feMerge", []() -> std::shared_ptr<SVGNode> { return SkSVGFeMerge::Make(); }},
    {"feMergeNode", []() -> std::shared_ptr<SVGNode> { return SkSVGFeMergeNode::Make(); }},
    {"feMorphology", []() -> std::shared_ptr<SVGNode> { return SkSVGFeMorphology::Make(); }},
    {"feOffset", []() -> std::shared_ptr<SVGNode> { return SkSVGFeOffset::Make(); }},
    {"fePointLight", []() -> std::shared_ptr<SVGNode> { return SkSVGFePointLight::Make(); }},
    {"feSpecularLighting",
     []() -> std::shared_ptr<SVGNode> { return SkSVGFeSpecularLighting::Make(); }},
    {"feSpotLight", []() -> std::shared_ptr<SVGNode> { return SkSVGFeSpotLight::Make(); }},
    {"feTurbulence", []() -> std::shared_ptr<SVGNode> { return SkSVGFeTurbulence::Make(); }},
    {"filter", []() -> std::shared_ptr<SVGNode> { return SkSVGFilter::Make(); }},
    {"g", []() -> std::shared_ptr<SVGNode> { return SkSVGG::Make(); }},
    {"image", []() -> std::shared_ptr<SVGNode> { return SkSVGImage::Make(); }},
    {"line", []() -> std::shared_ptr<SVGNode> { return SkSVGLine::Make(); }},
    {"linearGradient", []() -> std::shared_ptr<SVGNode> { return SkSVGLinearGradient::Make(); }},
    {"mask", []() -> std::shared_ptr<SVGNode> { return SkSVGMask::Make(); }},
    {"path", []() -> std::shared_ptr<SVGNode> { return SkSVGPath::Make(); }},
    {"pattern", []() -> std::shared_ptr<SVGNode> { return SkSVGPattern::Make(); }},
    {"polygon", []() -> std::shared_ptr<SVGNode> { return SkSVGPoly::MakePolygon(); }},
    {"polyline", []() -> std::shared_ptr<SVGNode> { return SkSVGPoly::MakePolyline(); }},
    {"radialGradient", []() -> std::shared_ptr<SVGNode> { return SkSVGRadialGradient::Make(); }},
    {"rect", []() -> std::shared_ptr<SVGNode> { return SkSVGRect::Make(); }},
    {"stop", []() -> std::shared_ptr<SVGNode> { return SkSVGStop::Make(); }},
    //    "svg" handled explicitly
    {"text", []() -> std::shared_ptr<SVGNode> { return SkSVGText::Make(); }},
    {"textPath", []() -> std::shared_ptr<SVGNode> { return SkSVGTextPath::Make(); }},
    {"tspan", []() -> std::shared_ptr<SVGNode> { return SkSVGTSpan::Make(); }},
    {"use", []() -> std::shared_ptr<SVGNode> { return SkSVGUse::Make(); }},
};

template <typename T>
int StringSearch(const std::vector<SortedDictionaryEntry<T>>& base, const std::string& target) {
  if (base.empty()) {
    return -1;
  }

  for (uint32_t i = 0; i < base.size(); i++) {
    if (base[i].fKey == target) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

struct ConstructionContext {
  explicit ConstructionContext(SVGIDMapper* mapper) : fParent(nullptr), fIDMapper(mapper) {
  }
  ConstructionContext(const ConstructionContext& other, const std::shared_ptr<SVGNode>& newParent)
      : fParent(newParent.get()), fIDMapper(other.fIDMapper) {
  }

  SVGNode* fParent;
  SVGIDMapper* fIDMapper;
};

bool set_string_attribute(SVGNode& node, const std::string& name, const std::string& value) {
  if (node.parseAndSetAttribute(name.c_str(), value.c_str())) {
    // Handled by new code path
    return true;
  }

  const int attrIndex = StringSearch(gAttributeParseInfo, name);
  if (attrIndex < 0) {
    return false;
  }

  ASSERT(static_cast<size_t>(attrIndex) < std::size(gAttributeParseInfo));
  const auto& attrInfo = gAttributeParseInfo[static_cast<size_t>(attrIndex)].fValue;
  return attrInfo.fSetter(node, attrInfo.fAttr, value.c_str());
}

void parse_node_attributes(const DOMNode* xmlNode, const std::shared_ptr<SVGNode>& svgNode,
                           SVGIDMapper* mapper) {

  for (const auto& attr : xmlNode->attributes) {
    auto name = attr.name;
    auto value = attr.value;
    if (name == "id") {
      mapper->insert({value, svgNode});
      continue;
    }
    set_string_attribute(*svgNode, name, value);
  }
}

std::shared_ptr<SVGNode> construct_svg_node(const ConstructionContext& ctx,
                                            const DOMNode* xmlNode) {
  std::string elem = xmlNode->name;
  const auto elemType = xmlNode->type;

  if (elemType == DOMNodeType::Text) {
    // Text literals require special handling.
    ASSERT(xmlNode->attributes.empty());
    auto txt = SkSVGTextLiteral::Make();
    txt->setText(xmlNode->name);
    ctx.fParent->appendChild(std::move(txt));

    return nullptr;
  }

  ASSERT(elemType == DOMNodeType::Element);

  auto makeNode = [](const ConstructionContext& ctx,
                     const std::string& elem) -> std::shared_ptr<SVGNode> {
    if (elem == "svg") {
      // Outermost SVG element must be tagged as such.
      return SVGSVG::Make(ctx.fParent ? SVGSVG::Type::kInner : SVGSVG::Type::kRoot);
    }

    // const int tagIndex =
    //     StringSearch(&gTagFactories[0].fKey, static_cast<int>(std::size(gTagFactories)),
    //                  elem.c_str(), sizeof(gTagFactories[0]));
    int tagIndex = StringSearch(gTagFactories, elem);
    if (tagIndex < 0) {
      return nullptr;
    }
    ASSERT(static_cast<size_t>(tagIndex) < std::size(gTagFactories));
    return gTagFactories[static_cast<size_t>(tagIndex)].fValue();
  };

  auto node = makeNode(ctx, elem);
  if (!node) {
    return nullptr;
  }

  parse_node_attributes(xmlNode, node, ctx.fIDMapper);

  ConstructionContext localCtx(ctx, node);
  std::shared_ptr<DOMNode> child = xmlNode->firstChild;
  while (child) {
    std::shared_ptr<SVGNode> childNode = construct_svg_node(localCtx, child.get());
    if (childNode) {
      node->appendChild(std::move(childNode));
    }
    child = child->nextSibling;
  }
  return node;
}

}  // anonymous namespace

// SVGDOM::Builder& SVGDOM::Builder::setFontManager(std::shared_ptr<SkFontMgr> fmgr) {
//     fFontMgr = std::move(fmgr);
//     return *this;
// }

// SVGDOM::Builder& SVGDOM::Builder::setResourceProvider(std::shared_ptr<skresources::ResourceProvider> rp) {
//     fResourceProvider = std::move(rp);
//     return *this;
// }

// SVGDOM::Builder& SVGDOM::Builder::setTextShapingFactory(std::shared_ptr<SkShapers::Factory> f) {
//     fTextShapingFactory = f;
//     return *this;
// }

std::shared_ptr<SVGDOM> SVGDOM::Builder::make(Data& data,
                                              std::shared_ptr<SVGFontManager> fontManager) const {
  // TRACE_EVENT0("skia", TRACE_FUNC);
  if (data.empty()) {
    return nullptr;
  }
  auto xmlDom = DOM::MakeFromData(data);
  if (!xmlDom) {
    return nullptr;
  }

  SVGIDMapper mapper;
  ConstructionContext ctx(&mapper);

  auto root = construct_svg_node(ctx, xmlDom->getRootNode().get());
  if (!root || root->tag() != SVGTag::Svg) {
    return nullptr;
  }

  // class NullResourceProvider final : public skresources::ResourceProvider {
  //     std::shared_ptr<SkData> load(const char[], const char[]) const override { return nullptr; }
  // };

  // auto resource_provider = fResourceProvider ? fResourceProvider
  //                                            : sk_make_sp<NullResourceProvider>();

  // auto factory = fTextShapingFactory ? fTextShapingFactory : SkShapers::Primitive::Factory();

  return std::shared_ptr<SVGDOM>(new SVGDOM(std::static_pointer_cast<SVGSVG>(root),
                                            std::move(mapper), std::move(fontManager)));
}

SVGDOM::SVGDOM(std::shared_ptr<SVGSVG> root, SVGIDMapper&& mapper,
               std::shared_ptr<SVGFontManager> fontManager)
    : root(std::move(root)), fontManager(std::move(fontManager)), _nodeIDMapper(std::move(mapper)) {
}

void SVGDOM::render(Canvas* canvas) {
  if (root) {
    if (!renderPicture) {
      SVGLengthContext lctx(_containerSize);
      SkSVGPresentationContext pctx;

      Recorder recorder;
      auto* drawCanvas = recorder.beginRecording();
      {
        SVGRenderContext renderCtx(canvas->getSurface()->getContext(), drawCanvas, fontManager,
                                   _nodeIDMapper, lctx, pctx, {nullptr, nullptr},
                                   canvas->getMatrix());
        root->render(renderCtx);
      }
      renderPicture = recorder.finishRecordingAsPicture();
    }
    canvas->drawPicture(renderPicture);
  }
}

void SVGDOM::renderNode(Canvas* canvas, SkSVGPresentationContext& pctx, const char* id) const {
  if (root) {
    SVGLengthContext lctx(_containerSize);
    SVGRenderContext renderCtx(canvas->getSurface()->getContext(), canvas, fontManager,
                               _nodeIDMapper, lctx, pctx, {nullptr, nullptr}, canvas->getMatrix());
    root->renderNode(renderCtx, SVGIRI(SVGIRI::Type::Local, SVGStringType(id)));
  }
}

const Size& SVGDOM::containerSize() const {
  return _containerSize;
}

void SVGDOM::setContainerSize(const Size& containerSize) {
  // TODO: inval
  _containerSize = containerSize;
}

std::shared_ptr<SVGNode> SVGDOM::findNodeById(const std::string& id) {
  auto iter = _nodeIDMapper.find(id);
  return iter == this->_nodeIDMapper.end() ? nullptr : iter->second;
}

// TODO(fuego): move this to SkSVGNode or its own CU.
bool SVGNode::setAttribute(const std::string& attributeName, const std::string& attributeValue) {
  return set_string_attribute(*this, attributeName, attributeValue);
}
}  // namespace tgfx