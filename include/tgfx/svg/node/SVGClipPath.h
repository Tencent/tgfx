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
#include "tgfx/core/Path.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGHiddenContainer.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

class SVGRenderContext;

class SVGClipPath final : public SVGHiddenContainer {
 public:
  static std::shared_ptr<SVGClipPath> Make() {
    return std::shared_ptr<SVGClipPath>(new SVGClipPath());
  }

  SVG_ATTR(ClipPathUnits, SVGObjectBoundingBoxUnits,
           SVGObjectBoundingBoxUnits(SVGObjectBoundingBoxUnits::Type::UserSpaceOnUse))

 private:
  friend class SVGRenderContext;

  SVGClipPath();

  bool parseAndSetAttribute(const std::string& name, const std::string& value) override;

  Path resolveClip(const SVGRenderContext& context) const;

  using INHERITED = SVGHiddenContainer;
};
}  // namespace tgfx
