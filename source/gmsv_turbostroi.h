﻿#pragma once

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

#define BUFFER_SIZE 131072
#define QUEUE_SIZE 32768

// STD
#include <thread>
#include <string>
#include <queue>
#include <map>
#include <mutex>
#include <vector>

// LuaJIT
#include "lua.hpp"

// SourceSDK
#include <GarrysMod/FactoryLoader.hpp>
#include <GarrysMod/Lua/Interface.h>
#include <eiface.h>
#include <convar.h>
#include <Color.h>

typedef struct {
	int message;
	char system_name[64];
	char name[64];
	double index;
	double value;
} thread_msg;

struct thread_userdata {
	double current_time;
	lua_State* L;
	int finished;

	std::queue<thread_msg> thread_to_sim, sim_to_thread;
	std::mutex thread_to_sim_mutex, sim_to_thread_mutex;
};

struct shared_message {
	char message[512];
};
