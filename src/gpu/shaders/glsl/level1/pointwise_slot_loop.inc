// The bounded pointwise tail of a multi-slot kernel: applies operator slots 0..PointwiseSlotCount-1
// in order. Include after pointwise_op.inc (and its slot-array unbind) in a kernel declaring
// PointwiseSlotCount.
//
// With a dst-reading XferProcessor the loop runs one pass longer when the XferProcessor mode is
// an advanced one; that pass blends the tail's result with xpDst at applyPointwiseOp's OP_BLEND
// site and returns it through xpBlended (xp_blend_pass.inc), so the kernel inlines xpBlendColors
// once. The pass reads slot fields at the last slot index, which keeps the uniform array reads in
// range; their values are ignored.
#if HAS_XP >= 1
vec4 applyPointwiseSlots(vec4 color, vec4 xpDst, out vec4 xpBlended) {
  vec4 result = color;
  xpBlended = vec4(0.0);
  int passCount = PointwiseSlotCount + (xpNeedsBlendPass() ? 1 : 0);
  int lastSlot = max(PointwiseSlotCount - 1, 0);
  for (int i = 0; i < passCount; ++i) {
    bool xpPass = i == PointwiseSlotCount;
    vec4 value = applyPointwiseOp(min(i, lastSlot), result, xpPass, xpDst);
    if (xpPass) {
      xpBlended = value;
    } else {
      result = value;
    }
  }
  return result;
}
#else
vec4 applyPointwiseSlots(vec4 color) {
  vec4 result = color;
  for (int i = 0; i < PointwiseSlotCount; ++i) {
    result = applyPointwiseOp(i, result);
  }
  return result;
}
#endif
