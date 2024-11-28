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
#include "tgfx/core/ColorFilter.h"
namespace tgfx {

class ColorFilterBase : public ColorFilter {
 public:
  /** 
   * If the filter can be represented by a source color plus Mode, this returns true, and sets (if
   * not NULL) the color and mode appropriately.If not, this returns false and ignores the
   * parameters.
   */
  virtual bool asColorMode(Color*, BlendMode*) const {
    return false;
  }
};

inline const ColorFilterBase* asColorFilterBase(const std::shared_ptr<ColorFilter>& filter) {
  return static_cast<const ColorFilterBase*>(filter.get());
}
}  // namespace tgfx