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
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "BuildCommand.h"

namespace tgfx {

/// Runs --verify-bundle: verifies the bundles of options.verifyBundleDir against the reachable
/// set — headers, identity hash, pool completeness, and (with a shader directory) the source
/// digests. It only reads the bundles and never compiles anything. Returns the process exit
/// code.
int RunVerifyCommand(const BuildOptions& options);

}  // namespace tgfx
