// Diagnostic, not a regression test: prints the reachable variant count of every registered shader
// family and the total, prefixed "[FB]". It asserts nothing; run it to see where a rule or axis
// change moved the bundle size:
//   TGFXFullTest_OpenGL --gtest_filter='DiagnosticFamilyBreakdown.*'
#include <cstdio>
#include "base/TGFXTest.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/ShaderPermutation.h"

namespace tgfx {

TGFX_TEST(DiagnosticFamilyBreakdown, Print) {
  size_t total = 0;
  for (size_t index = 0; index < FamilyCount(); ++index) {
    auto pairs = EnumerateReachablePermutations(Families()[index].name);
    if (!pairs.has_value()) {
      continue;
    }
    total += pairs->size();
    std::printf("[FB] %-44s %3zu\n", Families()[index].name, pairs->size());
  }
  std::printf("[FB] TOTAL %zu\n", total);
}

}  // namespace tgfx
