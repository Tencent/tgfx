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
#include <vector>
#include "tgfx/core/ImageFilter.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGFe.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

class SkSVGFeDistantLight;
class SkSVGFePointLight;
class SkSVGFeSpotLight;
// class SkSVGFilterContext;
// class SVGRenderContext;

class SkSVGFeLighting : public SkSVGFe {
 public:
  struct KernelUnitLength {
    SVGNumberType fDx;
    SVGNumberType fDy;
  };

  SVG_ATTR(SurfaceScale, SVGNumberType, 1)
  SVG_OPTIONAL_ATTR(UnitLength, KernelUnitLength)

 protected:
  explicit SkSVGFeLighting(SVGTag t) : INHERITED(t) {
  }

  std::vector<SVGFeInputType> getInputs() const final {
    return {this->getIn()};
  }

  bool parseAndSetAttribute(const char*, const char*) override;

  std::shared_ptr<ImageFilter> onMakeImageFilter(const SVGRenderContext&,
                                                 const SkSVGFilterContext&) const final {
    return nullptr;
  };
#ifdef RENDER_SVG
  sk_sp<SkImageFilter> onMakeImageFilter(const SVGRenderContext&,
                                         const SkSVGFilterContext&) const final;

  virtual sk_sp<SkImageFilter> makeDistantLight(const SVGRenderContext&, const SkSVGFilterContext&,
                                                const SkSVGFeDistantLight*) const = 0;

  virtual sk_sp<SkImageFilter> makePointLight(const SVGRenderContext&, const SkSVGFilterContext&,
                                              const SkSVGFePointLight*) const = 0;

  virtual sk_sp<SkImageFilter> makeSpotLight(const SVGRenderContext&, const SkSVGFilterContext&,
                                             const SkSVGFeSpotLight*) const = 0;

  SkColor resolveLightingColor(const SVGRenderContext&) const;

  SkPoint3 resolveXYZ(const SVGRenderContext&, const SkSVGFilterContext&, SkSVGNumberType,
                      SkSVGNumberType, SkSVGNumberType) const;
#endif

 private:
  using INHERITED = SkSVGFe;
};

class SkSVGFeSpecularLighting final : public SkSVGFeLighting {
 public:
  static std::shared_ptr<SkSVGFeSpecularLighting> Make() {
    return std::shared_ptr<SkSVGFeSpecularLighting>(new SkSVGFeSpecularLighting());
  }

  SVG_ATTR(SpecularConstant, SVGNumberType, 1)
  SVG_ATTR(SpecularExponent, SVGNumberType, 1)

 protected:
  bool parseAndSetAttribute(const char*, const char*) override;

#ifdef RENDER_SVG
  sk_sp<SkImageFilter> makeDistantLight(const SVGRenderContext&, const SkSVGFilterContext&,
                                        const SkSVGFeDistantLight*) const final;

  sk_sp<SkImageFilter> makePointLight(const SVGRenderContext&, const SkSVGFilterContext&,
                                      const SkSVGFePointLight*) const final;

  sk_sp<SkImageFilter> makeSpotLight(const SVGRenderContext&, const SkSVGFilterContext&,
                                     const SkSVGFeSpotLight*) const final;
#endif

 private:
  SkSVGFeSpecularLighting() : INHERITED(SVGTag::FeSpecularLighting) {
  }

  using INHERITED = SkSVGFeLighting;
};

class SkSVGFeDiffuseLighting final : public SkSVGFeLighting {
 public:
  static std::shared_ptr<SkSVGFeDiffuseLighting> Make() {
    return std::shared_ptr<SkSVGFeDiffuseLighting>(new SkSVGFeDiffuseLighting());
  }

  SVG_ATTR(DiffuseConstant, SVGNumberType, 1)

 protected:
  bool parseAndSetAttribute(const char*, const char*) override;

#ifdef RENDER_SVG
  sk_sp<SkImageFilter> makeDistantLight(const SVGRenderContext&, const SkSVGFilterContext&,
                                        const SkSVGFeDistantLight*) const final;

  sk_sp<SkImageFilter> makePointLight(const SVGRenderContext&, const SkSVGFilterContext&,
                                      const SkSVGFePointLight*) const final;

  sk_sp<SkImageFilter> makeSpotLight(const SVGRenderContext&, const SkSVGFilterContext&,
                                     const SkSVGFeSpotLight*) const final;
#endif
 private:
  SkSVGFeDiffuseLighting() : INHERITED(SVGTag::FeDiffuseLighting) {
  }

  using INHERITED = SkSVGFeLighting;
};

}  // namespace tgfx