/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

/**
 * Abstract callback interface for SVG parsing operations.
 * Implementations can be passed to SVGDOM::Make() to handling during parsing.
 */
class SVGCustomParser {
 public:
  virtual ~SVGCustomParser() = default;

  /**
   * Creates a default instance that adds all custom attributes to the SVGNode's customAttributes.
   */
  static std::shared_ptr<SVGCustomParser> Make();

  /**
   * Called when a custom attribute is encountered during parsing.
   * This method is invoked for attributes that are not standard SVGNode properties, allowing custom
   * handling logic to be implemented.
   */
  virtual void handleCustomAttribute(SVGNode& node, const std::string& name,
                                     const std::string& value) = 0;
};

}  // namespace tgfx