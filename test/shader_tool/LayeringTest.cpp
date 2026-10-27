//////////////////////////////////////////////////////////////////////////////////////////////////
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
//////////////////////////////////////////////////////////////////////////////////////////////////

// The shader tool and the runtime share a handful of headers, and the sharing only stays safe while
// it stays narrow. These tests read the #include lines of the sources and fail when the narrowness
// is lost, so a boundary is kept by a test instead of by remembering it:
//
//   contract layer   constants and pure functions; includes the standard library and nothing else
//        ^
//   declaration layer   the shader families and their permutation domains
//        ^
//   shader_build_tool   may include the two layers above and nothing else of the runtime
//
// Without the second rule the tool could start to depend on a runtime class, and with it on a GPU
// context it has no use for. Without the first, a "constant" header could grow a dependency and
// drag the same thing in through a side door.

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "gtest/gtest.h"

namespace tgfx {
namespace {

const std::string SourceDir = TGFX_SOURCE_DIR;

struct Include {
  std::string target;
  bool system = false;  // <...> rather than "..."
};

// The includes of one file, in order. A line is an include when it starts with #include after
// optional white space; the ones in comments are not part of any build, so they are skipped.
std::vector<Include> IncludesOf(const std::string& path) {
  std::vector<Include> includes;
  std::ifstream file(path);
  std::string line;
  bool inBlockComment = false;
  while (std::getline(file, line)) {
    if (inBlockComment) {
      auto end = line.find("*/");
      if (end == std::string::npos) {
        continue;
      }
      inBlockComment = false;
      line = line.substr(end + 2);
    }
    auto start = line.find_first_not_of(" \t");
    if (start == std::string::npos) {
      continue;
    }
    if (line.compare(start, 2, "/*") == 0 && line.find("*/", start) == std::string::npos) {
      inBlockComment = true;
      continue;
    }
    // "#include" may have white space between the hash and the keyword.
    if (line[start] != '#') {
      continue;
    }
    auto keyword = line.find_first_not_of(" \t", start + 1);
    if (keyword == std::string::npos || line.compare(keyword, 7, "include") != 0) {
      continue;
    }
    auto open = line.find_first_of("<\"", keyword + 7);
    if (open == std::string::npos) {
      continue;
    }
    auto close = line.find(line[open] == '<' ? '>' : '"', open + 1);
    if (close == std::string::npos) {
      continue;
    }
    includes.push_back({line.substr(open + 1, close - open - 1), line[open] == '<'});
  }
  return includes;
}

std::vector<std::string> FilesIn(const std::string& dir, const std::set<std::string>& extensions) {
  // Recursive: the tool's sources sit in sublayer directories (core/, compile/, ...), and a
  // boundary test that skips a sublayer does not check it at all.
  std::vector<std::string> files;
  for (const auto& item : std::filesystem::recursive_directory_iterator(dir)) {
    if (item.is_regular_file() && extensions.count(item.path().extension().string()) > 0) {
      files.push_back(item.path().string());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::string NameOf(const std::string& path) {
  return std::filesystem::path(path).filename().string();
}

// The headers of the runtime that the tool may include. Every one of them is a contract header or
// part of the declaration layer; adding to this list is a decision about the architecture, not a
// convenience, and it belongs in a review of its own.
const std::set<std::string>& AllowedRuntimeHeaders() {
  static const std::set<std::string> allowed = {
      // contract layer
      "gpu/AOTResourceProfile.h",
      "gpu/BundleFormat.h",
      "gpu/PrecompiledBundleIdentity.h",
      "gpu/ShaderBindingLayout.h",
      "gpu/ShaderKeyHash.h",
      "gpu/ShaderProfile.h",
      "gpu/Uniform.h",
      "gpu/shaders/KernelContract.h",
      // declaration layer
      "gpu/shaders/AllShaderFamilies.h",
      "gpu/shaders/PrecompiledShader.h",
  };
  return allowed;
}

// Headers of the allowed list that CMake generates into the build tree instead of keeping in src.
const std::set<std::string>& GeneratedHeaders() {
  static const std::set<std::string> generated = {"gpu/shaders/AllShaderFamilies.h"};
  return generated;
}

// The contract headers: constants and pure functions shared by the tool and the runtime.
const std::vector<std::string>& ContractHeaders() {
  static const std::vector<std::string> headers = {
      "gpu/AOTResourceProfile.h",
      "gpu/BundleFormat.h",
      "gpu/PrecompiledBundleIdentity.h",
      "gpu/ShaderBindingLayout.h",
      "gpu/ShaderKeyHash.h",
      "gpu/ShaderProfile.h",
      "gpu/Uniform.h",
      "gpu/shaders/KernelContract.h",
  };
  return headers;
}

// Headers and libraries the tool uses that are not part of the project.
bool IsThirdParty(const std::string& target) {
  return target == "zstd.h" || target == "zlib.h" || target == "spirv_cross.hpp" ||
         target == "spirv_msl.hpp" || target == "spirv_glsl.hpp" || target == "spirv_parser.hpp" ||
         target.rfind("shaderc/", 0) == 0 || target.rfind("src/tint/", 0) == 0 ||
         target.rfind("spirv", 0) == 0;
}

std::string ToolDir() {
  return SourceDir + "/tools/shader_build_tool";
}

}  // namespace

// ---- The tool reaches only the layers it is allowed to --------------------------------------

TEST(LayeringTest, TheToolIncludesOnlyAllowedRuntimeHeaders) {
  size_t checked = 0;
  for (const auto& file : FilesIn(ToolDir(), {".cpp", ".h"})) {
    for (const auto& include : IncludesOf(file)) {
      if (include.system) {
        continue;
      }
      // An include that resolves inside the tool directory itself names one of the tool's own
      // sublayer headers (core/, compile/, ...), not a runtime header. The "core/" prefix is
      // ambiguous — the runtime has src/core/ — so existence decides, not the spelling.
      if (std::filesystem::exists(ToolDir() + "/" + include.target)) {
        continue;
      }
      bool runtimePath = include.target.rfind("gpu/", 0) == 0 ||
                         include.target.rfind("core/", 0) == 0 ||
                         include.target.rfind("tgfx/", 0) == 0 ||
                         include.target.rfind("platform/", 0) == 0;
      if (!runtimePath) {
        continue;
      }
      ++checked;
      EXPECT_EQ(AllowedRuntimeHeaders().count(include.target), 1u)
          << NameOf(file) << " includes " << include.target
          << ", which is not a header the tool may depend on. The tool shares the contract layer "
             "and the declaration layer with the runtime and nothing else; if this one belongs "
             "there, add it to AllowedRuntimeHeaders in this test as a reviewed decision.";
    }
  }
  EXPECT_GT(checked, 0u);
}

TEST(LayeringTest, TheToolIncludesNoRuntimeClassThroughAPublicHeader) {
  // The public headers (tgfx/...) are the runtime's API; the tool has no business with them.
  for (const auto& file : FilesIn(ToolDir(), {".cpp", ".h"})) {
    for (const auto& include : IncludesOf(file)) {
      EXPECT_NE(include.target.rfind("tgfx/", 0), 0u) << NameOf(file) << " includes " << include.target;
    }
  }
}

TEST(LayeringTest, EveryAllowedHeaderExists) {
  for (const auto& header : AllowedRuntimeHeaders()) {
    if (GeneratedHeaders().count(header) > 0) {
      // CMake writes this header into the build tree; the generator is what guarantees it. What
      // can be checked here is that the name is the one the generator produces.
      EXPECT_EQ(header, "gpu/shaders/AllShaderFamilies.h");
      continue;
    }
    EXPECT_TRUE(std::filesystem::exists(SourceDir + "/src/" + header)) << header;
  }
}

TEST(LayeringTest, EveryGeneratedHeaderIsGeneratedByCMake) {
  // The list of generated headers is only valid while CMake still generates them.
  std::ifstream file(SourceDir + "/CMakeLists.txt");
  std::stringstream buffer;
  buffer << file.rdbuf();
  auto text = buffer.str();
  for (const auto& header : GeneratedHeaders()) {
    auto name = std::filesystem::path(header).filename().string();
    EXPECT_NE(text.find(name), std::string::npos)
        << name << " is listed as generated but CMakeLists.txt no longer mentions it";
  }
}

TEST(LayeringTest, EveryAllowedHeaderIsUsedByTheTool) {
  // A header on the list that nothing includes any more is a permission nobody needs, and a
  // permission kept around is one that gets used later without a review.
  std::set<std::string> used;
  for (const auto& file : FilesIn(ToolDir(), {".cpp", ".h"})) {
    for (const auto& include : IncludesOf(file)) {
      used.insert(include.target);
    }
  }
  for (const auto& header : AllowedRuntimeHeaders()) {
    // The declaration layer headers are reached through the families the tool links, so they may
    // be used only indirectly; the contract headers must be used by the tool directly.
    if (header.rfind("gpu/shaders/", 0) == 0 && header != "gpu/shaders/KernelContract.h") {
      continue;
    }
    EXPECT_EQ(used.count(header), 1u) << header << " is allowed but the tool does not include it";
  }
}

// ---- The contract layer stays a contract layer ----------------------------------------------

TEST(LayeringTest, ContractHeadersIncludeOnlyTheStandardLibrary) {
  for (const auto& header : ContractHeaders()) {
    auto path = SourceDir + "/src/" + header;
    ASSERT_TRUE(std::filesystem::exists(path)) << header;
    for (const auto& include : IncludesOf(path)) {
      EXPECT_TRUE(include.system) << header << " includes the project header " << include.target
                                  << "; a contract header may include only the standard library.";
      if (include.system) {
        // A system include that is not the standard library would be a third party dependency.
        EXPECT_EQ(include.target.find('/'), std::string::npos)
            << header << " includes " << include.target;
        EXPECT_EQ(include.target.find('.'), std::string::npos)
            << header << " includes " << include.target << ", which is not a standard header";
      }
    }
  }
}

TEST(LayeringTest, ContractHeadersAreNotRuntimeClasses) {
  // UniformData is a runtime class that happens to share a name prefix with a contract header;
  // the binding layout lives in ShaderBindingLayout.h exactly so the tool never needs UniformData.
  for (const auto& header : ContractHeaders()) {
    EXPECT_EQ(header.find("UniformData"), std::string::npos) << header;
  }
  std::set<std::string> toolIncludes;
  for (const auto& file : FilesIn(ToolDir(), {".cpp", ".h"})) {
    for (const auto& include : IncludesOf(file)) {
      toolIncludes.insert(include.target);
    }
  }
  EXPECT_EQ(toolIncludes.count("gpu/UniformData.h"), 0u);
}

TEST(LayeringTest, TheRuntimeOnlyProfileResolverIsNotUsedByTheTool) {
  // ShaderProfileResolver.h depends on the runtime Backend type. The tool uses ShaderProfile.h.
  for (const auto& file : FilesIn(ToolDir(), {".cpp", ".h"})) {
    for (const auto& include : IncludesOf(file)) {
      EXPECT_NE(include.target, "gpu/ShaderProfileResolver.h") << NameOf(file);
    }
  }
}

// ---- The tool's own modules ------------------------------------------------------------------

namespace {

// The include graph between the tool's own files, keyed by module: a .h and its .cpp are one
// node. An include names a module of the tool only when it resolves to a file under the tool
// directory — a plain "Foo.h" written by a sibling file, or a "core/Foo.h" written from another
// sublayer. The runtime headers ("gpu/...") and third-party headers resolve elsewhere and are
// not nodes of this graph.
std::map<std::string, std::set<std::string>> ToolModuleGraph() {
  std::map<std::string, std::set<std::string>> graph;
  for (const auto& file : FilesIn(ToolDir(), {".cpp", ".h"})) {
    auto module = std::filesystem::path(file).stem().string();
    graph[module];
    for (const auto& include : IncludesOf(file)) {
      if (include.system || IsThirdParty(include.target)) {
        continue;
      }
      if (!std::filesystem::exists(ToolDir() + "/" + include.target)) {
        continue;
      }
      auto target = std::filesystem::path(include.target).stem().string();
      if (target != module) {
        graph[module].insert(target);
      }
    }
  }
  return graph;
}

// Finds a cycle by depth first search and returns it as "a -> b -> a", or "" when there is none.
std::string FindCycle(const std::map<std::string, std::set<std::string>>& graph,
                      const std::string& node, std::vector<std::string>* path,
                      std::set<std::string>* done) {
  for (size_t i = 0; i < path->size(); ++i) {
    if ((*path)[i] == node) {
      std::string text;
      for (size_t j = i; j < path->size(); ++j) {
        text += (*path)[j] + " -> ";
      }
      return text + node;
    }
  }
  if (done->count(node) > 0) {
    return "";
  }
  path->push_back(node);
  auto found = graph.find(node);
  if (found != graph.end()) {
    for (const auto& next : found->second) {
      auto cycle = FindCycle(graph, next, path, done);
      if (!cycle.empty()) {
        return cycle;
      }
    }
  }
  path->pop_back();
  done->insert(node);
  return "";
}

}  // namespace

TEST(LayeringTest, TheToolModulesFormNoIncludeCycle) {
  auto graph = ToolModuleGraph();
  ASSERT_FALSE(graph.empty());
  std::set<std::string> done;
  for (const auto& entry : graph) {
    std::vector<std::string> path;
    auto cycle = FindCycle(graph, entry.first, &path, &done);
    EXPECT_TRUE(cycle.empty()) << "include cycle: " << cycle;
  }
}

TEST(LayeringTest, TheAnalysesDoNotDependOnTheProductsTheyFeed) {
  // Reflection, inline analysis and the cache read compiler output or a payload; they must not
  // know about the bundle files built from it, or the dependency turns around. The data structures
  // that travel between the stages are declared in BundleWriter.h, which is why that one is
  // allowed to be included.
  auto graph = ToolModuleGraph();
  const std::vector<std::string> analyses = {"ReflectionExtractor", "InlineExpansion", "StageCache"};
  const std::vector<std::string> products = {"BundleManifest", "BundleVerifier", "StageReport",
                                             "ContractChecks"};
  for (const auto& analysis : analyses) {
    ASSERT_EQ(graph.count(analysis), 1u) << analysis;
    for (const auto& product : products) {
      EXPECT_EQ(graph[analysis].count(product), 0u)
          << analysis << " depends on " << product << ", which is built from its output";
    }
  }
}

TEST(LayeringTest, TheCacheKnowsNothingOfShaders) {
  // The stage cache is a content addressed store: it hashes bytes and keeps bytes. Anything it
  // knew about shaders would be something its key might forget to cover.
  auto graph = ToolModuleGraph();
  ASSERT_EQ(graph.count("StageCache"), 1u);
  EXPECT_TRUE(graph["StageCache"].empty())
      << "StageCache includes another module of the tool";
}

TEST(LayeringTest, CoreModulesDependOnNothing) {
  // core/ is the bottom of the tool: every other sublayer may use it and it may use nothing.
  // The moment it includes another module the graph has a second bottom, and the layering the
  // other tests assume stops holding.
  auto graph = ToolModuleGraph();
  for (const auto& file : FilesIn(ToolDir() + "/core", {".cpp", ".h"})) {
    auto module = std::filesystem::path(file).stem().string();
    ASSERT_EQ(graph.count(module), 1u) << module << " of core/ is not in the module graph";
    EXPECT_TRUE(graph[module].empty())
        << module << " of core/ depends on " << *(graph[module].begin())
        << "; core is the bottom of the tool";
  }
}

TEST(LayeringTest, TheWriterDependsOnNoOtherModuleOfTheTool) {
  // BundleWriter.h holds the data every stage passes along and the writer of the bundle file. It
  // is the bottom of the tool, and only the output helper sits beneath it.
  auto graph = ToolModuleGraph();
  ASSERT_EQ(graph.count("BundleWriter"), 1u);
  for (const auto& dependency : graph["BundleWriter"]) {
    EXPECT_EQ(dependency, "OutputFile") << "BundleWriter depends on " << dependency;
  }
}

TEST(LayeringTest, MainIsTheOnlyModuleNothingElseIncludes) {
  auto graph = ToolModuleGraph();
  for (const auto& entry : graph) {
    EXPECT_EQ(entry.second.count("main"), 0u) << entry.first << " includes main";
  }
}

TEST(LayeringTest, TheStageCompilerDependsOnlyOnTheCompilePrimitives) {
  // StageCompiler owns the caches and is the only caller of the compiler primitives, so its
  // dependencies stop at the primitives, the persistent cache and the reflection extractor —
  // anything else and the compile layer has grown a side door.
  auto graph = ToolModuleGraph();
  ASSERT_EQ(graph.count("StageCompiler"), 1u);
  const std::set<std::string> allowed = {"ShaderCompiler", "StageCache", "ReflectionExtractor"};
  for (const auto& dependency : graph["StageCompiler"]) {
    EXPECT_EQ(allowed.count(dependency), 1u)
        << "StageCompiler depends on " << dependency
        << ", which is not part of the compile layer";
  }
}

TEST(LayeringTest, TheCycleFinderFindsACycle) {
  // The cycle test passes on a healthy graph, so the finder itself needs a case where it must fail.
  std::map<std::string, std::set<std::string>> graph = {{"a", {"b"}}, {"b", {"c"}}, {"c", {"a"}}};
  std::set<std::string> done;
  std::vector<std::string> path;
  EXPECT_EQ(FindCycle(graph, "a", &path, &done), "a -> b -> c -> a");

  std::map<std::string, std::set<std::string>> acyclic = {{"a", {"b", "c"}}, {"b", {"c"}}, {"c", {}}};
  std::set<std::string> acyclicDone;
  std::vector<std::string> acyclicPath;
  EXPECT_EQ(FindCycle(acyclic, "a", &acyclicPath, &acyclicDone), "");
}

TEST(LayeringTest, TheIncludeScannerReadsRealIncludesOnly) {
  auto path = ::testing::TempDir() + "tgfx_layering_scanner.h";
  {
    std::ofstream out(path);
    out << "#include <vector>\n"
           "#include \"gpu/Real.h\"\n"
           "  #  include   \"gpu/Indented.h\"\n"
           "// #include \"gpu/LineComment.h\"\n"
           "/*\n#include \"gpu/InsideBlock.h\"\n*/\n"
           "#include \"gpu/AfterBlock.h\"\n"
           "int x = 0;  // #include \"gpu/Trailing.h\"\n";
  }
  auto includes = IncludesOf(path);
  std::remove(path.c_str());
  std::vector<std::string> targets;
  for (const auto& include : includes) {
    targets.push_back(include.target);
  }
  EXPECT_EQ(targets, (std::vector<std::string>{"vector", "gpu/Real.h", "gpu/Indented.h",
                                               "gpu/AfterBlock.h"}))
      << "the scanner mis-reads the include lines";
}

}  // namespace tgfx
