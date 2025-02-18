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

#include "tgfx/svg/node/SVGFilter.h"
#include <cstddef>
#include <memory>
#include "svg/SVGAttributeParser.h"
#include "svg/SVGFilterContext.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/node/SVGFe.h"
#include "tgfx/svg/node/SVGFeBlend.h"
#include "tgfx/svg/node/SVGFeColorMatrix.h"
#include "tgfx/svg/node/SVGFeComposite.h"
#include "tgfx/svg/node/SVGFeGaussianBlur.h"
#include "tgfx/svg/node/SVGFeOffset.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

bool SVGFilter::parseAndSetAttribute(const std::string& name, const std::string& value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setX(SVGAttributeParser::parse<SVGLength>("x", name, value)) ||
         this->setY(SVGAttributeParser::parse<SVGLength>("y", name, value)) ||
         this->setWidth(SVGAttributeParser::parse<SVGLength>("width", name, value)) ||
         this->setHeight(SVGAttributeParser::parse<SVGLength>("height", name, value)) ||
         this->setFilterUnits(
             SVGAttributeParser::parse<SVGObjectBoundingBoxUnits>("filterUnits", name, value)) ||
         this->setPrimitiveUnits(
             SVGAttributeParser::parse<SVGObjectBoundingBoxUnits>("primitiveUnits", name, value));
}

void SVGFilter::applyProperties(SVGRenderContext* context) const {
  this->onPrepareToRender(context);
}

std::shared_ptr<ImageFilter> SVGFilter::buildFilterDAG(const SVGRenderContext& context) const {
  std::shared_ptr<ImageFilter> filter;
  SVGFilterContext filterContext(context.resolveOBBRect(X, Y, Width, Height, FilterUnits),
                                 PrimitiveUnits);
  SVGRenderContext localContext(context);
  this->applyProperties(&localContext);
  if (auto dropShadowFilter = buildDropShadowFilter(localContext, filterContext)) {
    return dropShadowFilter;
  }
  if (auto innerShadowFilter = buildInnerShadowFilter(localContext, filterContext)) {
    return innerShadowFilter;
  }

  SVGColorspace cs = SVGColorspace::SRGB;
  for (const auto& child : children) {
    if (!SVGFe::IsFilterEffect(child)) {
      continue;
    }

    const auto& feNode = static_cast<const SVGFe&>(*child);
    const auto& feResultType = feNode.getResult();

    // Propagate any inherited properties that may impact filter effect behavior (e.g.
    // color-interpolation-filters). We call this explicitly here because the SVGFe
    // nodes do not participate in the normal onRender path, which is when property
    // propagation currently occurs.
    SVGRenderContext localChildCtx(localContext);
    feNode.applyProperties(&localChildCtx);

    const Rect filterSubregion = feNode.resolveFilterSubregion(localChildCtx, filterContext);
    cs = feNode.resolveColorspace(localChildCtx, filterContext);
    filter = feNode.makeImageFilter(localChildCtx, filterContext);
    if (!filter) {
      return nullptr;
    }

    if (!feResultType.empty()) {
      filterContext.registerResult(feResultType, filter, filterSubregion, cs);
    }

    // Unspecified 'in' and 'in2' inputs implicitly resolve to the previous filter's result.
    filterContext.setPreviousResult(filter, filterSubregion, cs);
  }

  //TODO (YGAurora): Implement color space conversion
  return filter;
}

