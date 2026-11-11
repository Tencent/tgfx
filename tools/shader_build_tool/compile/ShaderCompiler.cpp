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

#include "compile/ShaderCompiler.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <regex>
#if !defined(_WIN32)
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
extern char** environ;
#endif
#include <shaderc/shaderc.hpp>
#include <spirv_hlsl.hpp>
#include <spirv_msl.hpp>
#include <spirv_parser.hpp>
#ifdef TGFX_SHADER_TOOL_HAS_D3DCOMPILER
#include <d3dcompiler.h>
#endif
#include "compile/WebGPUDialect.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#ifdef TGFX_SHADER_TOOL_HAS_TINT
#include "src/tint/lang/spirv/reader/reader.h"
#include "src/tint/lang/wgsl/writer/writer.h"
#endif

namespace tgfx {

std::string PrependDefines(const std::string& source, const std::vector<std::string>& defines) {
  std::string prefix;
  for (const auto& def : defines) {
    auto eq = def.find('=');
    if (eq != std::string::npos) {
      prefix += "#define " + def.substr(0, eq) + " " + def.substr(eq + 1) + "\n";
    } else {
      prefix += "#define " + def + "\n";
    }
  }
  // Insert defines after the #version line. Search for it since the file may start with comments.
  auto versionPos = source.find("#version");
  if (versionPos != std::string::npos) {
    auto versionEnd = source.find('\n', versionPos);
    if (versionEnd != std::string::npos) {
      return source.substr(0, versionEnd + 1) + prefix + source.substr(versionEnd + 1);
    }
  }
  return prefix + source;
}

std::string PreprocessGLSL(const std::string& source, ShaderStageType stage, std::string* error) {
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  auto shaderKind =
      (stage == ShaderStageType::Vertex) ? shaderc_vertex_shader : shaderc_fragment_shader;
  auto preprocessed = compiler.PreprocessGlsl(source, shaderKind, "shader", options);
  if (preprocessed.GetCompilationStatus() != shaderc_compilation_status_success) {
    *error = preprocessed.GetErrorMessage();
    return {};
  }
  std::string result{preprocessed.cbegin(), preprocessed.cend()};
  // The #line directives can sit between a layout qualifier and its declaration, which breaks
  // the line-oriented rewrites that consume this text.
  static std::regex lineRegex(R"((^|\n)[ \t]*#[ \t]*line[^\n]*)");
  result = std::regex_replace(result, lineRegex, "$1");
  return result;
}

std::vector<uint32_t> CompileGLSL(const std::string& source, ShaderStageType stage,
                                  const std::string& shaderName, uint32_t variantIndex,
                                  bool optimize, std::string* error) {
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetOptimizationLevel(optimize ? shaderc_optimization_level_performance
                                        : shaderc_optimization_level_zero);
  options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);

  auto shaderKind =
      (stage == ShaderStageType::Vertex) ? shaderc_vertex_shader : shaderc_fragment_shader;
  std::string filename = shaderName + "_variant_" + std::to_string(variantIndex);

  auto spvResult = compiler.CompileGlslToSpv(source, shaderKind, filename.c_str(), "main", options);

  if (spvResult.GetCompilationStatus() != shaderc_compilation_status_success) {
    *error = "[" + shaderName + " variant " + std::to_string(variantIndex) + "] " +
             spvResult.GetErrorMessage();
    return {};
  }

  return {spvResult.cbegin(), spvResult.cend()};
}

