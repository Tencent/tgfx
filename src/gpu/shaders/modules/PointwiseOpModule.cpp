// The pointwise-operator module's value binding. See PointwiseOpModule.h for the contract.
#include "gpu/shaders/modules/PointwiseOpModule.h"
#include "gpu/processors/AlphaThresholdFragmentProcessor.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/LumaFragmentProcessor.h"
#include "gpu/shaders/KernelContract.h"
#include "gpu/shaders/modules/ParamWriter.h"

namespace tgfx {

// Replicates the operator writers' slot-0 records: GLSLColorMatrixFragmentProcessor (column-major
// mat4 plus bias vector), GLSLLumaFragmentProcessor (three coefficients) and
// GLSLAlphaThresholdFragmentProcessor (one cutoff). Each also selects itself through OpType[0];
// the write is optional because only the slot kernels declare the array.
void PointwiseOpModule::Bind(const FragmentProcessor* processor, ParamWriter* writer) {
  if (IsProcessor<ColorMatrixFragmentProcessor>(processor)) {
    auto* op = static_cast<const ColorMatrixFragmentProcessor*>(processor);
    const auto& matrix = op->getMatrix();
    const float matrixData[16] = {
        matrix[0], matrix[5], matrix[10], matrix[15], matrix[1], matrix[6], matrix[11], matrix[16],
        matrix[2], matrix[7], matrix[12], matrix[17], matrix[3], matrix[8], matrix[13], matrix[18]};
    const float vectorData[4] = {matrix[4], matrix[9], matrix[14], matrix[19]};
    writer->setArrayElement(ColorMatrix, 0, matrixData);
    writer->setArrayElement(ColorVector, 0, vectorData);
    writer->setArrayElement(OpType, 0, Op::ColorMatrix);
    return;
  }
  if (IsProcessor<LumaFragmentProcessor>(processor)) {
    auto* op = static_cast<const LumaFragmentProcessor*>(processor);
    writer->setArrayElement(Kr, 0, op->getKr());
    writer->setArrayElement(Kg, 0, op->getKg());
    writer->setArrayElement(Kb, 0, op->getKb());
    writer->setArrayElement(OpType, 0, Op::Luma);
    return;
  }
  if (IsProcessor<AlphaThresholdFragmentProcessor>(processor)) {
    auto* op = static_cast<const AlphaThresholdFragmentProcessor*>(processor);
    writer->setArrayElement(Threshold, 0, op->getThreshold());
    writer->setArrayElement(OpType, 0, Op::AlphaThreshold);
  }
}

}  // namespace tgfx
