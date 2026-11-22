// The half-kernel weight for one symmetric blur tap: abs(offset) indexes the packed table (four
// weights per vec4), so the slot and lane selects spell the same workaround the runtime emission
// uses (SwiftShader miscompiles a dynamic index into a std140 array and silently reads zeros).
//@function tgfxBlurWeight
float tgfxBlurWeight(vec4[17] kernel, int offset) {
  int kernelOffset = abs(offset);
  int kernelSlot = kernelOffset / 4;
  vec4 kernelValues = kernel[0];
  if (kernelSlot == 1) { kernelValues = kernel[1]; }
  if (kernelSlot == 2) { kernelValues = kernel[2]; }
  if (kernelSlot == 3) { kernelValues = kernel[3]; }
  if (kernelSlot == 4) { kernelValues = kernel[4]; }
  if (kernelSlot == 5) { kernelValues = kernel[5]; }
  if (kernelSlot == 6) { kernelValues = kernel[6]; }
  if (kernelSlot == 7) { kernelValues = kernel[7]; }
  if (kernelSlot == 8) { kernelValues = kernel[8]; }
  if (kernelSlot == 9) { kernelValues = kernel[9]; }
  if (kernelSlot == 10) { kernelValues = kernel[10]; }
  if (kernelSlot == 11) { kernelValues = kernel[11]; }
  if (kernelSlot == 12) { kernelValues = kernel[12]; }
  if (kernelSlot == 13) { kernelValues = kernel[13]; }
  if (kernelSlot == 14) { kernelValues = kernel[14]; }
  if (kernelSlot == 15) { kernelValues = kernel[15]; }
  if (kernelSlot == 16) { kernelValues = kernel[16]; }
  int kernelLane = kernelOffset - kernelSlot * 4;
  float weight = kernelValues.x;
  if (kernelLane == 1) { weight = kernelValues.y; }
  if (kernelLane == 2) { weight = kernelValues.z; }
  if (kernelLane == 3) { weight = kernelValues.w; }
  return weight;
}
