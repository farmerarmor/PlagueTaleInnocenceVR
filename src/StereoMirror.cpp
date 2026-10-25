#include "StereoMirror.h"
#include <d3dcompiler.h>
using Microsoft::WRL::ComPtr;
bool StereoMirror::Initialize(ID3D11Device* d){
 if(device.Get()==d && vertex && pixel)return true;
 device=d;vertex.Reset();pixel.Reset();
 const char* vs="struct O{float4 p:SV_POSITION;float2 uv:TEXCOORD;};O main(uint i:SV_VertexID){O o;o.uv=float2((i<<1)&2,i&2);o.p=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}";
 const char* ps="Texture2D l:register(t0);Texture2D r:register(t1);SamplerState s:register(s0);float4 main(float4 p:SV_POSITION,float2 uv:TEXCOORD):SV_TARGET{return uv.x<0.5?l.SampleLevel(s,float2(uv.x*2,uv.y),0):r.SampleLevel(s,float2(uv.x*2-1,uv.y),0);}";
 ComPtr<ID3DBlob> v,p,e;
 if(FAILED(D3DCompile(vs,strlen(vs),nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&v,&e)) || FAILED(D3DCompile(ps,strlen(ps),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&p,&e)))return false;
 if(FAILED(d->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vertex)) || FAILED(d->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&pixel)))return false;
 D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
 D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
 D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=FALSE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;dd.DepthFunc=D3D11_COMPARISON_ALWAYS;
 return SUCCEEDED(d->CreateSamplerState(&sd,&sampler)) && SUCCEEDED(d->CreateRasterizerState(&rd,&raster)) && SUCCEEDED(d->CreateDepthStencilState(&dd,&depth));
}
bool StereoMirror::Draw(IDXGISwapChain* swap,ID3D11Texture2D* left,ID3D11Texture2D* right){
 if(!left || !right)return false;
 ComPtr<ID3D11Device> d;swap->GetDevice(IID_PPV_ARGS(&d));if(!d || !Initialize(d.Get()))return false;
 ComPtr<ID3D11Texture2D> back;ComPtr<ID3D11RenderTargetView> target;ComPtr<ID3D11ShaderResourceView> eyes[2];
 if(FAILED(swap->GetBuffer(0,IID_PPV_ARGS(&back))) || FAILED(d->CreateRenderTargetView(back.Get(),nullptr,&target)) || FAILED(d->CreateShaderResourceView(left,nullptr,&eyes[0])) || FAILED(d->CreateShaderResourceView(right,nullptr,&eyes[1])))return false;
 ComPtr<ID3D11DeviceContext> c;d->GetImmediateContext(&c);
 ComPtr<ID3D11RenderTargetView> oldTargets[8];ID3D11RenderTargetView* targetPtrs[8]{};ComPtr<ID3D11DepthStencilView> oldDSV;
 c->OMGetRenderTargets(8,targetPtrs,&oldDSV);for(int i=0;i<8;++i)oldTargets[i].Attach(targetPtrs[i]);
 ComPtr<ID3D11VertexShader> oldVS;ComPtr<ID3D11PixelShader> oldPS;ComPtr<ID3D11GeometryShader> oldGS;ComPtr<ID3D11HullShader> oldHS;ComPtr<ID3D11DomainShader> oldDS;
 c->VSGetShader(&oldVS,nullptr,nullptr);c->PSGetShader(&oldPS,nullptr,nullptr);c->GSGetShader(&oldGS,nullptr,nullptr);c->HSGetShader(&oldHS,nullptr,nullptr);c->DSGetShader(&oldDS,nullptr,nullptr);
 ComPtr<ID3D11InputLayout> oldLayout;c->IAGetInputLayout(&oldLayout);D3D11_PRIMITIVE_TOPOLOGY oldTopology;c->IAGetPrimitiveTopology(&oldTopology);
 ComPtr<ID3D11ShaderResourceView> oldViews[2];ID3D11ShaderResourceView* viewPtrs[2]{};c->PSGetShaderResources(0,2,viewPtrs);for(int i=0;i<2;++i)oldViews[i].Attach(viewPtrs[i]);
 ComPtr<ID3D11SamplerState> oldSampler;c->PSGetSamplers(0,1,&oldSampler);
 ComPtr<ID3D11RasterizerState> oldRaster;c->RSGetState(&oldRaster);D3D11_VIEWPORT oldViewport[16]{};UINT viewportCount=16;c->RSGetViewports(&viewportCount,oldViewport);
 ComPtr<ID3D11BlendState> oldBlend;FLOAT blendFactor[4]{};UINT sampleMask{};c->OMGetBlendState(&oldBlend,blendFactor,&sampleMask);
 ComPtr<ID3D11DepthStencilState> oldDepth;UINT stencilRef{};c->OMGetDepthStencilState(&oldDepth,&stencilRef);
 auto* t=target.Get();c->OMSetRenderTargets(1,&t,nullptr);c->OMSetBlendState(nullptr,nullptr,~0u);c->OMSetDepthStencilState(depth.Get(),0);c->RSSetState(raster.Get());
 D3D11_TEXTURE2D_DESC desc{};back->GetDesc(&desc);D3D11_VIEWPORT viewport{0,0,float(desc.Width),float(desc.Height),0,1};c->RSSetViewports(1,&viewport);
 c->IASetInputLayout(nullptr);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->VSSetShader(vertex.Get(),nullptr,0);c->PSSetShader(pixel.Get(),nullptr,0);c->GSSetShader(nullptr,nullptr,0);c->HSSetShader(nullptr,nullptr,0);c->DSSetShader(nullptr,nullptr,0);
 ID3D11ShaderResourceView* eyeViews[]={eyes[0].Get(),eyes[1].Get()};c->PSSetShaderResources(0,2,eyeViews);auto* s=sampler.Get();c->PSSetSamplers(0,1,&s);c->Draw(3,0);
 c->PSSetShaderResources(0,2,viewPtrs);s=oldSampler.Get();c->PSSetSamplers(0,1,&s);c->VSSetShader(oldVS.Get(),nullptr,0);c->PSSetShader(oldPS.Get(),nullptr,0);c->GSSetShader(oldGS.Get(),nullptr,0);c->HSSetShader(oldHS.Get(),nullptr,0);c->DSSetShader(oldDS.Get(),nullptr,0);
 c->IASetInputLayout(oldLayout.Get());c->IASetPrimitiveTopology(oldTopology);c->RSSetState(oldRaster.Get());c->RSSetViewports(viewportCount,oldViewport);c->OMSetBlendState(oldBlend.Get(),blendFactor,sampleMask);c->OMSetDepthStencilState(oldDepth.Get(),stencilRef);c->OMSetRenderTargets(8,targetPtrs,oldDSV.Get());
 return true;
}
