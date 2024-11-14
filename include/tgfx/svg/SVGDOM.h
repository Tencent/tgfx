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
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Size.h"
#include "tgfx/svg/SVGFontManager.h"
#include "tgfx/svg/SVGIDMapper.h"
#include "tgfx/svg/node/SVGSVG.h"

namespace tgfx {

class SVGDOM {
 public:
  class Builder final {
   public:
    // /**
    //  * Specify a font manager for loading fonts (e.g. from the system) to render <text>
    //  * SVG nodes.
    //  * If this is not set, but a font is required as part of rendering, the text will
    //  * not be displayed.
    //  */
    // Builder& setFontManager(sk_sp<SkFontMgr>);

    // /**
    //  * Specify a resource provider for loading images etc.
    //  */
    // Builder& setResourceProvider(sk_sp<skresources::ResourceProvider>);

    // /**
    //  * Specify the callbacks for dealing with shaping text. See also
    //  * modules/skshaper/utils/FactoryHelpers.h
    //  */
    // Builder& setTextShapingFactory(sk_sp<SkShapers::Factory>);

    std::shared_ptr<SVGDOM> make(Data&, std::shared_ptr<SVGFontManager>) const;

   private:
    // sk_sp<SkFontMgr>                             fFontMgr;
    // sk_sp<skresources::ResourceProvider>         fResourceProvider;
    // sk_sp<SkShapers::Factory>                    fTextShapingFactory;
  };

  // static std::shared_ptr<SVGDOM> MakeFromData(Data& data) {
  //   return Builder().make(data);
  // }

  /**
     * Returns the root (outermost) SVG element.
     */
  const std::shared_ptr<SVGSVG>& getRoot() const {
    return fRoot;
  }

  /**
     * Specify a "container size" for the SVG dom.
     *
     * This is used to resolve the initial viewport when the root SVG width/height are specified
     * in relative units.
     *
     * If the root dimensions are in absolute units, then the container size has no effect since
     * the initial viewport is fixed.
     */
  void setContainerSize(const Size&);

  /**
     * DEPRECATED: use getRoot()->intrinsicSize() to query the root element intrinsic size.
     *
     * Returns the SVG dom container size.
     *
     * If the client specified a container size via setContainerSize(), then the same size is
     * returned.
     *
     * When unspecified by clients, this returns the intrinsic size of the root element, as defined
     * by its width/height attributes.  If either width or height is specified in relative units
     * (e.g. "100%"), then the corresponding intrinsic size dimension is zero.
     */
  const Size& containerSize() const;

  // Returns the node with the given id, or nullptr if not found.
  std::shared_ptr<SVGNode> findNodeById(const char* id);

  void render(Canvas*) const;

  /** Render the node with the given id as if it were the only child of the root. */
  void renderNode(Canvas*, SkSVGPresentationContext&, const char* id) const;

 private:
  SVGDOM(std::shared_ptr<SVGSVG>, SVGIDMapper&&, std::shared_ptr<SVGFontManager> fontManager);

  const std::shared_ptr<SVGSVG> fRoot;
  const std::shared_ptr<SVGFontManager> fFontMgr;
  // const sk_sp<SkShapers::Factory>             fTextShapingFactory;
  const std::shared_ptr<ResourceProvider> fResourceProvider;
  const SVGIDMapper fIDMapper;
  Size fContainerSize;
};
}  // namespace tgfx