std::string TranslateToMSL(const std::vector<uint32_t>& spirv, ShaderStageType stage,
                           std::string* error) {
  // spirv-cross uses C++ exceptions internally for error reporting. This catch block is a
  // necessary boundary isolation to convert third-party exceptions into return values, as
  // TGFX project rules forbid exceptions in our own code.
  try {
    spirv_cross::Parser parser(spirv);
    parser.parse();
    spirv_cross::CompilerMSL mslCompiler(std::move(parser.get_parsed_ir()));

    spirv_cross::CompilerMSL::Options mslOptions;
    mslOptions.set_msl_version(2, 3);
    mslOptions.enable_decoration_binding = true;
    if (stage == ShaderStageType::Fragment) {
      mslOptions.use_framebuffer_fetch_subpasses = true;
    }
    mslCompiler.set_msl_options(mslOptions);

    auto commonOptions = mslCompiler.get_common_options();
    commonOptions.vertex.flip_vert_y = true;
    mslCompiler.set_common_options(commonOptions);

    auto executionModel = (stage == ShaderStageType::Vertex) ? spv::ExecutionModelVertex
                                                             : spv::ExecutionModelFragment;

    // Map SPIR-V resource bindings to MSL buffer/texture/sampler indices.
    // This must match the runtime MetalShaderModule::convertSPIRVToMSL logic:
    // mslBuffer = spvBinding, mslTexture = spvBinding, mslSampler = spvBinding.
    auto uboResources = mslCompiler.get_shader_resources().uniform_buffers;
    for (auto& ubo : uboResources) {
      uint32_t spvBinding = mslCompiler.get_decoration(ubo.id, spv::DecorationBinding);
      uint32_t spvDescSet = mslCompiler.get_decoration(ubo.id, spv::DecorationDescriptorSet);
      spirv_cross::MSLResourceBinding binding = {};
      binding.stage = executionModel;
      binding.desc_set = spvDescSet;
      binding.binding = spvBinding;
      binding.msl_buffer = spvBinding;
      mslCompiler.add_msl_resource_binding(binding);
    }

    auto sampledImages = mslCompiler.get_shader_resources().sampled_images;
    for (auto& image : sampledImages) {
      uint32_t spvBinding = mslCompiler.get_decoration(image.id, spv::DecorationBinding);
      uint32_t spvDescSet = mslCompiler.get_decoration(image.id, spv::DecorationDescriptorSet);
      spirv_cross::MSLResourceBinding binding = {};
      binding.stage = executionModel;
      binding.desc_set = spvDescSet;
      binding.binding = spvBinding;
      binding.msl_texture = spvBinding;
      binding.msl_sampler = spvBinding;
      mslCompiler.add_msl_resource_binding(binding);
    }

    // Note: subpass_inputs are NOT mapped here. When use_framebuffer_fetch_subpasses=true,
    // spirv-cross automatically converts subpassInput to [[color(N)]] in MSL without
    // needing explicit resource binding.

    return mslCompiler.compile();

  } catch (const spirv_cross::CompilerError& e) {
    *error = std::string("spirv-cross MSL error: ") + e.what();
  } catch (const std::exception& e) {
    *error = std::string("spirv-cross exception: ") + e.what();
  }
  return {};
}

