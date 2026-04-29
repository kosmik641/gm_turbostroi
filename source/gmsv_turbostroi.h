#pragma once
#include <string>
#include <atomic>
#include <unordered_map>
#include "version.h"

extern std::string g_LibraryFileName;
extern bool g_ForceThreadsFinished;
extern std::atomic<double> g_CurrentTime;
extern unsigned int g_ThreadTickrate;
extern std::unordered_map<std::string, std::string> g_LoadedFilesCache;
