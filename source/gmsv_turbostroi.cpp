#include "gmsv_turbostroi.h"

#include <thread>
#include <vector>
#include <queue>
#include <GarrysMod/Lua/Interface.h>
#include <convar.h>
#include <basehandle.h>
#include <globalvars_base.h>
#include <dbg.h>
#include <color.h>
#include "affinity.h"
#include "filesystem_gmod.h"
#include "source_sdk.h"
#include "shared_print.h"
#include "wagon.h"

namespace GM = GarrysMod::Lua;

#define PushCFunc(_function,_name) LUA->PushCFunction(_function); LUA->SetField(-2, _name)

//------------------------------------------------------------------------------
// Global variables
//------------------------------------------------------------------------------
std::string g_LibraryFileName;
bool g_ForceThreadsFinished = false; // For correct unrequire module
std::atomic<float> g_CurrentTime = 0.0f;
unsigned int g_ThreadTickrate = 10000; // [mcs] (10ms)
std::vector<TTrainSystem> g_MetrostroiSystemList;
std::queue<TTrainSystem> g_LoadSystemList;
std::unordered_map<std::string, std::string> g_LoadedFilesCache;

//------------------------------------------------------------------------------
// Global variables for Source SDK
//------------------------------------------------------------------------------
extern ICvar* cvar;
extern ICvar* g_pCVar;
extern CGlobalVarsBase* g_pServerGlobalVars;
extern ConVar g_CVarDisableCache;

//------------------------------------------------------------------------------
// Hook declaration
//------------------------------------------------------------------------------
void HookRunTrainEnt(GarrysMod::Lua::ILuaBase* LUA, int entStackPos, bool remove = false);

//------------------------------------------------------------------------------
// Metrostroi Lua API
//------------------------------------------------------------------------------
bool LoadSystem(GM::ILuaBase* LUA, CWagon* userdata, const char* filename)
{
	// Get file from cache
	bool useCache = !g_CVarDisableCache.GetBool();
	if (useCache)
	{
		const auto& cache_item = g_LoadedFilesCache.find(filename);
		if (cache_item != g_LoadedFilesCache.end())
		{
			const std::string& data = cache_item->second;
			return userdata->LoadBuffer(data.c_str(), data.size(), filename);
		}
	}

	// Read file
	const char* data = GMOD_FileRead(LUA, filename, "lsv"); // LUA server realm scripts = lsv
	if (data == nullptr)
	{
		ConColorMsg(Color(255, 0, 127, 255), "Turbostroi: File not found! ('%s')\n", filename);
		return false;
	}

	// Load file to CWagon
	bool loaded = userdata->LoadBuffer(data, strlen(data), filename);
	if (useCache && loaded) g_LoadedFilesCache.emplace(filename, data);

	return loaded;
}

inline int GetEntIndex(GM::ILuaBase* LUA, int iStackPos)
{	
	if (!LUA->IsType(iStackPos, GM::Type::Entity))
		return 0;

	auto* ud = reinterpret_cast<GM::ILuaBase::UserData*>(LUA->GetUserdata(iStackPos));
	CBaseHandle eh(ud ? *reinterpret_cast<unsigned int*>(ud->data) : 0);
	return eh.GetEntryIndex();
}

LUA_FUNCTION( API_InitializeTrain ) 
{
	int idx = GetEntIndex(LUA, 1);
	CWagon* userdata = CWagon::Create(idx);
	if (userdata == nullptr)
	{
		if (idx > 0)
		{
			// Train.DontAccelerateSimulation = true
			LUA->PushBool(true);
			LUA->SetField(1, "DontAccelerateSimulation");
		}

		ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: Fail to create CWagon!\n");
		return 0;
	}

	// If cache disabled, clear it
	if (g_CVarDisableCache.GetBool())
		g_LoadedFilesCache.clear();

	// Load neccessary files
	if (!LoadSystem(LUA, userdata, "metrostroi/lib_turbostroi_v2.lua") || !userdata->CheckLibLoaded())
	{
		ConColorMsg(Color(255, 0, 255, 255), "[!] Fail to load lib_turbostroi_v2.lua\n");

		// Train.DontAccelerateSimulation = true
		LUA->PushBool(true);
		LUA->SetField(1, "DontAccelerateSimulation");

		// Destroy CWagon
		delete userdata;

		// Hook call
		HookRunTrainEnt(LUA, 1);
		return 0;
	}

	// Load up all the systems
	LoadSystem(LUA, userdata, "metrostroi/sh_failsim.lua");
	for (TTrainSystem sys : g_MetrostroiSystemList)
	{
		LoadSystem(LUA, userdata, sys.file_name.c_str());
	}

	// Initialize all the systems reported by the train
	while (!g_LoadSystemList.empty())
	{
		userdata->AddLoadSystem(g_LoadSystemList.front());
		g_LoadSystemList.pop();
	}

	// Hook call
	HookRunTrainEnt(LUA, 1);

	//Create thread for simulation
	std::thread thread(&CWagon::SimulationThreadFn, userdata);
	thread.detach();

	return 0;
}

