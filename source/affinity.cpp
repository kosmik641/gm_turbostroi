#include "affinity.h"

#include <regex>
#include <boost/multiprecision/cpp_int.hpp>
#include <color.h>
#include <dbg.h>

size_t g_ProcessorCount = std::thread::hardware_concurrency();

#if defined(_WIN32)
// Init with Mask=0, Group=i
std::array<GROUP_AFFINITY, 32> g_MainThreadGroupAffinity { {
	{0,0}, {0,1}, {0,2}, {0,3}, {0,4}, {0,5}, {0,6}, {0,7},
	{0,8}, {0,9}, {0,10},{0,11},{0,12},{0,13},{0,14},{0,15},
	{0,16},{0,17},{0,18},{0,19},{0,20},{0,21},{0,22},{0,23},
	{0,24},{0,25},{0,26},{0,27},{0,28},{0,29},{0,30},{0,31}
} };

std::array<GROUP_AFFINITY, 32> g_SimThreadGroupAffinity { {
	{0,0}, {0,1}, {0,2}, {0,3}, {0,4}, {0,5}, {0,6}, {0,7},
	{0,8}, {0,9}, {0,10},{0,11},{0,12},{0,13},{0,14},{0,15},
	{0,16},{0,17},{0,18},{0,19},{0,20},{0,21},{0,22},{0,23},
	{0,24},{0,25},{0,26},{0,27},{0,28},{0,29},{0,30},{0,31}
} };

HANDLE CurrentThread()
{
	return ::GetCurrentThread();
}

DWORD CurrentCPU()
{
	return ::GetCurrentProcessorNumber();
}
#elif defined(POSIX)
cpu_set_t g_MainThreadGroupAffinity;
cpu_set_t g_SimThreadGroupAffinity;

pid_t CurrentThread()
{
	return ::gettid();
}

int CurrentCPU()
{
	return ::sched_getcpu();
}
#else
size_t g_MainThreadGroupAffinity = 0;
size_t g_SimThreadGroupAffinity = 0;

size_t CurrentThread()
{
	return 0;
}

size_t CurrentCPU()
{
	return 0;
}
#endif

//------------------------------------------------------------------------------
// String to boost::multiprecision number
//------------------------------------------------------------------------------
namespace mp = boost::multiprecision;
static std::pair<bool, mp::uint1024_t> ToMPNumber(const char* value)
{
	mp::uint1024_t num = 0;
	std::string affinityStr(value);

	const std::regex r2(R"(^0[bB][0-1]+)");
	const std::regex r10(R"(^[\d]+)");
	const std::regex r16(R"(^0[xX][0-9a-fA-F]+)");

	int base = 0;
	if (std::regex_match(affinityStr, r2))
		base = 2;
	else if (std::regex_match(affinityStr, r10))
		base = 10;
	else if (std::regex_match(affinityStr, r16))
		base = 16;

	try
	{
		if (base == 2)
		{
			num = 0;
			for (size_t i = 2; i < affinityStr.size(); i++)
			{
				if (affinityStr[i] == '1')
					mp::bit_set(num, affinityStr.size() - i - 1);
			}
		}
		else if (base == 10 || base == 16)
		{
			num.assign(affinityStr);
		}
		else
		{
			ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: Wrong format for train affinity mask!\n"
				"    Allowed binary, decimal and hexadecimal numbers. (e.g. 0b1011001101, 717, 0x2CD)\n");
			return std::pair(false, num);
		}
	}
	catch (const std::exception& e)
	{
		ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: Fail to store affinity mask: %s\n", e.what());
		return std::pair(false, num);
	}

	return std::pair(true, num);
}

bool SetThreadGroup(CPU_SET& group, const char* value)
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
			sizeof(KAFFINITY) * 8);
	}
#elif defined(POSIX)
	CPU_ZERO(&group);
	memcpy(&group, num.backend().limbs(), std::min(sizeof(group), num.backend().internal_limb_count));
#endif

	return true;
}

bool SetAffinityMask(std::thread::native_handle_type handle, const CPU_SET& group)
{
#if defined(_WIN32)
	bool status = true;

	// New WinAPI in Win11
	typedef BOOL(WINAPI* pFnSetThreadSelectedCpuSetMasks)(HANDLE Thread, PGROUP_AFFINITY CpuSetMasks, USHORT CpuSetMaskCount);
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