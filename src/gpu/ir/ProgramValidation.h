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
#include "gpu/ir/ProgramDescription.h"

namespace tgfx {

/**
 * Assigns stable IDs and locations to the interface entries, links the vertex writes to the
 * varying IDs, and checks the vertex side's completeness: every declared varying has a producer
 * and the position is written. The function mutates description only through the derived IDs and
 * producer flags; callers treat a false return as a rejected program.
 */
bool ValidateProgramDescription(ProgramDescription* description, std::string* error);

}  // namespace tgfx
