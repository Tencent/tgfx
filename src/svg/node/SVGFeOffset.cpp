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

#include "tgfx/svg/node/SVGFeOffset.h"
#include "svg/SVGAttributeParser.h"
#include "tgfx/core/ImageFilter.h"

namespace tgfx {

bool SVGFeOffset::parseAndSetAttribute(const std::string& name, const std::string& value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setDx(SVGAttributeParser::parse<SVGNumberType>("dx", name, value)) ||
         this->setDy(SVGAttributeParser::parse<SVGNumberType>("dy", name, value));
}

std::shared_ptr<ImageFilter> SVGFeOffset::onMakeImageFilter(
    const SVGRenderContext& /*context*/, const SVGFilterContext& /*filterContext*/) const {
  //TODO (YGaurora) offset filter not implemented
  return nullptr;
}

}  // namespace tgfx
