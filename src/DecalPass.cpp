#include "DecalPass.h"
#include <wrl/client.h>
#include <fstream>
#include <atomic>
#include <array>
#include <algorithm>
using Microsoft::WRL::ComPtr;
namespace DecalPass {
namespace {
const GUID NativeTag={0x24637df3,0x1e47,0x4f09,{0xbc,0x35,0x3f,0xf1,0x0e,0x36,0xad,0xa3}};
std::filesystem::path directory;
thread_local bool active{},candidate{},trace{};
thread_local int eye=-1;
uint64_t frame{},tick{},counts[2]{},skipped[2]{};UINT mode{},eyeMask{};
std::atomic<bool> requested{};
struct Timing {ComPtr<ID3D11Query> begin,end,disjoint;bool pending{};uint64_t frame{},submitted{},draws{},run{};UINT mode{},eyeMask{};};
std::array<Timing,8> timings;Timing* current{};
struct Row {uint64_t frame{},draws{};UINT mode{};double gpuMs{};};std::vector<Row> rows;
uint64_t run{},start{},deadline{},timedFrames{},excluded{},dropped{},runDraws{};bool benchmarking{};
void log(const std::string& value){std::ofstream f(directory/L"decal-pass.log",std::ios::app);f<<value<<'\n';}
bool tagged(ID3D11DeviceChild* shader){UINT value{},size=4;return shader && SUCCEEDED(shader->GetPrivateData(NativeTag,&size,&value)) && value==1;}
void report(const char* reason){
 std::vector<double> times;for(auto& r:rows)times.push_back(r.gpuMs);std::sort(times.begin(),times.end());double median=times.empty()?0:times[times.size()/2];
 std::ofstream f(directory/L"decal-benchmarks.jsonl",std::ios::app);f<<"{\"run\":"<<run<<",\"reason\":\""<<reason<<"\",\"scope\":\"whole native render interval, both eyes; GPU timestamps, not compositor FPS\",\"valid\":"<<(times.size()>=8?"true":"false")<<",\"samples\":"<<times.size()<<",\"medianGpuMs\":"<<median<<",\"trackedFrames\":"<<timedFrames<<",\"excludedFrames\":"<<excluded<<",\"droppedQueries\":"<<dropped<<",\"decalDraws\":"<<runDraws<<",\"rows\":[";
 for(size_t i=0;i<rows.size();++i){if(i)f<<',';auto& r=rows[i];f<<"{\"frame\":"<<r.frame<<",\"mode\":"<<r.mode<<",\"gpuMs\":"<<r.gpuMs<<",\"decalDraws\":"<<r.draws<<'}';}f<<"]}\n";
}
void poll(ID3D11DeviceContext* c,uint64_t now){
 for(auto& q:timings)if(q.pending){D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d{};UINT64 a{},b{};
  if(c->GetData(q.disjoint.Get(),&d,sizeof(d),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK && c->GetData(q.begin.Get(),&a,sizeof(a),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK && c->GetData(q.end.Get(),&b,sizeof(b),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK){
   if(benchmarking && q.run==run && !d.Disjoint && d.Frequency && b>=a && q.eyeMask==3 && rows.size()<256)rows.push_back({q.frame,q.draws,q.mode,1000.0*double(b-a)/double(d.Frequency)});else if(q.run==run)++dropped;q.pending=false;
  }else if(now-q.submitted>2000){q.pending=false;q.begin.Reset();q.end.Reset();q.disjoint.Reset();++dropped;}}
 if(benchmarking && now>deadline+2100){report("complete");benchmarking=false;}
}
}
void Initialize(const std::filesystem::path& path){directory=path;}
void RegisterShader(ID3D11DeviceChild* shader,const void* code,SIZE_T size){
 if(!shader || !code)return;uint64_t hash=14695981039346656037ull;for(SIZE_T i=0;i<size;++i){hash^=static_cast<const uint8_t*>(code)[i];hash*=1099511628211ull;}if(hash!=0x344a4c7f656885beull)return;
 UINT tag=1;shader->SetPrivateData(NativeTag,4,&tag);log("optional decal switch registered exact shader");
}
void Pixel(ID3D11PixelShader* ps){candidate=tagged(ps);}
void Benchmark(){requested=true;}
void Begin(uint64_t id,bool tracked,bool capture,ID3D11DeviceContext* c,bool hunter){
 frame=id;active=tracked && c;trace=capture;eye=-1;eyeMask=0;current=nullptr;if(!c)return;auto now=GetTickCount64();poll(c,now);bool request=requested.exchange(false);
 if(now-tick>=200 || request){tick=now;auto ini=directory/L"PlagueTaleVR.ini";int legacy=GetPrivateProfileIntW(L"Rendering",L"DecalMode",0,ini.c_str())==2;UINT next=GetPrivateProfileIntW(L"Rendering",L"DisableDecals",legacy,ini.c_str())?2:0;if(next!=mode){if(benchmarking){report("mode changed");benchmarking=false;}mode=next;log("DisableDecals="+std::to_string(mode==2));}}
 if(request){if(benchmarking)report("restarted");rows.clear();++run;start=now+1000;deadline=start+10000;timedFrames=excluded=dropped=runDraws=0;benchmarking=true;log("benchmark="+std::to_string(run)+" mode="+std::to_string(mode));}
 if(!benchmarking || now<start || now>deadline)return;
 if(!tracked || capture || hunter || c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE){++excluded;return;}++timedFrames;
 if(timedFrames%16)return;for(auto& q:timings)if(!q.pending){
  if(!q.begin || !q.end || !q.disjoint){q.begin.Reset();q.end.Reset();q.disjoint.Reset();ComPtr<ID3D11Device> d;c->GetDevice(&d);D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP,0};if(FAILED(d->CreateQuery(&desc,&q.begin)) || FAILED(d->CreateQuery(&desc,&q.end)))break;desc.Query=D3D11_QUERY_TIMESTAMP_DISJOINT;if(FAILED(d->CreateQuery(&desc,&q.disjoint)))break;}
  q.frame=id;q.submitted=now;q.draws=0;q.mode=mode;q.eyeMask=0;q.run=run;current=&q;c->Begin(q.disjoint.Get());c->End(q.begin.Get());return;
 }++dropped;
}
void Eye(int value){eye=value;if(active && value>=0 && value<2)eyeMask|=1u<<value;}
bool BeforeDraw(ID3D11DeviceContext* c){
 if(!active || !candidate || eye<0 || eye>1 || c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return true;
 ComPtr<ID3D11PixelShader> ps;c->PSGetShader(&ps,nullptr,nullptr);if(!tagged(ps.Get()))return true;++counts[eye];if(current)++current->draws;if(benchmarking && GetTickCount64()>=start && GetTickCount64()<=deadline)++runDraws;
 if(mode==2){++skipped[eye];return false;}return true;
}
void End(ID3D11DeviceContext* c){
 if(current){current->eyeMask=eyeMask;c->End(current->end.Get());c->End(current->disjoint.Get());current->pending=true;current=nullptr;}
 if(trace){std::error_code ec;auto folder=directory/L"gpu-captures"/(L"frame-"+std::to_wstring(frame));std::filesystem::create_directories(folder,ec);if(!ec){std::ofstream f(folder/L"decal-pass.json");f<<"{\"frame\":"<<frame<<",\"mode\":"<<mode<<",\"disableDecals\":"<<(mode==2?"true":"false")<<",\"target\":\"344a4c7f656885be\",\"draws\":["<<counts[0]<<','<<counts[1]<<"],\"skipped\":["<<skipped[0]<<','<<skipped[1]<<"]}";}}
 active=false;eye=-1;
}
}
