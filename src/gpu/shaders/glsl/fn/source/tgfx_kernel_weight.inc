// Reads one half-kernel weight from the 17 packed slots. The slot and the lane are picked with
// chains of compile-time indices rather than indexing a std140 uniform array with the dynamic
// offset directly: SwiftShader miscompiles a dynamic index into a block array and silently reads
// zeros, which drops the blur. Both blur paths (the linked call and the inline loop) read their
// weights through this one function, so the two forms cannot drift apart.
//@function tgfxKernelWeight
//@op KernelWeight
//@frag
float tgfxKernelWeight(vec4 kernelSlot0, vec4 kernelSlot1, vec4 kernelSlot2, vec4 kernelSlot3,
                       vec4 kernelSlot4, vec4 kernelSlot5, vec4 kernelSlot6, vec4 kernelSlot7,
                       vec4 kernelSlot8, vec4 kernelSlot9, vec4 kernelSlot10, vec4 kernelSlot11,
                       vec4 kernelSlot12, vec4 kernelSlot13, vec4 kernelSlot14, vec4 kernelSlot15,
                       vec4 kernelSlot16, float offset /*@in*/) {
  // The table is symmetric, so the offset is folded first: the inline loop passes the signed tap
  // index (j - radius), the linked call already passes abs(tap). The parameter is a float so a
  // runtime value (the IR loop counter) can flow through it; the int fold happens here.
  int kernelOffset = abs(int(offset));
  int kernelSlot = kernelOffset / 4;
  vec4 kernelValues = kernelSlot0;
  if (kernelSlot == 0) { kernelValues = kernelSlot0; }
  if (kernelSlot == 1) { kernelValues = kernelSlot1; }
  if (kernelSlot == 2) { kernelValues = kernelSlot2; }
  if (kernelSlot == 3) { kernelValues = kernelSlot3; }
  if (kernelSlot == 4) { kernelValues = kernelSlot4; }
  if (kernelSlot == 5) { kernelValues = kernelSlot5; }
  if (kernelSlot == 6) { kernelValues = kernelSlot6; }
  if (kernelSlot == 7) { kernelValues = kernelSlot7; }
  if (kernelSlot == 8) { kernelValues = kernelSlot8; }
  if (kernelSlot == 9) { kernelValues = kernelSlot9; }
  if (kernelSlot == 10) { kernelValues = kernelSlot10; }
  if (kernelSlot == 11) { kernelValues = kernelSlot11; }
  if (kernelSlot == 12) { kernelValues = kernelSlot12; }
  if (kernelSlot == 13) { kernelValues = kernelSlot13; }
  if (kernelSlot == 14) { kernelValues = kernelSlot14; }
  if (kernelSlot == 15) { kernelValues = kernelSlot15; }
  if (kernelSlot == 16) { kernelValues = kernelSlot16; }
  int kernelLane = kernelOffset - kernelSlot * 4;
  float weight = kernelValues.x;
  if (kernelLane == 1) { weight = kernelValues.y; }
  if (kernelLane == 2) { weight = kernelValues.z; }
  if (kernelLane == 3) { weight = kernelValues.w; }
  return weight;
}
