// Runtime clip uniform fields — include inside FragmentUniformBlock, after Rect/HasClip.
// These carry the rrect and local-space clip parameters for the runtime clip contract; the
// declared families' writers populate them from the draw's IR analytic term.
// LocalRect doubles as the local-space rect of the HasClip==3 path (same producer, same value).
#ifndef TGSL_RRECT_CLIP_UNIFORMS_INCLUDED
#define TGSL_RRECT_CLIP_UNIFORMS_INCLUDED

vec4 LocalRect;
// Per-corner radii in [TL, TR, BR, BL] order, consumed only by the HasClip==2 path.
vec4 RadiiX;
vec4 RadiiY;
// 1.0 = analytic AA ramp, 0.0 = hard edge (rrect path only).
float AntiAlias;
// Maps device coordinates to the clip's local space; identity for device-space clips.
mat3 DeviceToLocal;


#endif  // TGSL_RRECT_CLIP_UNIFORMS_INCLUDED
