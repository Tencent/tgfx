// Pure color-space transform fragment: the CS stage flags and the transform's records arrive as
// explicit arguments — one spelling shared by the interpreter's variant text, the GL assembly and
// the Vulkan body — and the transfer functions resolve through the shared transfer module.
#ifndef TGFX_OPS_COLOR_SPACE_XFORM_INCLUDED
#define TGFX_OPS_COLOR_SPACE_XFORM_INCLUDED

#include "operator/transfer_function.inc"
#include "fn/entry/color_space_xform.inc"

#endif  // TGFX_OPS_COLOR_SPACE_XFORM_INCLUDED
