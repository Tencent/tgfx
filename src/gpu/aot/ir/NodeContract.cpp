
#include "gpu/aot/ir/NodeContract.h"

namespace tgfx {
namespace {

// One row per AOTEffectKind, in enum order: the kind's traits. The row count assert pins the
// table to the enum, so adding a kind without adding a row fails the build instead of silently
// reading a stale row.
constexpr AOTKindTraits KindTraits[] = {
    // GeometryColor (graph root, designator): never scheduled.
    {false, AOTDesignator::GeometryColor},
    {true, AOTDesignator::None, false, AOTRemapKind::TextureMatrix},  // TextureSource
    {true, AOTDesignator::None, true},                                // ColorMatrix
    {true, AOTDesignator::None, true},                                // Luma
    {true, AOTDesignator::None, true},                                // AlphaThreshold
    {true, AOTDesignator::None, true},                                // ColorSpaceXform
    {true},
    {true},
    {true, AOTDesignator::None, false, AOTRemapKind::LocalMatrix},  // PerlinNoiseSource
    {true},
    {true, AOTDesignator::None, false, AOTRemapKind::LocalMatrix},  // GradientSource
    {true},
    // GeometryCoverage (graph root, designator): never scheduled.
    {false, AOTDesignator::GeometryCoverage},
    {true, AOTDesignator::OpaqueInput},  // GeometryColorOpaqueInput
    {true, AOTDesignator::WhiteInput},   // GeometryWhiteInput
    {true},
    {true},
    // The opaque-kernel node: the Gaussian blur is chain-fused (one catalog function with the
    // packed kernel riding uniforms); the glass kernels still take the dedicated families, and
    // the layout itself rejects them.
    {true},
};

constexpr size_t KindTraitCount = sizeof(KindTraits) / sizeof(AOTKindTraits);
static_assert(KindTraitCount == static_cast<size_t>(AOTEffectKind::OpaqueKernel) + 1,
              "every AOTEffectKind needs exactly one traits row in KindTraits");

}  // namespace

const AOTKindTraits& TraitsOf(AOTEffectKind kind) {
  static constexpr AOTKindTraits fallback = {};
  auto index = static_cast<size_t>(kind);
  return index < KindTraitCount ? KindTraits[index] : fallback;
}

}  // namespace tgfx