std::shared_ptr<ImageFilter> SVGFilter::buildDropShadowFilter(
    const SVGRenderContext& context, const SVGFilterContext& filterContext) const {
  if (children.size() < 3) {
    return nullptr;
  }

  size_t blurIndex = 0;
  for (size_t i = 0; i < children.size(); i++) {
    if (children[i]->tag() == SVGTag::FeGaussianBlur && i >= 1 &&
        children[i - 1]->tag() == SVGTag::FeOffset) {
      blurIndex = i;
      break;
    }
  }

  if (blurIndex < 1 || blurIndex == children.size() - 1) {
    return nullptr;
  }

  std::shared_ptr<SVGFeOffset> offsetFe =
      std::static_pointer_cast<SVGFeOffset>(children[blurIndex - 1]);
  std::shared_ptr<SVGFeGaussianBlur> blurFe =
      std::static_pointer_cast<SVGFeGaussianBlur>(children[blurIndex]);
  std::shared_ptr<SVGFeColorMatrix> colorMatrixFe = nullptr;

  if (blurIndex + 1 < children.size() && children[blurIndex + 1]->tag() == SVGTag::FeColorMatrix) {
    colorMatrixFe = std::static_pointer_cast<SVGFeColorMatrix>(children[blurIndex + 1]);
  } else if (blurIndex + 2 < children.size() &&
             children[blurIndex + 1]->tag() == SVGTag::FeComposite &&
             children[blurIndex + 2]->tag() == SVGTag::FeColorMatrix) {
    auto compositeFe = std::static_pointer_cast<SVGFeComposite>(children[blurIndex + 1]);
    if (compositeFe->getOperator() == SVGFeCompositeOperator::Arithmetic) {
      return nullptr;
    }
    colorMatrixFe = std::static_pointer_cast<SVGFeColorMatrix>(children[blurIndex + 2]);
  } else {
    return nullptr;
  }

  auto scale = context.transformForCurrentBoundBox(filterContext.primitiveUnits()).scale;
  auto dx = offsetFe->getDx() * scale.x * context.matrix().getScaleX();
  auto dy = offsetFe->getDy() * scale.y * context.matrix().getScaleY();

  auto blurrinessX = blurFe->getstdDeviation().X * scale.x * 4 * context.matrix().getScaleX();
  auto blurrinessY = blurFe->getstdDeviation().Y * scale.y * 4 * context.matrix().getScaleY();

  auto colorMatrix = colorMatrixFe->getValues();
  Color color{colorMatrix[4], colorMatrix[9], colorMatrix[14], colorMatrix[18]};

  return ImageFilter::DropShadow(dx, dy, blurrinessX, blurrinessY, color);
}

std::shared_ptr<ImageFilter> SVGFilter::buildInnerShadowFilter(
    const SVGRenderContext& context, const SVGFilterContext& filterContext) const {
  if (children.size() < 4) {
    return nullptr;
  }
  size_t compositeIndex = 0;
  for (size_t i = 0; i < children.size(); i++) {
    if (children[i]->tag() == SVGTag::FeComposite) {
      auto compositeFe = std::static_pointer_cast<SVGFeComposite>(children[i]);
      if (compositeFe->getOperator() == SVGFeCompositeOperator::Arithmetic) {
        compositeIndex = i;
        break;
      }
    }
  }
  if (compositeIndex < 2 || compositeIndex + 1 >= children.size()) {
    return nullptr;
  }
  if (children[compositeIndex + 1]->tag() == SVGTag::FeColorMatrix) {
    std::shared_ptr<SVGFeGaussianBlur> blurFe;
    std::shared_ptr<SVGFeOffset> offsetFe;

    if (children[compositeIndex - 2]->tag() == SVGTag::FeGaussianBlur &&
        children[compositeIndex - 1]->tag() == SVGTag::FeOffset) {
      blurFe = std::static_pointer_cast<SVGFeGaussianBlur>(children[compositeIndex - 2]);
      offsetFe = std::static_pointer_cast<SVGFeOffset>(children[compositeIndex - 1]);
    } else if (children[compositeIndex - 2]->tag() == SVGTag::FeOffset &&
               children[compositeIndex - 1]->tag() == SVGTag::FeGaussianBlur) {
      offsetFe = std::static_pointer_cast<SVGFeOffset>(children[compositeIndex - 2]);
      blurFe = std::static_pointer_cast<SVGFeGaussianBlur>(children[compositeIndex - 1]);
    } else {
      return nullptr;
    }

    auto scale = context.transformForCurrentBoundBox(filterContext.primitiveUnits()).scale;
    auto dx = offsetFe->getDx() * scale.x * context.matrix().getScaleX();
    auto dy = offsetFe->getDy() * scale.y * context.matrix().getScaleY();
    auto blurrinessX = blurFe->getstdDeviation().X * scale.x * 4 * context.matrix().getScaleX();
    auto blurrinessY = blurFe->getstdDeviation().Y * scale.y * 4 * context.matrix().getScaleY();
    auto colorMatrixFe = std::static_pointer_cast<SVGFeColorMatrix>(children[compositeIndex + 1]);
    auto colorMatrix = colorMatrixFe->getValues();
    Color color{colorMatrix[4], colorMatrix[9], colorMatrix[14], colorMatrix[18]};

    if (compositeIndex + 2 < children.size() &&
        children[compositeIndex + 2]->tag() == SVGTag::FeBlend) {
      return ImageFilter::InnerShadow(dx, dy, blurrinessX, blurrinessY, color);
    }
    return ImageFilter::InnerShadowOnly(dx, dy, blurrinessX, blurrinessY, color);
  }
  return nullptr;
}

}  // namespace tgfx