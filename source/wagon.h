#pragma once
#include "ring_buffer.h"
#include <memory>
#include <string>
#include <chrono>
extern "C"
{
#include "lj_alloc.h"
}

struct TThreadMsg {
	int message = 0;
	const char* system_name;
	const char* name;
	double index;
	double value;
};

struct TTrainSystem {
	TTrainSystem(std::string sysName, std::string sysFileName)
	{
		base_name = sysName;
		file_name = sysFileName;
	}

	std::string base_name;
	std::string file_name; // Can be local name of loaded system
};

class CWagon;
struct TLuaData 
{
	CWagon* self = nullptr;
	void* msp = nullptr;
	lua_State* L = nullptr;
	PRNGState prng{};
};

class CWagon {
public:
	CWagon();
	~CWagon();

	inline static TThreadMsg s_EmptyMsg{ 0 };
	// Garry's mod side
	bool SimSendMessage(int message, const char* system_name, const char* name, double index, double value);
	int SimRecvMessages(std::unique_ptr<TThreadMsg[]>& tmsgs);
	TThreadMsg& SimRecvMessage();
	int SimReadAvailable();

	// Turbostroi side
	bool ThreadSendMessage(int message, const char* system_name, const char* name, double index, double value);
	int ThreadRecvMessages(std::unique_ptr<TThreadMsg[]>& tmsgs);
	static int ThreadRecvMessages(lua_State* L);
	TThreadMsg& ThreadRecvMessage();
	int ThreadReadAvailable();

	bool LoadBuffer(const char* buf, size_t size, const char* filename);
	bool CheckLibLoaded();
	void AddLoadSystem(TTrainSystem& sys);

	void SimulationThreadFn();
	void Initialize();
	void Think();

	bool UpdateCurTime(float t);

	double CurrentTime();
	static int CurrentTime(lua_State* L);

	double PrevTime();
	static int PrevTime(lua_State* L);

	double DeltaTime();
	static int DeltaTime(lua_State* L);

	static int SysTime(lua_State* L);

	void SetEntHandle(unsigned long h);
	unsigned long EntHandle();

	int EntIndex();
	static int EntIndex(lua_State* L);

	void Finish();
	bool IsFinished();

private:
	TLuaData m_Lua{ this };
	std::chrono::steady_clock::time_point m_StartTime;
	float m_ServerCurTime = -1.0f;
	double m_CurrentTime = -1.0;
	double m_PrevTime = 0.0;
	double m_DeltaTime = 0.0;
	bool m_Finished = false;
	int m_SystemCount = 0;
	unsigned long m_EntHandle = 0;

	TThreadMsg m_Thread2SimMsg, m_Sim2ThreadMsg;
	RingBuffer<TThreadMsg, 256> m_Thread2Sim, m_Sim2Thread;
};

