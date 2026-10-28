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

// ---- sources / plan / compile / emit -------------------------------------------------------

/// A shader source, include, or the source directory itself could not be read.
inline constexpr const char* SourcesUnreadable = "sources.unreadable";

/// A registered family has no Compose enumerator, so the build cannot plan its variants.
inline constexpr const char* PlanNoEnumerator = "plan.missing-enumerator";

/// A reachable variant pair the declared domains cannot serve.
inline constexpr const char* PlanUnbuildable = "plan.unbuildable-permutation";

/// A stage failed to compile to SPIR-V.
inline constexpr const char* CompileGlsl = "compile.glsl";

/// The persistent stage cache could not be opened; the build runs with the memory cache only.
inline constexpr const char* CompileCacheDisabled = "compile.cache-disabled";

/// The reflection extractor met a SPIR-V type it does not know and fell back to a default.
inline constexpr const char* ReflectUnknownType = "compile.reflection-unknown-type";

/// A profile emitter could not produce its storage form for a variant.
inline constexpr const char* EmitFailed = "emit.failed";

/// The stored text of a dialect variant still carries a form it must not (a surviving set or
/// binding qualifier, a conditional directive, Vulkan-only syntax).
inline constexpr const char* EmitFormLeak = "emit.form-leak";

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

/// A bundle the reader contract rejects: bad magic, an out-of-range format version, a wrong
/// toolchain ABI, a mismatched profile tag, an inconsistent pool layout, or a data pool that
/// does not decompress.
inline constexpr const char* VerifyBundleHeader = "verify.bundle-header";

/// A pool entry that is not a sound stage: outside the file, a duplicate key, a key with no
/// rule-reachable candidate or several, or a data/reflection range outside its pool.
inline constexpr const char* VerifyBundlePool = "verify.bundle-pool";

/// A rule-reachable stage key missing from the bundle.
inline constexpr const char* VerifyBundleMissing = "verify.bundle-missing";

/// The recomputed identity hash disagrees with the stored one: the file changed after writing.
inline constexpr const char* VerifyBundleIdentity = "verify.bundle-identity";

/// The manifest beside the bundle does not describe exactly that file.
inline constexpr const char* VerifyManifest = "verify.manifest";

/// The bundle's source digest disagrees with the current sources (or could not be compared):
/// the bundle is stale.
inline constexpr const char* VerifyStale = "verify.stale";

/// Freshness was not checked because no --shader-dir was given; a warning, not a failure.
inline constexpr const char* VerifyFreshnessSkipped = "verify.freshness-skipped";

/// A backend whose bundle was required is absent.
inline constexpr const char* VerifyRequiredMissing = "verify.required-missing";

/// No bundle was found in the directory the verification was pointed at.
inline constexpr const char* VerifyNoBundles = "verify.no-bundles";

/// The --require-backends list is empty or names a backend that does not exist.
inline constexpr const char* VerifyBackendList = "verify.backend-list";

/// A reflected uniform name has no declared writer constant, or the declared type does not match
/// the reflected format.
inline constexpr const char* VerifyParamUndeclared = "verify.param.undeclared";

/// A declared parameter constant no variant's reflection carries: the declaration is dead or the
/// shader dropped the field.
inline constexpr const char* VerifyParamUnused = "verify.param.unused";

/// A preprocessor condition references an identifier that is neither a family permutation
/// dimension, a define in the shader sources, nor a whitelisted builtin: a typo would evaluate
/// silently to zero.
inline constexpr const char* VerifyMacroUnknown = "verify.macro.unknown";

// ---- output: writing the artifacts ----------------------------------------------------------

/// An output file (a report, a bundle, a manifest) could not be written.
inline constexpr const char* OutputWrite = "output.write-failed";

/// Two variants hashed to the same stage key with different content; the writer refuses to drop
/// either silently.
inline constexpr const char* OutputDuplicateKey = "output.duplicate-key";

/// Every code above, in declaration order. DiagnosticCodeTest keeps this list and the call sites
/// in step: a code referenced by a report site must be declared here, and every declared code
/// must be referenced by at least one site (an unreferenced one is dead, or a site that still
/// reports through something else).
inline constexpr const char* AllCodes[] = {
    SourcesUnreadable,     PlanNoEnumerator,   PlanUnbuildable,
    CompileGlsl,           CompileCacheDisabled, ReflectUnknownType,
    EmitFailed,            EmitFormLeak,
    VerifyClipField,       VerifyOpCode,       VerifySlotCapacity,
    VerifySourceFile,      VerifySourceFamily, VerifyReflection,
    VerifyBindingLayout,   VerifyInlinePolicy, VerifyNotApplicable,
    VerifyBundleHeader,    VerifyBundlePool,   VerifyBundleMissing,
    VerifyBundleIdentity,  VerifyManifest,     VerifyStale,
    VerifyFreshnessSkipped, VerifyRequiredMissing, VerifyNoBundles,
    VerifyBackendList,      VerifyParamUndeclared, VerifyParamUnused,
    VerifyMacroUnknown,
    OutputWrite,           OutputDuplicateKey,
};

}  // namespace DiagnosticCode

}  // namespace tgfx
