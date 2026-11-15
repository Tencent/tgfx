// LocalMask vertex hook: the mask's coordinate through its own transform, from the same source
// coordinate as the color transform. The expressions are bit-exact by contract.
void LocalMask_vertex(inout Vertex v) {
  LocalMask_vCoord = LocalMask_CoordMatrix * vec3(v.localCoord, 1.0);
}
