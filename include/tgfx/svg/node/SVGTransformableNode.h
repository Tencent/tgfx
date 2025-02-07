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

#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/SVGValue.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

class SVGRenderContext;
enum class SVGAttribute;

class SVGTransformableNode : public SVGNode {
 public:
  void setTransform(const SVGTransformType& t) {
    transform = t;
  }

 protected:
  SVGTransformableNode(SVGTag tag);

  bool onPrepareToRender(SVGRenderContext* context) const override;

  void onSetAttribute(SVGAttribute attribute, const SVGValue& value) override;

  void mapToParent(Path* path) const;

  void mapToParent(Rect* rect) const;

 private:
  SVGTransformType transform;

  using INHERITED = SVGNode;
};
}  // namespace tgfx
