// The analytic-clip module's value binding. See AnalyticClipModule.h for the contract.
#include "gpu/shaders/modules/AnalyticClipModule.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/shaders/modules/ParamWriter.h"

namespace tgfx {

// Replicates GLSLRectEffect::onSetData: the local rect and AA flag always, the device-to-local
// matrix for transformed clips, the half-pixel-outset device rect for direct device-space clips
// (HasClip 1), and the local-space form (HasClip 3). needTransform() is !isDeviceSpaceRect().
// The rrect form replicates GLSLRRectEffect::onSetData instead: the same local rect and AA flag,
// the two packed radius vectors, the always-declared device-to-local matrix, and HasClip 2.
void AnalyticClipModule::Bind(const FragmentProcessor* processor, ParamWriter* writer) {
  if (IsProcessor<RRectEffect>(processor)) {
    auto* clip = static_cast<const RRectEffect*>(processor);
    const auto& rect = clip->getLocalRect();
    const float localRect[4] = {rect.left, rect.top, rect.right, rect.bottom};
    writer->set(LocalRect, localRect);
    const auto& radii = clip->getRadii();
    const float radiiX[4] = {radii[0].x, radii[1].x, radii[2].x, radii[3].x};
    const float radiiY[4] = {radii[0].y, radii[1].y, radii[2].y, radii[3].y};
    writer->set(RadiiX, radiiX);
    writer->set(RadiiY, radiiY);
    writer->set(AntiAlias, clip->isAntiAlias() ? 1.0f : 0.0f);
    writer->set(DeviceToLocal, clip->getDeviceToLocal());
    writer->set(HasClip, 2);
    return;
  }
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
