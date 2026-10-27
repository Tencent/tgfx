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

#include "ContractChecks.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <utility>
#include "core/DiagnosticCodes.h"
#include "gpu/AOTResourceProfile.h"
#include "gpu/BundleFormat.h"
#include "gpu/shaders/KernelContract.h"

namespace tgfx {

namespace {

// Reports one violation of a contract check. Every check below records through this helper, so
// the code, the subject and the severity stay in one shape for the cli renderer and the tests.
void Report(Diagnostics* diagnostics, Severity severity, const char* code,
            const std::string& shader, const std::string& profile, int vert, int frag,
            std::string message) {
  Diagnostic diagnostic;
  diagnostic.severity = severity;
  diagnostic.layer = Layer::Verify;
  diagnostic.code = code;
  diagnostic.shader = shader;
  diagnostic.profile = profile;
  diagnostic.vertPermutationIndex = vert;
  diagnostic.fragPermutationIndex = frag;
  diagnostic.message = std::move(message);
  diagnostics->report(std::move(diagnostic));
}

bool HasUniform(const StageReflectionData& reflection, const std::string& name) {
  return std::any_of(reflection.uniforms.begin(), reflection.uniforms.end(),
                     [&name](const UniformEntry& entry) { return entry.name == name; });
}

}  // namespace

// Every shader whose fragment block carries the clip contract marker (HasClip) must declare the
// whole contract: a missing field means the onSetData writers (which reference the same
// ClipContract constants) would silently skip their upload and the clip would read stale data.
size_t ValidateClipContractFields(const std::vector<VariantData>& variants,
                                  Diagnostics* diagnostics) {
  const std::string contractFields[] = {
      ClipContract::Rect,          ClipContract::HasClip, ClipContract::LocalRect,
      ClipContract::RadiiX,        ClipContract::RadiiY,  ClipContract::AntiAlias,
      ClipContract::DeviceToLocal,
  };
  size_t errors = 0;
  for (const auto& variant : variants) {
    if (!HasUniform(variant.fragmentReflection, ClipContract::HasClip)) {
      continue;
    }
    for (const auto& field : contractFields) {
      if (!HasUniform(variant.fragmentReflection, field)) {
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifyClipField, variant.shaderName,
               variant.profileTag, -1, static_cast<int>(variant.fragPermutationIndex),
               "carries the clip contract but is missing uniform '" + field + "'");
        ++errors;
      }
    }
  }
  return errors;
}

// Parses every "#define OP_<NAME> <decimal>" line of an include into a name->value map. The value
// must be a bare decimal integer: stoi alone accepts prefixes like "0 + 1", which would silently
// pass a malformed define. Unparseable or non-standalone values are reported as errors.
// Line comments are stripped first: a "// #define OP_X ..." mention in a comment otherwise
// matches the scan and its value would overwrite the real define's (the parser keeps the LAST
// occurrence), so a comment could mask a genuine opcode mismatch.
static std::map<std::string, int> ParseOpDefines(const std::string& source, const char* fileName,
                                                 size_t* errors, Diagnostics* diagnostics) {
  std::map<std::string, int> defines;
  std::string stripped;
  stripped.reserve(source.size());
  size_t scan = 0;
  while (scan < source.size()) {
    auto lineEnd = source.find('\n', scan);
    auto line =
        source.substr(scan, (lineEnd == std::string::npos ? source.size() : lineEnd) - scan);
    auto comment = line.find("//");
    if (comment != std::string::npos) {
      line = line.substr(0, comment);
    }
    stripped += line;
    stripped += '\n';
    scan = lineEnd == std::string::npos ? source.size() : lineEnd + 1;
  }
  size_t scanPosition = 0;
  while (true) {
    auto defineStart = stripped.find("#define OP_", scanPosition);
    if (defineStart == std::string::npos) {
      break;
    }
    auto nameStart = defineStart + 8;  // past "#define "
    auto nameEnd = stripped.find(' ', nameStart);
    if (nameEnd == std::string::npos) {
      break;
    }
    auto defineName = stripped.substr(nameStart, nameEnd - nameStart);
    auto valueStart = stripped.find_first_not_of(" \t", nameEnd);
    auto valueEnd = stripped.find('\n', valueStart);
    auto valueText = stripped.substr(valueStart, valueEnd - valueStart);
    while (!valueText.empty() &&
           (valueText.back() == ' ' || valueText.back() == '\t' || valueText.back() == '\r')) {
      valueText.pop_back();
    }
    bool allDigits = !valueText.empty();
    for (char ch : valueText) {
      if (ch < '0' || ch > '9') {
        allDigits = false;
        break;
      }
    }
    if (!allDigits) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyOpCode, "", "", -1, -1,
             "cannot parse " + defineName + " value '" + valueText + "' in " + fileName +
                 " (expected a bare decimal integer)");
      ++*errors;
    } else {
      defines[defineName] = std::stoi(valueText);
    }
    scanPosition = nameEnd;
  }
  return defines;
}

