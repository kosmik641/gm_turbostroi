#include "wagon.h"
#include "shared_print.h"
#include <cstring>

#define LOCAL_L lua_State* L = m_Lua.L
#define LOCAL_SELF TLuaData* ud; \
lua_getallocf(L, (void**)&ud); \
CWagon* self = ud->self

extern SharedPrint g_SharedPrint;

// Wrapper to LuaJIT allocator
static void* l_alloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
	TLuaData* L = (TLuaData*)ud;
	return lj_alloc_f(L->msp, ptr, osize, nsize);
}

CWagon::CWagon()
{
	m_StartTime = std::chrono::steady_clock::now(); 

	// Alloc lua environment
	m_Lua.msp = lj_alloc_create(&m_Lua.prng);
	lj_alloc_setprng(m_Lua.msp, &m_Lua.prng);
	m_Lua.L = lua_newstate(l_alloc, &m_Lua);

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

int CWagon::ThreadRecvMessages(lua_State* L)
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

void CWagon::Think()
{
	LOCAL_L;
	lua_getglobal(L, "Think");
	if (lua_pcall(L, 0, 0, 0))
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

	LOCAL_L;
	lua_pushnumber(L, m_CurrentTime);
	lua_setglobal(L, "m_CurrentTime");

	lua_pushnumber(L, m_DeltaTime);
	lua_setglobal(L, "m_DeltaTime");

	lua_pushnumber(L, m_PrevTime);
	lua_setglobal(L, "m_PrevTime");
	
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

double CWagon::DeltaTime()
{
	return m_DeltaTime;
}

void CWagon::SetEntIndex(int idx)
{
	LOCAL_L;
	m_EntIndex = idx;

	lua_pushnumber(L, m_EntIndex);
	lua_setglobal(L, "m_EntIndex");
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
