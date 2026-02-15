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
#include "railnetwork.h"

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

LUA_FUNCTION( TS_API_InitializeTrain ) 
{
	CWagon* userdata = new CWagon();
	if (userdata == nullptr)
		return 0;

	// Train._CWagon = *CWagon
	LUA->PushUserType(userdata, GM::Type::LightUserData);
	LUA->SetField(1, "_CWagon");

	// Store entity index of wagon
	unsigned long hEnt = GMOD_GetEntHandle(LUA, 1);
	userdata->SetEntHandle(hEnt);
	g_RailNetwork.AddTrain(hEnt);

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

		// Train._CWagon = nil
		LUA->PushNil();
		LUA->SetField(1, "_CWagon");
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

LUA_FUNCTION( TS_API_DeinitializeTrain ) 
{
	g_RailNetwork.RemoveTrain(CBaseHandle(GMOD_GetEntHandle(LUA, 1)).GetEntryIndex());
	LUA->GetField(1,"_CWagon");
	{
		CWagon* userdata = LUA->GetUserType<CWagon>(-1, GM::Type::LightUserData);
		if (userdata) userdata->Finish();
	}
	LUA->Pop();

	LUA->PushNil();
	LUA->SetField(1,"_CWagon");

	HookRunTrainEnt(LUA, 1, true);
	return 0;
}

LUA_FUNCTION( TS_API_LoadSystem ) 
{
	const char* basename = LUA->GetString(1);
	const char* name = LUA->GetString(2);
	if (!basename || !name)
		return 0;

	g_LoadSystemList.emplace(name, basename);
	return 0;
}

LUA_FUNCTION( TS_API_RegisterSystem ) 
{
	const char* name = LUA->GetString(1);
	const char* filename = LUA->GetString(2);
	if (!name || !filename)
		return 0;
	
	g_MetrostroiSystemList.emplace_back(name, filename);
	ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: Registering system %s [%s]\n", name, filename);
	return 0;
}

LUA_FUNCTION( TS_API_TriggerInput )
{
	if (!LUA->IsType(1, GM::Type::Entity))
		return 0;

	LUA->GetField(1, "_CWagon");
	CWagon* userdata = LUA->GetUserType<CWagon>(-1, GM::Type::LightUserData);
	LUA->Pop();

	if (userdata == nullptr)
		return 0;

	const char* system_name = LUA->CheckString(2);
	const char* name = LUA->CheckString(3);
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

LUA_FUNCTION( TS_API_SendMessage ) 
{
	CWagon* userdata = LUA->GetUserType<CWagon>(1, GM::Type::LightUserData);

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

LUA_FUNCTION( TS_API_RecvMessages ) 
{
	// Not used
	return 0;
}

LUA_FUNCTION( TS_API_RecvMessage ) 
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

LUA_FUNCTION( TS_API_ReadAvailable ) 
{
	CWagon* userdata = LUA->GetUserType<CWagon>(1, GM::Type::LightUserData);

	if (userdata != nullptr)
		LUA->PushNumber(userdata->SimReadAvailable());
	else
		LUA->PushNumber(0);

	return 1;
}

LUA_FUNCTION( TS_API_SetSimulationFPS ) 
{
	double FPS = LUA->CheckNumber(1);
	if (FPS == 0)
		return 0;

	g_ThreadTickrate = (1000000.0 / FPS);
	ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Changed to %d FPS (%.02f ms delay)\n", (int)(FPS + 0.5), g_ThreadTickrate / 1000.0f);
	return 0;
}

LUA_FUNCTION( TS_API_SetMTAffinityMask )
{
	// Removed and saved for compability
	return 0;
}

LUA_FUNCTION( TS_API_SetSTAffinityMask ) 
{
	// Removed and saved for compability
	return 0;
}

LUA_FUNCTION( TS_API_StartRailNetwork )
{
	// Removed and saved for compability
	return 0;
}

//------------------------------------------------------------------------------
// RailNetwork Lua API
//------------------------------------------------------------------------------
LUA_FUNCTION(RN_API_Start)
{
	g_RailNetwork.Start();
	return 0;
}

LUA_FUNCTION(RN_API_Stop)
{
	g_RailNetwork.Stop();
	return 0;
}

LUA_FUNCTION(RN_API_GetTrackEditorPaths)
{
	return g_RailNetwork.GetTrackEditorPaths(LUA);
}

LUA_FUNCTION(RN_API_NearestNodes)
{
	return g_RailNetwork.NearestNodes(LUA);
}

LUA_FUNCTION(RN_API_GetPositionOnTrack)
{
	return g_RailNetwork.GetPositionOnTrack(LUA);
}

LUA_FUNCTION(RN_API_GetTrackPosition)
{
	return g_RailNetwork.GetTrackPosition(LUA);
}

LUA_FUNCTION(RN_API_UpdateSignalNames)
{
	return 0;
}

LUA_FUNCTION(RN_API_UpdateSignalEntities)
{
	return 0;
}

LUA_FUNCTION(RN_API_PostSignalInitialize)
{
	return 0;
}

LUA_FUNCTION(RN_API_UpdateSwitchEntities)
{
	return 0;
}

LUA_FUNCTION(RN_API_AddARSSubSection)
{
	return 0;
}

LUA_FUNCTION(RN_API_ScanTrack)
{
	return g_RailNetwork.ScanTrack(LUA);
}

LUA_FUNCTION(RN_API_GetSignalByName)
{
	return 0;
}

LUA_FUNCTION(RN_API_GetSwitchByName)
{
	return 0;
}

LUA_FUNCTION(RN_API_GetNextTrafficLight)
{
	return 0;
}

LUA_FUNCTION(RN_API_GetARSJoint)
{
	return 0;
}

LUA_FUNCTION(RN_API_GetTrackSwitches)
{
	return 0;
}

LUA_FUNCTION(RN_API_IsTrackOccupied)
{
	return 0;
}

LUA_FUNCTION(RN_API_PrintStatistics)
{
	g_RailNetwork.PrintStatistics();
	return 0;
}

LUA_FUNCTION(RN_API_LinkSignalEntity)
{
	return g_RailNetwork.LinkSignalEntity(LUA);
}

LUA_FUNCTION(RN_API_SigSetRoute)
{
	return g_RailNetwork.SigSetRoute(LUA);
}

// lua_run print(RailNetwork.GetTrackPath(0))
LUA_FUNCTION(RN_API_GetTrackPath)
{
	return g_RailNetwork.PushPath(LUA);
}

// lua_run print(RailNetwork.GetTrackNode(0,0))
LUA_FUNCTION(RN_API_GetTrackNode)
{
	return g_RailNetwork.PushNode(LUA);
}


LUA_FUNCTION(RN_API_Test)
{
	return 0;
}

//------------------------------------------------------------------------------
// SourceSDK
//------------------------------------------------------------------------------
LUA_FUNCTION_DECLARE( Think_handler )
{
	g_CurrentTime = g_pServerGlobalVars->curtime;
	g_SharedPrint.PrintAvailable();
	g_RailNetwork.Think();
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
	
	if (!InitSourceSDK(LUA))
		return 0;

	MathLib_Init();
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

		//Initialize Turbostroi API
		LUA->CreateTable();
		{
			PushCFunc(TS_API_InitializeTrain, "InitializeTrain");
			PushCFunc(TS_API_DeinitializeTrain, "DeinitializeTrain");
			PushCFunc(TS_API_StartRailNetwork, "StartRailNetwork");

			PushCFunc(TS_API_ReadAvailable, "ReadAvailable");
			PushCFunc(TS_API_SendMessage, "SendMessage");
			PushCFunc(TS_API_RecvMessages, "RecvMessages");
			PushCFunc(TS_API_RecvMessage, "RecvMessage");

			PushCFunc(TS_API_LoadSystem, "LoadSystem");
			PushCFunc(TS_API_RegisterSystem, "RegisterSystem");
			PushCFunc(TS_API_TriggerInput, "TriggerInput");

			PushCFunc(TS_API_SetSimulationFPS, "SetSimulationFPS");
			PushCFunc(TS_API_SetMTAffinityMask, "SetMTAffinityMask");
			PushCFunc(TS_API_SetSTAffinityMask, "SetSTAffinityMask");
		}
		LUA->SetField(-2, "Turbostroi");
		GMOD_Include(LUA, "metrostroi/lib_turbostroi_v2.lua");

		// Initialize Railnetwork API
		CRailNetwork::RegisterLuaUserData();
		LUA->CreateTable();
		{
			PushCFunc(RN_API_Start, "Start");
			PushCFunc(RN_API_Stop, "Stop");
			PushCFunc(RN_API_GetTrackEditorPaths, "GetTrackEditorPaths");
			PushCFunc(RN_API_LinkSignalEntity, "LinkSignalEntity");
			PushCFunc(RN_API_SigSetRoute, "SigSetRoute");

			//PushCFunc(RN_API_ARSJointScan, "ARSJointScan");
			//PushCFunc(RN_API_ARSJointScanBack, "ARSJointScanBack");

			// Railnetwork API
			PushCFunc(RN_API_NearestNodes, "NearestNodes");
			PushCFunc(RN_API_GetPositionOnTrack, "GetPositionOnTrack");
			PushCFunc(RN_API_GetTrackPosition, "GetTrackPosition");
			//PushCFunc(RN_API_UpdateSignalNames, "UpdateSignalNames");
			//PushCFunc(RN_API_UpdateSignalEntities, "UpdateSignalEntities");
			//PushCFunc(RN_API_PostSignalInitialize, "PostSignalInitialize");
			//PushCFunc(RN_API_UpdateSwitchEntities, "UpdateSwitchEntities");
			//PushCFunc(RN_API_AddARSSubSection, "AddARSSubSection");
			PushCFunc(RN_API_ScanTrack, "ScanTrack");
			//PushCFunc(RN_API_GetSignalByName, "GetSignalByName");
			//PushCFunc(RN_API_GetSwitchByName, "GetSwitchByName");
			//PushCFunc(RN_API_GetNextTrafficLight, "GetNextTrafficLight");
			//PushCFunc(RN_API_GetARSJoint, "GetARSJoint");
			//PushCFunc(RN_API_GetTrackSwitches, "GetTrackSwitches");
			//PushCFunc(RN_API_IsTrackOccupied, "IsTrackOccupied");
			//PushCFunc(RN_API_PredictTrainPositions, "PredictTrainPositions");
			//PushCFunc(RN_API_UpdateTrainPositions, "UpdateTrainPositions");
			//PushCFunc(RN_API_UpdateStations, "UpdateStations");
			//PushCFunc(RN_API_GetTravelTime, "GetTravelTime");
			//PushCFunc(RN_API_Load, "Load");
			//PushCFunc(RN_API_Save, "Save");
			PushCFunc(RN_API_PrintStatistics, "PrintStatistics");

			// For debugging
			PushCFunc(RN_API_GetTrackPath, "GetTrackPath");
			PushCFunc(RN_API_GetTrackNode, "GetTrackNode");
			PushCFunc(RN_API_Test, "Test");
		}
		LUA->SetField(-2, "RailNetwork");
		GMOD_Include(LUA, "metrostroi/lib_railnetwork.lua");
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
	g_RailNetwork.Stop();

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

	LUA->PushNil();
	LUA->SetField(GM::INDEX_GLOBAL, "RailNetwork");

	ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: DLL unloaded.\n");
	return 0;
}