// The leaf sampler dispatch: one statically bound sampler pick per instance, then the shared read
// swizzle. This is the text the legacy emitter generated for the chain's InstanceDispatchDecl
// (emit/StageEmitter.cpp EmitInstanceDispatch); the declared assembly spells it out because its
// assembler generates no instance dispatches.
#ifndef TGSL_POINTWISE_LEAF_DISPATCH_INCLUDED
#define TGSL_POINTWISE_LEAF_DISPATCH_INCLUDED

#if NTEX > 0
vec4 aotChainRead(int leafIndex, vec2 coord, int readSwizzle) {
  vec4 color;
  if (leafIndex == 0) {
    color = texture(TextureSampler_0, coord);
  } else if (leafIndex == 1) {
    color = texture(TextureSampler_1, coord);
  } else if (leafIndex == 2) {
    color = texture(TextureSampler_2, coord);
  } else {
    color = texture(TextureSampler_3, coord);
  }
  return aotReadSwizzle(color, readSwizzle);
}
#endif


#endif  // TGSL_POINTWISE_LEAF_DISPATCH_INCLUDED
