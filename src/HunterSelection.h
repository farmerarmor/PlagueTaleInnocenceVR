#pragma once
#include <cstddef>
// Keyboard state independent of rendering. One selected shader can be hidden;
// moving the cursor immediately restores the previous shader.
struct HunterSelection {
 bool enabled{};int index=-1;
 void Home(){enabled=false;index=-1;}
 void Enable(){enabled=true;index=-1;}
 void Scanned(std::size_t count){index=enabled && count?0:-1;}
 void Move(int direction,std::size_t count){
  if(!enabled || !count)return;
  const auto size=int(count);index=index<0?(direction<0?size-1:0):(index+direction+size)%size;
 }
 bool HasSelection(std::size_t count)const{return enabled && index>=0 && std::size_t(index)<count;}
};
