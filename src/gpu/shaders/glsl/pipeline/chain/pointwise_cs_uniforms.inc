// The program-level color-space block: one block per program (at most one ColorSpaceXform
// operator per chain/tail/operator slot), the fields the shared PM_APPLY_COLOR_SPACE_XFORM math
// reads. Every kernel that can apply a ColorSpaceXform operator declares it: the chain
// interpreter, the YUV tail and the single-operator TexturedEffect kernel. Include inside
// FragmentUniformBlock.
int ChainCSFlags;
vec4 ChainSrcTF0;
vec4 ChainSrcTF1;
int ChainSrcTFType;
vec4 ChainSrcOOTF;
mat3 ChainColorXform;
vec4 ChainDstOOTF;
vec4 ChainDstTF0;
vec4 ChainDstTF1;
int ChainDstTFType;
