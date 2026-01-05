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
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/svg/node/SVGTransformableNode.h"

namespace tgfx {

class SVGLengthContext;
class SVGNode;
class SVGRenderContext;
enum class SVGTag;

class SVGShape : public SVGTransformableNode {
 public:
  void appendChild(std::shared_ptr<SVGNode> node) override;

 protected:
  explicit SVGShape(SVGTag tag);

  void onRender(const SVGRenderContext& context) const override;

  virtual void onDrawFill(Canvas* canvas, const SVGLengthContext& lengthContext, const Paint& paint,
                          PathFillType fillType) const = 0;

  virtual void onDrawStroke(Canvas* canvas, const SVGLengthContext& lengthContext,
                            const Paint& paint, PathFillType fillType,
                            std::shared_ptr<PathEffect> pathEffect) const = 0;

 private:
  using INHERITED = SVGTransformableNode;
};

}  // namespace tgfx
