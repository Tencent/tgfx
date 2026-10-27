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

// ---- verify: the build-time contract checks ------------------------------------------------

/// A variant carries the clip contract but is missing one of its uniform fields.
inline constexpr const char* VerifyClipField = "verify.clip.missing-field";

/// A GLSL OP_* define disagrees with the C++ op-code constant, or is unparseable/unknown.
inline constexpr const char* VerifyOpCode = "verify.opcode.mismatch";

/// A GLSL slot capacity disagrees with the C++ resource capacity.
inline constexpr const char* VerifySlotCapacity = "verify.capacity.mismatch";

/// A shader source refers to a file that does not exist.
inline constexpr const char* VerifySourceFile = "verify.reference.missing-file";

/// A shader source refers to a shader family that is not registered.
inline constexpr const char* VerifySourceFamily = "verify.reference.unknown-family";

/// A reflected sampler/uniform violates the reflection contract the loader enforces on read.
inline constexpr const char* VerifyReflection = "verify.reflection.invalid";

/// A shader resource sits on a set/binding/group the runtime pipeline does not bind there.
inline constexpr const char* VerifyBindingLayout = "verify.binding.misplaced";

/// A function's duplicated instructions exceed the inline expansion policy.
inline constexpr const char* VerifyInlinePolicy = "verify.inline-policy";

/// A check could not run at all (a pattern it scans for is absent, a file it reads is not the
/// format it expects). Reported as Unverifiable, never as a silent skip.
inline constexpr const char* VerifyNotApplicable = "verify.unverifiable";

// ---- output: writing the artifacts ----------------------------------------------------------

/// An output file (a report, a bundle, a manifest) could not be written.
inline constexpr const char* OutputWrite = "output.write-failed";

}  // namespace DiagnosticCode

}  // namespace tgfx
