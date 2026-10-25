#include "ShaderHunter.h"
#include "HunterSelection.h"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <windows.h>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <atomic>
using Microsoft::WRL::ComPtr;
namespace ShaderHunter {
namespace {
const GUID Tag={0x9d39ab25,0x63db,0x4fda,{0xa8,0x64,0x21,0x91,0x04,0x34,0xb5,0xf8}};
struct Identity {uint64_t hash{};UINT stage{},bytecode{};};
struct Entry {Identity shader;std::string resources;uint64_t counts[3]{},firstUse{},registered{};UINT maxVertices{},width{},height{},format{};};
struct Key {uint64_t hash;UINT stage;bool operator==(const Key&)const=default;};
struct KeyHash {size_t operator()(Key k)const{return size_t(k.hash^(uint64_t(k.stage)<<56));}};
std::filesystem::path directory;
std::mutex registryMutex;
std::vector<Entry> registry,catalog;
std::unordered_map<Key,size_t,KeyHash> lookup;
std::atomic<uint64_t> fallbackCounter{1},sequence{},commands{},unknownBindings{},hiddenCalls{};
std::atomic<size_t> registeredCount{};
std::atomic<bool> hunting{},collecting{};
std::atomic<uint64_t> hiddenHash{};
std::atomic<UINT> hiddenStage{};
std::atomic<int> currentEye{-1};
uint64_t frame{},pose{},validFrames{},lastWrite{},catalogGeneration{};
bool active{},scanRequested{},markCapture{},pressed[5]{};
HunterSelection selection;
std::string hex(uint64_t n){std::ostringstream s;s<<std::hex<<std::setw(16)<<std::setfill('0')<<n;return s.str();}
std::string quoted(const std::string& value){std::string s="\"";for(auto c:value){if(c=='\\'||c=='\"')s+='\\';if(static_cast<unsigned char>(c)>=32)s+=c;}return s+'\"';}
const char* stageName(UINT s){const char* names[]={"pixel","vertex","compute","geometry","hull","domain"};return s<6?names[s]:"unknown";}
Identity read(ID3D11DeviceChild* shader,UINT stage){
 if(!shader)return {};
 Identity id{};UINT size=sizeof(id);
 if(SUCCEEDED(shader->GetPrivateData(Tag,&size,&id)) && id.hash)return id;
 // Shaders created before injection are huntable with an explicit object
 // token when their bytecode was unavailable; they are never discarded.
 std::lock_guard lock(registryMutex);size=sizeof(id);if(SUCCEEDED(shader->GetPrivateData(Tag,&size,&id)) && id.hash)return id;
 id={0xf000000000000000ull|fallbackCounter.fetch_add(1),stage,0};shader->SetPrivateData(Tag,sizeof(id),&id);++unknownBindings;Key key{id.hash,id.stage};
 if(!lookup.contains(key)){lookup[key]=registry.size();Entry e{};e.shader=id;e.registered=registry.size();e.resources="Bytecode unavailable: shader bound before creation was observed";registry.push_back(std::move(e));registeredCount=registry.size();}
 return id;
}
void publishSelection(){
 if(selection.HasSelection(catalog.size())){auto id=catalog[size_t(selection.index)].shader;hiddenStage=id.stage;hiddenHash=id.hash;hunting=true;}
 else {hunting=selection.enabled;hiddenHash=0;}
}
void status(){
 UINT totals[6]{},observed[6]{},fallbacks{};{std::lock_guard lock(registryMutex);for(auto& e:registry){++totals[e.shader.stage];if(e.firstUse)++observed[e.shader.stage];if(!e.shader.bytecode)++fallbacks;}}
 std::ofstream f(directory/L"shader-hunter-status.json.tmp");
 f<<"{\"sourceFrame\":"<<frame<<",\"poseId\":"<<pose<<",\"active\":"<<(active?"true":"false")<<",\"enabled\":"<<(selection.enabled?"true":"false")<<",\"hidden\":"<<(hiddenHash.load()?"true":"false")<<",\"index\":"<<selection.index<<",\"total\":"<<catalog.size()<<",\"stage\":"<<hiddenStage.load()<<",\"stageName\":"<<quoted(stageName(hiddenStage.load()))<<",\"selected\":[";
 if(hiddenHash.load())f<<quoted(hex(hiddenHash.load()));
 f<<"],\"registeredByStage\":[";for(int i=0;i<6;++i){if(i)f<<',';f<<totals[i];}f<<"],\"observedByStage\":[";for(int i=0;i<6;++i){if(i)f<<',';f<<observed[i];}
 f<<"],\"unavailableBytecode\":"<<fallbacks<<",\"commandsObserved\":"<<commands.load()<<",\"hiddenCommands\":"<<hiddenCalls.load()<<"}";f.close();
 MoveFileExW((directory/L"shader-hunter-status.json.tmp").c_str(),(directory/L"shader-hunter-status.json").c_str(),MOVEFILE_REPLACE_EXISTING);
}
void event(const char* name){status();std::ofstream f(directory/L"shader-hunter.log",std::ios::app);f<<name<<" frame="<<frame<<" enabled="<<selection.enabled<<" index="<<selection.index<<"/"<<catalog.size()<<" stage="<<stageName(hiddenStage.load())<<" selected="<<hex(hiddenHash.load())<<'\n';}
void controls(){
 DWORD foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground);const bool focused=foreground==GetCurrentProcessId();
 const int keys[]={VK_HOME,VK_END,VK_PRIOR,VK_NEXT,VK_DELETE};
 for(unsigned k=0;k<5;++k){bool down=(GetAsyncKeyState(keys[k])&0x8000)!=0;bool edge=focused && down&&!pressed[k];pressed[k]=down;if(!edge)continue;
  if(k==0){selection.Home();scanRequested=markCapture=false;collecting=false;publishSelection();event("Home");return;}
  if(k==1){selection.Enable();scanRequested=true;collecting=true;publishSelection();event("End");continue;}
  if(!selection.enabled)continue;
  if(k==2 || k==3){selection.Move(k==2?-1:1,catalog.size());publishSelection();event(k==2?"PageUp":"PageDown");continue;}
  if(k==4 && selection.HasSelection(catalog.size())){
   const auto& e=catalog[size_t(selection.index)];const auto id=e.shader;
   std::ofstream marks(directory/L"shader-hunter-marks.jsonl",std::ios::app);
   marks<<"{\"sourceFrame\":"<<frame<<",\"poseId\":"<<pose<<",\"stage\":"<<id.stage<<",\"stageName\":"<<quoted(stageName(id.stage))<<",\"index\":"<<selection.index<<",\"hash\":"<<quoted(hex(id.hash))<<",\"identityKind\":"<<quoted(id.bytecode?"bytecode":"object-token")<<",\"bindings\":"<<quoted(e.resources)<<"}\n";marks.close();
   markCapture=true;std::ofstream request(directory/L"host-capture.flag");request<<"marked shader "<<hex(id.hash);event("Delete");
  }
 }
}
void record(Identity id,ID3D11DeviceContext* c,UINT vertices,bool compute){
 if(!id.hash)return;
 std::lock_guard lock(registryMutex);const auto found=lookup.find({id.hash,id.stage});if(found==lookup.end())return;
 auto& e=registry[found->second];int which=currentEye.load();if(which<0 || which>1)which=2;++e.counts[which];e.maxVertices=std::max(e.maxVertices,vertices);
 if(!e.firstUse){e.firstUse=++sequence;
  if(!compute){ComPtr<ID3D11RenderTargetView> target;ComPtr<ID3D11DepthStencilView> depth;c->OMGetRenderTargets(1,&target,&depth);ComPtr<ID3D11Resource> resource;ComPtr<ID3D11Texture2D> texture;if(target)target->GetResource(&resource);else if(depth)depth->GetResource(&resource);if(resource)resource.As(&texture);if(texture){D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);e.width=d.Width;e.height=d.Height;e.format=d.Format;}}
 }
}
bool skip(Identity id){return hunting.load() && id.hash && id.hash==hiddenHash.load() && id.stage==hiddenStage.load();}
}
void Initialize(const std::filesystem::path& dir){directory=dir;}
void RegisterShader(ID3D11DeviceChild* shader,const void* code,SIZE_T size,UINT stage){
 if(!shader || !code || !size || stage>=6)return;
 Identity id{14695981039346656037ull,stage,1};for(SIZE_T i=0;i<size;++i){id.hash^=static_cast<const uint8_t*>(code)[i];id.hash*=1099511628211ull;}shader->SetPrivateData(Tag,sizeof(id),&id);
 std::lock_guard lock(registryMutex);Key key{id.hash,id.stage};if(lookup.contains(key))return;
 Entry e{};e.shader=id;e.registered=registry.size();
 ComPtr<ID3D11ShaderReflection> reflection;if(SUCCEEDED(D3DReflect(code,size,__uuidof(ID3D11ShaderReflection),&reflection))){
  D3D11_SHADER_DESC desc{};if(SUCCEEDED(reflection->GetDesc(&desc)))for(UINT i=0;i<desc.BoundResources;++i){D3D11_SHADER_INPUT_BIND_DESC b{};if(FAILED(reflection->GetResourceBindingDesc(i,&b)))continue;if(!e.resources.empty())e.resources+="; ";e.resources+=b.Name?b.Name:"?";e.resources+='@';e.resources+=std::to_string(b.BindPoint);}
 }else e.resources="Shader bytecode captured; reflection unavailable";
 lookup[key]=registry.size();registry.push_back(std::move(e));registeredCount=registry.size();
}
// Query actual bindings at each hunt draw, covering ClearState, command lists
// and multiple contexts rather than trusting a thread-local shader guess.
void Vertex(ID3D11VertexShader*){}
void Pixel(ID3D11PixelShader*){}
void Compute(ID3D11ComputeShader*){}
void Begin(uint64_t id,uint64_t trackingPose,bool vr){
 frame=id;pose=trackingPose;active=vr;currentEye=-1;controls();
 if(!vr){if(hunting.exchange(false)){selection.Home();hiddenHash=0;collecting=false;event("VRStopped");}return;}
 if(++validFrames==120)collecting=true;
}
bool CaptureRequested(){const bool result=markCapture;markCapture=false;return result;}
void Eye(int e){currentEye=e;}
bool Enabled(){return hunting.load();}
bool Draw(ID3D11DeviceContext* c,UINT vertices,const char*){
 if(!hunting.load() && !collecting.load())return true;++commands;
 ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;
 c->PSGetShader(&ps,nullptr,nullptr);c->VSGetShader(&vs,nullptr,nullptr);c->GSGetShader(&gs,nullptr,nullptr);c->HSGetShader(&hs,nullptr,nullptr);c->DSGetShader(&ds,nullptr,nullptr);
 Identity ids[]={read(ps.Get(),0),read(vs.Get(),1),read(gs.Get(),3),read(hs.Get(),4),read(ds.Get(),5)};bool allowed=true;
 for(auto id:ids){record(id,c,vertices,false);if(skip(id))allowed=false;}if(!allowed)++hiddenCalls;return allowed;
}
bool Dispatch(ID3D11DeviceContext* c){
 if(!hunting.load() && !collecting.load())return true;++commands;ComPtr<ID3D11ComputeShader> cs;c->CSGetShader(&cs,nullptr,nullptr);auto id=read(cs.Get(),2);record(id,c,0,true);bool allowed=!skip(id);if(!allowed)++hiddenCalls;return allowed;
}
void End(){
 currentEye=-1;if(!scanRequested && !collecting.load() && !selection.enabled && registeredCount.load()==catalog.size())return;
 const auto now=GetTickCount64();if(!scanRequested && now-lastWrite<500)return;lastWrite=now;
 std::vector<Entry> all;{std::lock_guard lock(registryMutex);all=registry;}
 // Every captured creation is included even if absent from this frame.
 // Used shaders come first; late entries append without moving the cursor.
 if(scanRequested){std::stable_sort(all.begin(),all.end(),[](const Entry& a,const Entry& b){if(bool(a.firstUse)!=bool(b.firstUse))return bool(a.firstUse);return a.firstUse && b.firstUse?a.firstUse<b.firstUse:a.registered<b.registered;});catalog=all;selection.Scanned(catalog.size());scanRequested=false;publishSelection();}
 else {std::unordered_map<Key,size_t,KeyHash> known;for(auto& e:catalog)known[{e.shader.hash,e.shader.stage}]=1;for(auto& e:all)if(!known.contains({e.shader.hash,e.shader.stage}))catalog.push_back(e);}
 std::unordered_map<Key,size_t,KeyHash> latest;for(size_t i=0;i<all.size();++i)latest[{all[i].shader.hash,all[i].shader.stage}]=i;for(auto& e:catalog){auto it=latest.find({e.shader.hash,e.shader.stage});if(it!=latest.end())e=all[it->second];}
 std::ofstream f(directory/L"shader-hunter-catalog.json.tmp");f<<"{\"sourceFrame\":"<<frame<<",\"generation\":"<<++catalogGeneration<<",\"scope\":\"all six stages, all observed creations and bindings, cumulative across frames and contexts\",\"shaders\":[";
 for(size_t i=0;i<catalog.size();++i){auto& e=catalog[i];if(i)f<<',';f<<"{\"hash\":"<<quoted(hex(e.shader.hash))<<",\"stage\":"<<e.shader.stage<<",\"stageName\":"<<quoted(stageName(e.shader.stage))<<",\"identityKind\":"<<quoted(e.shader.bytecode?"bytecode":"object-token")<<",\"firstDraw\":"<<e.firstUse<<",\"left\":"<<e.counts[0]<<",\"right\":"<<e.counts[1]<<",\"outsideEyes\":"<<e.counts[2]<<",\"maxVertices\":"<<e.maxVertices<<",\"width\":"<<e.width<<",\"height\":"<<e.height<<",\"format\":"<<e.format<<",\"bindings\":"<<quoted(e.resources)<<'}';}f<<"]}";f.close();MoveFileExW((directory/L"shader-hunter-catalog.json.tmp").c_str(),(directory/L"shader-hunter-catalog.json").c_str(),MOVEFILE_REPLACE_EXISTING);
 if(!selection.enabled)collecting=false;status();
}
}
