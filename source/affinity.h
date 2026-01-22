#pragma once
#include <thread>

#if defined(_WIN32)
#include <Windows.h>
#include <array>
#include "version.h"
using CPU_SET = std::array<GROUP_AFFINITY, 32>;
HANDLE CurrentThread();
DWORD CurrentCPU();
#elif defined(POSIX)
#include <sched.h>
using CPU_SET = cpu_set_t;
pid_t CurrentThread();
int CurrentCPU();
#else
using CPU_SET = size_t;
size_t CurrentThread();
size_t CurrentCPU();
#endif

extern size_t g_ProcessorCount;
extern CPU_SET g_MainThreadGroupAffinity;
extern CPU_SET g_SimThreadGroupAffinity;

bool SetThreadGroup(CPU_SET& group, const char* value);
bool SetAffinityMask(std::thread::native_handle_type handle, const CPU_SET& group);
