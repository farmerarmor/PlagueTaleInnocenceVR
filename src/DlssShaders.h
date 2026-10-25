#pragma once
inline constexpr char DlssInputsShader[]=R"hlsl(
Texture2D<uint> packedMotion:register(t0);
Texture2D<float> nativeDepth:register(t1);
Texture2D<float> previousDepth:register(t2);
RWTexture2D<float2> motion:register(u0);
RWTexture2D<float> depth:register(u1);
cbuffer Global:register(b0){float4 c[90];};
cbuffer Dimensions:register(b1){uint2 size;float2 padding;};
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=size))return;
 uint packed=packedMotion.Load(int3(id.xy,0));
 float2 uvDelta=float2(f16tof32(packed&65535),f16tof32(packed>>16));
 // Native material writers: current raster UV - previous clip UV - current
 // native jitter UV. DLSS expects previous minus current, in input pixels.
 // Opaque MRT writers remove current jitter and write previous NDC depth.
 // The native unlit/sky UAV writer leaves jitter in its vectors and writes Z=0.
 float2 jitterUv=previousDepth.Load(int3(id.xy,0))!=0?c[89].xy*float2(.5,-.5):0;
 motion[id.xy]=-(uvDelta+jitterUv)*size;
 depth[id.xy]=nativeDepth.Load(int3(id.xy,0));
}
)hlsl";
// The native upscale quad already covers the full output target. Copy the
// reconstructed pixel exactly; do not run its low-resolution spatial filter.
inline constexpr char DlssOutputShader[]=R"hlsl(
Texture2D<float4> reconstructed:register(t0);
float4 main(float4 position:SV_Position):SV_Target {
 return reconstructed.Load(int3(int2(position.xy),0));
}
)hlsl";
