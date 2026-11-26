// Reads one half-kernel weight from the two packed slots of a 2D blur axis. The slot and the
// lane are picked with compile-time index chains rather than a dynamic index into a uniform
// array, which SwiftShader miscompiles (it silently reads zeros and drops the blur). The offset
// is a float so a runtime value (the IR loop counter) flows through it; the int fold happens
// here. The 2D form carries two slots (KERNEL_SIZE = MAX_RADIUS + 1 = 5 lanes).
//@function tgtxKernelWeight2
//@op KernelWeight2
//@frag
float tgtxKernelWeight2(vec4 kernelSlot0, vec4 kernelSlot1, float offset /*@in*/) {
  int kernelOffset = abs(int(offset));
  int kernelSlot = kernelOffset / 4;
  vec4 kernelValues = kernelSlot0;
  if (kernelSlot == 1) {
    kernelValues = kernelSlot1;
  }
  int kernelLane = kernelOffset - kernelSlot * 4;
  float weight = kernelValues.x;
  if (kernelLane == 1) {
    weight = kernelValues.y;
  }
  if (kernelLane == 2) {
    weight = kernelValues.z;
  }
  if (kernelLane == 3) {
    weight = kernelValues.w;
  }
  return weight;
}
