// The tiled-source module's value binding. See TiledSourceModule.h for the contract.
#include "gpu/shaders/modules/TiledSourceModule.h"
#include "gpu/Swizzle.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/shaders/modules/ParamWriter.h"

namespace tgfx {

// Replicates GLSLTiledTextureEffect::onSetData from the same resolved sampling snapshot.
void TiledSourceModule::Bind(const FragmentProcessor* processor, ParamWriter* writer) {
  auto* tiled = static_cast<const TiledTextureEffect*>(processor);
  const auto* sampling = tiled->resolveSampling();
  if (sampling == nullptr) {
    return;
  }
  auto* view = tiled->getTextureView();
  if (view != nullptr) {
    writer->set(ReadSwizzle, AOTReadSwizzleMode(view->getTexture()->format()));
  }
  if (sampling->usesShaderDimensions) {
    const float dimensions[2] = {sampling->shaderDimensions.x, sampling->shaderDimensions.y};
    writer->set(Dimension, dimensions);
  }
  const float subset[4] = {sampling->shaderSubset.left, sampling->shaderSubset.top,
                           sampling->shaderSubset.right, sampling->shaderSubset.bottom};
  writer->set(Subset, subset);
  const float clampRect[4] = {sampling->shaderClamp.left, sampling->shaderClamp.top,
                              sampling->shaderClamp.right, sampling->shaderClamp.bottom};
  writer->set(Clamp, clampRect);
  writer->set(ShaderModeX, static_cast<int32_t>(sampling->shaderModeX));
  writer->set(ShaderModeY, static_cast<int32_t>(sampling->shaderModeY));
  writer->set(AlphaOnly, sampling->alphaOnly ? 1 : 0);
  writer->set(Strict, sampling->strict ? 1 : 0);
}

}  // namespace tgfx
