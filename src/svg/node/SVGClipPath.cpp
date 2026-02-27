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

#include "tgfx/svg/node/SVGClipPath.h"
#include "svg/SVGAttributeParser.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"

namespace tgfx {

SVGClipPath::SVGClipPath() : INHERITED(SVGTag::ClipPath) {
}

bool SVGClipPath::parseAndSetAttribute(const std::string& name, const std::string& value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setClipPathUnits(
             SVGAttributeParser::parse<SVGObjectBoundingBoxUnits>("clipPathUnits", name, value));
}

Path SVGClipPath::resolveClip(const SVGRenderContext& context) const {
  auto clip = this->asPath(context);

  const auto transform = context.transformForCurrentBoundBox(ClipPathUnits);
  const auto m = Matrix::MakeTrans(transform.offset.x, transform.offset.y) *
                 Matrix::MakeScale(transform.scale.x, transform.scale.y);
  clip.transform(m);

  return clip;
}

}  // namespace tgfx
