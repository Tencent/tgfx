#version 150
layout(std140) uniform VertexUniformBlock {
     vec4 tgfx_RTAdjust;
     mat3 CoordTransformMatrix_0_P0;
};

in vec2 aPosition;

out vec2 TransformedCoords_0_P0;

void main() {
    TransformedCoords_0_P0 = (CoordTransformMatrix_0_P0 * vec3(aPosition, 1)).xy;
    gl_Position = vec4(aPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0, 1);
}

