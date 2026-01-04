#include "wagon.h"
#include "shared_print.h"
#include <cstring>
extern "C"
{
#include "lj_obj.h"
}

#define LOCAL_L lua_State* L = m_Lua.L
#define LOCAL_SELF CWagon* self = static_cast<TLuaData*>(G(L)->allocd)->self

extern SharedPrint g_SharedPrint;

// Wrapper to LuaJIT allocator
static void* l_alloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
	TLuaData* d = static_cast<TLuaData*>(ud);
	return lj_alloc_f(d->msp, ptr, osize, nsize);
}

CWagon::CWagon()
{
	m_StartTime = std::chrono::steady_clock::now(); 

	// Alloc lua environment
	m_Lua.L = luaL_newstate();
	lua_getallocf(m_Lua.L, &m_Lua.msp); // Get original mspace
	lua_setallocf(m_Lua.L, l_alloc, &m_Lua); // Replace with our data

	LOCAL_L;
	luaL_openlibs(L);

	// TURBOSTROI = true
	lua_pushboolean(L, true);
	lua_setglobal(L, "TURBOSTROI");

	// print()
	lua_pushcfunction(L, &SharedPrint::PrintL);
	lua_setglobal(L, "print");

	// CurTime()
	lua_pushcfunction(L, &CWagon::CurrentTime);
	lua_setglobal(L, "CurTime");

	// PrevTime()
	lua_pushcfunction(L, &CWagon::PrevTime);
	lua_setglobal(L, "PrevTime");

	// FrameTime()
	lua_pushcfunction(L, &CWagon::DeltaTime);
	lua_setglobal(L, "FrameTime");

	// SysTime()
	lua_pushcfunction(L, &CWagon::SysTime);
	lua_setglobal(L, "SysTime");

	// GetEntIndex()
	lua_pushcfunction(L, &CWagon::EntIndex);
	lua_setglobal(L, "EntIndex");

	// RecvMessages()
	lua_pushcfunction(L, &CWagon::ThreadRecvMessages);
	lua_setglobal(L, "RecvMessages");

	// _userdata = this
	lua_pushlightuserdata(L, this);
	lua_setglobal(L, "_userdata");
}

CWagon::~CWagon()
{
	lua_close(m_Lua.L);
	lj_alloc_destroy(m_Lua.msp);
}

bool CWagon::SimSendMessage(int message, const char* system_name, const char* name, double index, double value)
{
	TThreadMsg tmsg;
	tmsg.message = message;
	tmsg.system_name = system_name;
	tmsg.name = name;
	tmsg.index = index;
	tmsg.value = value;

	return m_Sim2Thread.push(tmsg);
}

int CWagon::SimRecvMessages(std::unique_ptr<TThreadMsg[]>& tmsgs)
{
	// Not used
	return 0;
}

TThreadMsg& CWagon::SimRecvMessage()
{
	if (m_Thread2Sim.pop(m_Thread2SimMsg))
		return m_Thread2SimMsg;
	else
		return s_EmptyMsg;
}

int CWagon::SimReadAvailable()
{
	return m_Thread2Sim.size();
}

bool CWagon::ThreadSendMessage(int message, const char* system_name, const char* name, double index, double value)
{
	TThreadMsg tmsg;
	tmsg.message = message;
	tmsg.system_name = system_name;
	tmsg.name = name;
	tmsg.index = index;
	tmsg.value = value;

	return m_Thread2Sim.push(tmsg);
}

int CWagon::ThreadRecvMessages(std::unique_ptr<TThreadMsg[]>& tmsgs)
{
	// Not used
	return 0;
}

int CWagon::ThreadRecvMessages(lua_State* L)
{
	// Not used
	return 0;
}

TThreadMsg& CWagon::ThreadRecvMessage()
{
	if (m_Sim2Thread.pop(m_Sim2ThreadMsg))
		return m_Sim2ThreadMsg;
	else
		return s_EmptyMsg;
}

int CWagon::ThreadReadAvailable()
{
	return m_Sim2Thread.size();
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

void CWagon::Think()
{
	LOCAL_L;
	lua_getglobal(L, "Think");
	lua_pushnumber(L, m_DeltaTime);
	if (lua_pcall(L, 1, 0, 0))
	{
		std::string err = lua_tostring(L, -1);
		err += '\n';
		g_SharedPrint.Push(err.c_str());
		lua_pop(L, 1);
	}
}

bool CWagon::UpdateCurTime(float t)
{
	if (m_ServerCurTime == t) return false;
	m_ServerCurTime = t;

	double tCurTime = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_StartTime).count();

	m_PrevTime = m_CurrentTime;
	m_DeltaTime = tCurTime - m_PrevTime;
	m_CurrentTime = tCurTime;
	
	return true;
}

double CWagon::CurrentTime()
{
	return m_CurrentTime;
}

int CWagon::CurrentTime(lua_State* L)
{
	LOCAL_SELF;
	lua_pushnumber(L, self->m_CurrentTime);
	return 1;
}

double CWagon::PrevTime()
{
	return m_PrevTime;
}

int CWagon::PrevTime(lua_State* L)
{
	LOCAL_SELF;
	lua_pushnumber(L, self->m_PrevTime);
	return 1;
}

double CWagon::DeltaTime()
{
	return m_DeltaTime;
}

int CWagon::DeltaTime(lua_State* L)
{
	LOCAL_SELF;
	lua_pushnumber(L, self->m_DeltaTime);
	return 1;
}

int CWagon::SysTime(lua_State* L)
{
	LOCAL_SELF;
	double tSysTime = std::chrono::duration<double>(std::chrono::steady_clock::now() - self->m_StartTime).count();
	lua_pushnumber(L, tSysTime);
	return 1;
}

void CWagon::SetEntIndex(int idx)
{
	m_EntIndex = idx;
}

int CWagon::EntIndex()
{
	return m_EntIndex;
}

int CWagon::EntIndex(lua_State* L)
{
	LOCAL_SELF;
	lua_pushnumber(L, self->m_EntIndex);
	return 1;
}

void CWagon::Finish()
{
    m_Finished = true;
}

bool CWagon::IsFinished()
{
	return m_Finished;
}
