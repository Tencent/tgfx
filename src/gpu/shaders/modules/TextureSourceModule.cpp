// The texture-source module's value binding. See TextureSourceModule.h for the contract.
#include "gpu/shaders/modules/TextureSourceModule.h"
#include "gpu/YUVColorConversion.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/shaders/modules/ParamWriter.h"

namespace tgfx {

// Moves a TextureEffect's per-texture values into the writer. The semantics replicate
// GLSLTextureEffect::onSetData field for field — the full-bounds Subset fallback, the
// value-gated AlphaStart, the clamp gate — so the migration is byte-for-byte; that writer
// stays until the JIT retires and serves the un-migrated families meanwhile.
void TextureSourceModule::Bind(const FragmentProcessor* processor, ParamWriter* writer) {
  auto* texture = static_cast<const TextureEffect*>(processor);
  if (!texture->isTextureInstantiated()) {
    // The texture is not instantiated (a failed or deferred upload): the processor's own writer
    // returns without touching any field in this state, and so does the module.
    return;
  }
  float rect[4] = {};
  texture->computeSubsetRect(rect);
  writer->set(Subset, rect);
  writer->set(SubsetClampOn, texture->hasSubset() ? 1 : 0);
  if (texture->hasRGBAAA()) {
    // Value-gated exactly like the processor writer: single-plane draws leave the field at its
    // previous content, which the shader never reads (HasRgbaaa is 0).
    auto coord = texture->alphaStartCoord();
    const float alphaStart[2] = {coord.x, coord.y};
    writer->set(AlphaStart, alphaStart);
  }
  writer->set(AlphaOnly, texture->isAlphaOnly() ? 1 : 0);
  writer->set(HasRgbaaa, texture->hasRGBAAA() ? 1 : 0);
  auto* yuvTexture = texture->getYUVTexture();
  if (yuvTexture != nullptr) {
    writer->set(YUVLimitedRange, IsLimitedYUVColorRange(yuvTexture->yuvColorSpace()) ? 1 : 0);
    if (auto conversion = YUVColorConversionMatrix(yuvTexture->yuvColorSpace())) {
      writer->set(Mat3ColorConversion, *conversion);
    }
  }
}

}  // namespace tgfx
