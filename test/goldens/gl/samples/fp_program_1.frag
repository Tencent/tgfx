#version 150
layout(std140) uniform FragmentUniformBlock {
     vec4 Color_P0;
     mat4 Matrix_P1;
     vec4 Vector_P1;
};

in float Coverage_P0;

out vec4 tgfx_FragColor;

vec4 pmApplyColorMatrix(vec4 color, mat4 matrix, vec4 vector) {
  color = vec4(color.rgb / max(color.a, 9.9999997473787516e-05), color.a);
  color = matrix * color + vector;
  color = clamp(color, 0.0, 1.0);
  color.rgb *= color.a;
  return color;
}


void main() {
    vec4 outputColor_P0;
    vec4 outputCoverage_P0;
    { // Processor0 : QuadPerEdgeAAGeometryProcessor
        outputCoverage_P0 = vec4(Coverage_P0);
        outputColor_P0 = Color_P0;
    }
    vec4 output_P1;
    { // Processor1 : ColorMatrixFragmentProcessor
        output_P1 = pmApplyColorMatrix(outputColor_P0, Matrix_P1, Vector_P1);
    }
    { // Processor2 : EmptyXferProcessor
        tgfx_FragColor = output_P1 * outputCoverage_P0;
    }
}

