#include "gmsv_turbostroi.h"
namespace GM = GarrysMod::Lua;

//------------------------------------------------------------------------------
// Global variables
//------------------------------------------------------------------------------
bool g_ForceThreadsFinished = false; // For correct unrequire module
std::atomic<float> g_CurrentTime = 0.0f;
unsigned int g_ThreadTickrate = 10000; // [mcs] (10ms)
std::vector<TTrainSystem> g_MetrostroiSystemList;
std::queue<TTrainSystem> g_LoadSystemList;
std::unordered_map<std::string, std::string> g_LoadedFilesCache;
extern SharedPrint g_SharedPrint;

//------------------------------------------------------------------------------
// Global variables for Source SDK
//------------------------------------------------------------------------------
extern ICvar* cvar;
extern ICvar* g_pCVar;
extern CGlobalVars* g_pServerGlobalVars;
extern ConVar g_CVarDisableCache;
extern ConVar g_CVarMainCores;
extern ConVar g_CVarTrainCores;

//------------------------------------------------------------------------------
// Affinity groups
//------------------------------------------------------------------------------
extern size_t g_ProcessorCount;
#if defined(_WIN32)
extern std::array<GROUP_AFFINITY, 32> g_MainThreadGroupAffinity;
extern std::array<GROUP_AFFINITY, 32> g_SimThreadGroupAffinity;
#elif defined(POSIX)
extern cpu_set_t g_MainThreadGroupAffinity;
extern cpu_set_t g_SimThreadGroupAffinity;
#else
extern size_t g_MainThreadGroupAffinity;
extern size_t g_SimThreadGroupAffinity;
#endif

//------------------------------------------------------------------------------
// Turbostroi sim thread
//------------------------------------------------------------------------------
void threadSimulation(CWagon* userdata)
{
	if (userdata == nullptr)
	{
		ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: Fail to get thread userdata!\n");
		return;
	}

	if (SetAffinityMask(CurrentThread(), g_SimThreadGroupAffinity))
	{
		std::string str = "[!] Train thread running on CPU";
		str += std::to_string(CurrentCPU());
		str += "\n";

		g_SharedPrint.Push(str);
	}
	else
	{
		ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: Failed to set affinity mask of train thread!\n");
	}

	userdata->SimulationThreadFn();

	//Release resources
	g_SharedPrint.Push("[!] Terminating train thread\n");
	delete userdata;
}

