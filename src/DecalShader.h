#pragma once
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <cstdint>
void ComputeHashRetail(const BYTE*,UINT,BYTE*);
namespace DecalShader {
inline HRESULT Build(const void* bytes,SIZE_T size,ID3DBlob** result,std::string& diagnostic){
 using Word=uint32_t;
 if(!bytes || size<32 || size%4)return E_INVALIDARG;
 const auto* words=static_cast<const Word*>(bytes);if(words[0]!=0x43425844 || words[6]!=size || words[7]>(size-32)/4)return E_INVALIDARG;
 Microsoft::WRL::ComPtr<ID3DBlob> text;auto hr=D3DDisassemble(bytes,size,0,nullptr,&text);if(FAILED(hr))return hr;
 // Associate each disassembled instruction with its unchanged token sequence.
 // Copying tokens avoids a source-language recompile changing packed materials.
 std::vector<std::string> lines;std::istringstream input(static_cast<const char*>(text->GetBufferPointer()));std::string line;bool program=false;
 while(std::getline(input,line)){auto a=line.find_first_not_of(" \t\r");if(a==std::string::npos || line.compare(a,2,"//")==0)continue;auto z=line.find_last_not_of(" \t\r");line=line.substr(a,z-a+1);if(!program){program=line=="ps_5_0";continue;}if(line[0]==0)break;lines.push_back(line);}
 struct Instruction{std::vector<Word> words;};std::vector<Instruction> instructions;UINT shaderChunk=UINT(-1);
 for(UINT i=0;i<words[7];++i){UINT offset=words[8+i];if(offset>size-8)return E_INVALIDARG;auto* chunk=reinterpret_cast<const Word*>(static_cast<const unsigned char*>(bytes)+offset);if(chunk[1]>size-offset-8)return E_INVALIDARG;
  if(chunk[0]!=0x58454853 && chunk[0]!=0x52444853)continue;shaderChunk=i;if(chunk[1]<8 || chunk[1]%4)return E_INVALIDARG;
  auto* code=chunk+2;size_t count=chunk[1]/4;if(code[1]!=count)return E_INVALIDARG;
  for(size_t at=2;at<count;){size_t n=(code[at]>>24)&127;if((code[at]&2047)==53){if(at+1>=count)return E_INVALIDARG;n=code[at+1];}if(!n || n>count-at)return E_INVALIDARG;instructions.push_back({std::vector<Word>(code+at,code+at+n)});at+=n;}
 }
 if(shaderChunk==UINT(-1) || lines.size()!=instructions.size()){diagnostic="Native instructions could not be associated with tokens";return E_FAIL;}
 const auto find=[&](const char* wanted){size_t found=lines.size();for(size_t i=0;i<lines.size();++i)if(lines[i]==wanted){if(found!=lines.size())return lines.size();found=i;}return found;};
 auto first=find("mad r0.xy, v0.xyxx, cb0[20].xyxx, cb0[20].zwzz"),coord=find("ftou r2.xyzw, v0.xyyy"),load=find("ld_uav_typed_indexable(texture2d)(uint,uint,uint,uint) r1.w, r2.xwww, u1.yzwx"),shift=find("ushr r3.z, r1.w, l(20)"),test=find("and r1.x, r3.z, cb2[19].x");
 if(first>=lines.size() || coord>=lines.size() || load>=lines.size() || shift>=lines.size() || test+4>=lines.size() || lines[test+1]!="and r1.x, r1.x, l(48)" || lines[test+2]!="if_z r1.x" || lines[test+3]!="ret" || lines[test+4]!="endif"){diagnostic="Exact native material predicate did not match";return E_FAIL;}
 std::vector<Word> programCode;auto* originalChunk=reinterpret_cast<const Word*>(static_cast<const unsigned char*>(bytes)+words[8+shaderChunk]);programCode.push_back(originalChunk[2]);programCode.push_back(0);
 const auto append=[&](size_t index){auto& v=instructions[index].words;programCode.insert(programCode.end(),v.begin(),v.end());};
 for(size_t i=0;i<instructions.size();++i){if(i==first){append(coord);append(load);append(shift);for(size_t j=0;j<5;++j)append(test+j);}if(i>=test && i<test+5)continue;append(i);}
 programCode[1]=UINT(programCode.size());
 std::vector<Word> container(words,words+8+words[7]);
 for(UINT i=0;i<words[7];++i){auto* chunk=reinterpret_cast<const Word*>(static_cast<const unsigned char*>(bytes)+words[8+i]);container[8+i]=UINT(container.size()*4);container.push_back(chunk[0]);if(i==shaderChunk){container.push_back(UINT(programCode.size()*4));container.insert(container.end(),programCode.begin(),programCode.end());}else {container.push_back(chunk[1]);container.insert(container.end(),chunk+2,chunk+2+chunk[1]/4);}}
 container[6]=UINT(container.size()*4);
 ComputeHashRetail(reinterpret_cast<const BYTE*>(container.data())+20,UINT(container.size()*4-20),reinterpret_cast<BYTE*>(container.data())+4);
 // Rebuild the container/checksum with the installed D3D compiler. Shader
 // declarations, native signatures and reflection bindings remain unchanged.
 hr=D3DStripShader(container.data(),container.size()*4,D3DCOMPILER_STRIP_DEBUG_INFO,result);if(FAILED(hr))diagnostic="Could not rebuild modified shader container";return hr;
}
}