// Validates one OP_* include against an expected name->value set: every expected define must
// exist with the exact value, and no other OP_* define may appear (a stray define means the
// kernel grew an opcode the C++ side has not anchored yet, which would surface as
// misinterpretation at runtime since the evaluation switches have no default branch).
static size_t ValidateOpDefines(const std::string& shaderDir, const char* fileName,
                                const std::map<std::string, int>& expected,
                                Diagnostics* diagnostics) {
  std::ifstream file(shaderDir + "/level1/" + fileName);
  if (!file.is_open()) {
    // The include itself is gone: nothing can be compared, which is a failed check, not a
    // passing one.
    Report(diagnostics, Severity::Unverifiable, DiagnosticCode::VerifyNotApplicable, "", "", -1,
           -1, std::string("cannot open ") + fileName + " under " + shaderDir);
    return 1;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  size_t errors = 0;
  auto defines = ParseOpDefines(buffer.str(), fileName, &errors, diagnostics);
  for (const auto& [name, value] : expected) {
    auto it = defines.find(name);
    if (it == defines.end()) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyOpCode, "", "", -1, -1,
             "OP define missing in " + std::string(fileName) + ": " + name);
      ++errors;
    } else if (it->second != value) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyOpCode, "", "", -1, -1,
             name + " is " + std::to_string(it->second) + " but the C++ contract says " +
                 std::to_string(value));
      ++errors;
    }
  }
  for (const auto& [name, value] : defines) {
    if (expected.find(name) == expected.end()) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyOpCode, "", "", -1, -1,
             "unknown OP define in " + std::string(fileName) + ": " + name +
                 " (anchor it in the C++ contract or remove the define)");
      ++errors;
    }
  }
  return errors;
}

// The chain slot op codes ride the Packed.x selector, so the GLSL OP_* defines and the C++
// AOTChainOp enum (anchored to the ChainOp constants) must agree value-for-value.
size_t ValidateChainOpCodes(const std::string& shaderDir, Diagnostics* diagnostics) {
  return ValidateOpDefines(shaderDir, "pointwise_chain_ops.inc",
                           {{"OP_COLOR_MATRIX", ChainOp::ColorMatrix},
                            {"OP_LUMA", ChainOp::Luma},
                            {"OP_ALPHA_THRESHOLD", ChainOp::AlphaThreshold},
                            {"OP_COLOR_SPACE_XFORM", ChainOp::ColorSpaceXform},
                            {"OP_NONE", ChainOp::None},
                            {"OP_TEXTURE", ChainOp::Texture},
                            {"OP_CONST_COLOR", ChainOp::ConstColor},
                            {"OP_BLEND", ChainOp::Blend},
                            {"OP_AARECT_COVERAGE", ChainOp::AARectCoverage},
                            {"OP_GRADIENT", ChainOp::Gradient},
                            {"OP_LOCAL_RECT_COVERAGE", ChainOp::LocalRectCoverage},
                            {"OP_RRECT_COVERAGE", ChainOp::RRectCoverage},
                            {"OP_TEX_MODULATE", ChainOp::TexModulate},
                            {"OP_INPUT_OPAQUE", ChainOp::InputOpaque},
                            {"OP_MUL_ALPHA", ChainOp::MulAlpha},
                            {"OP_PERLIN_NOISE", ChainOp::PerlinNoise}},
                           diagnostics);
}

