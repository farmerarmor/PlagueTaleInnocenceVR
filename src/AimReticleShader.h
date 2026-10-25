#pragma once
inline constexpr char AimReticleShaderCode[]=R"hlsl(
cbuffer Global : register(b0) {float4 c[90];};
cbuffer AimCamera : register(b13) {row_major float4x4 aimToEye;};
struct Input {float4 position:POSITION;float4 color:COLOR;float2 uv:TEXCOORD0;};
struct Output {float4 position:SV_Position;float4 previous:PREVIOUS_POSITION;
 float4 color:COLOR;float3 uv:TEXCOORD0;float instanceColor:INSTANCE_COLOR;
 uint2 indexes:INSTANCE_INDEXES;};
Output main(Input v) {
 Output o=(Output)0;
 float4 nativePoint=float4(v.position.xyz,1);
 float4 clip=mul(nativePoint,aimToEye);
 // Keep the native overlay's depth/layering; replace only its view/lens.
 float nativeW=dot(nativePoint,c[52]);
 float nativeZ=dot(nativePoint,c[51]);
 o.position=float4(clip.xy,nativeZ/max(abs(nativeW),1e-7)*clip.w,clip.w);
 o.color=v.color;o.uv.xy=v.uv;o.instanceColor=1;
 return o;
}
)hlsl";

// Captured secondary aim meshes, including their camera-facing outline variant.
// Keep the original material interpolants and object/UV animation. Only the
// overlay camera's final projection is replaced; world materials fall through.
inline constexpr char AimIndicatorShaderCode[]=R"hlsl(
cbuffer Global : register(b0) {float4 c[90];};
#ifdef BILLBOARD
cbuffer Light : register(b1) {float4 light[3];};
cbuffer Object : register(b2) {float4 obj[32];};
#else
cbuffer Object : register(b1) {float4 obj[32];};
#endif
cbuffer AimCamera : register(b13) {row_major float4x4 aimToEye;};
struct Input {float4 position:POSITION;float4 tangent:TANGENT;float3 normal:NORMAL;
 float4 uv:TEXCOORD0;float4 color:TEXCOORD8;};
struct Output {float4 position:SV_Position;float4 previous:PREVIOUS_POSITION;
 float4 color:COLOR;float4 uv:TEXCOORD0;float3 normal:TEXSPACE_NORMAL;
 float instanceColor:INSTANCE_COLOR;float3 texPosition:TEXSPACE_POS;
 uint2 indexes:INSTANCE_INDEXES;};
Output main(Input v) {
 Output o=(Output)0;
#ifdef BILLBOARD
 float4 localPosition=float4(v.position.xyz,1);
 float4 translated=float4(dot(localPosition,obj[0]),dot(localPosition,obj[1]),dot(localPosition,obj[2]),1);
 float3 expansion=length(obj[0].xyz)*(v.uv.w*c[3].xyz+v.uv.z*c[4].xyz);
 o.texPosition=translated.xyz+c[35].xyz+expansion;
 float4 relativePosition=float4(o.texPosition-c[35].xyz,1);
 float4 viewPosition=float4(dot(relativePosition,c[31]),dot(relativePosition,c[32]),dot(relativePosition,c[33]),1);
 float3 previous=float3(dot(localPosition,obj[4]),dot(localPosition,obj[5]),dot(localPosition,obj[6]));
 previous=previous+c[40].xyz+expansion-c[40].xyz;
 float4 previousPosition=float4(previous,1);
 o.color=float4(v.color.xyz,1);o.normal=light[0].xyz;
#else
 float4 translated=float4(dot(v.position,obj[0]),dot(v.position,obj[1]),dot(v.position,obj[2]),v.position.w);
 float4 viewPosition=float4(dot(translated,c[31]),dot(translated,c[32]),dot(translated,c[33]),v.position.w);
 o.texPosition=translated.xyz+c[35].xyz;
 float4 previousPosition=float4(dot(v.position,obj[4]),dot(v.position,obj[5]),dot(v.position,obj[6]),v.position.w);
 o.color=v.color;o.uv.zw=v.uv.zw;
 o.normal=float3(dot(v.normal,obj[8].xyz),dot(v.normal,obj[9].xyz),dot(v.normal,obj[10].xyz));
#endif
 float4 nativeClip=float4(dot(viewPosition,c[49]),dot(viewPosition,c[50]),dot(viewPosition,c[51]),dot(viewPosition,c[52]));
 o.position=nativeClip;
 bool overlay=all(abs(c[35])<1e-6) && all(abs(c[31]-float4(-1,0,0,0))<1e-6)
  && all(abs(c[32]-float4(0,1,0,0))<1e-6) && all(abs(c[33]-float4(0,0,1,0))<1e-6);
 if(overlay){float4 clip=mul(viewPosition,aimToEye);o.position=float4(clip.xy,nativeClip.z/max(abs(nativeClip.w),1e-7)*clip.w,clip.w);}
 o.previous=float4(dot(previousPosition,c[41]),dot(previousPosition,c[42]),dot(previousPosition,c[43]),dot(previousPosition,c[44]));
 float3 uv=float3(v.uv.xy,1);o.uv.xy=float2(dot(uv,obj[30].xyz),dot(uv,obj[31].xyz));
 return o;
}
)hlsl";
