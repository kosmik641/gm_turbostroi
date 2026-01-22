#pragma once
#include <string>
#include <atomic>
#include <unordered_map>
#include "version.h"

extern bool g_ForceThreadsFinished;
extern std::atomic<float> g_CurrentTime;
extern unsigned int g_ThreadTickrate;
extern std::unordered_map<std::string, std::string> g_LoadedFilesCache;
