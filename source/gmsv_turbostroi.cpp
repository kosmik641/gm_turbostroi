#include "gmsv_turbostroi.h"
using namespace GarrysMod::Lua;
//------------------------------------------------------------------------------
// Global variables
//------------------------------------------------------------------------------
bool g_ForceThreadsFinished = false; // For correct unrequire module
CGlobalVars* g_pServerGlobalVars = nullptr;
std::atomic<float> g_CurrentTime = 0.0f;
unsigned int g_ThreadTickrate = 10000; // [mcs] (10ms)
int g_SimThreadAffinityMask = 0xFFFFFFFF;
std::vector<TTrainSystem> g_MetrostroiSystemList;
std::queue<TTrainSystem> g_LoadSystemList;
std::unordered_map<std::string, std::string> g_LoadedFilesCache;
SharedPrint g_SharedPrint;

//------------------------------------------------------------------------------
// Global variables for Source SDK
//------------------------------------------------------------------------------
extern ICvar* cvar;
extern ICvar* g_pCVar;
static ConVar g_CVarDisableCache("turbostroi_disable_cache", "0", FCVAR_NONE, "Disable scripts cache for development");
static ConCommand g_CmdClearCache("turbostroi_clear_cache", ClearLoadCache, "Clear cache for reload systems");
static ConCommand g_CmdClearPrint("turbostroi_clear_print", ClearPrintQueue, "Clear print queue");

//------------------------------------------------------------------------------
// Turbostroi sim thread API
//------------------------------------------------------------------------------
extern "C" TURBOSTROI_EXPORT bool ThreadSendMessage(void* p, int message, const char* system_name, const char* name, double index, double value)
{
	CWagon* userdata = (CWagon*)p;

	if (userdata == nullptr)
		return false;

	return userdata->ThreadSendMessage(message, system_name, name, index, value);
}

extern "C" TURBOSTROI_EXPORT TThreadMsg ThreadRecvMessage(void* p)
{
	CWagon* userdata = (CWagon*)p;

	if (userdata == nullptr)
		return TThreadMsg();

	return userdata->ThreadRecvMessage();;
}

extern "C" TURBOSTROI_EXPORT int ThreadReadAvailable(void* p)
{
	CWagon* userdata = (CWagon*)p;

	if (userdata == nullptr)
		return 0;

	return userdata->ThreadReadAvailable();
}

//------------------------------------------------------------------------------
// Turbostroi sim thread
//------------------------------------------------------------------------------
void threadSimulation(CWagon* userdata)
{
#if defined(_WIN32)
	if (!SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(g_SimThreadAffinityMask)))
	{
		ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: SetSTAffinityMask failed on train thread!\n");
	}
#elif defined(POSIX)
	cpu_set_t cpuSet;
	CPU_ZERO(&cpuSet);

	for (int i = 0; i < 32; i++)
		if (g_SimThreadAffinityMask & (1 << i))
			CPU_SET(i, &cpuSet);

	if (sched_setaffinity(getpid(), sizeof(cpuSet), &cpuSet))
		ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: SetSTAffinityMask failed on train thread! (0x%04X)\n", errno);
#else
	ConColorMsg(Color(255, 255, 0, 255), "Turbostroi: Set affinity not supported on your system!");
#endif

	if (userdata == nullptr)
	{
		ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: Fail to get thread userdata!\n");
		return;
	}

	std::this_thread::sleep_for(std::chrono::microseconds(g_ThreadTickrate)); // Wait for first messages from engine
	while (!g_ForceThreadsFinished && userdata && !userdata->IsFinished())
	{
		if (!userdata->UpdateCurTime(g_CurrentTime)) // Wait for server update
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(3));
			continue;
		}
		userdata->Think();

		std::this_thread::sleep_for(std::chrono::microseconds(g_ThreadTickrate));
	}

	//Release resources
	g_SharedPrint.Push("[!] Terminating train thread\n");
	delete userdata;
}

//------------------------------------------------------------------------------
// Metrostroi Lua API
//------------------------------------------------------------------------------
void loadLua(GarrysMod::Lua::ILuaBase* LUA, CWagon* userdata, const char* filename)
{
	// Get file from cache
	bool useCache = !g_CVarDisableCache.GetBool();
	if (useCache)
	{
		auto cache_item = g_LoadedFilesCache.find(filename);
		if (cache_item != g_LoadedFilesCache.end())
		{
			userdata->LoadBuffer(cache_item->second.c_str(), filename);
			return;
		}
	}

	// Read file
	const char* file_data = nullptr;
	LUA->GetField(-1, "Read");
		LUA->PushString(filename);
		LUA->PushString("LUA");
	LUA->Call(2, 1); //file.Read(filename, "LUA")
	file_data = LUA->GetString(-1);
	LUA->Pop();

	// Load file to CWagon
	if (file_data != nullptr)
	{
		userdata->LoadBuffer(file_data, filename);
		if (useCache) g_LoadedFilesCache.emplace(filename, file_data);
	}
	else
		ConColorMsg(Color(255, 0, 127, 255), "Turbostroi: File not found! ('%s')\n", filename);
}

