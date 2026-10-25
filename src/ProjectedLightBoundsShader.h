#pragma once
inline constexpr const char* ProjectedLightBoundsShaderCode=R"hlsl(
cbuffer EyeGlobal : register(b12) {float4 global[90];};
cbuffer Omni : register(b13) {float4 light[70];};
struct Input {float4 position:POSITION;};
struct Output {float4 position:SV_Position;float4 uv:TEXCOORD0;float color:INSTANCE_COLOR;uint2 indexes:INSTANCE_INDEXES;};
Output main(Input v){
 float2 low=-1,high=1;
#ifndef FULL_SCREEN
 low=1e20;high=-1e20;bool crossesEye=false;
 float3 center=light[0].xyz;float radius=abs(light[3].y);
 [unroll]for(uint i=0;i<8;++i){
  float3 corner=center+radius*float3((i&1)?1:-1,(i&2)?1:-1,(i&4)?1:-1);
  float4 p=float4(corner,1);
  float4 clip=float4(dot(p,global[49]),dot(p,global[50]),dot(p,global[51]),dot(p,global[52]));
  crossesEye=crossesEye || clip.w<=0.0001;
  float2 ndc=clip.xy/max(clip.w,0.0001);low=min(low,ndc);high=max(high,ndc);
 }
 if(crossesEye){low=-1;high=1;}else{low=max(low-0.002,-1);high=min(high+0.002,1);}
#endif
 // Native scale signs are both negative; retain winding and interpolate
 // UV plus clip XY from the corrected rectangle for depth reconstruction.
 float2 clip=(low+high)*0.5-v.position.xy*(high-low)*0.5;
 Output o;o.position=float4(clip,0,1);o.uv=float4(clip*float2(0.5,-0.5)+0.5,clip);o.color=0;o.indexes=0;return o;
}
)hlsl";