// The plain pointwise slot selects its operator the same way, so pointwise_op.inc must agree
// with the AOTPointwiseOpType enum (anchored to the PointwiseOp constants). Chain-only operators
// (Texture and the coverage/modulator ops) must not appear here.
size_t ValidatePointwiseOpCodes(const std::string& shaderDir, Diagnostics* diagnostics) {
  return ValidateOpDefines(shaderDir, "pointwise_op.inc",
                           {{"OP_COLOR_MATRIX", PointwiseOp::ColorMatrix},
                            {"OP_LUMA", PointwiseOp::Luma},
                            {"OP_ALPHA_THRESHOLD", PointwiseOp::AlphaThreshold},
                            {"OP_COLOR_SPACE_XFORM", PointwiseOp::ColorSpaceXform},
                            {"OP_NONE", PointwiseOp::None},
                            {"OP_CONST_COLOR", PointwiseOp::ConstColor},
                            {"OP_BLEND", PointwiseOp::Blend}},
                           diagnostics);
}

// The GLSL kernels carry their slot capacities as literals behind #ifndef / #if guards while the
// runtime uploads uniform arrays sized by C++ constants, so a drifted literal reads past (or
// wastes) what the runtime wrote. Parsing the literal at build time and comparing it to the
// constant keeps the two in lockstep without injecting extra defines into every variant.
size_t ValidateSlotCapacities(const std::string& shaderDir, Diagnostics* diagnostics) {
  size_t errors = 0;
  const std::regex slotSuffixPattern("#define\\s+TGFX_SLOT_ARRAY_SUFFIX\\s*\\[\\s*(\\d+)\\s*\\]");
  const char* slotKernels[] = {"perlin_noise.frag", "yuv_texture_fill.frag"};
  for (const char* fileName : slotKernels) {
    std::ifstream file(shaderDir + "/level1/" + fileName);
    if (!file.is_open()) {
      Report(diagnostics, Severity::Unverifiable, DiagnosticCode::VerifyNotApplicable, "", "", -1,
             -1, std::string("cannot open ") + fileName + " under " + shaderDir);
      ++errors;
      continue;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    std::smatch match;
    if (!std::regex_search(text, match, slotSuffixPattern)) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifySlotCapacity, "", "", -1, -1,
             std::string(fileName) + " declares no TGFX_SLOT_ARRAY_SUFFIX");
      ++errors;
    } else if (std::stoul(match[1].str()) != MaxPointwiseSlots) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifySlotCapacity, "", "", -1, -1,
             std::string(fileName) + " slot capacity " + match[1].str() +
                 " does not match MaxPointwiseSlots (" + std::to_string(MaxPointwiseSlots) + ")");
      ++errors;
    }
  }
  std::ifstream chainFile(shaderDir + "/level1/pointwise_chain.frag");
  if (!chainFile.is_open()) {
    Report(diagnostics, Severity::Unverifiable, DiagnosticCode::VerifyNotApplicable, "", "", -1,
           -1, std::string("cannot open pointwise_chain.frag under ") + shaderDir);
    return errors + 1;
  }
  std::stringstream chainBuffer;
  chainBuffer << chainFile.rdbuf();
  const std::string chainText = chainBuffer.str();
  const std::regex capacityPattern("#define\\s+CHAIN_SLOT_CAPACITY\\s+(\\d+)");
  std::set<size_t> values;
  for (std::sregex_iterator it(chainText.begin(), chainText.end(), capacityPattern);
       it != std::sregex_iterator(); ++it) {
    values.insert(std::stoul((*it)[1].str()));
  }
  if (values.empty()) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifySlotCapacity, "", "", -1, -1,
           "pointwise_chain.frag declares no CHAIN_SLOT_CAPACITY");
    ++errors;
  }
  const size_t plainCapacity = ChainResourceCapacity.instructions;
  const size_t tiledCapacity = TiledGraphResourceCapacity.instructions;
  const size_t noiseCapacity = NoiseGraphResourceCapacity.instructions;
  // The chain shader selects the capacity with a two-branch #if on RESOURCE_KIND, so the plain
  // and tiled-graph capacities must share one value; a third distinct capacity needs a new branch
  // and would fail here on purpose.
  if (tiledCapacity != plainCapacity) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifySlotCapacity, "", "", -1, -1,
           "TiledGraph capacity " + std::to_string(tiledCapacity) +
               " differs from the plain chain capacity " + std::to_string(plainCapacity) +
               ", which the shader's two-branch capacity #if cannot express");
    ++errors;
  }
  if (values.count(plainCapacity) == 0 || values.count(noiseCapacity) == 0 ||
      values.size() != (plainCapacity == noiseCapacity ? 1 : 2)) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifySlotCapacity, "", "", -1, -1,
           "pointwise_chain.frag capacities do not match the C++ resource capacities (expected " +
               std::to_string(plainCapacity) + " and " + std::to_string(noiseCapacity) + ")");
    ++errors;
  }
  return errors;
}

