#pragma once
// Native shader 7d6c9892c70a07a6: same CB/SRV/UAV layout, 16x16 tiles,
// 64-thread groups, candidate/pixel overlap tests and 256-word tile records.
// Only the four side planes use the complete centred-eye projection.
inline constexpr const char* LightListShaderCode=R"hlsl(
cbuffer Global : register(b0) {float4 c[90];};
cbuffer User : register(b1) {float4 u[58];};
cbuffer Lights : register(b2) {float4 lights[3841];};
Texture2D<float4> sZBuffer : register(t1);
RWStructuredBuffer<uint> gLightList : register(u0);
#ifndef NO_TILE_CULL
groupshared uint minDepth,maxDepth,candidateCount,candidates[256];
groupshared uint masks[2],selectedCount,selected[64];
#endif
[numthreads(64,1,1)]
void main(uint thread:SV_GroupIndex,uint3 group:SV_GroupID) {
 uint2 size=(uint2)u[1].xy,grid=(uint2)u[0].xy;
 if(group.x>=grid.x || group.y>=grid.y)return;
 uint lightCount=min(asuint(lights[3840].x),256u);
 uint base=(group.y*grid.x+group.x)*256u;
#ifdef NO_TILE_CULL
 if(thread==0){uint count=min(lightCount,64u);gLightList[base]=count;for(uint i=0;i<count;++i)gLightList[base+1+i]=i;}
#else
 if(thread==0){minDepth=0x7f7fffff;maxDepth=0;candidateCount=selectedCount=0;}
 if(thread<2)masks[thread]=0;
 GroupMemoryBarrierWithGroupSync();
 uint2 first=group.xy*16+uint2((thread&3)*4,thread>>2);
 float3 points[4];float localMin=3.402823466e+38,localMax=0;
 [unroll]for(uint sample=0;sample<4;++sample){
  uint2 pixel=min(first+uint2(sample,0),size-1);
  float depth=sZBuffer.Load(int3(pixel,0)).x;
  float z=c[51].w/(depth-c[51].z);
  localMin=min(localMin,z);localMax=max(localMax,z);
  float4 clip=float4((float2(pixel)+0.5)*u[1].zw*float2(2,-2)+float2(-1,1),depth,1);
  float4 position=float4(dot(clip,c[53]),dot(clip,c[54]),dot(clip,c[55]),dot(clip,c[56]));
  points[sample]=position.xyz/position.w;
 }
 InterlockedMin(minDepth,asuint(max(localMin,0)));
 InterlockedMax(maxDepth,asuint(max(localMax,0)));
 GroupMemoryBarrierWithGroupSync();
 float2 lower=float2(group.xy*16)/size,upper=float2(min((group.xy+1)*16,size))/size;
 float xmin=lower.x*2-1,xmax=upper.x*2-1,ymax=1-lower.y*2,ymin=1-upper.y*2;
 float4 planes[4]={c[49]-xmin*c[52],xmax*c[52]-c[49],ymax*c[52]-c[50],c[50]-ymin*c[52]};
 for(uint light=thread;light<lightCount;light+=64){
  float4 sphere=lights[3584+light];bool inside=true;
  [unroll]for(uint side=0;side<4;++side)inside=inside && dot(planes[side],float4(sphere.xyz,1))>=-sphere.w*length(planes[side].xyz);
  inside=inside && sphere.z+ sphere.w>=asfloat(minDepth) && sphere.z-sphere.w<=asfloat(maxDepth);
  if(inside){uint index;InterlockedAdd(candidateCount,1,index);candidates[index]=light;}
 }
 GroupMemoryBarrierWithGroupSync();
 uint count=min(candidateCount,64u);uint localMasks[2]={0,0};
 for(uint index=0;index<count;++index){
  float4 sphere=lights[3584+candidates[index]];bool overlaps=false;
  [unroll]for(uint sample=0;sample<4;++sample){float3 delta=points[sample]-sphere.xyz;overlaps=overlaps || length(delta)<sphere.w;}
  if(overlaps)localMasks[index>>5]|=1u<<(index&31);
 }
 InterlockedOr(masks[0],localMasks[0]);InterlockedOr(masks[1],localMasks[1]);
 GroupMemoryBarrierWithGroupSync();
 if(thread<count && (masks[thread>>5]&(1u<<(thread&31)))){uint index;InterlockedAdd(selectedCount,1,index);selected[index]=candidates[thread];}
 GroupMemoryBarrierWithGroupSync();
 if(thread==0){gLightList[base]=selectedCount;for(uint index=0;index<selectedCount;++index)gLightList[base+1+index]=selected[index];}
#endif
}
)hlsl";
