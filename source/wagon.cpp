#include "wagon.h"
#include "shared_print.h"
#include <cstring>

#define LOCAL_L lua_State* L = m_ThreadLua
extern SharedPrint g_SharedPrint;

CWagon::CWagon()
{
	m_ThreadLua = luaL_newstate();
	LOCAL_L;
	luaL_openlibs(L);

	// TURBOSTROI = true
	lua_pushboolean(L, true);
	lua_setglobal(L, "TURBOSTROI");

	// print()
	lua_pushcfunction(L, &SharedPrint::PrintL);
	lua_setglobal(L, "print");

	// RecvMessages()
	lua_pushcfunction(L, &CWagon::ThreadRecvMessages);
	lua_setglobal(L, "RecvMessages");

	// _userdata = this
	lua_pushlightuserdata(L, this);
	lua_setglobal(L, "_userdata");
}

CWagon::~CWagon()
{
	lua_close(m_ThreadLua);
}

bool CWagon::SimSendMessage(int message, const char* system_name, const char* name, double index, double value)
{
	TThreadMsg tmsg;
	tmsg.message = message;
	strncpy(tmsg.system_name, system_name, 63);
	strncpy(tmsg.name, name, 63);
	tmsg.index = index;
	tmsg.value = value;

	m_Sim2ThreadMtx.lock();
	bool res = m_Sim2Thread.push(tmsg);
	m_Sim2ThreadMtx.unlock();
	return res;
}

int CWagon::SimRecvMessages(std::unique_ptr<TThreadMsg[]>& tmsgs)
{
	// Not used
	return 0;
}

TThreadMsg CWagon::SimRecvMessage()
{
	m_Thread2SimMtx.lock();
	TThreadMsg tmsg;
	m_Thread2Sim.pop(tmsg);
	m_Thread2SimMtx.unlock();
	return tmsg;
}

int CWagon::SimReadAvailable()
{
	m_Thread2SimMtx.lock();
	int n = m_Thread2Sim.size();
	m_Thread2SimMtx.unlock();
	return n;
}

bool CWagon::ThreadSendMessage(int message, const char* system_name, const char* name, double index, double value)
{
	TThreadMsg tmsg;
	tmsg.message = message;
	strncpy(tmsg.system_name, system_name, 63);
	strncpy(tmsg.name, name, 63);
	tmsg.index = index;
	tmsg.value = value;

	m_Thread2SimMtx.lock();
	bool res = m_Thread2Sim.push(tmsg);
	m_Thread2SimMtx.unlock();
	return res;
}

int CWagon::ThreadRecvMessages(std::unique_ptr<TThreadMsg[]>& tmsgs)
{
	// Not used
	return 0;
}

int CWagon::ThreadRecvMessages(lua_State* state)
{
	// Not used
	return 0;
}

TThreadMsg CWagon::ThreadRecvMessage()
{
	m_Sim2ThreadMtx.lock();
	TThreadMsg tmsg;
	m_Sim2Thread.pop(tmsg);
	m_Sim2ThreadMtx.unlock();

	return tmsg;
}

int CWagon::ThreadReadAvailable()
{
	m_Sim2ThreadMtx.lock();
	int n = m_Sim2Thread.size();
	m_Sim2ThreadMtx.unlock();
	return n;
}

void CWagon::LoadBuffer(const char* buf, const char* filename)
{
	LOCAL_L;
	if (luaL_loadbuffer(L, buf, strlen(buf), filename)
		|| lua_pcall(L, 0, LUA_MULTRET, 0))
	{
		std::string err = lua_tostring(L, -1);
		err += '\n';
		g_SharedPrint.Push(err.c_str());
		lua_pop(L, 1);
	}

}

void CWagon::AddLoadSystem(TTrainSystem& sys)
{
	LOCAL_L;
	lua_getglobal(L, "LoadSystems");
		lua_newtable(L);
			lua_pushnumber(L, 1);
			lua_pushstring(L, sys.file_name.c_str());
		lua_settable(L, -3);

			lua_pushnumber(L, 2);
			lua_pushstring(L, sys.base_name.c_str());
		lua_settable(L, -3);

			lua_pushnumber(L, ++m_SystemCount);
			lua_pushvalue(L, -2);
		lua_settable(L, -4); // [i] = {sys.file_name,sys.base_name}
	lua_pop(L, 2);
}

void CWagon::Initialize()
{
	LOCAL_L;
	lua_getglobal(L, "Initialize");
	if (lua_pcall(L, 0, 0, 0))
	{
		std::string err = lua_tostring(L, -1);
		err += '\n';
		g_SharedPrint.Push(err.c_str());
		lua_pop(L, 1);
	}
}

void CWagon::Think(bool skipped)
{
	LOCAL_L;

	lua_pushnumber(L, m_CurrentTime);
	lua_setglobal(L, "CurrentTime");

	lua_getglobal(L, "Think");
	lua_pushboolean(L, skipped);
	if (lua_pcall(L, 1, 0, 0))
	{
		std::string err = lua_tostring(L, -1);
		err += '\n';
		g_SharedPrint.Push(err.c_str());
		lua_pop(L, 1);
	}
}

void CWagon::SetCurrentTime(double t)
{
	m_PrevTime = m_CurrentTime;
	m_DeltaTime = t - m_PrevTime;
	m_CurrentTime = t;
}

double CWagon::CurrentTime()
{
	return m_CurrentTime;
}

double CWagon::DeltaTime()
{
	return m_DeltaTime;
}

void CWagon::Finish()
{
    m_Finished = true;
}

bool CWagon::IsFinished()
{
	return m_Finished;
}


