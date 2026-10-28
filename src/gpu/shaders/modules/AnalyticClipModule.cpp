// The analytic-clip module's value binding. See AnalyticClipModule.h for the contract.
#include "gpu/shaders/modules/AnalyticClipModule.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/shaders/modules/ParamWriter.h"

namespace tgfx {

// Replicates GLSLRectEffect::onSetData: the local rect and AA flag always, the device-to-local
// matrix for transformed clips, the half-pixel-outset device rect for direct device-space clips
// (HasClip 1), and the local-space form (HasClip 3). needTransform() is !isDeviceSpaceRect().
void AnalyticClipModule::Bind(const FragmentProcessor* processor, ParamWriter* writer) {
  auto* clip = static_cast<const RectEffect*>(processor);
  const auto& rect = clip->getRect();
  const float localRect[4] = {rect.left, rect.top, rect.right, rect.bottom};
  writer->set(LocalRect, localRect);
  writer->set(AntiAlias, clip->isAntiAlias() ? 1.0f : 0.0f);
  if (!clip->isDeviceSpaceRect()) {
    writer->set(DeviceToLocal, clip->getDeviceToLocal());
  } else {
    const auto outset = rect.makeOutset(0.5f, 0.5f);
    const float deviceRect[4] = {outset.left, outset.top, outset.right, outset.bottom};
    writer->set(Rect, deviceRect);
  }
  writer->set(HasClip, clip->isDeviceSpaceRect() ? 1 : 3);
}

}  // namespace tgfx