//------------------------------------------------------------------------------
// Metrostroi Lua API
//------------------------------------------------------------------------------
bool loadLua(GM::ILuaBase* LUA, CWagon* userdata, const char* filename)
{
	// Get file from cache
	bool useCache = !g_CVarDisableCache.GetBool();
	if (useCache)
	{
		auto cache_item = g_LoadedFilesCache.find(filename);
		if (cache_item != g_LoadedFilesCache.end())
		{
			return userdata->LoadBuffer(cache_item->second.c_str(), filename);
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
		bool loaded = userdata->LoadBuffer(file_data, filename);
		if (useCache && loaded) g_LoadedFilesCache.emplace(filename, file_data);
		return loaded;
	}
	else
	{
		ConColorMsg(Color(255, 0, 127, 255), "Turbostroi: File not found! ('%s')\n", filename);
		return true; // Ignore not founded file
	}
}

int GetEntIndex(GM::ILuaBase* LUA, int iStackPos)
{	
	if (LUA->GetType(iStackPos) != GM::Type::Entity)
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

	// Train._CWagon = *CWagon
	LUA->PushUserType(userdata, GM::Type::LightUserData);
	LUA->SetField(1, "_CWagon");

	// Store entity index of wagon
	userdata->SetEntIndex(GetEntIndex(LUA, 1));

	// If cache disabled, clear it
	if (g_CVarDisableCache.GetBool())
		g_LoadedFilesCache.clear();
	
	// Push GLOB, file on top
	LUA->PushSpecial(GM::SPECIAL_GLOB);
	LUA->GetField(-1, "file");

	// Load neccessary files
	if (!loadLua(LUA, userdata, "metrostroi/lib_turbostroi_v2.lua") || !userdata->CheckLibLoaded())
	{
		ConColorMsg(Color(255, 0, 255, 255), "[!] Fail to load lib_turbostroi_v2.lua\n");

		// Pop GLOB, file
		LUA->Pop(2);

		// Train.DontAccelerateSimulation = true
		LUA->PushBool(true);
		LUA->SetField(1, "DontAccelerateSimulation");

		// Train._CWagon = nil
		LUA->PushNil();
		LUA->SetField(1, "_CWagon");
		delete userdata;

		// Hook call
		HookRunTrainEnt(LUA, 1);
		return 0;
	}

	// Load up all the systems
	loadLua(LUA, userdata, "metrostroi/sh_failsim.lua");
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

	// Hook call
	HookRunTrainEnt(LUA, 1);

	//Create thread for simulation
	std::thread thread(threadSimulation, userdata);
	thread.detach();

	return 0;
}

LUA_FUNCTION( API_DeinitializeTrain ) 
{
	LUA->GetField(1,"_CWagon");
		CWagon* userdata = LUA->GetUserType<CWagon>(-1, GM::Type::LightUserData);
		if (userdata) userdata->Finish();
	LUA->Pop();

	LUA->PushNil();
	LUA->SetField(1,"_CWagon");

	HookRunTrainEnt(LUA, 1, true);
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

LUA_FUNCTION( API_TriggerInput )
{
	if (!LUA->IsType(1, GM::Type::Entity) ||
		!LUA->IsType(2, GM::Type::String) ||
		!LUA->IsType(3, GM::Type::String))
		return 0;

	LUA->GetField(1, "_CWagon");
	CWagon* userdata = LUA->GetUserType<CWagon>(-1, GM::Type::LightUserData);
	LUA->Pop();

	if (userdata == nullptr)
		return 0;

	const char* system_name = LUA->GetString(2);
	const char* name = LUA->GetString(3);
	double value;

	int valueT = LUA->GetType(4);
	if (valueT == GM::Type::Number)
		value = LUA->GetNumber(4);
	else if (valueT == GM::Type::Bool)
		value = LUA->GetBool(4);
	else
		value = 0;

	userdata->SimSendMessage(3, system_name, name, 0, value);

	return 0;
}

LUA_FUNCTION( API_SendMessage ) 
{
	CWagon* userdata = LUA->GetUserType<CWagon>(1, GM::Type::LightUserData);

	if (userdata == nullptr)
	{
		LUA->PushBool(false);
		return 1;
	}

	LUA->CheckType(2,GM::Type::Number);
	LUA->CheckType(3,GM::Type::String);
	LUA->CheckType(4,GM::Type::String);
	LUA->CheckType(5,GM::Type::Number);
	LUA->CheckType(6,GM::Type::Number);

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
	CWagon* userdata = LUA->GetUserType<CWagon>(1, GM::Type::LightUserData);

	if (userdata == nullptr)
		return 0;

	TThreadMsg& tmsg = userdata->SimRecvMessage();
	LUA->PushNumber(tmsg.message);
	LUA->PushString(tmsg.system_name);
	LUA->PushString(tmsg.name);
	LUA->PushNumber(tmsg.index);
	LUA->PushNumber(tmsg.value);
	return 5;
}

LUA_FUNCTION( API_ReadAvailable ) 
{
	CWagon* userdata = LUA->GetUserType<CWagon>(1, GM::Type::LightUserData);

	if (userdata != nullptr)
		LUA->PushNumber(userdata->SimReadAvailable());
	else
		LUA->PushNumber(0);

	return 1;
}

LUA_FUNCTION( API_SetSimulationFPS ) 
{
	LUA->CheckType(1, GM::Type::Number);
	double FPS = LUA->GetNumber(1);
	if (FPS == 0)
		return 0;

	g_ThreadTickrate = (1000000.0 / FPS);
	ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Changed to %d FPS (%.02f ms delay)\n", (int)(FPS + 0.5), g_ThreadTickrate / 1000.0f);
	return 0;
}

LUA_FUNCTION( API_SetMTAffinityMask )
{
	// Removed and saved for compability
	return 0;
}

LUA_FUNCTION( API_SetSTAffinityMask ) 
{
	// Removed and saved for compability
	return 0;
}

LUA_FUNCTION( API_StartRailNetwork ) 
{
	// Not implemented
	return 0;
}

//------------------------------------------------------------------------------
// SourceSDK
//------------------------------------------------------------------------------
LUA_FUNCTION_DECLARE( Think_handler )
{
	g_CurrentTime = g_pServerGlobalVars->curtime;
	g_SharedPrint.PrintAvailable();
	return 0;
}

void HookRunTrainEnt(GM::ILuaBase* LUA, int entStackPos, bool remove)
{
	if (LUA == nullptr)
		return;

	LUA->PushSpecial(GM::SPECIAL_GLOB);
	{
		LUA->GetField(-1, "hook");
		if (LUA->IsType(-1, GM::Type::Table))
		{
			LUA->GetField(-1, "Run");
			if (LUA->IsType(-1, GM::Type::Function))
			{
				LUA->PushString(remove ? "TurbostroiWagonRemove" : "TurbostroiWagonCreate");
				LUA->Push(entStackPos);
				LUA->Call(2, 0);
			}
		}
	}
	LUA->Pop(2);
}

void CVarMainCoresCallback(IConVar* var, const char* pOldValue, float flOldValue)
{
	ConVarRef ref(var);

	if (!SetThreadGroup(g_MainThreadGroupAffinity, ref.GetString()))
		return;

	ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Main thread running on CPU%d\n", CurrentCPU());
	if (!SetAffinityMask(CurrentThread(), g_MainThreadGroupAffinity))
	{
		ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: Set main thread affinity mask failed!\n");
	}
	else
	{
		ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Changed to CPU%d\n", CurrentCPU());
	};
}

void CVarTrainCoresCallback(IConVar* var, const char* pOldValue, float flOldValue)
{
	ConVarRef ref(var);
	SetThreadGroup(g_SimThreadGroupAffinity, ref.GetString());
}

void InstallHooks(GM::ILuaBase* LUA)
{
	ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: Installing hooks!\n");
	LUA->PushSpecial(GM::SPECIAL_GLOB);
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
// Initialization
//------------------------------------------------------------------------------
GMOD_MODULE_OPEN()
{
	if (!InitSourceSDK())
		return 0;

	//Check whether being ran on server
	LUA->PushSpecial(GM::SPECIAL_GLOB);
		LUA->GetField(-1,"SERVER");
		if (LUA->IsType(-1, GM::Type::Nil))
		{
			ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: DLL failed to initialize (gm_turbostroi.dll can only be used on server)\n");
			return 0;
		}
		LUA->Pop(); //SERVER

		//Check for global table
		LUA->GetField(-1,"Metrostroi");
		if (LUA->IsType(-1, GM::Type::Nil))
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

			PushCFunc(API_ReadAvailable, "ReadAvailable");
			PushCFunc(API_SendMessage, "SendMessage");
			PushCFunc(API_RecvMessages, "RecvMessages");
			PushCFunc(API_RecvMessage, "RecvMessage");

			PushCFunc(API_LoadSystem, "LoadSystem");
			PushCFunc(API_RegisterSystem, "RegisterSystem");
			PushCFunc(API_TriggerInput, "TriggerInput");

			PushCFunc(API_SetSimulationFPS, "SetSimulationFPS");
			PushCFunc(API_SetMTAffinityMask, "SetMTAffinityMask");
			PushCFunc(API_SetSTAffinityMask, "SetSTAffinityMask");
		LUA->SetField(-2,"Turbostroi");

		LUA->GetField(-1, "include");
			LUA->PushString("metrostroi/lib_turbostroi_v2.lua");
			LUA->Call(1, 0);

	LUA->Pop();

	//Print some information
	ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: [" TURBOSTROI_VERSION "] DLL initialized (built " __DATE__ ")\n");
	ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: Running with %i cores\n", g_ProcessorCount);

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

	ShutdownSourceSDK();

	LUA->PushSpecial(GM::SPECIAL_GLOB);
		LUA->GetField(-1, "Metrostroi");
			if (LUA->IsType(-1, GM::Type::Table)) {
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
	LUA->SetField(GM::INDEX_GLOBAL, "Turbostroi");

	ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: DLL unloaded.\n");
	return 0;
}