std::string TranslateToHLSL(const std::vector<uint32_t>& spirv, ShaderStageType stage,
                            std::string* error) {
  // Same exception boundary as TranslateToMSL: spirv-cross reports through exceptions internally
  // and tgfx code does not use them.
  try {
    spirv_cross::Parser parser(spirv);
    parser.parse();
    spirv_cross::CompilerHLSL hlslCompiler(std::move(parser.get_parsed_ir()));

    spirv_cross::CompilerHLSL::Options hlslOptions;
    // Shader model 5.0 is what D3D12ShaderModule asks D3DCompile for (vs_5_0 / ps_5_0), and the
    // highest model that still produces DXBC. SM 5.1+ would need DXIL.
    hlslOptions.shader_model = 50;
    hlslCompiler.set_hlsl_options(hlslOptions);

    auto commonOptions = hlslCompiler.get_common_options();
    // The templates are Vulkan GLSL, whose clip space has +y pointing down. Direct3D, like Metal,
    // has +y pointing up, so the emitted vertex shader must negate it. The other backends keep the
    // axis untouched, which is why this lives here and not in the shared sources.
    commonOptions.vertex.flip_vert_y = true;
    hlslCompiler.set_common_options(commonOptions);

    auto executionModel = (stage == ShaderStageType::Vertex) ? spv::ExecutionModelVertex
                                                             : spv::ExecutionModelFragment;

    // Pin every register explicitly instead of letting spirv-cross assign them. The D3D12 root
    // signature is built from the same logical bindings (see D3D12RenderPipeline.h), so an
    // automatic assignment that happened to disagree would produce a pipeline whose CBV and
    // descriptor-table registers do not match the bytecode.
    auto resources = hlslCompiler.get_shader_resources();
    for (auto& ubo : resources.uniform_buffers) {
      uint32_t spvBinding = hlslCompiler.get_decoration(ubo.id, spv::DecorationBinding);
      uint32_t spvDescSet = hlslCompiler.get_decoration(ubo.id, spv::DecorationDescriptorSet);
      spirv_cross::HLSLResourceBinding binding = {};
      binding.stage = executionModel;
      binding.desc_set = spvDescSet;
      binding.binding = spvBinding;
      binding.cbv.register_binding = spvBinding;
      hlslCompiler.add_hlsl_resource_binding(binding);
    }

    for (auto& image : resources.sampled_images) {
      uint32_t spvBinding = hlslCompiler.get_decoration(image.id, spv::DecorationBinding);
      uint32_t spvDescSet = hlslCompiler.get_decoration(image.id, spv::DecorationDescriptorSet);
      spirv_cross::HLSLResourceBinding binding = {};
      binding.stage = executionModel;
      binding.desc_set = spvDescSet;
      binding.binding = spvBinding;
      // A combined image sampler becomes Texture2D at register(t{i}) plus a SamplerState at
      // register(s{i}). Both are pinned so the pipeline's separate SRV and Sampler descriptor
      // tables address the same index, which is what SamplerBinding(D3D12, i) == {i, i} states.
      binding.srv.register_binding = spvBinding;
      binding.sampler.register_binding = spvBinding;
      hlslCompiler.add_hlsl_resource_binding(binding);
    }

    return hlslCompiler.compile();

  } catch (const spirv_cross::CompilerError& e) {
    *error = std::string("spirv-cross HLSL error: ") + e.what();
  } catch (const std::exception& e) {
    *error = std::string("spirv-cross exception: ") + e.what();
  }
  return {};
}

#ifdef TGFX_SHADER_TOOL_HAS_TINT
static std::vector<uint32_t> compileSPIRVForWGSL(const std::string& source, ShaderStageType stage,
                                                 std::string* error) {
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetOptimizationLevel(shaderc_optimization_level_performance);
  options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
  auto shaderKind =
      (stage == ShaderStageType::Vertex) ? shaderc_vertex_shader : shaderc_fragment_shader;
  auto spvResult = compiler.CompileGlslToSpv(source, shaderKind, "shader", "main", options);
  if (spvResult.GetCompilationStatus() != shaderc_compilation_status_success) {
    *error = spvResult.GetErrorMessage();
    return {};
  }
  return {spvResult.cbegin(), spvResult.cend()};
}
#endif

