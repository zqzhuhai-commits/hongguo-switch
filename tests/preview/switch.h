#pragma once
#include <chrono>
#include <thread>
#include <cstdint>
using Result=int;
struct SwkbdConfig{};
inline void svcSleepThread(int64_t ns){std::this_thread::sleep_for(std::chrono::nanoseconds(ns));}
inline uint64_t armGetSystemTick(){return std::chrono::steady_clock::now().time_since_epoch().count();}
inline uint64_t armNsToTicks(int64_t ns){return (uint64_t)ns;}
inline void appletSetMediaPlaybackState(bool){}
inline Result swkbdCreate(SwkbdConfig*,int){return -1;}
inline void swkbdConfigMakePresetDefault(SwkbdConfig*){}
inline void swkbdConfigSetStringLenMax(SwkbdConfig*,int){}
inline void swkbdConfigSetGuideText(SwkbdConfig*,const char*){}
inline Result swkbdShow(SwkbdConfig*,char*,size_t){return -1;}
inline void swkbdClose(SwkbdConfig*){}
#define R_FAILED(r) ((r)<0)
#define R_SUCCEEDED(r) ((r)>=0)
