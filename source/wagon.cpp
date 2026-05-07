#include "wagon.h"

#include "gmsv_turbostroi.h"
#include "affinity.h"
#include "shared_print.h"
#include <const.h>
#include <cstring>
#include <thread>
#include <array>
#include <lua.hpp>

extern "C"
{
#include "lj_obj.h"
}

#define LOCAL_L lua_State* L = m_Lua.L
#define LOCAL_SELF CWagon* self = static_cast<TLuaData*>(G(L)->allocd)->self

// CWagon pointers
static std::array<CWagon*, MAX_EDICTS> g_Wagons{};

// RunString() flag
bool g_RunStringEnabled = false;

// Wrapper to LuaJIT allocator
static void* l_alloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
	TLuaData* d = static_cast<TLuaData*>(ud);
	return lj_alloc_f(d->msp, ptr, osize, nsize);
}


CWagon* CWagon::Create(unsigned int idx)
{
	if (idx > 0 && idx < g_Wagons.size())
		return new CWagon(idx);

	return nullptr;
}

CWagon* CWagon::CWagonByIndex(unsigned int idx)
{
	if (idx > 0 && idx < g_Wagons.size()) // 0 is World
		return g_Wagons[idx];

	return nullptr;
}

CWagon::CWagon(unsigned int idx)
{
	m_StartTime = std::chrono::steady_clock::now();
	m_Thread2Sim = reinterpret_cast<TThreadMsgBuffer*>(std::calloc(1, sizeof(TThreadMsgBuffer)));
	m_Sim2Thread = reinterpret_cast<TThreadMsgBuffer*>(std::calloc(1, sizeof(TThreadMsgBuffer)));

	// Store entity index and pointer
	m_EntIndex = idx;
	AddToArray();

	// Alloc lua environment
	m_Lua.L = luaL_newstate();
	lua_getallocf(m_Lua.L, &m_Lua.msp); // Get original mspace
	lua_setallocf(m_Lua.L, l_alloc, &m_Lua); // Replace with our data

	LOCAL_L;
	luaL_openlibs(L);

	// TURBOSTROI = true
	lua_pushboolean(L, true);
	lua_setglobal(L, "TURBOSTROI");

	// LIB_TURBOSTROI_FILENAME = g_LibraryFileName
	lua_pushstring(L, g_LibraryFileName.c_str());
	lua_setglobal(L, "LIB_TURBOSTROI_FILENAME");

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

	// _CWagon = this
	lua_pushlightuserdata(L, this);
	lua_setglobal(L, "_CWagon");
}

CWagon::~CWagon()
{
	lua_close(m_Lua.L);
	lj_alloc_destroy(m_Lua.msp);
	RemoveFromArray();

	std::free(reinterpret_cast<void*>(m_Sim2Thread));
	std::free(reinterpret_cast<void*>(m_Thread2Sim));
}

bool CWagon::SimSendMessage(int message, const char* system_name, const char* name, double index, double value)
{
	TThreadMsg tmsg;
	tmsg.message = message;
	tmsg.system_name = system_name;
	tmsg.name = name;
	tmsg.index = index;
	tmsg.value = value;

	return m_Sim2Thread->push(tmsg);
}

int CWagon::SimRecvMessages([[maybe_unused]] std::unique_ptr<TThreadMsg[]>& tmsgs)
{
	// Not used
	return 0;
}

TThreadMsg& CWagon::SimRecvMessage()
{
	if (m_Thread2Sim->pop(m_Thread2SimMsg))
		return m_Thread2SimMsg;
	else
		return s_EmptyMsg;
}

int CWagon::SimReadAvailable()
{
	return m_Thread2Sim->size();
}

bool CWagon::ThreadSendMessage(int message, const char* system_name, const char* name, double index, double value)
{
	TThreadMsg tmsg;
	tmsg.message = message;
	tmsg.system_name = system_name;
	tmsg.name = name;
	tmsg.index = index;
	tmsg.value = value;

	return m_Thread2Sim->push(tmsg);
}

