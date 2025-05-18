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

struct train_system {
	train_system(std::string sysName, std::string sysFileName)
	{
		BaseName = std::move(sysName);
		FileName = std::move(sysFileName);
	}

	std::string BaseName;
	std::string FileName; // Can be local name of loaded system
};

struct thread_msg {
	int message;
	char system_name[64];
	char name[64];
	double index;
	double value;
};

struct thread_userdata {
	double current_time;
	lua_State* L;
	int finished;

	std::queue<thread_msg> thread_to_sim, sim_to_thread;
	Mutex thread_to_sim_mutex, sim_to_thread_mutex;
};

struct shared_message {
	char message[512];
};
