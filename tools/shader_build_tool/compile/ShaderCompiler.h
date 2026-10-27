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

#include <cstdint>
#include <string>
#include <vector>

namespace tgfx {

enum class ShaderStageType { Vertex, Fragment };

// The compiler primitives (CompileGLSL, TranslateToMSL, CompileGLSLToWGSL, CompileMSLToMetallib)
// moved to compile/StageCompiler.h: they are private to the compile sublayer, and StageCompiler
// is their only caller.

/// Selects the platform the Metal bundle is compiled for: the SDK (macosx, iphoneos or
/// iphonesimulator) and, optionally, the minimum OS version. A metallib is tied to the platform it
/// was compiled for: one built for macOS fails to create a pipeline on an iOS device or simulator,
/// and an iOS one fails on macOS devices that are not in the simulator. The default is the macOS
/// SDK with no minimum version, what the tool produced before the target was selectable. Must be
/// called before anything is compiled or digested. Returns false with *error on an unknown SDK or a
/// malformed version.
bool SetMetalTarget(const std::string& sdk, const std::string& minOS, std::string* error);

/// Describes how metallibs are produced (compiler invocation, link command, and the
/// TGFX_METAL_EXTRA_FLAGS experiment hook). Part of the Metal bundle's source digest: a different
/// invocation can produce different binaries from identical shader text.
std::string MetalCompilerFingerprint();

/// The output of `xcrun metal --version` and the macOS SDK version and build, or "" when they
/// cannot be queried. Not part of the source digest; it keys the stage cache's metallib entries so
/// a toolchain update recompiles them.
std::string MetalToolchainVersion();

/// Prepends #define directives to shader source from a list of "NAME=value" strings.
std::string PrependDefines(const std::string& source, const std::vector<std::string>& defines);

/// Runs the standard GLSL preprocessor (shaderc) over the source: macros are expanded, every
/// conditional block is resolved, comments are dropped, and the #line directives it emits are
/// removed. #version, #extension and #pragma lines are kept. Returns an empty string and sets
/// error on failure.
std::string PreprocessGLSL(const std::string& source, ShaderStageType stage, std::string* error);

}  // namespace tgfx
