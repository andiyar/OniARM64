// metal_shaders.h — MSL source for the M1 passthrough pipeline, compiled at
// runtime via newLibraryWithSource (build-time .metallib is an M5 option).
#ifndef METAL_SHADERS_H
#define METAL_SHADERS_H

static const char *kMetalShaderSource =
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"\n"
"// Must match MetalScreenVertex in metal_internal.h (28 bytes).\n"
"struct VSIn {\n"
"    packed_float3 pos;\n"
"    packed_float3 uvw;     // (u*invW, v*invW, invW); w==1 for sprites\n"
"    uchar4        color;\n"
"};\n"
"\n"
"struct VSOut {\n"
"    float4 position [[position]];\n"
"    float3 uvw;\n"
"    float4 color;\n"
"};\n"
"\n"
"vertex VSOut oni_vertex(const device VSIn *verts [[buffer(0)]],\n"
"                        constant float2 &screen  [[buffer(1)]],\n"
"                        uint vid                 [[vertex_id]])\n"
"{\n"
"    VSIn v = verts[vid];\n"
"    VSOut out;\n"
"    // glOrtho(0, w, h, 0, 0, 1) + ZCOORD double-negation == this mapping:\n"
"    // x: 0..w -> -1..1, y: 0..h -> 1..-1 (flip), z: 0..1 passthrough.\n"
"    out.position = float4(v.pos.x * (2.0f / screen.x) - 1.0f,\n"
"                          1.0f - v.pos.y * (2.0f / screen.y),\n"
"                          v.pos.z, 1.0f);\n"
"    out.uvw   = float3(v.uvw);\n"
"    out.color = float4(v.color) * (1.0f / 255.0f);\n"
"    return out;\n"
"}\n"
"\n"
"fragment float4 oni_fragment(VSOut in                  [[stage_in]],\n"
"                             texture2d<float> tex0     [[texture(0)]],\n"
"                             sampler          samp0    [[sampler(0)]])\n"
"{\n"
"    // Hyperbolic texturing: linear-in-screen-space interpolation of\n"
"    // (u/w, v/w, 1/w), divided per fragment == GL's glTexCoord4f q path.\n"
"    float2 uv = in.uvw.xy / in.uvw.z;\n"
"    return tex0.sample(samp0, uv) * in.color;\n"
"}\n";

#endif // METAL_SHADERS_H
