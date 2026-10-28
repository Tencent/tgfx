// The blur-kernel module's value binding. See BlurKernelModule.h for the contract.
#include "gpu/shaders/modules/BlurKernelModule.h"
#include "gpu/processors/GaussianBlur1DFragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/shaders/modules/ParamWriter.h"

namespace tgfx {

// Replicates GLSLGaussianBlur1DFragmentProcessor::onSetData: the packed half-kernel and radius,
// the device-space sample step derived through the child's coordinate transform, the runtime mask
// default, and the child-shape flags (tiled / alpha-only) the kernel branches on.
void BlurKernelModule::Bind(const FragmentProcessor* processor, ParamWriter* writer) {
  auto* blur = static_cast<const GaussianBlur1DFragmentProcessor*>(processor);
  writer->set(Kernel, blur->packedKernel());
  writer->set(Radius, blur->getKernelRadius());
  auto* child = blur->childProcessor(0);
  Point stepVectors[] = {{0, 0}, {blur->getStepLength(), 0}};
  if (blur->getDirection() == GaussianBlurDirection::Vertical) {
    stepVectors[1] = {0, blur->getStepLength()};
  }
  auto transform = child->coordTransform(0);
  auto matrix = transform->getTotalMatrix();
  matrix.mapPoints(stepVectors, 2);
  Point step = stepVectors[1] - stepVectors[0];
  const float stepValues[2] = {step.x, step.y};
  writer->set(Step, stepValues);
  writer->set(HasDeviceMask, 0);
  writer->set(TiledChild, IsProcessor<TiledTextureEffect>(child) ? 1 : 0);
  bool alphaChild = false;
  if (IsProcessor<TextureEffect>(child)) {
    alphaChild = static_cast<const TextureEffect*>(child)->isAlphaOnly();
  } else if (IsProcessor<TiledTextureEffect>(child)) {
    alphaChild = static_cast<const TiledTextureEffect*>(child)->isAlphaOnly();
  }
  writer->set(AlphaChild, alphaChild ? 1 : 0);
}

}  // namespace tgfx
