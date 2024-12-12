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

#include "tgfx/svg/node/SVGClipPath.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/svg/SVGAttributeParser.h"

namespace tgfx {

SVGClipPath::SVGClipPath() : INHERITED(SVGTag::ClipPath) {
}

bool SVGClipPath::parseAndSetAttribute(const char* n, const char* v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setClipPathUnits(
             SVGAttributeParser::parse<SVGObjectBoundingBoxUnits>("clipPathUnits", n, v));
}

#ifndef RENDER_SVG
Path SVGClipPath::resolveClip(const SVGRenderContext& ctx) const {
  auto clip = this->asPath(ctx);

  const auto obbt = ctx.transformForCurrentOBB(ClipPathUnits);
  const auto m = Matrix::MakeTrans(obbt.offset.x, obbt.offset.y) *
                 Matrix::MakeScale(obbt.scale.x, obbt.scale.y);
  clip.transform(m);

  return clip;
}
#endif
}  // namespace tgfx