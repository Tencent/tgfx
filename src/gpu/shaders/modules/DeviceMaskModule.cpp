// The device-mask module's value binding. See DeviceMaskModule.h for the contract.
#include "gpu/shaders/modules/DeviceMaskModule.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/shaders/modules/ParamWriter.h"

namespace tgfx {

// Replicates GLSLDeviceSpaceTextureEffect::onSetData on the shared-kernel (decoy) path: the
// scaled device-to-texture matrix, the runtime apply switch, and the full-bounds decoy rect.
// The family binds this module only for kernels that declare DeviceMaskSubset, which is what
// selects that branch in the processor writer.
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
  float rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  if (textureView->getTexture()->type() == TextureType::Rectangle) {
    rect[2] = static_cast<float>(textureView->width());
    rect[3] = static_cast<float>(textureView->height());
  }
  writer->set(DeviceMaskSubset, rect);
}

}  // namespace tgfx
