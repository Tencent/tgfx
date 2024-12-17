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

#include "SVGNodeConstructor.h"
#include "core/utils/Log.h"
#include "svg/SVGAttributeParser.h"
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

namespace tgfx {

bool SVGNodeConstructor::SetIRIAttribute(SVGNode& node, SVGAttribute attr,
                                         const std::string& stringValue) {
  auto parseResult = SVGAttributeParser::parse<SVGIRI>(stringValue);
  if (!parseResult.has_value()) {
    return false;
  }

  node.setAttribute(attr, SVGStringValue(parseResult->iri()));
  return true;
}

bool SVGNodeConstructor::SetStringAttribute(SVGNode& node, SVGAttribute attr,
                                            const std::string& stringValue) {
  SVGStringType strType = SVGStringType(stringValue);
  node.setAttribute(attr, SVGStringValue(strType));
  return true;
}

bool SVGNodeConstructor::SetTransformAttribute(SVGNode& node, SVGAttribute attr,
                                               const std::string& stringValue) {
  auto parseResult = SVGAttributeParser::parse<SVGTransformType>(stringValue);
  if (!parseResult.has_value()) {
    return false;
  }

  node.setAttribute(attr, SVGTransformValue(*parseResult));
  return true;
}

bool SVGNodeConstructor::SetLengthAttribute(SVGNode& node, SVGAttribute attr,
                                            const std::string& stringValue) {
  auto parseResult = SVGAttributeParser::parse<SVGLength>(stringValue);
  if (!parseResult.has_value()) {
    return false;
  }

  node.setAttribute(attr, SVGLengthValue(*parseResult));
  return true;
}

bool SVGNodeConstructor::SetViewBoxAttribute(SVGNode& node, SVGAttribute attr,
                                             const std::string& stringValue) {
  SVGViewBoxType viewBox;
  SVGAttributeParser parser(stringValue);
  if (!parser.parseViewBox(&viewBox)) {
    return false;
  }

  node.setAttribute(attr, SVGViewBoxValue(viewBox));
  return true;
}

bool SVGNodeConstructor::SetObjectBoundingBoxUnitsAttribute(SVGNode& node, SVGAttribute attr,
                                                            const std::string& stringValue) {
  auto parseResult = SVGAttributeParser::parse<SVGObjectBoundingBoxUnits>(stringValue);
  if (!parseResult.has_value()) {
    return false;
  }

  node.setAttribute(attr, SVGObjectBoundingBoxUnitsValue(*parseResult));
  return true;
}

bool SVGNodeConstructor::SetPreserveAspectRatioAttribute(SVGNode& node, SVGAttribute attr,
                                                         const std::string& stringValue) {
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
  explicit StyleIterator(const std::string& str) : pos(str.data()) {
  }

  std::tuple<std::string, std::string> next() {
    std::string name;
    std::string value;

    if (pos) {
      const char* sep = this->nextSeparator();
      ASSERT(*sep == ';');
      ASSERT(*sep == '\0')

      const char* valueSep = strchr(pos, ':');
      if (valueSep && valueSep < sep) {
        name = TrimmedString(pos, valueSep - 1);
        value = TrimmedString(valueSep + 1, sep - 1);
      }

      pos = *sep ? sep + 1 : nullptr;
    }

    return std::make_tuple(name, value);
  }

 private:
  const char* nextSeparator() const {
    const char* sep = pos;
    while (*sep != ';' && *sep != '\0') {
      sep++;
    }
    return sep;
  }

  const char* pos;
};

bool SVGNodeConstructor::SetStyleAttributes(SVGNode& node, SVGAttribute,
                                            const std::string& stringValue) {

  std::string name;
  std::string value;
  StyleIterator iter(stringValue);
  for (;;) {
    std::tie(name, value) = iter.next();
    if (name.empty()) {
      break;
    }
    SetAttribute(node, name, value);
  }

  return true;
}

std::unordered_map<std::string, AttrParseInfo> SVGNodeConstructor::attributeParseInfo = {
    {"cx", {SVGAttribute::Cx, SVGNodeConstructor::SetLengthAttribute}},
    {"cy", {SVGAttribute::Cy, SVGNodeConstructor::SetLengthAttribute}},
    {"filterUnits",
     {SVGAttribute::FilterUnits, SVGNodeConstructor::SetObjectBoundingBoxUnitsAttribute}},
    // focal point x & y
    {"fx", {SVGAttribute::Fx, SVGNodeConstructor::SetLengthAttribute}},
    {"fy", {SVGAttribute::Fy, SVGNodeConstructor::SetLengthAttribute}},
    {"height", {SVGAttribute::Height, SVGNodeConstructor::SetLengthAttribute}},
    {"preserveAspectRatio",
     {SVGAttribute::PreserveAspectRatio, SVGNodeConstructor::SetPreserveAspectRatioAttribute}},
    {"r", {SVGAttribute::R, SVGNodeConstructor::SetLengthAttribute}},
    {"rx", {SVGAttribute::Rx, SVGNodeConstructor::SetLengthAttribute}},
    {"ry", {SVGAttribute::Ry, SVGNodeConstructor::SetLengthAttribute}},
    {"style", {SVGAttribute::Unknown, SVGNodeConstructor::SetStyleAttributes}},
    {"text", {SVGAttribute::Text, SVGNodeConstructor::SetStringAttribute}},
    {"transform", {SVGAttribute::Transform, SVGNodeConstructor::SetTransformAttribute}},
    {"viewBox", {SVGAttribute::ViewBox, SVGNodeConstructor::SetViewBoxAttribute}},
    {"width", {SVGAttribute::Width, SVGNodeConstructor::SetLengthAttribute}},
    {"x", {SVGAttribute::X, SVGNodeConstructor::SetLengthAttribute}},
    {"x1", {SVGAttribute::X1, SVGNodeConstructor::SetLengthAttribute}},
    {"x2", {SVGAttribute::X2, SVGNodeConstructor::SetLengthAttribute}},
    {"xlink:href", {SVGAttribute::Href, SVGNodeConstructor::SetIRIAttribute}},
    {"y", {SVGAttribute::Y, SVGNodeConstructor::SetLengthAttribute}},
    {"y1", {SVGAttribute::Y1, SVGNodeConstructor::SetLengthAttribute}},
    {"y2", {SVGAttribute::Y2, SVGNodeConstructor::SetLengthAttribute}},
};

using ElementFactory = std::function<std::shared_ptr<SVGNode>()>;
std::unordered_map<std::string, ElementFactory> ElementFactories = {
    {"a", []() -> std::shared_ptr<SVGNode> { return SVGG::Make(); }},
    {"circle", []() -> std::shared_ptr<SVGNode> { return SVGCircle::Make(); }},
    {"clipPath", []() -> std::shared_ptr<SVGNode> { return SVGClipPath::Make(); }},
    {"defs", []() -> std::shared_ptr<SVGNode> { return SVGDefs::Make(); }},
    {"ellipse", []() -> std::shared_ptr<SVGNode> { return SVGEllipse::Make(); }},
    {"feBlend", []() -> std::shared_ptr<SVGNode> { return SVGFeBlend::Make(); }},
    {"feColorMatrix", []() -> std::shared_ptr<SVGNode> { return SVGFeColorMatrix::Make(); }},
    {"feComponentTransfer",
     []() -> std::shared_ptr<SVGNode> { return SVGFeComponentTransfer::Make(); }},
    {"feComposite", []() -> std::shared_ptr<SVGNode> { return SVGFeComposite::Make(); }},
    {"feDiffuseLighting",
     []() -> std::shared_ptr<SVGNode> { return SVGFeDiffuseLighting::Make(); }},
    {"feDisplacementMap",
     []() -> std::shared_ptr<SVGNode> { return SVGFeDisplacementMap::Make(); }},
    {"feDistantLight", []() -> std::shared_ptr<SVGNode> { return SVGFeDistantLight::Make(); }},
    {"feFlood", []() -> std::shared_ptr<SVGNode> { return SVGFeFlood::Make(); }},
    {"feFuncA", []() -> std::shared_ptr<SVGNode> { return SVGFeFunc::MakeFuncA(); }},
    {"feFuncB", []() -> std::shared_ptr<SVGNode> { return SVGFeFunc::MakeFuncB(); }},
    {"feFuncG", []() -> std::shared_ptr<SVGNode> { return SVGFeFunc::MakeFuncG(); }},
    {"feFuncR", []() -> std::shared_ptr<SVGNode> { return SVGFeFunc::MakeFuncR(); }},
    {"feGaussianBlur", []() -> std::shared_ptr<SVGNode> { return SVGFeGaussianBlur::Make(); }},
    {"feImage", []() -> std::shared_ptr<SVGNode> { return SVGFeImage::Make(); }},
    {"feMerge", []() -> std::shared_ptr<SVGNode> { return SVGFeMerge::Make(); }},
    {"feMergeNode", []() -> std::shared_ptr<SVGNode> { return SVGFeMergeNode::Make(); }},
    {"feMorphology", []() -> std::shared_ptr<SVGNode> { return SVGFeMorphology::Make(); }},
    {"feOffset", []() -> std::shared_ptr<SVGNode> { return SVGFeOffset::Make(); }},
    {"fePointLight", []() -> std::shared_ptr<SVGNode> { return SVGFePointLight::Make(); }},
    {"feSpecularLighting",
     []() -> std::shared_ptr<SVGNode> { return SVGFeSpecularLighting::Make(); }},
    {"feSpotLight", []() -> std::shared_ptr<SVGNode> { return SVGFeSpotLight::Make(); }},
    {"feTurbulence", []() -> std::shared_ptr<SVGNode> { return SVGFeTurbulence::Make(); }},
    {"filter", []() -> std::shared_ptr<SVGNode> { return SVGFilter::Make(); }},
    {"g", []() -> std::shared_ptr<SVGNode> { return SVGG::Make(); }},
    {"image", []() -> std::shared_ptr<SVGNode> { return SVGImage::Make(); }},
    {"line", []() -> std::shared_ptr<SVGNode> { return SVGLine::Make(); }},
    {"linearGradient", []() -> std::shared_ptr<SVGNode> { return SVGLinearGradient::Make(); }},
    {"mask", []() -> std::shared_ptr<SVGNode> { return SVGMask::Make(); }},
    {"path", []() -> std::shared_ptr<SVGNode> { return SVGPath::Make(); }},
    {"pattern", []() -> std::shared_ptr<SVGNode> { return SVGPattern::Make(); }},
    {"polygon", []() -> std::shared_ptr<SVGNode> { return SVGPoly::MakePolygon(); }},
    {"polyline", []() -> std::shared_ptr<SVGNode> { return SVGPoly::MakePolyline(); }},
    {"radialGradient", []() -> std::shared_ptr<SVGNode> { return SVGRadialGradient::Make(); }},
    {"rect", []() -> std::shared_ptr<SVGNode> { return SVGRect::Make(); }},
    {"stop", []() -> std::shared_ptr<SVGNode> { return SVGStop::Make(); }},
    //    "svg" handled explicitly
    {"text", []() -> std::shared_ptr<SVGNode> { return SVGText::Make(); }},
    {"textPath", []() -> std::shared_ptr<SVGNode> { return SVGTextPath::Make(); }},
    {"tspan", []() -> std::shared_ptr<SVGNode> { return SVGTSpan::Make(); }},
    {"use", []() -> std::shared_ptr<SVGNode> { return SVGUse::Make(); }},
};

bool SVGNodeConstructor::SetAttribute(SVGNode& node, const std::string& name,
                                      const std::string& value) {
  if (node.parseAndSetAttribute(name, value)) {
    // Handled by new code path
    return true;
  }

  if (auto iter = attributeParseInfo.find(name); iter != attributeParseInfo.end()) {
    auto setter = iter->second.setter;
    return setter(node, iter->second.attribute, value);
  }
  return true;
}

void SVGNodeConstructor::ParseNodeAttributes(const DOMNode* xmlNode,
                                             const std::shared_ptr<SVGNode>& svgNode,
                                             SVGIDMapper* mapper) {

  for (const auto& attr : xmlNode->attributes) {
    auto name = attr.name;
    auto value = attr.value;
    if (name == "id") {
      mapper->insert({value, svgNode});
      continue;
    }
    SetAttribute(*svgNode, name, value);
  }
}

std::shared_ptr<SVGNode> SVGNodeConstructor::ConstructSVGNode(const ConstructionContext& context,
                                                              const DOMNode* xmlNode) {
  std::string elementName = xmlNode->name;
  const auto elementType = xmlNode->type;

  if (elementType == DOMNodeType::Text) {
    // Text literals require special handling.
    ASSERT(xmlNode->attributes.empty());
    auto text = SVGTextLiteral::Make();
    text->setText(xmlNode->name);
    context.parentNode->appendChild(std::move(text));
    return nullptr;
  }

  ASSERT(elementType == DOMNodeType::Element);

  auto makeNode = [](const ConstructionContext& context,
                     const std::string& elementName) -> std::shared_ptr<SVGNode> {
    if (elementName == "svg") {
      // Outermost SVG element must be tagged as such.
      return SVGSVG::Make(context.parentNode ? SVGSVG::Type::kInner : SVGSVG::Type::kRoot);
    }

    if (auto iter = ElementFactories.find(elementName); iter != ElementFactories.end()) {
      return iter->second();
    }
    //can't find the element factory
    ASSERT(false);
  };

  auto node = makeNode(context, elementName);
  if (!node) {
    return nullptr;
  }

  ParseNodeAttributes(xmlNode, node, context.nodeIDMapper);

  ConstructionContext localCtx(context, node);
  std::shared_ptr<DOMNode> child = xmlNode->firstChild;
  while (child) {
    std::shared_ptr<SVGNode> childNode = ConstructSVGNode(localCtx, child.get());
    if (childNode) {
      node->appendChild(std::move(childNode));
    }
    child = child->nextSibling;
  }
  return node;
}

}  // namespace tgfx