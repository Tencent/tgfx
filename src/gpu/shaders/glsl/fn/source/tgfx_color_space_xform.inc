// The FP-tree entry for the color-space transform: the chain function's math with every input as a
// call argument, so a whole-program IR call can reach it. The body stays a single forward, so the
// transfer and gamut math keeps exactly one implementation.
//@function tgfxColorSpaceXform
//@op ColorSpaceXform
//@frag
vec4 tgfxColorSpaceXform(vec4 color /*@in*/, int flags /*@in*/, vec4 srcTF0 /*@in*/,
                         vec4 srcTF1 /*@in*/, int srcTFType /*@in*/, vec4 srcOOTF /*@in*/,
                         mat3 colorXform /*@in*/, vec4 dstOOTF /*@in*/, vec4 dstTF0 /*@in*/,
                         vec4 dstTF1 /*@in*/, int dstTFType /*@in*/) {
  return chainApplyColorSpaceXform(color, flags, srcTF0, srcTF1, srcTFType, srcOOTF, colorXform,
                                   dstOOTF, dstTF0, dstTF1, dstTFType);
}
