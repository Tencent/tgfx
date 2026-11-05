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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "UniformABCompare.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "gpu/GlobalCache.h"
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gtest/gtest.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/PixelFormat.h"
#include "utils/ProjectPath.h"
#include "utils/TestUtils.h"

namespace tgfx {
namespace {

// Both routes dump under one label; the harness toggles the variable around each route within one
// process, and ProgramWriter reads it per call (not cached) for exactly that reason.
void SetDumpLabel(const char* label) {
#if defined(_WIN32)
  _putenv_s("TGFX_UNIF_DUMP", label != nullptr ? label : "");
#else
  if (label != nullptr) {
    setenv("TGFX_UNIF_DUMP", label, 1);
  } else {
    unsetenv("TGFX_UNIF_DUMP");
  }
#endif
}

std::vector<std::string> SplitLines(const std::string& text) {
  std::vector<std::string> lines;
  size_t pos = 0;
  while (pos < text.size()) {
    auto end = text.find('\n', pos);
    if (end == std::string::npos) {
      lines.push_back(text.substr(pos));
      break;
    }
    lines.push_back(text.substr(pos, end - pos));
    pos = end + 1;
  }
  return lines;
}

// A sampler line's texture-format number, rewritten to the channel-family semantic it denotes.
// Backends pick their own native channel order when uploading the same image (the golden's author
// uploaded BGRA where D3D12 uploads RGBA), so the raw PixelFormat value is backend noise the
// pinned trace must not carry: what the trace protects is "this slot bound a texture of such
// size and such channel family", not "the driver happens to prefer this byte order". A format
// outside the known families keeps its number, so an unexpected upload is still a visible drift.
std::string NormalizeSamplerLine(const std::string& line) {
  static const std::pair<int, const char*> families[] = {
      {static_cast<int>(PixelFormat::RGBA_8888), "rgba8"},
      {static_cast<int>(PixelFormat::BGRA_8888), "rgba8"},
      {static_cast<int>(PixelFormat::ALPHA_8), "alpha8"},
      {static_cast<int>(PixelFormat::GRAY_8), "alpha8"},
  };
  auto pos = line.find("format=");
  if (pos == std::string::npos) {
    return line;
  }
  auto numberStart = pos + strlen("format=");
  auto numberEnd = line.find_first_not_of("0123456789", numberStart);
  if (numberEnd == std::string::npos) {
    numberEnd = line.size();
  }
  auto number = atoi(line.c_str() + numberStart);
  for (const auto& family : families) {
    if (family.first == number) {
      return line.substr(0, numberStart) + family.second + line.substr(numberEnd);
    }
  }
  return line;
}

// One route's dump, normalized to the pinned form: a dump header (it carries the program key,
// whose ClassIDs are link-order noise, plus the writer's route/suffix tags) becomes a positional
// [DRAW] marker, a sampler-slot line keeps everything but its pixel-format number, and a value
// line keeps its field name and values in offset order. A draw one route drops shows up as a
// shifted sequence instead of an invisible hole; everything else the run prints (logs) is dropped.
std::string NormalizeTrace(const std::string& raw) {
  std::string out;
  for (const auto& line : SplitLines(raw)) {
    if (line.rfind("[UNIF ", 0) == 0) {
      if (line.find("sampler slot=") != std::string::npos) {
        out += NormalizeSamplerLine(line) + '\n';
      } else {
        // Keep the writer context (route, the structural/suffix tags, the field count), drop only
        // the label — it embeds the program key, whose ClassIDs are link-order noise.
        auto routePos = line.find("route=");
        out += routePos != std::string::npos ? "[DRAW] " + line.substr(routePos) + '\n'
                                             : std::string("[DRAW]\n");
      }
      continue;
    }
    if (line.find(" = [") != std::string::npos) {
      out += line + '\n';
    }
  }
  return out;
}

// The route's full trace: the program cache is cleared so the capture pays the first-prepare
// path, and the capture spans the scene and the flush that submits it. Returns an empty string
// when the route produced no dump — the scene drew nothing, or the bundle is gone.
std::string RunRoute(Context* context, const std::function<void()>& draw) {
  auto* cache = context->precompiledShaderCache();
  if (!LoadEmbeddedBundle(cache, context->backend())) {
    return {};
  }
  context->globalCache()->clearPrograms();
  testing::internal::CaptureStdout();
  draw();
  context->flushAndSubmit(true);
  auto captured = testing::internal::GetCapturedStdout();
  return NormalizeTrace(captured);
}

std::string GoldenPath(const std::string& name) {
  return ProjectPath::Absolute("test/baseline/uniform/" + name + ".txt");
}

// The first line where the golden and the captured trace disagree, with context on both sides, so
// a drift is diagnosable from the failure message without re-running the dump by hand.
std::string FirstDivergence(const std::string& expected, const std::string& actual) {
  auto gold = SplitLines(expected);
  auto cap = SplitLines(actual);
  size_t index = 0;
  while (index < gold.size() && index < cap.size() && gold[index] == cap[index]) {
    ++index;
  }
  auto window = [&index](const std::vector<std::string>& lines) {
    size_t begin = index > 2 ? index - 2 : 0;
    size_t end = std::min(index + 3, lines.size());
    std::string out;
    for (size_t i = begin; i < end; ++i) {
      out += (i == index ? "> " : "  ") + lines[i] + '\n';
    }
    return out;
  };
  return "first divergence at line " + std::to_string(index + 1) + " (golden " +
         std::to_string(gold.size()) + " lines, trace " + std::to_string(cap.size()) +
         " lines)\n--- golden ---\n" + window(gold) + "--- trace ---\n" + window(cap);
}

}  // namespace

std::string PinUniformTrace(Context* context, const std::string& name,
                            const std::function<void()>& draw) {
  if (context == nullptr || !draw) {
    return "no context or scene\n";
  }
  SetDumpLabel("ab");
  auto aot = RunRoute(context, draw);
  // A trace that is not even run-to-run stable cannot be pinned — catch that here, not as a
  // mysterious golden diff later.
  auto aotAgain = RunRoute(context, draw);
  SetDumpLabel(nullptr);
  if (aot.empty()) {
    return "the route produced no uniform dump — the scene drew nothing, or the bundle failed to "
           "load\n";
  }
  if (aot != aotAgain) {
    return "the AOT route's trace is not run-to-run stable\n--- first ---\n" + aot +
           "--- second ---\n" + aotAgain;
  }
  auto trace = "--- aot ---\n" + aot;
  auto path = GoldenPath(name);
  if (std::getenv("TGFX_ACCEPT_BASELINE") != nullptr) {
    std::error_code ec = {};
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << trace;
    std::printf("[uniform baseline] wrote %s\n", path.c_str());
    return {};
  }
  std::ifstream in(path, std::ios::binary);
  if (!in.good()) {
    return "no golden at " + path +
           " — run once with TGFX_ACCEPT_BASELINE set to write it, then "
           "commit it together with the change\n";
  }
  std::stringstream buffer;
  buffer << in.rdbuf();
  auto golden = buffer.str();
  // The goldens predating the runtime-route removal carry both sections; only the AOT section is
  // still reproducible.
  const std::string aotSectionMarker = "--- aot ---\n";
  auto sectionStart = golden.find(aotSectionMarker);
  if (sectionStart != std::string::npos) {
    golden = golden.substr(sectionStart);
  }
  if (golden == trace) {
    return {};
  }
  return "the uniform trace drifted from the golden at " + path + "\n" +
         FirstDivergence(golden, trace);
}

}  // namespace tgfx
