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
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

namespace tgfx {

// The stable identifiers of every diagnostic the tool can report. This registry is the single
// source of the vocabulary:
//
//   core/Diagnostics.h         the record model that carries a code
//   every sublayer             reports through DiagnosticCode::<Name> constants
//   cli/                       renders the code into the human line and the JSON report
//
// A code is spelled "<layer>.<domain>.<kind>" and never changes once released: tests assert on
// it, CI greps for it, and the JSON report stores it, so renaming one is a report format change,
// not a wording change. Every code declared here must be referenced by at least one report site
// (and every referenced code must be declared here); DiagnosticCodeTest enforces both directions
// the same way LayeringTest enforces its header whitelist.
//
// Severity is a property of the site, not of the code: the same code can be reported as an Error
// by a gate and as an Unverifiable by a check that could not run.

namespace DiagnosticCode {

/// A site that has no code yet. Every call site must name its code before this one is retired;
/// it exists so the record model could land before the call sites did.
inline constexpr const char* Unclassified = "internal.unclassified";

}  // namespace DiagnosticCode

}  // namespace tgfx
