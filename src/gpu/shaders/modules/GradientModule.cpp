// The gradient module's value bindings. See GradientModule.h for the contract.
#include "gpu/shaders/modules/GradientModule.h"
#include "gpu/processors/ClampedGradientEffect.h"
#include "gpu/processors/ConicGradientLayout.h"
#include "gpu/processors/SingleIntervalGradientColorizer.h"
#include "gpu/shaders/modules/ParamWriter.h"

namespace tgfx {
namespace {

void WriteColor(ParamWriter* writer, const Param<ParamType::Float4>& param, const Color& color) {
  const float values[4] = {color.red, color.green, color.blue, color.alpha};
  writer->set(param, values);
}

}  // namespace

// Replicates GLSLClampedGradientEffect::onSetData: the two border colors, the runtime mask
// default, and the LayoutType branch selected from the layout child's kind.
void GradientModule::BindEffect(const FragmentProcessor* processor, ParamWriter* writer) {
  auto* effect = static_cast<const ClampedGradientEffect*>(processor);
  WriteColor(writer, LeftBorderColor, effect->getLeftBorderColor());
  WriteColor(writer, RightBorderColor, effect->getRightBorderColor());
  writer->set(HasDeviceMask, 0);
  writer->set(LayoutType, ClampedGradientEffect::LayoutTypeIndex(effect->getGradLayout()->name()));
}

// Replicates GLSLConicGradientLayout::onSetData.
void GradientModule::BindConicLayout(const FragmentProcessor* processor, ParamWriter* writer) {
  auto* layout = static_cast<const ConicGradientLayout*>(processor);
  writer->set(Bias, layout->getBias());
  writer->set(Scale, layout->getScale());
}

// Replicates GLSLSingleIntervalGradientColorizer::onSetData.
void GradientModule::BindColorizer(const FragmentProcessor* processor, ParamWriter* writer) {
  auto* colorizer = static_cast<const SingleIntervalGradientColorizer*>(processor);
  WriteColor(writer, Start, colorizer->startColor());
  WriteColor(writer, End, colorizer->endColor());
  writer->set(ColorizerKind, 0);
}

}  // namespace tgfx
