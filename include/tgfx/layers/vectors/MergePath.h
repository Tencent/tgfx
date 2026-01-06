/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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
#include "tgfx/layers/vectors/VectorElement.h"

namespace tgfx {

/**
 * MergePath merges multiple paths in the same group into one.
 */
class MergePath : public VectorElement {
 public:
  MergePath() = default;

  /**
   * Returns the operation used to merge paths.
   */
  PathOp pathOp() const {
    return _pathOp;
  }

  /**
   * Sets the operation used to merge paths.
   */
  void setPathOp(PathOp value);

 protected:
  Type type() const override {
    return Type::MergePath;
  }

  void apply(VectorContext* context) override;

 private:
  PathOp _pathOp = PathOp::Append;
};

}  // namespace tgfx