std::string CompileGLSLToWGSL(const std::string& source, ShaderStageType stage,
                              const std::string& shaderName, uint32_t variantIndex,
                              std::string* error) {
  std::string errorPrefix = "[" + shaderName + " variant " + std::to_string(variantIndex) + "] ";
#ifdef TGFX_SHADER_TOOL_HAS_TINT
  std::string compileError;
  // Template shaders wrap conditional sampler declarations in #if blocks; the regex passes of
  // preprocessWebGPU must see only the live declarations, or the reassigned binding numbers
  // would leave holes against the reflection's sampler list.
  auto expandedSource = PreprocessGLSL(source, stage, &compileError);
  if (expandedSource.empty()) {
    *error = errorPrefix + "GLSL preprocess: " + compileError;
    return {};
  }
  auto vulkanGLSL = RewriteSamplerLayoutForWGSL(expandedSource);
  auto spirv = compileSPIRVForWGSL(vulkanGLSL, stage, &compileError);
  if (spirv.empty()) {
    *error = errorPrefix + "GLSL to SPIR-V: " + compileError;
    return {};
  }
  // The reader options mirror WebGPUShaderModule::compileShader so the AOT and JIT paths
  // produce equivalent WGSL from the same shader sources.
  tint::spirv::reader::Options readerOptions;
  readerOptions.allow_non_uniform_derivatives = true;
  auto program = tint::spirv::reader::Read(spirv, readerOptions);
  if (!program.IsValid()) {
    std::string diagnostics;
    for (const auto& diag : program.Diagnostics()) {
      diagnostics += " " + diag.message.Plain();
    }
    *error = errorPrefix + "tint SPIR-V reader:" + diagnostics;
    return {};
  }
  tint::wgsl::writer::Options writerOptions;
  auto wgslResult = tint::wgsl::writer::Generate(program, writerOptions);
  if (wgslResult != tint::Success) {
    *error = errorPrefix + "tint WGSL writer failed to generate WGSL";
    return {};
  }
  if (wgslResult->wgsl.empty()) {
    *error = errorPrefix + "tint generated empty WGSL code";
    return {};
  }
  return std::move(wgslResult->wgsl);
#else
  // Unreachable in practice: the CMake wiring defines TGFX_SHADER_TOOL_HAS_TINT exactly when
  // "webgpu" can appear in --backends. Kept explicit so a misconfigured build fails loudly
  // instead of silently emitting an empty WebGPU bundle.
  (void)source;
  (void)stage;
  *error =
      errorPrefix + "WebGPU translation is unavailable: build with -DTGFX_BUILD_WEBGPU_BUNDLE=ON";
  return {};
#endif
}

// The Metal toolchain invocation, built from the target SDK. Both the compile command and
// MetalCompilerFingerprint() read the same strings, so the bundle's source digest changes whenever
// the way metallibs are produced changes, including when the target SDK does. The defaults (the
// macOS SDK, no minimum OS flag) are exactly what the tool always used.
static std::string gMetalSdk = "macosx";
static std::string gMetalMinOS = "";

bool SetMetalTarget(const std::string& sdk, const std::string& minOS, std::string* error) {
  if (sdk != "macosx" && sdk != "iphoneos" && sdk != "iphonesimulator") {
    *error = "unknown Metal SDK \"" + sdk + "\" (expected macosx, iphoneos or iphonesimulator)";
    return false;
  }
  // The version ends up on a command line, so only digits and dots are accepted.
  bool validVersion =
      minOS.empty() || (minOS.size() <= 16 && minOS.front() != '.' && minOS.back() != '.' &&
                        minOS.find("..") == std::string::npos);
  for (char c : minOS) {
    validVersion = validVersion && ((c >= '0' && c <= '9') || c == '.');
  }
  if (!validVersion) {
    *error = "invalid minimum OS version \"" + minOS + "\" (expected digits and dots, e.g. 15.0)";
    return false;
  }
  gMetalSdk = sdk;
  gMetalMinOS = minOS;
  return true;
}

static std::string MetalCompileFlags() {
  std::string flags = "-sdk " + gMetalSdk +
                      " metal -std=" + (gMetalSdk == "macosx" ? "macos-metal2.3" : "ios-metal2.3") +
                      " -O2";
  if (!gMetalMinOS.empty()) {
    const char* option = gMetalSdk == "macosx"     ? "-mmacosx-version-min="
                         : gMetalSdk == "iphoneos" ? "-mios-version-min="
                                                   : "-mios-simulator-version-min=";
    flags += std::string(" ") + option + gMetalMinOS;
  }
  return flags;
}

static std::string MetalLinkCommand() {
  return "xcrun -sdk " + gMetalSdk + " metallib";
}

std::string MetalCompilerFingerprint() {
  const char* extraFlags = std::getenv("TGFX_METAL_EXTRA_FLAGS");
  return "compile=xcrun " + MetalCompileFlags() + ";link=" + MetalLinkCommand() +
         ";extra=" + (extraFlags != nullptr ? extraFlags : "");
}

