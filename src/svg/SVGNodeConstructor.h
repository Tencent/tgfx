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

#include "tgfx/svg/SVGDOM.h"
#include "tgfx/svg/xml/XMLDOM.h"

namespace tgfx {

struct ConstructionContext {
  explicit ConstructionContext(SVGIDMapper* mapper) : parentNode(nullptr), nodeIDMapper(mapper) {
  }
  ConstructionContext(const ConstructionContext& other, const std::shared_ptr<SVGNode>& newParent)
      : parentNode(newParent.get()), nodeIDMapper(other.nodeIDMapper) {
  }

  SVGNode* parentNode;
  SVGIDMapper* nodeIDMapper;
};

using AttributeSetter = std::function<bool(SVGNode&, SVGAttribute, const std::string&)>;
struct AttrParseInfo {
  SVGAttribute attribute;
  AttributeSetter setter;
};

class SVGNodeConstructor {
 public:
  static std::shared_ptr<SVGNode> ConstructSVGNode(const ConstructionContext& context,
                                                   const DOMNode* xmlNode);

  static bool SetAttribute(SVGNode& node, const std::string& name, const std::string& value);

 private:
  static void ParseNodeAttributes(const DOMNode* xmlNode, const std::shared_ptr<SVGNode>& svgNode,
                                  SVGIDMapper* mapper);

  static bool SetStyleAttributes(SVGNode& node, SVGAttribute, const std::string& stringValue);

  static bool SetPreserveAspectRatioAttribute(SVGNode& node, SVGAttribute attr,
                                              const std::string& stringValue);

  static bool SetObjectBoundingBoxUnitsAttribute(SVGNode& node, SVGAttribute attr,
                                                 const std::string& stringValue);

  static bool SetViewBoxAttribute(SVGNode& node, SVGAttribute attr, const std::string& stringValue);

  static bool SetLengthAttribute(SVGNode& node, SVGAttribute attr, const std::string& stringValue);

  static bool SetTransformAttribute(SVGNode& node, SVGAttribute attr,
                                    const std::string& stringValue);

  static bool SetStringAttribute(SVGNode& node, SVGAttribute attr, const std::string& stringValue);

  static bool SetIRIAttribute(SVGNode& node, SVGAttribute attr, const std::string& stringValue);

  static std::unordered_map<std::string, AttrParseInfo> attributeParseInfo;
};

}  // namespace tgfx