// The device-mask module's value binding. See DeviceMaskModule.h for the contract.
#include "gpu/shaders/modules/DeviceMaskModule.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/shaders/modules/ParamWriter.h"

namespace tgfx {

// Replicates GLSLDeviceSpaceTextureEffect::onSetData: the scaled device-to-texture matrix and
// the runtime apply switch, then either the shared kernel's never-read subset decoy or the bare
// mask's no-op fallbacks (full-bounds subset, alpha-only flag, whole-plane clip rect, HasClip 0),
// selected by DeviceMaskSubset presence exactly as the processor writer does.
void DeviceMaskModule::Bind(const FragmentProcessor* processor, ParamWriter* writer) {
  auto* mask = static_cast<const DeviceSpaceTextureEffect*>(processor);
  auto textureView = mask->getTextureProxy()->getTextureView();
  if (textureView == nullptr) {
    return;
  }
  auto deviceCoordMatrix = mask->getUVMatrix();
  auto scale = textureView->getTextureCoord(1, 1);
  deviceCoordMatrix.postScale(scale.x, scale.y);
  writer->set(DeviceCoordMatrix, deviceCoordMatrix);
  writer->set(HasDeviceMask, 1);
  float noOpRect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  if (textureView->getTexture()->type() == TextureType::Rectangle) {
    noOpRect[2] = static_cast<float>(textureView->width());
    noOpRect[3] = static_cast<float>(textureView->height());
  }
  if (writer->hasField("DeviceMaskSubset")) {
    writer->set(DeviceMaskSubset, noOpRect);
    return;
  }
  writer->set(Subset, noOpRect);
  writer->set(AlphaOnly, mask->isAlphaOnly() ? 1 : 0);
  const float noClip[4] = {-1e9f, -1e9f, 1e9f, 1e9f};
  writer->set(Rect, noClip);
  writer->set(HasClip, 0);
}

}  // namespace tgfx