#if !defined(_WIN32)
namespace {

std::vector<std::string> SplitOnSpaces(const std::string& text) {
  std::vector<std::string> words;
  std::string word;
  for (char c : text) {
    if (c == ' ' || c == '\t' || c == '\n') {
      if (!word.empty()) {
        words.push_back(word);
        word.clear();
      }
    } else {
      word += c;
    }
  }
  if (!word.empty()) {
    words.push_back(word);
  }
  return words;
}

// A directory private to this process, created on first use and removed at exit. Every scratch
// file gets a name from a process-wide counter, so concurrent compiles in this process and other
// shader_build_tool processes (two build directories generating at once) never share a path.
class ScratchDir {
 public:
  static ScratchDir& Get() {
    static ScratchDir dir;
    return dir;
  }

  ~ScratchDir() {
    if (!root.empty()) {
      rmdir(root.c_str());
    }
  }

  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;

  // Returns "" when the directory could not be created.
  std::string newFile(const std::string& extension) {
    if (root.empty()) {
      return "";
    }
    return root + "/s" + std::to_string(counter.fetch_add(1)) + extension;
  }

  const std::string& error() const {
    return createError;
  }

 private:
  ScratchDir() {
    const char* base = std::getenv("TMPDIR");
    std::string parent = base != nullptr && base[0] != '\0' ? base : "/tmp";
    while (parent.size() > 1 && parent.back() == '/') {
      parent.pop_back();
    }
    std::string pattern = parent + "/tgfx_shader_build.XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    if (mkdtemp(buffer.data()) != nullptr) {
      root = buffer.data();
    } else {
      createError = "cannot create a scratch directory under " + parent + ": " + strerror(errno);
    }
  }

  std::string root = {};
  std::string createError = {};
  std::atomic<uint64_t> counter = {0};
};

std::string ReadTextFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  return text;
}

// Runs args[0] (looked up on PATH) with the given arguments, without a shell, writing its stderr
// (and its stdout too when captureStdout is set) to outputPath. Returns the exit status, 128 +
// signal when it was killed, or -1 with *error set when it could not be started.
int RunProcess(const std::vector<std::string>& args, const std::string& outputPath,
               std::string* error, bool captureStdout = false) {
  std::vector<char*> argv;
  for (const auto& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, outputPath.c_str(),
                                   O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (captureStdout) {
    posix_spawn_file_actions_adddup2(&actions, STDERR_FILENO, STDOUT_FILENO);
  }
  pid_t pid = 0;
  int spawnResult = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  if (spawnResult != 0) {
    *error = "cannot start " + args[0] + ": " + strerror(spawnResult);
    return -1;
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      *error = "cannot wait for " + args[0] + ": " + strerror(errno);
      return -1;
    }
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1;
}

// Runs one step of the Metal toolchain. On failure *error names the step, the exit status and the
// tail of what the tool printed to stderr.
bool RunMetalStep(const char* step, const std::vector<std::string>& args,
                  const std::string& stderrPath, std::string* error) {
  std::string spawnError;
  int status = RunProcess(args, stderrPath, &spawnError);
  auto output = ReadTextFile(stderrPath);
  std::remove(stderrPath.c_str());
  if (status == 0) {
    return true;
  }
  constexpr size_t MaxOutput = 4000;
  if (output.size() > MaxOutput) {
    output = "..." + output.substr(output.size() - MaxOutput);
  }
  *error = std::string(step) + " failed" +
           (status < 0 ? ": " + spawnError : " with exit status " + std::to_string(status)) +
           (output.empty() ? "" : "\n" + output);
  return false;
}

}  // namespace
#endif

