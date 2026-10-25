#pragma once
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <sstream>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <d3d11shader.h>
void ComputeHashRetail(const BYTE*,UINT,BYTE*);
namespace FirstPersonHeadShader {
inline constexpr char Prefix[]=R"hlsl(
cbuffer Global : register(b0) {float4 c[90];};
cbuffer Object : register(OBJECT_BINDING) {float4 obj[32];};
cbuffer LocalHead : register(b13) {
 float4 playerOrigin;float4 viewOrigin;
 float4 playerRows[3];float4 viewRows[3];
};
void main(){
 float3 origin=float3(obj[0].w,obj[1].w,obj[2].w)+c[35].xyz;
 bool local=all(abs(origin-playerOrigin.xyz)<0.002);
 bool scene=all(abs(c[35].xyz-viewOrigin.xyz)<0.002);
 [unroll]for(uint i=0;i<3;++i){
  local=local && all(abs(obj[i].xyz-playerRows[i].xyz)<0.0005);
  scene=scene && all(abs(c[31+i].xyz-viewRows[i].xyz)<0.0005);
 }
 if(local && scene)discard;
}
)hlsl";
struct Program {using Word=uint32_t;std::vector<std::vector<Word>> instructions;std::vector<std::string> lines;UINT chunk=UINT(-1);};
inline HRESULT ObjectSlot(const void* bytes,SIZE_T size,UINT& slot){
 Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;auto hr=D3DReflect(bytes,size,IID_PPV_ARGS(&reflection));if(FAILED(hr))return hr;
 D3D11_SHADER_INPUT_BIND_DESC object{},global{};D3D11_SHADER_BUFFER_DESC objectBuffer{},globalBuffer{};
 if(FAILED(reflection->GetResourceBindingDescByName("CBufferObjectConstant_Z",&object)) || FAILED(reflection->GetResourceBindingDescByName("CBufferGlobalConstant_Z",&global)) || object.Type!=D3D_SIT_CBUFFER || global.Type!=D3D_SIT_CBUFFER || global.BindPoint!=0 || object.BindPoint>=13)return E_INVALIDARG;
 if(FAILED(reflection->GetConstantBufferByName("CBufferObjectConstant_Z")->GetDesc(&objectBuffer)) || FAILED(reflection->GetConstantBufferByName("CBufferGlobalConstant_Z")->GetDesc(&globalBuffer)) || objectBuffer.Size!=512 || globalBuffer.Size!=1440)return E_INVALIDARG;
 slot=object.BindPoint;return S_OK;
}
inline HRESULT Parse(const void* bytes,SIZE_T size,Program& result){
 using Word=uint32_t;if(!bytes || size<32 || size%4)return E_INVALIDARG;const auto* w=static_cast<const Word*>(bytes);
 if(w[0]!=0x43425844 || w[6]!=size || w[7]>(size-32)/4)return E_INVALIDARG;
 Microsoft::WRL::ComPtr<ID3DBlob> disassembly;auto hr=D3DDisassemble(bytes,size,0,nullptr,&disassembly);if(FAILED(hr))return hr;
 std::istringstream input(static_cast<const char*>(disassembly->GetBufferPointer()));std::string line;bool program=false;
 while(std::getline(input,line)){auto a=line.find_first_not_of(" \t\r");if(a==std::string::npos || line.compare(a,2,"//")==0)continue;auto z=line.find_last_not_of(" \t\r");line=line.substr(a,z-a+1);if(!program){program=line=="ps_5_0";continue;}if(line[0]==0)break;result.lines.push_back(line);}
 for(UINT i=0;i<w[7];++i){UINT offset=w[8+i];if(offset>size-8)return E_INVALIDARG;auto* chunk=reinterpret_cast<const Word*>(static_cast<const BYTE*>(bytes)+offset);if(chunk[1]>size-offset-8 || chunk[1]%4)return E_INVALIDARG;
  if(chunk[0]!=0x58454853 && chunk[0]!=0x52444853)continue;if(result.chunk!=UINT(-1) || chunk[1]<8)return E_INVALIDARG;result.chunk=i;auto* code=chunk+2;size_t count=chunk[1]/4;if(code[1]!=count || code[0]!=0x50)return E_INVALIDARG;
  for(size_t at=2;at<count;){size_t n=(code[at]>>24)&127;if((code[at]&2047)==53){if(at+1>=count)return E_INVALIDARG;n=code[at+1];}if(!n || n>count-at)return E_INVALIDARG;result.instructions.emplace_back(code+at,code+at+n);at+=n;}
 }
 return result.chunk!=UINT(-1) && result.lines.size()==result.instructions.size()?S_OK:E_INVALIDARG;
}
inline HRESULT Build(const void* bytes,SIZE_T size,ID3DBlob** output,std::string& error){
 using Word=uint32_t;Program native,helper;auto hr=Parse(bytes,size,native);if(FAILED(hr)){error="native pixel program not recognized";return hr;}
 Microsoft::WRL::ComPtr<ID3DBlob> compiled,diagnostic;UINT objectSlot{};
 if(FAILED(hr=ObjectSlot(bytes,size,objectSlot))){error="native material object/global binding layout not recognized";return hr;}
 const auto binding="b"+std::to_string(objectSlot);D3D_SHADER_MACRO macros[]={{"OBJECT_BINDING",binding.c_str()},{nullptr,nullptr}};
 hr=D3DCompile(Prefix,strlen(Prefix),"local-player-head-mask",macros,nullptr,"main","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&compiled,&diagnostic);
 if(FAILED(hr)){error=diagnostic?static_cast<const char*>(diagnostic->GetBufferPointer()):"head-mask compilation failed";return hr;}
 if(FAILED(hr=Parse(compiled->GetBufferPointer(),compiled->GetBufferSize(),helper)))return hr;
 size_t nativeTemps=native.lines.size(),helperTemps=helper.lines.size(),helperConstants=helper.lines.size(),first=native.lines.size();
 unsigned oldCount{},newCount{};
 for(size_t i=0;i<native.lines.size();++i){const auto& line=native.lines[i];if(line.rfind("dcl_constantbuffer CB13[",0)==0){error="native material already uses reserved b13";return E_FAIL;}if(line.rfind("dcl_temps ",0)==0){nativeTemps=i;oldCount=native.instructions[i][1];}if(first==native.lines.size() && line.rfind("dcl_",0)!=0)first=i;}
 for(size_t i=0;i<helper.lines.size();++i){const auto& line=helper.lines[i];if(line.rfind("dcl_temps ",0)==0){helperTemps=i;newCount=helper.instructions[i][1];}if(line.rfind("dcl_constantbuffer CB13[",0)==0)helperConstants=i;}
 if(nativeTemps==native.lines.size() || helperTemps==helper.lines.size() || helperConstants==helper.lines.size() || first==native.lines.size()){error="head-mask declaration boundary missing";return E_FAIL;}
 bool discard=false;std::vector<Word> prefix;
 for(size_t i=0;i<helper.lines.size();++i){const auto& line=helper.lines[i];if(line.rfind("dcl_",0)==0 || line=="ret")continue;
  // Prefix has no outputs/resources/flow; its temporary registers are dead
  // before the first original instruction and need no register remapping.
  if(line.find(" o")!=std::string::npos || line.find(" v")!=std::string::npos || line.rfind("if",0)==0 || line=="else" || line=="endif"){error="unexpected head-mask helper program";return E_FAIL;}
  discard|=line.rfind("discard_",0)==0;prefix.insert(prefix.end(),helper.instructions[i].begin(),helper.instructions[i].end());
 }
 if(!discard){error="head-mask helper has no discard";return E_FAIL;}
 const auto* words=static_cast<const Word*>(bytes);const auto* chunk=reinterpret_cast<const Word*>(static_cast<const BYTE*>(bytes)+words[8+native.chunk]);std::vector<Word> code{chunk[2],0};
 for(size_t i=0;i<native.instructions.size();++i){if(i==first)code.insert(code.end(),prefix.begin(),prefix.end());if(i==nativeTemps){auto declaration=helper.instructions[helperConstants];code.insert(code.end(),declaration.begin(),declaration.end());auto temps=native.instructions[i];temps[1]=std::max(oldCount,newCount);code.insert(code.end(),temps.begin(),temps.end());}else code.insert(code.end(),native.instructions[i].begin(),native.instructions[i].end());}
 code[1]=UINT(code.size());std::vector<Word> container(words,words+8+words[7]);
 for(UINT i=0;i<words[7];++i){auto* part=reinterpret_cast<const Word*>(static_cast<const BYTE*>(bytes)+words[8+i]);container[8+i]=UINT(container.size()*4);container.push_back(part[0]);if(i==native.chunk){container.push_back(UINT(code.size()*4));container.insert(container.end(),code.begin(),code.end());}else{container.push_back(part[1]);container.insert(container.end(),part+2,part+2+part[1]/4);}}
 container[6]=UINT(container.size()*4);ComputeHashRetail(reinterpret_cast<const BYTE*>(container.data())+20,UINT(container.size()*4-20),reinterpret_cast<BYTE*>(container.data())+4);
 // The native signatures and all original instructions remain exact. Drop
 // reflection, which would otherwise omit the new private b13 binding.
 hr=D3DStripShader(container.data(),container.size()*4,D3DCOMPILER_STRIP_DEBUG_INFO|D3DCOMPILER_STRIP_REFLECTION_DATA,output);if(FAILED(hr))error="modified material container rejected";return hr;
}
}
