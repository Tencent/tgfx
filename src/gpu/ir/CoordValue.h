/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except
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

#include <string>
#include "gpu/SLType.h"
#include "gpu/ir/ProgramDescription.h"

namespace tgfx {

/// One FP coordinate after the vertex/fragment interface has been declared. The name is what the
/// fragment shader spells; the ID is the stable link to the varying producer. Keeping both during
/// the migration lets the printers stay text-compatible while validation uses the ID.
struct CoordValue {
  std::string coordName = {};
  SLType coordType = SLType::Void;
  VaryingID varying = INVALID_VARYING;

  const std::string& name() const {
    return coordName;
  }

  SLType type() const {
    return coordType;
  }
};

}  // namespace tgfx