std::string MetalToolchainVersion() {
#if defined(_WIN32)
  return "";
#else
  auto& scratch = ScratchDir::Get();
  std::string version;
  const std::vector<std::vector<std::string> > queries = {
      {"xcrun", "-sdk", gMetalSdk, "metal", "--version"},
      {"xcrun", "-sdk", gMetalSdk, "--show-sdk-version"},
      {"xcrun", "-sdk", gMetalSdk, "--show-sdk-build-version"}};
  for (const auto& query : queries) {
    auto outputPath = scratch.newFile(".txt");
    if (outputPath.empty()) {
      return "";
    }
    std::string error;
    int status = RunProcess(query, outputPath, &error, true);
    auto output = ReadTextFile(outputPath);
    std::remove(outputPath.c_str());
    if (status != 0 || output.empty()) {
      return "";
    }
    version += output;
  }
  return version;
#endif
}

// --- Direct3D HLSL to DXBC ---------------------------------------------------------------
//
// A d3d12 bundle holds either the HLSL text (the backend's D3DCompile at module creation) or the
// DXBC bytecode compiled here. The offline form removes the per-process compile from the first
// frame; it is produced with the same compiler and flags the backend uses, so the two paths differ
// only in when the work happens. Invoked in-process rather than by spawning fxc: no command line to
// quote, no SDK path to discover, and the bytecode is exactly what the runtime would have made.
static bool gD3D12UseDxbc = false;

bool SetD3D12ShaderFormat(const std::string& format, std::string* error) {
  // The flag is accepted on every host, including ones that cannot produce DXBC: the digest and
  // the verifier both need to state which form a bundle holds, and neither compiles anything. The
  // availability check happens where the bytecode is actually produced (CompileHLSLToDXBC), so a
  // host without D3DCompiler still fails the build that genuinely asked for bytecode — it just
  // does not fail every unrelated build of every other profile.
  if (format == "dxbc") {
    gD3D12UseDxbc = true;
    return true;
  }
  if (format == "hlsl") {
    gD3D12UseDxbc = false;
    return true;
  }
  *error = "unknown D3D12 shader format \"" + format + "\" (expected dxbc or hlsl)";
  return false;
}

bool D3D12UsesDxbc() {
  return gD3D12UseDxbc;
}

std::string HLSLCompilerFingerprint() {
  // States the chosen payload form, so flipping TGFX_D3D12_SHADER_FORMAT invalidates the source
  // digest and the bundle is rebuilt instead of being reused with the other form's bytes.
  return std::string("format=") + (gD3D12UseDxbc ? "dxbc" : "hlsl") +
         ";compile=D3DCompile strictness|optimization_level3 entry=main";
}

#if defined(TGFX_SHADER_TOOL_HAS_D3DCOMPILER)
// Only the DXBC path below consumes the profile string, so the helper rides the same
// availability macro — without it every non-Windows host build would flag it unused.
static const char* HlslStageProfile(ShaderStageType stage) {
  return stage == ShaderStageType::Vertex ? "vs_5_0" : "ps_5_0";
}
#endif

std::vector<uint8_t> CompileHLSLToDXBC(const std::string& hlslSource, ShaderStageType stage,
                                       std::string* error) {
  std::string localError;
  if (error == nullptr) {
    error = &localError;
  }
  const char* stageName = stage == ShaderStageType::Vertex ? "vertex" : "fragment";
#if defined(TGFX_SHADER_TOOL_HAS_D3DCOMPILER)
  // The flags are fixed, never DEBUG: the bytecode ships in a bundle, so the tool's own build
  // configuration must not change it. D3DCOMPILE_DEBUG would embed debug info and make the blob
  // differ between the tool's Debug and Release builds.
  UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
  ID3DBlob* codeBlob = nullptr;
  ID3DBlob* errorBlob = nullptr;
  auto hr = D3DCompile(hlslSource.data(), hlslSource.size(), nullptr, nullptr, nullptr, "main",
                       HlslStageProfile(stage), flags, 0, &codeBlob, &errorBlob);
  if (FAILED(hr)) {
    char code[16] = {};
    std::snprintf(code, sizeof(code), "0x%08X", static_cast<unsigned>(hr));
    std::string message = std::string("D3DCompile failed (HRESULT=") + code + ")";
    if (errorBlob != nullptr) {
      message += ": ";
      message.append(static_cast<const char*>(errorBlob->GetBufferPointer()),
                     errorBlob->GetBufferSize());
      errorBlob->Release();
    }
    *error = std::string("compiling the ") + stageName + " stage: " + message;
    if (codeBlob != nullptr) {
      codeBlob->Release();
    }
    return {};
  }
  if (errorBlob != nullptr) {
    errorBlob->Release();
  }
  const auto* bytes = static_cast<const uint8_t*>(codeBlob->GetBufferPointer());
  std::vector<uint8_t> data(bytes, bytes + codeBlob->GetBufferSize());
  codeBlob->Release();
  if (data.empty()) {
    *error = std::string("the ") + stageName + " DXBC blob is empty";
  }
  return data;
#else
  (void)hlslSource;
  *error = std::string("cannot compile the ") + stageName +
           " stage to DXBC: this build of shader_build_tool has no D3DCompiler";
  return {};
#endif
}

