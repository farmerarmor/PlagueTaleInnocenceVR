#pragma once
#include <cstdint>
// Keep initial game/menu/eye-target allocation native. Once tracked gameplay
// has settled, saved DLSS selections activate automatically and normal mode
// changes retain the already-tested transition path.
struct DlssStartupGate {
 uint64_t since{},lastGameplay{};bool timing{},opened{};
 bool Update(uint64_t now,bool gameplay,bool blocked=false){
  if(opened)return true;
  if(blocked){timing=false;return false;}
  // The engine also calls empty render intervals between world views. These
  // must not restart the timer; a real loading/tracking gap still does.
  if(!gameplay){if(timing && now-lastGameplay>2000)timing=false;return false;}
  if(!timing){since=now;timing=true;}
  lastGameplay=now;
  if(now-since>=10000)opened=true;
  return opened;
 }
};
