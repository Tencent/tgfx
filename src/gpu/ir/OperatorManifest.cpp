/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to make the open source community by tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/ir/OperatorManifest.h"

#include "GeneratedOperators.h"

namespace tgfx {

const std::vector<OperatorDesc>& Operators() {
  // The table comes from the same //@op signatures as the Emit/upload glue (GeneratedOperators.cpp
  // generates it beside the bundle): the .inc files are the single source of truth, and a
  // signature drift fails the build of the generated file instead of surfacing at runtime. The
  // ids live in GeneratedOperatorIDs.h, generated from the same set, so the enum, the name lookup
  // and this table cannot disagree.
  //
  // One entry stays hand-written: Clamp is a wrapper over the GLSL built-in clamp(), so it has no
  // .inc source, no bundle module, and only the SPIR-V hand lowering behind it.
  static const std::vector<OperatorDesc> operators = [] {
    std::vector<OperatorDesc> table(kGeneratedOperatorDescs,
                                    kGeneratedOperatorDescs + kGeneratedOperatorDescCount);
    table.push_back({OperatorID::Clamp, "Clamp", "clamp", SLType::Float4,
                     {SLType::Float4, SLType::Float, SLType::Float}, {}});
    return table;
  }();
  return operators;
}

const OperatorDesc* FindOperator(OperatorID id) {
  for (const auto& op : Operators()) {
    if (op.id == id) {
      return &op;
    }
  }
  return nullptr;
}

size_t OperatorCount() {
  return Operators().size();
}

const OperatorDesc* OperatorAt(size_t index) {
  return index < Operators().size() ? &Operators()[index] : nullptr;
}

}  // namespace tgfx