int GetEntIndex(GarrysMod::Lua::ILuaBase* LUA, int iStackPos)
{	
	if (LUA->GetType(iStackPos) != GarrysMod::Lua::Type::Entity)
		return -1;

	int entIndex = -1;
	LUA->GetField(iStackPos, "EntIndex");
		LUA->Push(iStackPos); // WagonEnt
			LUA->Call(1, 1); // Entity.EntIndex()
			entIndex = LUA->GetNumber(-1);
		LUA->Pop(); // Result
	return entIndex;
}

LUA_FUNCTION( API_InitializeTrain ) 
{
	CWagon* userdata = new CWagon();

	// Store entity index of wagon
	userdata->SetEntIndex(GetEntIndex(LUA, 1));

	// If cache disabled, clear it
	if (g_CVarDisableCache.GetBool())
		g_LoadedFilesCache.clear();
	
	// Push GLOB, file on top
	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
	LUA->GetField(-1, "file");

	// Load neccessary files
	loadLua(LUA, userdata, "metrostroi/lib_turbostroi_v2.lua");
	loadLua(LUA, userdata, "metrostroi/sh_failsim.lua");

	// Load up all the systems
	for (TTrainSystem sys : g_MetrostroiSystemList)
	{
		loadLua(LUA, userdata, sys.file_name.c_str());
	}

	// Pop GLOB, file
	LUA->Pop(2);

	// Initialize all the systems reported by the train
	while (!g_LoadSystemList.empty())
	{
		userdata->AddLoadSystem(g_LoadSystemList.front());
		g_LoadSystemList.pop();
	}

	// Run initialize
	userdata->Initialize();

	// _sim_userdata = *CWagon
	LUA->PushUserType(userdata, 1);
	LUA->SetField(1, "_sim_userdata");

	//Create thread for simulation
	std::thread thread(threadSimulation, userdata);
	thread.detach();

	return 0;
}

LUA_FUNCTION( API_DeinitializeTrain ) 
{
	LUA->GetField(1,"_sim_userdata");
		CWagon* userdata = LUA->GetUserType<CWagon>(-1, 1);
		if (userdata) userdata->Finish();
	LUA->Pop();

	LUA->PushNil();
	LUA->SetField(1,"_sim_userdata");

	return 0;
}

LUA_FUNCTION( API_LoadSystem ) 
{
	const char* basename = LUA->GetString(1);
	const char* name = LUA->GetString(2);
	if (!basename || !name)
		return 0;

	g_LoadSystemList.emplace(name, basename);
	return 0;
}

LUA_FUNCTION( API_RegisterSystem ) 
{
	const char* name = LUA->GetString(1);
	const char* filename = LUA->GetString(2);
	if (!name || !filename)
		return 0;
	
	g_MetrostroiSystemList.emplace_back(name, filename);
	ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: Registering system %s [%s]\n", name, filename);
	return 0;
}

LUA_FUNCTION( API_SendMessage ) 
{
	LUA->GetField(1, "_sim_userdata");
	CWagon* userdata = LUA->GetUserType<CWagon>(-1, 1);
	LUA->Pop();

	if (userdata == nullptr)
	{
		LUA->PushBool(false);
		return 1;
	}

	LUA->CheckType(2,Type::Number);
	LUA->CheckType(3,Type::String);
	LUA->CheckType(4,Type::String);
	LUA->CheckType(5,Type::Number);
	LUA->CheckType(6,Type::Number);

	int message = LUA->GetNumber(2);
	const char* system_name = LUA->GetString(3);
	const char* name = LUA->GetString(4);
	double index = LUA->GetNumber(5);
	double value = LUA->GetNumber(6);

	bool sended = userdata->SimSendMessage(message,
		system_name,name,
		index,value);

	LUA->PushBool(sended);
	return 1;
}

LUA_FUNCTION( API_RecvMessages ) 
{
	// Not used
	return 0;
}

