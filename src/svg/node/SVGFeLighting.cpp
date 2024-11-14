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

#include "tgfx/svg/node/SVGFeLighting.h"
#include "tgfx/svg/SVGAttributeParser.h"

namespace tgfx {

bool SkSVGFeLighting::parseAndSetAttribute(const char* n, const char* v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setSurfaceScale(SVGAttributeParser::parse<SVGNumberType>("surfaceScale", n, v)) ||
         this->setKernelUnitLength(SVGAttributeParser::parse<SkSVGFeLighting::KernelUnitLength>(
             "kernelUnitLength", n, v));
}

template <>
bool SVGAttributeParser::parse<SkSVGFeLighting::KernelUnitLength>(
    SkSVGFeLighting::KernelUnitLength* kernelUnitLength) {
  std::vector<SVGNumberType> values;
  if (!this->parse(&values)) {
    return false;
  }

  kernelUnitLength->fDx = values[0];
  kernelUnitLength->fDy = values.size() > 1 ? values[1] : values[0];
  return true;
}

#ifdef RENDER_SVG
sk_sp<SkImageFilter> SkSVGFeLighting::onMakeImageFilter(const SVGRenderContext& ctx,
                                                        const SkSVGFilterContext& fctx) const {
  for (const auto& child : fChildren) {
    switch (child->tag()) {
      case SkSVGTag::kFeDistantLight:
        return this->makeDistantLight(ctx, fctx,
                                      static_cast<const SkSVGFeDistantLight*>(child.get()));
      case SkSVGTag::kFePointLight:
        return this->makePointLight(ctx, fctx, static_cast<const SkSVGFePointLight*>(child.get()));
      case SkSVGTag::kFeSpotLight:
        return this->makeSpotLight(ctx, fctx, static_cast<const SkSVGFeSpotLight*>(child.get()));
      default:
        // Ignore unknown children, such as <desc> elements
        break;
    }
  }

  SkDebugf("lighting filter effect needs exactly one light source\n");
  return nullptr;
}

SkColor SkSVGFeLighting::resolveLightingColor(const SVGRenderContext& ctx) const {
  const auto color = this->getLightingColor();
  if (!color.isValue()) {
    // Uninherited presentation attributes should have a concrete value by now.
    SkDebugf("unhandled: lighting-color has no value\n");
    return SK_ColorWHITE;
  }

  return ctx.resolveSvgColor(*color);
}

SkPoint3 SkSVGFeLighting::resolveXYZ(const SVGRenderContext& ctx, const SkSVGFilterContext& fctx,
                                     SkSVGNumberType x, SkSVGNumberType y,
                                     SkSVGNumberType z) const {
  const auto obbt = ctx.transformForCurrentOBB(fctx.primitiveUnits());
  const auto xy = SkV2{x, y} * obbt.scale + obbt.offset;
  z = SkSVGLengthContext({obbt.scale.x, obbt.scale.y})
          .resolve(SkSVGLength(z * 100.f, SkSVGLength::Unit::kPercentage),
                   SkSVGLengthContext::LengthType::kOther);
  return SkPoint3::Make(xy.x, xy.y, z);
}
#endif

bool SkSVGFeSpecularLighting::parseAndSetAttribute(const char* n, const char* v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setSpecularConstant(
             SVGAttributeParser::parse<SVGNumberType>("specularConstant", n, v)) ||
         this->setSpecularExponent(
             SVGAttributeParser::parse<SVGNumberType>("specularExponent", n, v));
}

#ifdef RENDER_SVG
sk_sp<SkImageFilter> SkSVGFeSpecularLighting::makeDistantLight(
    const SVGRenderContext& ctx, const SkSVGFilterContext& fctx,
    const SkSVGFeDistantLight* light) const {
  const SkPoint3 dir = light->computeDirection();
  return SkImageFilters::DistantLitSpecular(
      this->resolveXYZ(ctx, fctx, dir.fX, dir.fY, dir.fZ), this->resolveLightingColor(ctx),
      this->getSurfaceScale(), fSpecularConstant, fSpecularExponent,
      fctx.resolveInput(ctx, this->getIn(), this->resolveColorspace(ctx, fctx)),
      this->resolveFilterSubregion(ctx, fctx));
}

sk_sp<SkImageFilter> SkSVGFeSpecularLighting::makePointLight(const SVGRenderContext& ctx,
                                                             const SkSVGFilterContext& fctx,
                                                             const SkSVGFePointLight* light) const {
  return SkImageFilters::PointLitSpecular(
      this->resolveXYZ(ctx, fctx, light->getX(), light->getY(), light->getZ()),
      this->resolveLightingColor(ctx), this->getSurfaceScale(), fSpecularConstant,
      fSpecularExponent, fctx.resolveInput(ctx, this->getIn(), this->resolveColorspace(ctx, fctx)),
      this->resolveFilterSubregion(ctx, fctx));
}

sk_sp<SkImageFilter> SkSVGFeSpecularLighting::makeSpotLight(const SVGRenderContext& ctx,
                                                            const SkSVGFilterContext& fctx,
                                                            const SkSVGFeSpotLight* light) const {
  const auto& limitingConeAngle = light->getLimitingConeAngle();
  const float cutoffAngle = limitingConeAngle.isValid() ? *limitingConeAngle : 180.f;

  return SkImageFilters::SpotLitSpecular(
      this->resolveXYZ(ctx, fctx, light->getX(), light->getY(), light->getZ()),
      this->resolveXYZ(ctx, fctx, light->getPointsAtX(), light->getPointsAtY(),
                       light->getPointsAtZ()),
      light->getSpecularExponent(), cutoffAngle, this->resolveLightingColor(ctx),
      this->getSurfaceScale(), fSpecularConstant, fSpecularExponent,
      fctx.resolveInput(ctx, this->getIn(), this->resolveColorspace(ctx, fctx)),
      this->resolveFilterSubregion(ctx, fctx));
}
#endif

bool SkSVGFeDiffuseLighting::parseAndSetAttribute(const char* n, const char* v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setDiffuseConstant(
             SVGAttributeParser::parse<SVGNumberType>("diffuseConstant", n, v));
}

#ifdef RENDER_SVG
sk_sp<SkImageFilter> SkSVGFeDiffuseLighting::makeDistantLight(
    const SVGRenderContext& ctx, const SkSVGFilterContext& fctx,
    const SkSVGFeDistantLight* light) const {
  const SkPoint3 dir = light->computeDirection();
  return SkImageFilters::DistantLitDiffuse(
      this->resolveXYZ(ctx, fctx, dir.fX, dir.fY, dir.fZ), this->resolveLightingColor(ctx),
      this->getSurfaceScale(), this->getDiffuseConstant(),
      fctx.resolveInput(ctx, this->getIn(), this->resolveColorspace(ctx, fctx)),
      this->resolveFilterSubregion(ctx, fctx));
}

sk_sp<SkImageFilter> SkSVGFeDiffuseLighting::makePointLight(const SVGRenderContext& ctx,
                                                            const SkSVGFilterContext& fctx,
                                                            const SkSVGFePointLight* light) const {
  return SkImageFilters::PointLitDiffuse(
      this->resolveXYZ(ctx, fctx, light->getX(), light->getY(), light->getZ()),
      this->resolveLightingColor(ctx), this->getSurfaceScale(), this->getDiffuseConstant(),
      fctx.resolveInput(ctx, this->getIn(), this->resolveColorspace(ctx, fctx)),
      this->resolveFilterSubregion(ctx, fctx));
}

sk_sp<SkImageFilter> SkSVGFeDiffuseLighting::makeSpotLight(const SVGRenderContext& ctx,
                                                           const SkSVGFilterContext& fctx,
                                                           const SkSVGFeSpotLight* light) const {
  const auto& limitingConeAngle = light->getLimitingConeAngle();
  const float cutoffAngle = limitingConeAngle.isValid() ? *limitingConeAngle : 180.f;

  return SkImageFilters::SpotLitDiffuse(
      this->resolveXYZ(ctx, fctx, light->getX(), light->getY(), light->getZ()),
      this->resolveXYZ(ctx, fctx, light->getPointsAtX(), light->getPointsAtY(),
                       light->getPointsAtZ()),
      light->getSpecularExponent(), cutoffAngle, this->resolveLightingColor(ctx),
      this->getSurfaceScale(), this->getDiffuseConstant(),
      fctx.resolveInput(ctx, this->getIn(), this->resolveColorspace(ctx, fctx)),
      this->resolveFilterSubregion(ctx, fctx));
}
#endif
}  // namespace tgfx