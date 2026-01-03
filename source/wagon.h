#pragma once
#include "ring_buffer.h"
#include "mutex.h"
#include "lua.hpp"
#include <string>
#include <chrono>

struct TThreadMsg {
	int message = 0;
	char system_name[64]{};
	char name[64]{};
	double index = 0;
	double value = 0;
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

class CWagon {
public:
	CWagon();
	~CWagon();

	// Garry's mod side
	bool SimSendMessage(int message, const char* system_name, const char* name, double index, double value);
	int SimRecvMessages(std::unique_ptr<TThreadMsg[]>& tmsgs);
	TThreadMsg SimRecvMessage();
	int SimReadAvailable();

	// Turbostroi side
	bool ThreadSendMessage(int message, const char* system_name, const char* name, double index, double value);
	int ThreadRecvMessages(std::unique_ptr<TThreadMsg[]>& tmsgs);
	static int ThreadRecvMessages(lua_State* state);
	TThreadMsg ThreadRecvMessage();
	int ThreadReadAvailable();

	void LoadBuffer(const char* buf, const char* filename);
	void AddLoadSystem(TTrainSystem& sys);

	void Initialize();
	void Think();

	bool UpdateCurTime(float t);

	double CurrentTime();
	static int CurrentTime(lua_State* state);

	double DeltaTime();

	void SetEntIndex(int idx);
	int EntIndex();
	static int EntIndex(lua_State* state);

	void Finish();
	bool IsFinished();

private:
	lua_State* m_ThreadLua = nullptr;
	std::chrono::steady_clock::time_point m_StartTime;
	float m_ServerCurTime = -1.0f;
	double m_CurrentTime = -1.0;
	double m_PrevTime = 0.0;
	double m_DeltaTime = 0.0;
	bool m_Finished = false;
	int m_SystemCount = 0;
	int m_EntIndex = -1;

	RingBuffer<TThreadMsg, 256> m_Thread2Sim, m_Sim2Thread;
	Mutex m_Thread2SimMtx, m_Sim2ThreadMtx;
};