////////////////////////////////////////////////////////////////////////////////////////////////
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
//  license is distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <set>
#include <string>
#include "gpu/ir/ProgramDescription.h"

namespace tgfx {

/// The call-site facts the printer needs: the names declared as combined samplers, which the WGSL
/// path has to address as separate texture and sampler resources.
struct WgslCallSite {
  std::set<std::string> samplerNames = {};
};

/**
 * Turns one stage's ProgramIR body into the complete WGSL module the browser compiles. The body
 * arrives already printed by ProgramBodyPrinter with the WGSL traits; this class owns the stage
 * shell: the uniform struct and its bind group, the stage inputs and outputs, and the entry point.
 */
class WGSLPrinter {
 public:
  static std::string PrintVertex(const InterfaceDesc& interface, const std::string& functions,
                                 const std::string& body, const WgslCallSite& callSite);

  static std::string PrintFragment(const InterfaceDesc& interface, const std::string& functions,
                                   const std::string& body, const WgslCallSite& callSite);
};

}  // namespace tgfx
