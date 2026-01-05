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
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGHiddenContainer.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

class SVGStop : public SVGHiddenContainer {
 public:
  static constexpr SVGTag tag = SVGTag::Stop;

  static std::shared_ptr<SVGStop> Make() {
    return std::shared_ptr<SVGStop>(new SVGStop());
  }

  SVG_ATTR(Offset, SVGLength, SVGLength(0, SVGLength::Unit::Percentage))

 protected:
  bool parseAndSetAttribute(const std::string& name, const std::string& value) override;

 private:
  SVGStop();

  using INHERITED = SVGHiddenContainer;
};

}  // namespace tgfx
