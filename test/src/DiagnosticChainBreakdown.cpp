// Diagnostic, not a regression test: prints the composition of the PointwiseChain reachable set
// (pairs per geometry layout, per vertex combination and per fragment XP/mask combination),
// prefixed "[CB]". It asserts nothing; run it to see how a chain dimension change reshaped the set:
//   TGFXFullTest_OpenGL --gtest_filter='DiagnosticChainBreakdown.*'
#include <cstdio>
#include <map>
#include <string>
#include "base/TGFXTest.h"
#include "gpu/shaders/PermutationRules.h"
#include "gpu/shaders/level1/PointwiseChainShader.h"

namespace tgfx {

TGFX_TEST(DiagnosticChainBreakdown, Print) {
  auto pairs = PointwiseChainShader::Reachable();
  std::printf("[CB] total=%zu\n", pairs.size());
  std::map<std::string, int> byLayout;
  std::map<std::string, int> byCombo;
  std::set<uint32_t> verts;
  std::set<uint32_t> frags;
  for (const auto& p : pairs) {
    verts.insert(p.first);
    frags.insert(p.second);
    auto v = PointwiseChainShader::VD::domain().decode(p.first);
    using VD = PointwiseChainShader::VD;
    char key[160];
    std::snprintf(key, sizeof(key), "GP%d cov%d uv%d tex%d sub%d", v[VD::GP_LAYOUT],
                  v[VD::HAS_COVERAGE], v[VD::HAS_UV_COORD], v[VD::TEXTURE_COUNT],
                  v[VD::HAS_SUBSET]);
    byCombo[key]++;
    char lay[16];
    std::snprintf(lay, sizeof(lay), "GP%d", v[VD::GP_LAYOUT]);
    byLayout[lay]++;
  }
  std::printf("[CB] distinct vert stages=%zu distinct frag stages=%zu\n", verts.size(),
              frags.size());
  for (const auto& e : byLayout) {
    std::printf("[CB] layout %s: %d\n", e.first.c_str(), e.second);
  }
  for (const auto& e : byCombo) {
    std::printf("[CB] %s -> %d pairs\n", e.first.c_str(), e.second);
  }
  std::map<std::string, int> byFrag;
  for (const auto& p : pairs) {
    auto f = PointwiseChainShader::FD::domain().decode(p.second);
    using FD = PointwiseChainShader::FD;
    char key[96];
    std::snprintf(key, sizeof(key), "xp%d mask%d", f[FD::HAS_XP], f[FD::HAS_MASK_TEXTURE]);
    byFrag[key]++;
  }
  for (const auto& e : byFrag) {
    std::printf("[CB] frag %s: %d\n", e.first.c_str(), e.second);
  }
}

}  // namespace tgfx