LUA_FUNCTION( API_RecvMessage ) 
{
	LUA->GetField(1, "_sim_userdata");
	CWagon* userdata = LUA->GetUserType<CWagon>(-1, 1);
	LUA->Pop();

	if (userdata == nullptr)
		return 0;

	TThreadMsg tmsg = userdata->SimRecvMessage();
	LUA->PushNumber(tmsg.message);
	LUA->PushString(tmsg.system_name);
	LUA->PushString(tmsg.name);
	LUA->PushNumber(tmsg.index);
	LUA->PushNumber(tmsg.value);
	return 5;
}

LUA_FUNCTION( API_ReadAvailable ) 
{
	LUA->GetField(1, "_sim_userdata");
	CWagon* userdata = LUA->GetUserType<CWagon>(-1, 1);
	LUA->Pop();

	if (userdata != nullptr)
		LUA->PushNumber(userdata->SimReadAvailable());
	else
		LUA->PushNumber(0);

	return 1;
}

LUA_FUNCTION( API_SetSimulationFPS ) 
{
	LUA->CheckType(1,Type::Number);
	double FPS = LUA->GetNumber(1);
	if (!FPS)
		return 0;

	g_ThreadTickrate = (1000000.0 / FPS);
	ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Changed to %d FPS (%.02f ms delay)\n", (int)(FPS + 0.5), g_ThreadTickrate / 1000.0f);
	return 0;
}

LUA_FUNCTION( API_SetMTAffinityMask ) 
{
	LUA->CheckType(1, Type::Number);
	int MTAffinityMask = (int)LUA->GetNumber(1);

	if (MTAffinityMask == 0)
	{
		ConColorMsg(Color(255, 255, 0, 255), "Turbostroi: Affinity mask 0 not allowed.\n");
		MTAffinityMask = 0xFFFFFFFF; // Set mask to all processors
	}

#if defined(_WIN32)
	ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Main Thread Running on CPU%i\n", GetCurrentProcessorNumber());

	if (!SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(MTAffinityMask)))
		ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: SetMTAffinityMask failed!\n");
	else
		ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Changed to CPU%i\n", GetCurrentProcessorNumber());
#elif defined(POSIX)
	cpu_set_t cpuSet;
	CPU_ZERO(&cpuSet);

	for (int i = 0; i < 32; i++)
		if (MTAffinityMask & (1 << i))
			CPU_SET(i, &cpuSet);

	if (sched_setaffinity(getpid(), sizeof(cpuSet), &cpuSet))
		ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: SetMTAffinityMask failed on train thread! (0x%04X)\n", errno);
	else
		ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Changed to CPU%i\n", sched_getcpu());
#else
	ConColorMsg(Color(255, 255, 0, 255), "Turbostroi: Set affinity not supported on your system!");
#endif

	return 0;
}

LUA_FUNCTION( API_SetSTAffinityMask ) 
{
	LUA->CheckType(1, Type::Number);
	g_SimThreadAffinityMask = LUA->GetNumber(1);

	if (g_SimThreadAffinityMask == 0)
	{
		ConColorMsg(Color(255, 255, 0, 255), "Turbostroi: Affinity mask 0 not allowed.\n");
		g_SimThreadAffinityMask = 0xFFFFFFFF; // Set mask to all processors
	}
	
	ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Assign Train Threads affinity to %d\n", g_SimThreadAffinityMask);
	return 0;
}

LUA_FUNCTION( API_StartRailNetwork ) 
{
	// Not implemented
	return 0;
}

//------------------------------------------------------------------------------
// Initialization SourceSDK
//------------------------------------------------------------------------------
//extern CGlobalVars* gpGlobals;
LUA_FUNCTION_DECLARE( Think_handler )
{
	g_CurrentTime = g_pServerGlobalVars->curtime;
	g_SharedPrint.PrintAvailable();
	return 0;
}

void InstallHooks(ILuaBase* LUA)
{
	ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: Installing hooks!\n");
	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
		LUA->GetField(-1, "hook");
			LUA->GetField(-1, "Add");
				LUA->PushString("Think");
				LUA->PushString("Turbostroi_TargetTimeSync");
				LUA->PushCFunction(Think_handler);
			LUA->Call(3, 0);
	LUA->Pop(2);
}

void ClearLoadCache(const CCommand &command)
{
	auto cacheSize = g_LoadedFilesCache.size();
	if (g_LoadedFilesCache.empty())
		ConColorMsg(Color(255, 255, 0, 255), "Turbostroi: No files in cache. Nothing to clear.\n");
	else
	{
		g_LoadedFilesCache.clear();
		ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Cleared %d files in cache!\n", cacheSize);
	}	
}

void ClearPrintQueue(const CCommand& command)
{
	g_SharedPrint.ClearPrintQueue();
}

