/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "tgfx/core/Shader.h"

namespace tgfx {
/**
 * MaskFilter is the base class for filters that perform transformations on the mask before drawing
 * it.
 */
class MaskFilter {
 public:
  /**
   * Creates a new MaskFilter that draws the mask using the alpha channel of the given shader.
   * If inverted is true, the mask is inverted before drawing.
   */
  static std::shared_ptr<MaskFilter> MakeShader(std::shared_ptr<Shader> shader,
                                                bool inverted = false);

  virtual ~MaskFilter() = default;

  /**
   * Returns a mask filter that will apply the specified viewMatrix to this mask filter when
   * drawing. The specified matrix will be applied after any matrix associated with this mask
   * filter.
   */
  virtual std::shared_ptr<MaskFilter> makeWithMatrix(const Matrix& viewMatrix) const = 0;

 protected:
  enum class Type { Shader, None };

  /**
   * Returns the type of this mask filter.
   */
  virtual Type type() const = 0;

  /**
   * Returns true if this mask filter is equivalent to the specified mask filter.
   */
  virtual bool isEqual(const MaskFilter* maskFilter) const = 0;

 private:
  virtual PlacementPtr<FragmentProcessor> asFragmentProcessor(const FPArgs& args,
                                                              const Matrix* uvMatrix) const = 0;

  virtual void getDeferredGraphics(DeferredGraphics* graphics) const = 0;

  friend class OpsCompositor;
  friend class Picture;
  friend class Types;
};
}  // namespace tgfx
