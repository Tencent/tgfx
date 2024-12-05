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

#include <memory>
// #include "include/core/SkImage.h"
// #include "include/core/SkRect.h"
// #include "include/core/SkRefCnt.h"
// #include "include/private/base/SkAPI.h"
// #include "include/private/base/SkDebug.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGNode.h"
#include "tgfx/svg/node/SVGTransformableNode.h"

namespace tgfx {

class SVGRenderContext;

namespace skresources {
class ResourceProvider;
}

class SkSVGImage final : public SkSVGTransformableNode {
 public:
  static std::shared_ptr<SkSVGImage> Make() {
    return std::shared_ptr<SkSVGImage>(new SkSVGImage());
  }

  void appendChild(std::shared_ptr<SVGNode>) override {
  }

  struct ImageInfo {
    std::shared_ptr<Image> fImage;
    Rect fDst;
  };

#ifndef RENDER_SVG
  bool onPrepareToRender(SVGRenderContext*) const override;
  void onRender(const SVGRenderContext&) const override;
  Path onAsPath(const SVGRenderContext&) const override;
  Rect onObjectBoundingBox(const SVGRenderContext&) const override;
  static ImageInfo LoadImage(const SVGIRI&, const Rect&, SVGPreserveAspectRatio);
#endif

  SVG_ATTR(X, SVGLength, SVGLength(0))
  SVG_ATTR(Y, SVGLength, SVGLength(0))
  SVG_ATTR(Width, SVGLength, SVGLength(0))
  SVG_ATTR(Height, SVGLength, SVGLength(0))
  SVG_ATTR(Href, SVGIRI, SVGIRI())
  SVG_ATTR(PreserveAspectRatio, SVGPreserveAspectRatio, SVGPreserveAspectRatio())

 protected:
  bool parseAndSetAttribute(const char*, const char*) override;

 private:
  SkSVGImage() : INHERITED(SVGTag::Image) {
  }

  using INHERITED = SkSVGTransformableNode;
};

}  // namespace tgfx