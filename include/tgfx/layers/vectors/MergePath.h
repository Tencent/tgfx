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

#include "tgfx/layers/vectors/VectorElement.h"

namespace tgfx {

/**
 * Defines the operation used to merge multiple paths.
 */
enum class MergePathOp {
  /**
   * Appends paths together without any boolean operation.
   */
  Append,
  /**
   * Union (inclusive-or) the paths together.
   */
  Union,
  /**
   * Subtract subsequent paths from the first path.
   */
  Difference,
  /**
   * Intersect the paths together.
   */
  Intersect,
  /**
   * Exclusive-or the paths together.
   */
  XOR
};

/**
 * MergePath merges multiple paths in the same group into one.
 */
class MergePath : public VectorElement {
 public:
  /**
   * Creates a new MergePath instance.
   */
  static std::shared_ptr<MergePath> Make();

  /**
   * Returns the operation used to merge paths.
   */
  MergePathOp mode() const {
    return _mode;
  }

  /**
   * Sets the operation used to merge paths.
   */
  void setMode(MergePathOp value);

 protected:
  Type type() const override {
    return Type::MergePath;
  }

  void apply(VectorContext* context) override;

 protected:
  MergePath() = default;

 private:
  MergePathOp _mode = MergePathOp::Append;
};

}  // namespace tgfx
