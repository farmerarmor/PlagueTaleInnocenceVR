#include "GpuCapture.h"
#include "ShaderHunter.h"
#include <MinHook.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
using Microsoft::WRL::ComPtr;
static unsigned checks{};
static void require(bool good,const char* message){++checks;if(!good){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
static ComPtr<ID3DBlob> compile(const char* source,const char* profile){ComPtr<ID3DBlob> code,error;auto hr=D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"main",profile,0,0,&code,&error);if(FAILED(hr)&&error)fprintf(stderr,"%s\n",static_cast<const char*>(error->GetBufferPointer()));require(SUCCEEDED(hr),profile);return code;}
static std::string read(const std::filesystem::path& path){std::ifstream f(path);std::ostringstream s;s<<f.rdbuf();return s.str();}
int wmain(int argc,wchar_t** argv){
 require(argc==2,"diagnostic output directory supplied");std::filesystem::path dir=argv[1];std::filesystem::create_directories(dir);
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
 require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)),"hardware D3D11 device");
 require(level>=D3D_FEATURE_LEVEL_11_0,"all six stages supported");
 auto earlyCode=compile("float4 main(uint id:SV_VertexID):SV_Position{return float4(1,0,0,1);}","vs_5_0");ComPtr<ID3D11VertexShader> early;
 require(SUCCEEDED(device->CreateVertexShader(earlyCode->GetBufferPointer(),earlyCode->GetBufferSize(),nullptr,&early)),"create shader before hook installation");
 require(MH_Initialize()==MH_OK,"initialize hooks");
 require(GpuCapture::Install(device.Get(),context.Get(),dir,0),"install complete production hook set");require(MH_EnableHook(MH_ALL_HOOKS)==MH_OK,"enable hooks");
 auto v=compile("float4 main(uint id:SV_VertexID):SV_Position{return float4(0,0,0,1);}","vs_5_0");
 auto p=compile("float4 main():SV_Target{return float4(1,1,1,1);}","ps_5_0");
 auto g=compile("struct V{float4 p:SV_Position;};[maxvertexcount(3)] void main(triangle V input[3],inout TriangleStream<V> stream){stream.Append(input[0]);stream.Append(input[1]);stream.Append(input[2]);}","gs_5_0");
 auto h=compile("struct V{float4 p:SV_Position;};struct C{float e[3]:SV_TessFactor;float i:SV_InsideTessFactor;};C constants(InputPatch<V,3> p){C c;c.e[0]=c.e[1]=c.e[2]=1;c.i=1;return c;}[domain(\"tri\")][partitioning(\"integer\")][outputtopology(\"triangle_cw\")][outputcontrolpoints(3)][patchconstantfunc(\"constants\")]V main(InputPatch<V,3> p,uint id:SV_OutputControlPointID){return p[id];}","hs_5_0");
 auto d=compile("struct V{float4 p:SV_Position;};struct C{float e[3]:SV_TessFactor;float i:SV_InsideTessFactor;};[domain(\"tri\")]V main(C c,float3 uv:SV_DomainLocation,const OutputPatch<V,3> p){V v;v.p=p[0].p;return v;}","ds_5_0");
 auto k=compile("[numthreads(1,1,1)]void main(uint3 id:SV_DispatchThreadID){}","cs_5_0");
 ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11GeometryShader> gs,so;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;ComPtr<ID3D11ComputeShader> cs;
 require(SUCCEEDED(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs)),"create vertex");
 require(SUCCEEDED(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps)),"create pixel");
 require(SUCCEEDED(device->CreateGeometryShader(g->GetBufferPointer(),g->GetBufferSize(),nullptr,&gs)),"create geometry");
 D3D11_SO_DECLARATION_ENTRY entry{0,"SV_Position",0,0,4,0};UINT stride=16;
 require(SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(g->GetBufferPointer(),g->GetBufferSize(),&entry,1,&stride,1,0,nullptr,&so)),"create geometry stream output");
 require(SUCCEEDED(device->CreateHullShader(h->GetBufferPointer(),h->GetBufferSize(),nullptr,&hs)),"create hull");
 require(SUCCEEDED(device->CreateDomainShader(d->GetBufferPointer(),d->GetBufferSize(),nullptr,&ds)),"create domain");
 require(SUCCEEDED(device->CreateComputeShader(k->GetBufferPointer(),k->GetBufferSize(),nullptr,&cs)),"create compute");
 ComPtr<ID3D11DeviceContext> deferred;require(SUCCEEDED(device->CreateDeferredContext(0,&deferred)),"hook future deferred context");
 D3D11_BUFFER_DESC bd{};bd.ByteWidth=32;bd.Usage=D3D11_USAGE_DEFAULT;bd.MiscFlags=D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;UINT zeros[8]{};D3D11_SUBRESOURCE_DATA data{zeros,0,0};ComPtr<ID3D11Buffer> args;
 require(SUCCEEDED(device->CreateBuffer(&bd,&data,&args)),"indirect command arguments");
 for(uint64_t i=1;i<=120;++i)ShaderHunter::Begin(i,i,true);
 for(auto* c:{context.Get(),deferred.Get()}){
  c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);c->GSSetShader(gs.Get(),nullptr,0);c->HSSetShader(hs.Get(),nullptr,0);c->DSSetShader(ds.Get(),nullptr,0);c->CSSetShader(cs.Get(),nullptr,0);
  c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);ShaderHunter::Eye(0);
  c->Draw(0,0);c->DrawIndexed(0,0,0);c->DrawInstanced(0,0,0,0);c->DrawIndexedInstanced(0,0,0,0,0);c->DrawAuto();c->DrawInstancedIndirect(args.Get(),0);c->DrawIndexedInstancedIndirect(args.Get(),0);c->Dispatch(0,0,0);c->DispatchIndirect(args.Get(),0);
 }
 context->VSSetShader(early.Get(),nullptr,0);context->Draw(0,0);
 ShaderHunter::End();auto status=read(dir/L"shader-hunter-status.json");auto catalog=read(dir/L"shader-hunter-catalog.json");
 require(status.find("\"registeredByStage\":[1,2,1,1,1,1]")!=std::string::npos,"all six shader creation stages registered including early-object fallback (SO deduplicated by bytecode)");
 require(status.find("\"observedByStage\":[1,2,1,1,1,1]")!=std::string::npos,"all six stages observed at actual commands");
 require(status.find("\"commandsObserved\":19")!=std::string::npos,"all nine command forms on immediate and deferred contexts observed, plus early-shader draw");
 require(status.find("\"unavailableBytecode\":1")!=std::string::npos && catalog.find("\"identityKind\":\"object-token\"")!=std::string::npos,"pre-injection shader remains huntable with explicit token");
 require(catalog.find("\"left\":14")!=std::string::npos,"graphics shader counted on both context classes");
 require(catalog.find("\"left\":4")!=std::string::npos,"compute shader counted through direct and indirect commands on both context classes");
 // Validate the expanded capture using real MRT and pixel-UAV bindings, with
 // a nonzero texture-array slice. No draw or native-game shader is simulated.
 context->ClearState();
 auto texture=[&](DXGI_FORMAT format,UINT bind,UINT layers=1){D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=32;td.MipLevels=td.SampleDesc.Count=1;td.ArraySize=layers;td.Format=format;td.BindFlags=bind;ComPtr<ID3D11Texture2D> result;require(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&result)),"binding-capture texture");return result;};
 auto packed=texture(DXGI_FORMAT_R32_TYPELESS,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE,4),previous=texture(DXGI_FORMAT_R32_FLOAT,D3D11_BIND_RENDER_TARGET),z=texture(DXGI_FORMAT_R24G8_TYPELESS,D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE),color=texture(DXGI_FORMAT_R11G11B10_FLOAT,D3D11_BIND_RENDER_TARGET);
 D3D11_RENDER_TARGET_VIEW_DESC rd{};rd.Format=DXGI_FORMAT_R32_UINT;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;rd.Texture2DArray.FirstArraySlice=2;rd.Texture2DArray.ArraySize=1;ComPtr<ID3D11RenderTargetView> motionView,previousView,colorView;require(SUCCEEDED(device->CreateRenderTargetView(packed.Get(),&rd,&motionView)) && SUCCEEDED(device->CreateRenderTargetView(previous.Get(),nullptr,&previousView)) && SUCCEEDED(device->CreateRenderTargetView(color.Get(),nullptr,&colorView)),"MRT capture views");
 D3D11_DEPTH_STENCIL_VIEW_DESC zd{};zd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;zd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;ComPtr<ID3D11DepthStencilView> depth;require(SUCCEEDED(device->CreateDepthStencilView(z.Get(),&zd,&depth)),"depth capture view");
 // All RTVs must use matching view dimensions; a single-layer array view is
 // legal alongside a one-layer depth view on this test device.
 ID3D11RenderTargetView* rtvs[6]={nullptr,nullptr,nullptr,nullptr,motionView.Get(),previousView.Get()};context->OMSetRenderTargets(6,rtvs,depth.Get());GpuCapture::Begin(9001,1);GpuCapture::Eye(0);GpuCapture::RecordCommand(context.Get(),"mrt-layout",0,0);
 auto velocity=texture(DXGI_FORMAT_R32_UINT,D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE),prior=texture(DXGI_FORMAT_R32_FLOAT,D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE);ComPtr<ID3D11UnorderedAccessView> uv,pv;require(SUCCEEDED(device->CreateUnorderedAccessView(velocity.Get(),nullptr,&uv)) && SUCCEEDED(device->CreateUnorderedAccessView(prior.Get(),nullptr,&pv)),"pixel UAV capture views");
 context->OMSetRenderTargets(0,nullptr,nullptr);ID3D11RenderTargetView* scene=colorView.Get();ID3D11UnorderedAccessView* uavs[]={uv.Get(),pv.Get()};context->OMSetRenderTargetsAndUnorderedAccessViews(1,&scene,depth.Get(),1,2,uavs,nullptr);GpuCapture::RecordCommand(context.Get(),"pixel-uav-layout",0,0);GpuCapture::End(context.Get());context->Flush();
 for(unsigned attempt=0;attempt<100 && !std::filesystem::exists(dir/L"gpu-captures/frame-9001/trace.json");++attempt){GpuCapture::Flush();Sleep(10);}
 auto trace=read(dir/L"gpu-captures/frame-9001/trace.json");require(trace.find("\"role\":\"rtv\",\"slot\":4")!=std::string::npos && trace.find("\"slice\":2,\"layers\":1")!=std::string::npos,"capture records MRT slot and actual array slice");require(trace.find("\"role\":\"om-uav\",\"slot\":1")!=std::string::npos && trace.find("\"role\":\"om-uav\",\"slot\":2")!=std::string::npos,"capture records native pixel-UAV bindings");require(trace.find("\"role\":\"dsv\"")!=std::string::npos && trace.find("\"bindFlags\":")!=std::string::npos,"capture records depth and resource creation flags");
 require(SUCCEEDED(device->GetDeviceRemovedReason()),"device remains healthy");
 MH_DisableHook(MH_ALL_HOOKS);MH_Uninitialize();printf("PASS: %u shader coverage checks, all six stages and all nine command forms on immediate/deferred contexts.\n",checks);return 0;
}
