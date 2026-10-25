#pragma once
// Captured inventory object and silhouette vertices. Keep native material,
// animation and lighting outputs; only move the overlay camera onto the UI plane.
inline constexpr char InventoryUIShaderCode[]=R"hlsl(
cbuffer Global : register(b0) {float4 c[90];};
cbuffer Object : register(b1) {float4 obj[32];};
cbuffer VRPanel : register(b13) {row_major float4x4 panel;};
struct Input {float4 position:POSITION;float4 tangent:TANGENT;float3 normal:NORMAL;
 float4 uv:TEXCOORD0;float4 color:TEXCOORD8;};
struct Output {float4 position:SV_Position;float4 previous:PREVIOUS_POSITION;
 float4 color:COLOR;float4 uv:TEXCOORD0;
#ifdef LIT
 float4 tangent:TEXSPACE_MAT0;float4 bitangent:TEXSPACE_MAT1;float4 normal:TEXSPACE_MAT2;
#else
 float3 normal:TEXSPACE_NORMAL;
#endif
 float instanceColor:INSTANCE_COLOR;
#ifndef LIT
 float3 texPosition:TEXSPACE_POS;
#endif
 uint2 indexes:INSTANCE_INDEXES;};
Output main(Input v) {
 Output o=(Output)0;
 float4 translated=float4(dot(v.position,obj[0]),dot(v.position,obj[1]),dot(v.position,obj[2]),v.position.w);
 float4 view=float4(dot(translated,c[31]),dot(translated,c[32]),dot(translated,c[33]),v.position.w);
 float3 world=translated.xyz+c[35].xyz;
 float4 nativeClip=float4(dot(view,c[49]),dot(view,c[50]),dot(view,c[51]),dot(view,c[52]));
 o.position=nativeClip;
 // The same mesh shader can render world geometry. Only the captured identity
 // overlay camera has no world position and these three exact view rows.
 bool overlay=all(abs(c[35])<1e-6) && all(abs(c[31]-float4(-1,0,0,0))<1e-6)
  && all(abs(c[32]-float4(0,1,0,0))<1e-6) && all(abs(c[33]-float4(0,0,1,0))<1e-6);
 if(overlay && nativeClip.w>1e-6){
  // Native inventory stereo adds opposite horizontal offsets to Z and W.
  // Recover the centered UI coordinates, then use the same 16:9 plane as text.
  float2 logical=float2(dot(view.xy,c[49].xy),dot(view.xy,c[50].xy))/nativeClip.w;
  float4 clip=mul(float4(logical,0,1),panel);
  o.position=float4(clip.xy,nativeClip.z/nativeClip.w*clip.w,clip.w);
 }
 float4 previous=float4(dot(v.position,obj[4]),dot(v.position,obj[5]),dot(v.position,obj[6]),v.position.w);
 o.previous=float4(dot(previous,c[41]),dot(previous,c[42]),dot(previous,c[43]),dot(previous,c[44]));
 o.color=v.color;o.uv.zw=v.uv.zw;
 float3 uv=float3(v.uv.xy,1);o.uv.xy=float2(dot(uv,obj[30].xyz),dot(uv,obj[31].xyz));
 float3 normal=float3(dot(v.normal,obj[8].xyz),dot(v.normal,obj[9].xyz),dot(v.normal,obj[10].xyz));
#ifdef LIT
 float3 tangent=float3(dot(v.tangent.xyz,obj[8].xyz),dot(v.tangent.xyz,obj[9].xyz),dot(v.tangent.xyz,obj[10].xyz));
 float3 bitangent=sign(v.tangent.w)*cross(v.normal,v.tangent.xyz);
 bitangent=float3(dot(bitangent,obj[8].xyz),dot(bitangent,obj[9].xyz),dot(bitangent,obj[10].xyz));
 o.tangent=float4(tangent,world.x);o.bitangent=float4(bitangent,world.y);o.normal=float4(normal,world.z);
#else
 o.normal=normal;o.texPosition=world;
#endif
 return o;
}
)hlsl";