//------------------------------------------------------------------------------
// SourceSDK
//------------------------------------------------------------------------------
bool RegisterConCommands()
{
	SourceSDK::FactoryLoader vstdlib_loader("vstdlib");
	cvar = g_pCVar = vstdlib_loader.GetInterface<ICvar>(CVAR_INTERFACE_VERSION);
	if (g_pCVar == nullptr)
	{
		ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: Unable to load CVAR Interface!\n");
		return false;
	}

	g_pCVar->RegisterConCommand(&g_CmdClearCache);
	g_pCVar->RegisterConCommand(&g_CmdClearPrint);
	g_pCVar->RegisterConCommand(&g_CVarDisableCache);
	return true;
}

bool GetGlobalVars()
{
	SourceSDK::FactoryLoader server_loader("server");
	IPlayerInfoManager* pIPlayerInfoManager = server_loader.GetInterface<IPlayerInfoManager>(INTERFACEVERSION_PLAYERINFOMANAGER);

	if (pIPlayerInfoManager == nullptr)
	{
		ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: Unable to load PlayerInfoManager Interface!\n");
		return false;
	}
	g_pServerGlobalVars = pIPlayerInfoManager->GetGlobalVars();
	
	return (g_pServerGlobalVars != nullptr);
}

//------------------------------------------------------------------------------
// Initialization
//------------------------------------------------------------------------------
GMOD_MODULE_OPEN()
{
	if (!GetGlobalVars())
		return 0;

	if (!RegisterConCommands())
		return 0;

	//Check whether being ran on server
	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
		LUA->GetField(-1,"SERVER");
		if (LUA->IsType(-1, Type::Nil))
		{
			ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: DLL failed to initialize (gm_turbostroi.dll can only be used on server)\n");
			return 0;
		}
		LUA->Pop(); //SERVER

		//Check for global table
		LUA->GetField(-1,"Metrostroi");
		if (LUA->IsType(-1, Type::Nil))
		{
			ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: DLL failed to initialize (cannot be used standalone without metrostroi addon)\n");
			return 0;
		}
		LUA->Pop(); //Metrostroi
		
		InstallHooks(LUA);

		//Initialize API
		LUA->CreateTable();
			PushCFunc(API_InitializeTrain, "InitializeTrain");
			PushCFunc(API_DeinitializeTrain, "DeinitializeTrain");
			PushCFunc(API_StartRailNetwork, "StartRailNetwork");

			PushCFunc(API_SendMessage, "SendMessage");
			PushCFunc(API_RecvMessages, "RecvMessages");
			PushCFunc(API_RecvMessage, "RecvMessage");

			PushCFunc(API_LoadSystem, "LoadSystem");
			PushCFunc(API_RegisterSystem, "RegisterSystem");

			PushCFunc(API_ReadAvailable, "ReadAvailable");
			PushCFunc(API_SetSimulationFPS, "SetSimulationFPS");
			PushCFunc(API_SetMTAffinityMask, "SetMTAffinityMask");
			PushCFunc(API_SetSTAffinityMask, "SetSTAffinityMask");
		LUA->SetField(-2,"Turbostroi");
	LUA->Pop();

	//Print some information
	ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: DLL initialized (built " __DATE__ ")\n");
	ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: Running with %i cores\n", std::thread::hardware_concurrency());

	if (!g_CurrentTime.is_lock_free())
		ConColorMsg(Color(255, 255, 0, 255), "Turbostroi: Not fully supported! Perfomance may be decreased.\n");

	return 0;
}

//------------------------------------------------------------------------------
// Deinitialization
//------------------------------------------------------------------------------
GMOD_MODULE_CLOSE()
{
	g_ForceThreadsFinished = true;
	if (g_pCVar != nullptr)
	{
		g_pCVar->UnregisterConCommand(&g_CmdClearCache);
		g_pCVar->UnregisterConCommand(&g_CmdClearPrint);
		g_pCVar->UnregisterConCommand(&g_CVarDisableCache);
	}

	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
		LUA->GetField(-1, "Metrostroi");
			if (LUA->IsType(-1, Type::Table)) {
				LUA->CreateTable();
				LUA->SetField(-2, "TurbostroiRegistered");
			}
		LUA->Pop();
		LUA->GetField(-1, "hook");
			LUA->GetField(-1, "Remove");
				LUA->PushString("Think");
				LUA->PushString("Turbostroi_TargetTimeSync");
				LUA->Call(2, 0);
	LUA->Pop(2);
	
	LUA->PushNil();
	LUA->SetField(GarrysMod::Lua::INDEX_GLOBAL, "Turbostroi");

	ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: DLL unloaded.\n");
	return 0;
}