int CWagon::ThreadRecvMessages([[maybe_unused]] std::unique_ptr<TThreadMsg[]>& tmsgs)
{
	// Not used
	return 0;
}

int CWagon::ThreadRecvMessages([[maybe_unused]] lua_State* L)
{
	// Not used
	return 0;
}

TThreadMsg& CWagon::ThreadRecvMessage()
{
	if (m_Sim2Thread->pop(m_Sim2ThreadMsg))
		return m_Sim2ThreadMsg;
	else
		return s_EmptyMsg;
}

int CWagon::ThreadReadAvailable()
{
	return m_Sim2Thread->size();
}

bool CWagon::LoadBuffer(const char* buf, size_t size, const char* filename)
{
	LOCAL_L;
	if (luaL_loadbuffer(L, buf, size, filename)
		|| lua_pcall(L, 0, LUA_MULTRET, 0))
	{
		std::string err = lua_tostring(L, -1);
		err += '\n';
		g_SharedPrint.Push(err);
		lua_pop(L, 1);
		return false;
	}

	return true;
}

bool CWagon::CheckLibLoaded()
{
	LOCAL_L;

	lua_getglobal(L, "LIB_TURBOSTROI_VERSION");
	if (lua_type(L, -1) == LUA_TSTRING)
	{
		const char* ver = lua_tostring(L, -1);
		lua_pop(L, 1);

		if (strcmp(ver, TURBOSTROI_VERSION) == 0)
			return true;
		
		g_SharedPrint.Push("[!] Incompatable lib_turbostroi_v2.lua version (");
		g_SharedPrint.Push(ver);
		g_SharedPrint.Push("), please update to " TURBOSTROI_VERSION_PRINT "\n");
		return false;
	}
	else
	{
		g_SharedPrint.Push("[!] Incompatable lib_turbostroi_v2.lua version, please update to " TURBOSTROI_VERSION_PRINT "\n");
		return false;
	}
}

void CWagon::AddLoadSystem(TTrainSystem& sys)
{
	LOCAL_L;
	lua_getglobal(L, "LoadSystems");
	{
		if (lua_type(L, -1) != LUA_TTABLE)
		{
			lua_pop(L, 1);
			lua_newtable(L);
			lua_setglobal(L, "LoadSystems");
			lua_getglobal(L, "LoadSystems");
		}

		lua_newtable(L);
		{
			lua_pushstring(L, sys.file_name.c_str());
			lua_rawseti(L, -2, 1);

			lua_pushstring(L, sys.base_name.c_str());
			lua_rawseti(L, -2, 2);
		}
		lua_rawseti(L, -2, ++m_SystemCount);
	}
	lua_settop(L, 0);
}

void CWagon::RunString(const char* buf, unsigned int uid)
{
	if (!g_RunStringEnabled)
		return;

	LOCAL_L;
	static std::string str;
	
	str.clear();
	str += 
R"======(local _retdata=""
local print = function(...)
for k,v in ipairs({...}) do _retdata = _retdata..tostring(v).."\t" end
    _retdata = _retdata.."\n"
end
)======";

	str += buf;
	str += "\nreturn _retdata";

	m_RunStringMutex.lock();
	{
		static std::string _retdata;
		int load = luaL_loadbuffer(L, str.c_str(), str.size(), nullptr);
		if (load == 0) lua_pcall(L, 0, 1, 0);

		_retdata.clear();
		_retdata = lua_tostring(L, -1);
		lua_pop(L, 1);

		ThreadSendMessage(5, _retdata.c_str(), "", uid, EntIndex());
	}
	m_RunStringMutex.unlock();
}