static bool IsIdentifierChar(char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
         ch == '_';
}

static bool EndsWith(const std::string& text, const std::string& suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Names that end in "Shader" but are not tgfx shader families.
static bool IsExternalShaderName(const std::string& name) {
  return name == "SwiftShader";
}

size_t ValidateShaderSourceReferences(const std::string& shaderDir,
                                      const std::vector<std::string>& registeredShaders,
                                      Diagnostics* diagnostics) {
  namespace fs = std::filesystem;
  std::error_code code;
  std::set<std::string> existingFiles;
  std::vector<fs::path> sourcePaths;
  for (fs::recursive_directory_iterator it(shaderDir, code), end; !code && it != end;
       it.increment(code)) {
    if (!it->is_regular_file()) {
      continue;
    }
    existingFiles.insert(it->path().filename().string());
    auto extension = it->path().extension().string();
    if (extension == ".inc" || extension == ".frag" || extension == ".vert") {
      sourcePaths.push_back(it->path());
    }
  }
  if (code || sourcePaths.empty()) {
    Report(diagnostics, Severity::Unverifiable, DiagnosticCode::VerifyNotApplicable, "", "", -1,
           -1, "cannot list shader sources under " + shaderDir);
    return 1;
  }
  const std::set<std::string> registered(registeredShaders.begin(), registeredShaders.end());
  const std::string fileSuffixes[] = {".inc", ".frag", ".vert"};
  size_t errors = 0;
  for (const auto& path : sourcePaths) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    const auto text = buffer.str();
    const auto fileName = path.filename().string();
    size_t position = 0;
    while (position < text.size()) {
      if (!IsIdentifierChar(text[position])) {
        ++position;
        continue;
      }
      size_t tokenEnd = position;
      while (tokenEnd < text.size() && IsIdentifierChar(text[tokenEnd])) {
        ++tokenEnd;
      }
      const auto token = text.substr(position, tokenEnd - position);
      // A "<module>_uniforms.inc" or "pointwise_*.inc" mention is a placeholder, not a file name.
      const bool placeholder =
          position > 0 && (text[position - 1] == '>' || text[position - 1] == '*');
      for (const auto& suffix : fileSuffixes) {
        if (!placeholder && text.compare(tokenEnd, suffix.size(), suffix) == 0 &&
            (tokenEnd + suffix.size() >= text.size() ||
             !IsIdentifierChar(text[tokenEnd + suffix.size()])) &&
            existingFiles.count(token + suffix) == 0) {
          Report(diagnostics, Severity::Error, DiagnosticCode::VerifySourceFile, "", "", -1, -1,
                 fileName + " refers to " + token + suffix +
                     ", which does not exist under " + shaderDir);
          ++errors;
        }
      }
      if (EndsWith(token, "Shader") && token.size() > 6 && token[0] >= 'A' && token[0] <= 'Z' &&
          !IsExternalShaderName(token) && registered.count(token) == 0) {
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifySourceFamily, "", "", -1, -1,
               fileName + " refers to shader family " + token +
                   ", which is not registered (removed or merged into another family?)");
        ++errors;
      }
      position = tokenEnd;
    }
  }
  return errors;
}

