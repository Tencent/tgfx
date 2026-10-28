// The PorterDuff XP module's value binding. See PorterDuffXPModule.h for the contract.
#include "gpu/shaders/modules/PorterDuffXPModule.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/shaders/modules/ParamWriter.h"

namespace tgfx {

// Replicates GLSLPorterDuffXferProcessor::setData: the blend mode first (the framebuffer-fetch
// path reads it before any dst texture exists), then the dst fetch state when one is present.
void PorterDuffXPModule::Bind(const XferProcessor* processor, ParamWriter* writer) {
  auto* xp = static_cast<const PorterDuffXferProcessor*>(processor);
  writer->set(XPBlendMode, static_cast<int32_t>(xp->getBlendMode()));
  const auto& dstInfo = xp->getDstTextureInfo();
  if (dstInfo.textureProxy == nullptr) {
    return;
  }
  auto dstView = dstInfo.textureProxy->getTextureView();
  if (dstView == nullptr) {
    return;
  }
  const float upperLeft[2] = {dstInfo.offset.x, dstInfo.offset.y};
  writer->set(DstTextureUpperLeft, upperLeft);
  int width = dstView->width();
  int height = dstView->height();
  if (dstView->getTexture()->type() == TextureType::Rectangle) {
    width = 1;
    height = 1;
  }
  const float scales[2] = {1.f / static_cast<float>(width), 1.f / static_cast<float>(height)};
  writer->set(DstTextureCoordScale, scales);
}

}  // namespace tgfx
