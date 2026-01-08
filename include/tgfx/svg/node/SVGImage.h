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

#pragma once

#include <memory>
#include "tgfx/core/Image.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Stream.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGNode.h"
#include "tgfx/svg/node/SVGTransformableNode.h"

namespace tgfx {

class SVGRenderContext;

namespace skresources {
class ResourceProvider;
}

class SVGImage final : public SVGTransformableNode {
 public:
  static std::shared_ptr<SVGImage> Make() {
    return std::shared_ptr<SVGImage>(new SVGImage());
  }

  void appendChild(std::shared_ptr<SVGNode> /*node*/) override {
  }

  struct ImageInfo {
    std::shared_ptr<Image> image;
    Rect destinationRect;
  };

  bool onPrepareToRender(SVGRenderContext* conetxt) const override;
  void onRender(const SVGRenderContext& conetxt) const override;
  Path onAsPath(const SVGRenderContext& conetxt) const override;
  Rect onObjectBoundingBox(const SVGRenderContext& conetxt) const override;
  static ImageInfo LoadImage(const SVGIRI& iri, const Rect& viewPort);

  SVG_ATTR(X, SVGLength, SVGLength(0))
  SVG_ATTR(Y, SVGLength, SVGLength(0))
  SVG_ATTR(Width, SVGLength, SVGLength(0))
  SVG_ATTR(Height, SVGLength, SVGLength(0))
  SVG_ATTR(Href, SVGIRI, SVGIRI())
  SVG_ATTR(PreserveAspectRatio, SVGPreserveAspectRatio, SVGPreserveAspectRatio())

 protected:
  bool parseAndSetAttribute(const std::string& name, const std::string& value) override;

 private:
  SVGImage() : INHERITED(SVGTag::Image) {
  }

  using INHERITED = SVGTransformableNode;
};

}  // namespace tgfx
