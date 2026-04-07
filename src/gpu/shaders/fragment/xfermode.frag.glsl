// Copyright (C) 2026 Tencent. All rights reserved.
// xfermode.frag.glsl - XfermodeFragmentProcessor modular shader function.
// Composes two child FP colors using a blend mode.
//
// This is a container FP: it calls child FPs and blends their results.
// The actual blend mode dispatch (AppendMode) is performed by the builder at compile time.
//
// Three child modes:
//   TwoChild - both src and dst are child FPs, input alpha modulates result
//   DstChild - input is src, child is dst
//   SrcChild - child is src, input is dst
//
// Required macros:
//   TGFX_XFP_CHILD_MODE - 0=TwoChild, 1=DstChild, 2=SrcChild
//
// Note: The blend mode operation itself (AppendMode) is expanded inline by the builder
// since it requires compile-time selection of the blend formula. This .glsl file documents
// the container structure for reference.

#ifndef TGFX_XFP_CHILD_MODE
#define TGFX_XFP_CHILD_MODE 0
#endif

// Pseudo-code for the three modes:
//
// TwoChild mode (TGFX_XFP_CHILD_MODE == 0):
//   vec4 opaqueInput = vec4(inputColor.rgb, 1.0);
//   vec4 srcColor = childFP0(opaqueInput);
//   vec4 dstColor = childFP1(opaqueInput);
//   output = AppendMode(srcColor, vec4(1.0), dstColor, blendMode);
//   output *= inputColor.a;
//
// DstChild mode (TGFX_XFP_CHILD_MODE == 1):
//   vec4 childColor = childFP0(vec4(1.0));
//   output = AppendMode(inputColor, vec4(1.0), childColor, blendMode);
//
// SrcChild mode (TGFX_XFP_CHILD_MODE == 2):
//   vec4 childColor = childFP0(vec4(1.0));
//   output = AppendMode(childColor, vec4(1.0), inputColor, blendMode);
