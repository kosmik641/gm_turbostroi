#pragma once

#if defined(_WIN32)
#include <Windows.h>
#elif defined(POSIX)
#include <sched.h>
#endif

#if defined(_MSC_VER)
#define TURBOSTROI_EXPORT __declspec(dllexport)
#define TURBOSTROI_IMPORT __declspec(dllimport)
#elif defined(__GNUC__)
#define TURBOSTROI_EXPORT __attribute__((visibility("default")))
#define TURBOSTROI_IMPORT
#else
#define TURBOSTROI_EXPORT
#define TURBOSTROI_IMPORT
#pragma warning Unknown dynamic link import/export semantics.
#endif

#define PushCFunc(_function,_name) LUA->PushCFunction(_function); LUA->SetField(-2, _name)

// STD
#include <thread>
#include <string>
#include <queue>
#include <map>
#include <vector>
#include <atomic>

// LuaJIT
#include "lua.hpp"

// SourceSDK
#include <GarrysMod/FactoryLoader.hpp>
#include <GarrysMod/Lua/Interface.h>
#include <eiface.h>
#include <convar.h>
#include <color.h>

// Mutex
#include "mutex.h"

// Shared print
#include "shared_print.h"

// GlobalTrain
#include "wagon.h"
