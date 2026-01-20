#pragma once
#include "version.h"

#if defined(_WIN32)
#include <Windows.h>
#elif defined(POSIX)
#include <sched.h>
#endif

#define PushCFunc(_function,_name) LUA->PushCFunction(_function); LUA->SetField(-2, _name)

// STD
#include <thread>
#include <string>
#include <queue>
#include <unordered_map>
#include <vector>
#include <atomic>

// Multithreading
#include "affinity.h"

// SourceSDK
#include <GarrysMod/Lua/Interface.h>
#include "source_sdk.h"

// Shared print
#include "shared_print.h"

// GlobalTrain
#include "wagon.h"

void HookRunTrainEnt(GarrysMod::Lua::ILuaBase* LUA, int entStackPos, bool remove = false);
void CVarMainCoresCallback(IConVar* var, const char* pOldValue, float flOldValue);