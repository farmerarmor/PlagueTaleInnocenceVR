#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>

// The conversion touches only CS b0-b1/t0-t2/u0-u1. Unbind native outputs
// explicitly while reading their resources; preserve append counters, constant
// buffer ranges and predication without switching the game's context state.
class DlssComputeState {
 template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
 ID3D11DeviceContext1* context;
 Ptr<ID3D11ComputeShader> shader;
 Ptr<ID3D11ClassInstance> instances[256];UINT instanceCount=256;
 Ptr<ID3D11Buffer> constants[2];UINT first[2]{},counts[2]{};
 Ptr<ID3D11ShaderResourceView> resources[3];
 Ptr<ID3D11UnorderedAccessView> computeOutputs[8],pixelOutputs[8];
 Ptr<ID3D11RenderTargetView> targets[8];Ptr<ID3D11DepthStencilView> depth;
 Ptr<ID3D11Predicate> predicate;BOOL predicateValue{};
public:
 explicit DlssComputeState(ID3D11DeviceContext1* c):context(c){
  ID3D11ClassInstance* classInstances[256]{};context->CSGetShader(&shader,classInstances,&instanceCount);
  for(UINT i=0;i<instanceCount;++i)instances[i].Attach(classInstances[i]);
  ID3D11Buffer* buffers[2]{};context->CSGetConstantBuffers1(0,2,buffers,first,counts);for(UINT i=0;i<2;++i)constants[i].Attach(buffers[i]);
  ID3D11ShaderResourceView* inputs[3]{};context->CSGetShaderResources(0,3,inputs);for(UINT i=0;i<3;++i)resources[i].Attach(inputs[i]);
  ID3D11UnorderedAccessView* cs[8]{},*ps[8]{};ID3D11RenderTargetView* rt[8]{};context->CSGetUnorderedAccessViews(0,8,cs);context->OMGetRenderTargetsAndUnorderedAccessViews(8,rt,&depth,0,8,ps);
  for(UINT i=0;i<8;++i){computeOutputs[i].Attach(cs[i]);pixelOutputs[i].Attach(ps[i]);targets[i].Attach(rt[i]);}
  context->GetPredication(&predicate,&predicateValue);context->SetPredication(nullptr,FALSE);
  ID3D11UnorderedAccessView* none[8]{};context->CSSetUnorderedAccessViews(0,8,none,nullptr);context->OMSetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,8,none,nullptr);
 }
 ~DlssComputeState(){
  ID3D11UnorderedAccessView* none[8]{};ID3D11ShaderResourceView* empty[3]{};context->CSSetUnorderedAccessViews(0,8,none,nullptr);context->CSSetShaderResources(0,3,empty);
  ID3D11RenderTargetView* rt[8]{};ID3D11UnorderedAccessView* ps[8]{},*cs[8]{};UINT rtCount=0,keep[8];
  for(UINT i=0;i<8;++i){rt[i]=targets[i].Get();if(rt[i])rtCount=i+1;ps[i]=pixelOutputs[i].Get();cs[i]=computeOutputs[i].Get();keep[i]=UINT(-1);}
  context->OMSetRenderTargetsAndUnorderedAccessViews(rtCount,rt,depth.Get(),rtCount,8-rtCount,ps+rtCount,keep+rtCount);
  context->CSSetUnorderedAccessViews(0,8,cs,keep);
  ID3D11Buffer* buffers[2]={constants[0].Get(),constants[1].Get()};context->CSSetConstantBuffers1(0,2,buffers,first,counts);
  ID3D11ShaderResourceView* inputs[3]={resources[0].Get(),resources[1].Get(),resources[2].Get()};context->CSSetShaderResources(0,3,inputs);
  ID3D11ClassInstance* classInstances[256]{};for(UINT i=0;i<instanceCount;++i)classInstances[i]=instances[i].Get();context->CSSetShader(shader.Get(),classInstances,instanceCount);
  context->SetPredication(predicate.Get(),predicateValue);
 }
 DlssComputeState(const DlssComputeState&)=delete;
 DlssComputeState& operator=(const DlssComputeState&)=delete;
};
