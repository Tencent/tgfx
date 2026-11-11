/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
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
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <vector>
#include "core/Diagnostics.h"
#include "core/Types.h"

namespace tgfx {

// Every check reports each violation as a Diagnostic (core/Diagnostics.h) with its stable code
// and returns the number it reported; the build gates on the sum. The checks never print — the
// cli renders the records — so a test asserts the code and the subject fields instead of
// capturing stderr.

size_t ValidateMacroNames(const std::string& shaderDir, Diagnostics* diagnostics);

/// Checks the shared pointwise dispatch (operator/pointwise_color_dispatch.inc) against the op
/// list: every operator needs an `op == OP_<NAME>` branch or a `tgfx_pointwise_exempt <Name>:
/// <reason>` entry, every branch must name a listed operator, and an exemption must state its
/// reason. Reports verify.pointwise-dispatch and returns the violation count.
size_t ValidatePointwiseDispatch(const std::string& shaderDir, Diagnostics* diagnostics);

/// Checks that every source reading the SPEC_ accessors declares `#pragma tgfx_spec_reader`.
/// Reports spec.reader-undeclared and returns the violation count.
size_t ValidateSpecReaders(const std::string& shaderDir, Diagnostics* diagnostics);

/// Validates reflection-level invariants across every compiled variant: texture samplers must be
/// named TextureSampler_<N> with N dense from zero, array sizes must be at least one (and exactly
/// one for samplers), and uniform/sampler names must be unique and NUL-free — everything the
/// runtime loader rejects, caught at build time instead. Returns the error count.
size_t ValidateReflectionContracts(const std::vector<VariantData>& variants,
                                   Diagnostics* diagnostics);

}  // namespace tgfx
