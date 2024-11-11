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

#include "Op.h"

namespace tgfx {
class ClearOp : public Op {
 public:
  DEFINE_OP_CLASS_ID

  static std::unique_ptr<ClearOp> Make(Color color, const Rect& scissor);

  void prepare(Context*, uint32_t) override {
  }

  void execute(RenderPass* renderPass) override;

 private:
  ClearOp(Color color, const Rect& scissor) : Op(ClassID()), color(color), scissor(scissor) {
  }

  bool onCombineIfPossible(Op* op) override;

  Color color = Color::Transparent();
  Rect scissor = Rect::MakeEmpty();
};
}  // namespace tgfx
