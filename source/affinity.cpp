#include "affinity.h"
#include <regex>

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
std::pair<bool, mp::uint1024_t> ToMPNumber(const char* value)
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
			num.assign(affinityStr.c_str());
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