// Reflection-level invariants the runtime loader enforces on read: catching them at build time
// keeps a broken writer from shipping bundles that the loader would reject (or worse, accept with
// gaps in the sampler binding table).
size_t ValidateReflectionContracts(const std::vector<VariantData>& variants,
                                   Diagnostics* diagnostics) {
  size_t errors = 0;
  const auto maxEntries = BundleFormat::Reflection::MaxListEntries;
  const auto maxNameLength = BundleFormat::Reflection::MaxNameLength;
  for (const auto& variant : variants) {
    const StageReflectionData* stages[] = {&variant.vertexReflection, &variant.fragmentReflection};
    for (const auto* reflection : stages) {
      // The bundle writer serializes name lengths and per-stage entry counts as uint8
      // (BundleWriter's WriteU8 sites): values over the format's byte would wrap silently (a
      // 256-byte name is written with length 0), so reject them here at build time instead.
      if (reflection->uniforms.size() > maxEntries || reflection->samplers.size() > maxEntries) {
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection, variant.shaderName,
               variant.profileTag, -1, -1,
               "reflection exceeds the " + std::to_string(maxEntries) +
                   "-entry serialization limit (uniforms=" +
                   std::to_string(reflection->uniforms.size()) +
                   ", samplers=" + std::to_string(reflection->samplers.size()) + ")");
        ++errors;
      }
      std::map<std::string, int> seenNames;
      for (const auto* list : {&reflection->uniforms, &reflection->samplers}) {
        bool isSamplerList = list == &reflection->samplers;
        for (const auto& entry : *list) {
          if (entry.name.find('\0') != std::string::npos) {
            Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection,
                   variant.shaderName, variant.profileTag, -1, -1,
                   "reflection carries an embedded NUL in a name");
            ++errors;
            continue;
          }
          if (entry.name.size() > maxNameLength) {
            Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection,
                   variant.shaderName, variant.profileTag, -1, -1,
                   "entry '" + entry.name + "' exceeds the " + std::to_string(maxNameLength) +
                       "-byte name serialization limit");
            ++errors;
            continue;
          }
          if (++seenNames[entry.name] > 1) {
            Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection,
                   variant.shaderName, variant.profileTag, -1, -1,
                   "declares duplicate name '" + entry.name + "' in one stage");
            ++errors;
          }
          if (entry.arraySize < 1 || (isSamplerList && entry.arraySize != 1)) {
            Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection,
                   variant.shaderName, variant.profileTag, -1, -1,
                   "entry '" + entry.name + "' has arraySize " +
                       std::to_string(entry.arraySize) +
                       " (uniforms need >= 1, samplers need exactly 1)");
            ++errors;
          }
        }
      }
      // Ordinary texture bindings must use the generic TextureSampler_<N> pattern with N dense
      // from zero (a gap would shift every later binding on the runtime side); dedicated-purpose
      // samplers use their registered semantic names instead (SamplerContract in KernelContract.h).
      int nextSamplerIndex = 0;
      for (const auto& sampler : reflection->samplers) {
        const std::string prefix = SamplerContract::TextureSamplerPrefix;
        bool generic = sampler.name.size() > prefix.size() &&
                       sampler.name.compare(0, prefix.size(), prefix) == 0;
        if (generic) {
          auto digits = sampler.name.substr(prefix.size());
          generic = !digits.empty() && std::all_of(digits.begin(), digits.end(),
                                                   [](char ch) { return ch >= '0' && ch <= '9'; });
        }
        if (SamplerContract::IsSemanticSamplerName(sampler.name)) {
          continue;
        }
        int parsed = -1;
        if (generic) {
          parsed = std::stoi(sampler.name.substr(prefix.size()));
        }
        if (!generic) {
          Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection,
                 variant.shaderName, variant.profileTag, -1, -1,
                 "declares sampler '" + sampler.name +
                     "' (neither the TextureSampler_<N> pattern nor a registered semantic name; "
                     "see SamplerContract in KernelContract.h)");
          ++errors;
        } else if (parsed != nextSamplerIndex) {
          Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection,
                 variant.shaderName, variant.profileTag, -1, -1,
                 "generic sampler #" + std::to_string(nextSamplerIndex) + " is named '" +
                     sampler.name + "' (indices must be dense from zero)");
          ++errors;
        }
        ++nextSamplerIndex;
      }
    }
  }
  return errors;
}

}  // namespace tgfx
