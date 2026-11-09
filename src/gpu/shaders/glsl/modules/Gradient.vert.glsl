// Gradient vertex hook: the gradient layout's own transform applied to the geometry's local
// coordinate. The expression is verbatim from the former kernels/shape_instanced_texture_coverage
// .vert (the gradient color FP precedes the coverage FP, so it owned the first transform slot).
void Gradient_vertex(inout Vertex v) {
  Gradient_vCoord = (Gradient_CoordMatrix * vec3(v.localCoord, 1.0)).xy;
}
