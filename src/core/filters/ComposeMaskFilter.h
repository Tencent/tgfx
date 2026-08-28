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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "tgfx/core/MaskFilter.h"

namespace tgfx {
class ComposeMaskFilter : public MaskFilter {
 public:
  ComposeMaskFilter(std::shared_ptr<MaskFilter> inner, std::shared_ptr<MaskFilter> outer)
      : inner(std::move(inner)), outer(std::move(outer)) {
  }

  std::shared_ptr<MaskFilter> makeWithMatrix(const Matrix& viewMatrix) const override;

 protected:
  Type type() const override {
    return Type::Compose;
  }

  bool isEqual(const MaskFilter* maskFilter) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(const FPArgs& args,
                                                      const Matrix* uvMatrix) const override;

 private:
  std::shared_ptr<MaskFilter> inner;
  std::shared_ptr<MaskFilter> outer;
};
}  // namespace tgfx
