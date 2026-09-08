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

#include <string>
#include <vector>
#include "BundleWriter.h"

namespace tgfx {

/// Validates that every compiled variant carrying the clip contract (HasClip) declares all
/// ClipContract uniform fields. Returns the error count.
size_t ValidateClipContractFields(const std::vector<VariantData>& variants);

/// Validates the OP_* defines in pointwise_chain_eval.inc against the ChainOp constants that
/// anchor the AOTChainOp enum. Returns the error count.
size_t ValidateChainOpCodes(const std::string& shaderDir);

}  // namespace tgfx
