// The Perlin-noise module's value binding. See PerlinNoiseModule.h for the contract.
#include "gpu/shaders/modules/PerlinNoiseModule.h"
#include "gpu/aot/AOTPointwiseSlotWriter.h"
#include "gpu/processors/PerlinNoiseFragmentProcessor.h"
#include "gpu/shaders/KernelContract.h"
#include "gpu/shaders/modules/ChainParams.h"
#include "gpu/shaders/modules/ParamWriter.h"

namespace tgfx {

// Replicates GLSLPerlinNoiseFragmentProcessor::onSetData: the frequency pair, the stitch extents
// (zero when not stitching), the folded noise parameters, the pointwise slots defaulted to None
// (a composed operator FP overwrites slot 0 later in the walk), the loop bound, and the per-slot
// records — the last through the shared native slot writer so the record layout stays in one
// place.
void PerlinNoiseModule::Bind(const FragmentProcessor* processor, ParamWriter* writer) {
  auto* perlin = static_cast<const PerlinNoiseFragmentProcessor*>(processor);
  const auto* paintingData = perlin->getPaintingData();
  const float baseFrequency[2] = {paintingData->baseFrequencyX, paintingData->baseFrequencyY};
  writer->set(BaseFrequency, baseFrequency);
  float stitchData[2] = {0.0f, 0.0f};
  if (perlin->isStitchTiles()) {
    stitchData[0] = static_cast<float>(paintingData->stitchWidth);
    stitchData[1] = static_cast<float>(paintingData->stitchHeight);
  }
  writer->set(StitchData, stitchData);
  writer->set(NoiseType, static_cast<int32_t>(perlin->getNoiseType()));
  writer->set(NumOctaves, perlin->getNumOctaves());
  writer->set(StitchTiles, perlin->isStitchTiles() ? 1 : 0);
  for (size_t index = 0; index < MaxPointwiseSlots; ++index) {
    writer->setArrayElement(OpType, index, Op::None);
  }
  writer->set(ChainParams::PointwiseSlotCount, static_cast<int32_t>(MaxPointwiseSlots));
  for (size_t index = 0; index < perlin->pointwiseSlotCount(); ++index) {
    UploadAOTPointwiseSlot(const_cast<UniformData*>(writer->rawData()), index, MaxPointwiseSlots,
                           perlin->pointwiseSlot(index));
  }
}

}  // namespace tgfx