std::vector<uint8_t> CompileMSLToMetallib(const std::string& mslSource, ShaderStageType stage,
                                          std::string* error) {
  std::string localError;
  if (error == nullptr) {
    error = &localError;
  }
  const char* stageName = stage == ShaderStageType::Vertex ? "vertex" : "fragment";
#if defined(_WIN32)
  *error = std::string("cannot compile the ") + stageName +
           " stage: the Metal toolchain (xcrun) is only available on macOS";
  return {};
#else
  // xcrun metal compiles MSL to AIR, then xcrun metallib links the AIR into a .metallib. Both run
  // without a shell, on files in this process's scratch directory.
  auto& scratch = ScratchDir::Get();
  auto mslPath = scratch.newFile(".metal");
  if (mslPath.empty()) {
    *error = scratch.error();
    return {};
  }
  auto airPath = scratch.newFile(".air");
  auto libPath = scratch.newFile(".metallib");
  auto logPath = scratch.newFile(".log");
  {
    std::ofstream file(mslPath, std::ios::binary);
    file.write(mslSource.data(), static_cast<std::streamsize>(mslSource.size()));
    if (!file.good()) {
      *error = "cannot write " + mslPath;
      std::remove(mslPath.c_str());
      return {};
    }
  }

  // TGFX_METAL_EXTRA_FLAGS (experiment hook): extra xcrun metal arguments, split on whitespace.
  // Used by the FMA-attribution experiments (fast-math/contraction/optimization-level controls)
  // to rebuild the bundle under different compilation settings without touching the default
  // pipeline. Unset in production builds.
  std::vector<std::string> compile = {"xcrun"};
  for (const auto& word : SplitOnSpaces(MetalCompileFlags())) {
    compile.push_back(word);
  }
  if (const char* extraFlags = std::getenv("TGFX_METAL_EXTRA_FLAGS")) {
    for (const auto& word : SplitOnSpaces(extraFlags)) {
      compile.push_back(word);
    }
  }
  compile.insert(compile.end(), {"-c", mslPath, "-o", airPath});
  bool compiled = RunMetalStep("xcrun metal", compile, logPath, error);
  std::remove(mslPath.c_str());
  if (!compiled) {
    std::remove(airPath.c_str());
    *error = std::string("compiling the ") + stageName + " stage: " + *error;
    return {};
  }

  auto link = SplitOnSpaces(MetalLinkCommand());
  link.insert(link.end(), {airPath, "-o", libPath});
  bool linked = RunMetalStep("xcrun metallib", link, logPath, error);
  std::remove(airPath.c_str());
  if (!linked) {
    std::remove(libPath.c_str());
    *error = std::string("linking the ") + stageName + " stage: " + *error;
    return {};
  }

  std::ifstream libFile(libPath, std::ios::binary);
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(libFile)),
                            std::istreambuf_iterator<char>());
  libFile.close();
  std::remove(libPath.c_str());
  if (data.empty()) {
    *error = std::string("the ") + stageName + " metallib is empty or unreadable";
  }
  return data;
#endif
}

}  // namespace tgfx