//------------------------------------------------------------------------------
// Simulation thread
//------------------------------------------------------------------------------
void CWagon::SimulationThreadFn()
{
	m_ThreadRunning = true;
	if (SetAffinityMask(CurrentThread(), g_SimThreadGroupAffinity))
	{
		std::string str = "[!] Train thread running on CPU";
		str += std::to_string(CurrentCPU());
		str += "\n";

		g_SharedPrint.Push(str);
	}
	else
	{
		g_SharedPrint.Push("Turbostroi: Failed to set affinity mask of train thread!\n");
	}

	// Save Think() to registry
	LOCAL_L;
	lua_getglobal(L, "Think");
	m_ThinkRef = luaL_ref(L, LUA_REGISTRYINDEX);

	// Run initialize
	Initialize();

	// Wait for first messages from engine
	std::this_thread::sleep_for(std::chrono::microseconds(g_ThreadTickrate));

	// Run think
	while (!g_ForceThreadsFinished && !m_Finish)
	{
		double curTime = g_CurrentTime.load(std::memory_order_relaxed);
		if (!UpdateCurTime(curTime)) // Wait for server update
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(3));
			continue;
		}

		if (g_RunStringEnabled) m_RunStringMutex.lock();
		Think();
		if (g_RunStringEnabled) m_RunStringMutex.unlock();

		std::this_thread::sleep_for(std::chrono::microseconds(g_ThreadTickrate));
	}

	//Release resources
	lua_unref(L, m_ThinkRef);
	g_SharedPrint.Push("[!] Terminating train thread\n");
	m_ThreadRunning = false;
}

void CWagon::Initialize()
{
	LOCAL_L;
	lua_getglobal(L, "Initialize");
	if (lua_pcall(L, 0, 0, 0))
	{
		std::string err = lua_tostring(L, -1);
		err += '\n';
		g_SharedPrint.Push(err);
		lua_pop(L, 1);
	}
}

void CWagon::Think()
{
	LOCAL_L;
	lua_getref(L, m_ThinkRef);
	lua_pushnumber(L, m_DeltaTime);
	if (lua_pcall(L, 1, 0, 0))
	{
		std::string err = lua_tostring(L, -1);
		err += '\n';
		g_SharedPrint.Push(err);
		lua_pop(L, 1);
	}
}

bool CWagon::UpdateCurTime(double t)
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

void CWagon::SetEntIndex(unsigned int idx)
{
	m_EntIndex = idx;
}

unsigned int CWagon::EntIndex()
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
	m_Finish = true;
}

bool CWagon::ThreadRunning()
{
	return m_ThreadRunning;
}

void CWagon::AddToArray()
{
	if (m_EntIndex > 0 && m_EntIndex < g_Wagons.size())
		g_Wagons[m_EntIndex] = this;
}

void CWagon::RemoveFromArray()
{
	if (m_EntIndex > 0 && m_EntIndex < g_Wagons.size())
		g_Wagons[m_EntIndex] = nullptr;
}

//------------------------------------------------------------------------------
// Turbostroi sim thread API for FFI
//------------------------------------------------------------------------------
extern "C" TURBOSTROI_EXPORT bool ThreadSendMessage(void* p, int message, const char* system_name, const char* name, double index, double value)
{
	CWagon* userdata = static_cast<CWagon*>(p);

	if (userdata == nullptr)
		return false;

	return userdata->ThreadSendMessage(message, system_name, name, index, value);
}

extern "C" TURBOSTROI_EXPORT TThreadMsg& ThreadRecvMessage(void* p)
{
	CWagon* userdata = static_cast<CWagon*>(p);

	if (userdata == nullptr)
		return CWagon::s_EmptyMsg;

	return userdata->ThreadRecvMessage();
}

extern "C" TURBOSTROI_EXPORT int ThreadReadAvailable(void* p)
{
	CWagon* userdata = static_cast<CWagon*>(p);

	if (userdata == nullptr)
		return 0;

	return userdata->ThreadReadAvailable();
}