LUA_FUNCTION( API_DeinitializeTrain ) 
{
	CWagon* userdata = CWagon::CWagonByIndex(GetEntIndex(LUA, 1));
	if (userdata)
	{
		HookRunTrainEnt(LUA, 1, true);
		userdata->Finish();
	}
	
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

	CWagon* userdata = CWagon::CWagonByIndex(GetEntIndex(LUA, 1));
	if (userdata == nullptr)
		return 0;

	const char* system_name = LUA->GetString(2);
	const char* name = LUA->GetString(3);
	double value;

	if (LUA->IsType(4, GM::Type::Number))
		value = LUA->GetNumber(4);
	else if (LUA->IsType(4, GM::Type::Bool))
		value = LUA->GetBool(4);
	else
		value = 0;

	userdata->SimSendMessage(3, system_name, name, 0, value);

	return 0;
}

LUA_FUNCTION( API_SendMessage ) 
{
	CWagon* userdata = CWagon::CWagonByIndex(GetEntIndex(LUA, 1));
	if (userdata == nullptr)
	{
		LUA->PushBool(false);
		return 1;
	}

	int message = LUA->CheckNumber(2);
	const char* system_name = LUA->CheckString(3);
	const char* name = LUA->CheckString(4);
	double index = LUA->CheckNumber(5);
	double value = LUA->CheckNumber(6);

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
	CWagon* userdata = CWagon::CWagonByIndex(GetEntIndex(LUA, 1));
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
	CWagon* userdata = CWagon::CWagonByIndex(GetEntIndex(LUA, 1));

	if (userdata != nullptr)
		LUA->PushNumber(userdata->SimReadAvailable());
	else
		LUA->PushNumber(0);

	return 1;
}

LUA_FUNCTION( API_SetSimulationFPS ) 
{
	double FPS = LUA->CheckNumber(1);
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

	int top = LUA->Top();
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

	LUA->Pop(LUA->Top() - top);
}

void InstallHooks(GM::ILuaBase* LUA)
{
	ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: Installing hooks!\n");

	int top = LUA->Top();
	LUA->PushSpecial(GM::SPECIAL_GLOB);
	{
		LUA->GetField(-1, "hook");
		if (LUA->IsType(-1, GM::Type::Table))
		{
			LUA->GetField(-1, "Add");
			if (LUA->IsType(-1, GM::Type::Function))
			{
				LUA->PushString("Think");
				LUA->PushString("Turbostroi_TargetTimeSync");
				LUA->PushCFunction(Think_handler);
				LUA->Call(3, 0);
			}
		}
	}
		
	LUA->Pop(LUA->Top() - top);
}

//------------------------------------------------------------------------------
// Initialization
//------------------------------------------------------------------------------
GMOD_MODULE_OPEN()
{
	g_LibraryFileName = LUA->GetString(1);
	LUA->Pop(1); // Library filename
	
	if (!InitSourceSDK())
		return 0;

	LUA->PushSpecial(GM::SPECIAL_GLOB);
	{
		// Check whether being ran on server
		LUA->GetField(-1, "SERVER");
		if (LUA->IsType(-1, GM::Type::Nil))
		{
			ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: DLL failed to initialize (gm_turbostroi.dll can only be used on server)\n");
			return 0;
		}
		LUA->Pop(); //SERVER

		// Check for global table
		LUA->GetField(-1, "Metrostroi");
		if (LUA->IsType(-1, GM::Type::Nil))
		{
			ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: DLL failed to initialize (cannot be used standalone without metrostroi addon)\n");
			return 0;
		}
		LUA->Pop(); //Metrostroi

		InstallHooks(LUA);

		//Initialize API
		LUA->CreateTable();
		{
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
		}
		LUA->SetField(-2, "Turbostroi");

		// Include to GMod side
		LUA->GetField(-1, "include");
		if (LUA->IsType(-1, GM::Type::Function))
		{
			LUA->PushString("metrostroi/lib_turbostroi_v2.lua");
			LUA->Call(1, 0);
		}
	}
	LUA->Pop(); // GM::SPECIAL_GLOB

	// Print some information
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