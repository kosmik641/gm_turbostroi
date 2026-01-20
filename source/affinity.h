#pragma once
#include <color.h>
#include <dbg.h>
#include <array>
#include <thread>
#include <boost/multiprecision/cpp_int.hpp>

#if defined(_WIN32)
#include <Windows.h>
#include <version.h>
HANDLE CurrentThread();
DWORD CurrentCPU();
#elif defined(POSIX)
#include <sched.h>
pid_t CurrentThread();
int CurrentCPU();
#else
size_t CurrentThread();
size_t CurrentCPU();
#endif

std::pair<bool, boost::multiprecision::uint1024_t> ToMPNumber(const char* value);

template <typename T>
bool SetThreadGroup(T& group, const char* value)
{
	auto parseResult = ToMPNumber(value);

	if (!parseResult.first)
		return false;

	auto& num = parseResult.second;

#if defined(_WIN32)
	size_t maskGroups = 0;
	for (size_t i = 0; i < group.size(); i++)
	{
		auto mask = num >> (sizeof(KAFFINITY) * 8 * i);
		group[i].Mask = mask.convert_to<KAFFINITY>();

		if (group[i].Mask)
			maskGroups++;

		// Ignore groups greater than CPU count
		if ((i + 1) * (sizeof(KAFFINITY) * 8) > g_ProcessorCount)
		{
			size_t lastCPUsCount = (g_ProcessorCount - i * sizeof(KAFFINITY) * 8);
			size_t lastMask = (1 << lastCPUsCount) - 1;

			group[i].Mask &= lastMask;
			break;
		}
	}

	// Warning for Win10 and older
	if (!IsWindows11() && maskGroups > 1)
	{
		ConColorMsg(Color(255, 255, 0, 255),
			"Turbostroi: Warning: Affinity mask is used that covers more than one processor group.\n"
			"  Affinity mask must specify CPU cores only in 1 processor group. One group contains %u cores.\n"
		    "  For more information check link:\n"
			"  https://learn.microsoft.com/en-us/windows/win32/procthread/processor-groups#behavior-starting-with-windows-11-and-windows-server-2022\n",
			sizeof(KAFFINITY)*8);
	}
#elif defined(POSIX)
	CPU_ZERO(&group);
	memcpy(&group, num.backend().limbs(), std::min(sizeof(group), num.backend().internal_limb_count));
#endif

	return true;
};



template <typename T>
bool SetAffinityMask(std::thread::native_handle_type handle, const T& group)
{
#if defined(_WIN32)
	bool status = true;

	// New WinAPI in Win11
	typedef BOOL(*pFnSetThreadSelectedCpuSetMasks)(HANDLE Thread, PGROUP_AFFINITY CpuSetMasks, USHORT CpuSetMaskCount);
	static pFnSetThreadSelectedCpuSetMasks fnSetThreadSelectedCpuSetMasks = nullptr;
	static char win11API = -1;
	if (win11API == -1)
	{
		HMODULE kernel32 = LoadLibrary("kernel32.dll");
		if (kernel32)
		{
			fnSetThreadSelectedCpuSetMasks = (pFnSetThreadSelectedCpuSetMasks)GetProcAddress(kernel32, "SetThreadSelectedCpuSetMasks");
			if (fnSetThreadSelectedCpuSetMasks)
				win11API = 1;
			else
				win11API = 0;
		}

		ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Using WinAPI SetThreadSelectedCpuSetMasks: %s\n", win11API ? "Yes" : "No");
	}

	if (win11API)
	{
		size_t groupCount = ((g_ProcessorCount - 1) / (sizeof(KAFFINITY) * 8)) + 1;
		if (fnSetThreadSelectedCpuSetMasks(handle, (PGROUP_AFFINITY)group.data(), groupCount) == 0)
		{
			ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: Set affinity group failed! (0x%04X)\n", GetLastError());
			status = false;
		}	
	}
	else
	{
		for (size_t i = 0; i < group.size(); i++)
		{
			if (::SetThreadGroupAffinity(handle, &group[i], nullptr) == 0)
			{
				ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: Set affinity group #%u failed! (%d)\n", i, GetLastError());
				status = false;
			}

			// Ignore groups greater than CPU count
			if ((i + 1) * sizeof(KAFFINITY) * 8 > g_ProcessorCount)
				break;
		}
	}

	return status;
#elif defined(POSIX)
	if (sched_setaffinity(handle, sizeof(group), &group) != 0)
	{
		ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: Set affinity failed on thread! (%d)\n", errno);
		return false;
	}

	return true;
#else
	ConColorMsg(Color(255, 255, 0, 255), "Turbostroi: Set affinity not supported on your system!");
	return true;
#endif
}