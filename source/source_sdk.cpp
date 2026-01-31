#include "source_sdk.h"

#include "gmsv_turbostroi.h"
#include "filesystem_stl.h"
#include "affinity.h"
#include "shared_print.h"
#include <GarrysMod/FactoryLoader.hpp>
#include <GarrysMod/Lua/Interface.h>
#include <cbase.h>
#include <utlbuffer.h>
#include <edict.h>
#include <globalvars_base.h>
#include <convar.h>
#include <keyvalues.h>
#include <game/server/iplayerinfo.h>
#include <dbg.h>
#include <color.h>

namespace GM = GarrysMod::Lua;

//------------------------------------------------------------------------------
// IFileSystem interface
//------------------------------------------------------------------------------
CFileSystem_STL g_FileSystemSTL; // for save settings in "cfg" folder
//IFileSystem* g_pFileSystem = nullptr;

//------------------------------------------------------------------------------
// Console variables
//------------------------------------------------------------------------------
ConVar g_CVarDisableCache("turbostroi_disable_cache", "0", FCVAR_NONE, "Disable scripts cache for development");
ConVar g_CVarMainCores("turbostroi_main_cores", "0", FCVAR_NONE, "Set affinity mask for main thread", CVarMainCoresCallback);
ConVar g_CVarTrainCores("turbostroi_train_cores", "0", FCVAR_NONE, "Set affinity mask for train threads", CVarTrainCoresCallback);
ConCommand g_CmdClearCache("turbostroi_clear_cache", ClearLoadCache, "Clear cache for reload systems");
ConCommand g_CmdClearPrint("turbostroi_clear_print", ClearPrintQueue, "Clear print queue");

//------------------------------------------------------------------------------
// "turbostroi_main_cores" callback
//------------------------------------------------------------------------------
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

//------------------------------------------------------------------------------
// "turbostroi_train_cores" callback
//------------------------------------------------------------------------------
void CVarTrainCoresCallback(IConVar* var, const char* pOldValue, float flOldValue)
{
	ConVarRef ref(var);
	SetThreadGroup(g_SimThreadGroupAffinity, ref.GetString());
}

//------------------------------------------------------------------------------
// "turbostroi_clear_cache" callback
//------------------------------------------------------------------------------
void ClearLoadCache(const CCommand& command)
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

//------------------------------------------------------------------------------
// "turbostroi_clear_print" callback
//------------------------------------------------------------------------------
void ClearPrintQueue(const CCommand& command)
{
	g_SharedPrint.ClearPrintQueue();
}

//------------------------------------------------------------------------------
// Engine interface
//------------------------------------------------------------------------------
IVEngineServer* g_pVEngineServer = nullptr;
IVEngineServer* engine = nullptr;
static bool GetVEngineServer()
{
	SourceSDK::FactoryLoader engine_loader("engine");
	g_pVEngineServer = engine = engine_loader.GetInterface<IVEngineServer>(INTERFACEVERSION_VENGINESERVER);

	if (g_pVEngineServer == nullptr)
	{
		ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: Unable to load VEngineServer Interface!\n");
		return false;
	}

	return true;
}

//------------------------------------------------------------------------------
// Server edicts list
//------------------------------------------------------------------------------
edict_t* g_pEdictList = nullptr; // Size = MAX_EDICTS;
static bool GetEdictsList()
{
	g_pEdictList = g_pVEngineServer->PEntityOfEntIndex(0);
	if (g_pEdictList == nullptr)
	{
		ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: Unable to get edicts list!\n");
		return false;
	}

	return true;
}

//------------------------------------------------------------------------------
// Server global vars
//------------------------------------------------------------------------------
CGlobalVarsBase* g_pServerGlobalVars = nullptr;
static bool GetGlobalVars()
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
// Turbostroi settings
//------------------------------------------------------------------------------
KeyValues* g_pKVTurbostroi = nullptr;
static void SaveSettings(IFileSystem* filesystem)
{
	if (filesystem == nullptr || g_pKVTurbostroi == nullptr)
		return;

	// Save to KeyValues
	if (g_CVarMainCores.IsRegistered())
		g_pKVTurbostroi->SetString(g_CVarMainCores.GetName(), g_CVarMainCores.GetString());

	if (g_CVarTrainCores.IsRegistered())
		g_pKVTurbostroi->SetString(g_CVarTrainCores.GetName(), g_CVarTrainCores.GetString());

	// Write to file
	FileHandle_t f = filesystem->Open("garrysmod/cfg/turbostroi.cfg", "wb", "MOD");
	if (f != FILESYSTEM_INVALID_HANDLE)
	{
		CUtlBuffer buf;
		g_pKVTurbostroi->RecursiveSaveToFile(buf, 0, false, false);
		filesystem->Write(buf.Base(), buf.TellPut(), f);
		filesystem->Close(f);
	}
	else
		ConColorMsg(Color(255, 255, 0, 255), "Turbostroi: Fail to save settings.\n");
}

static bool RegisterConCommands()
{
	SourceSDK::FactoryLoader vstdlib_loader("vstdlib");
	cvar = g_pCVar = vstdlib_loader.GetInterface<ICvar>(CVAR_INTERFACE_VERSION);
	if (g_pCVar == nullptr)
	{
		ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: Unable to load Cvar Interface!\n");
		return false;
	}

	// Register commands
	g_pCVar->RegisterConCommand(&g_CVarDisableCache);
	g_pCVar->RegisterConCommand(&g_CVarMainCores);
	g_pCVar->RegisterConCommand(&g_CVarTrainCores);
	g_pCVar->RegisterConCommand(&g_CmdClearCache);
	g_pCVar->RegisterConCommand(&g_CmdClearPrint);
	
	return true;
}

static void LoadConVariables(IFileSystem* filesystem)
{
	if (filesystem == nullptr)
		return;

	// Load values (FCVAR_ARCHIVE not working)
	g_pKVTurbostroi = new KeyValues("Turbostroi");
	if (g_pKVTurbostroi->LoadFromFile(filesystem, "garrysmod/cfg/turbostroi.cfg", "MOD"))
	{
		const char* main_cores = g_pKVTurbostroi->GetString(g_CVarMainCores.GetName(), g_CVarMainCores.GetDefault());
		const char* train_cores = g_pKVTurbostroi->GetString(g_CVarTrainCores.GetName(), g_CVarTrainCores.GetDefault());
		g_CVarMainCores.SetValue(main_cores);
		g_CVarTrainCores.SetValue(train_cores);
	}
	else
	{
		std::string main_cores = "1";
		std::string train_cores = "254";

		// Load values from Lua CVars
		KeyValues* kvLuaCVars = new KeyValues("CVars");
		if (kvLuaCVars->LoadFromFile(filesystem, "garrysmod/cfg/server.vdf", "MOD"))
		{
			main_cores = kvLuaCVars->GetString(g_CVarMainCores.GetName(), "1");
			train_cores = kvLuaCVars->GetString(g_CVarTrainCores.GetName(), "254");
		}
		kvLuaCVars->deleteThis();
		kvLuaCVars = nullptr;

		g_CVarMainCores.SetValue(main_cores.c_str());
		g_CVarTrainCores.SetValue(train_cores.c_str());

		g_pKVTurbostroi->SetString(g_CVarMainCores.GetName(), main_cores.c_str());
		g_pKVTurbostroi->SetString(g_CVarTrainCores.GetName(), train_cores.c_str());
	}

	g_CVarMainCores.SetDefault("1");
	g_CVarTrainCores.SetDefault("254");
}

GM::ILuaBase* g_Lua = nullptr;
bool InitSourceSDK(GM::ILuaBase* LUA)
{
	g_Lua = LUA;

	if (!GetVEngineServer())
		return false;

	if (!GetEdictsList())
		return false;

	if (!GetGlobalVars())
		return false;

	if (!RegisterConCommands())
		return false;

	LoadConVariables(&g_FileSystemSTL);

	return true;
}

void ShutdownSourceSDK()
{
	SaveSettings(&g_FileSystemSTL);

	if (g_pKVTurbostroi != nullptr)
	{
		g_pKVTurbostroi->deleteThis();
		g_pKVTurbostroi = nullptr;
	}

	if (g_pCVar != nullptr)
	{
		g_pCVar->UnregisterConCommand(&g_CVarDisableCache);
		g_pCVar->UnregisterConCommand(&g_CVarMainCores);
		g_pCVar->UnregisterConCommand(&g_CVarTrainCores);
		g_pCVar->UnregisterConCommand(&g_CmdClearCache);
		g_pCVar->UnregisterConCommand(&g_CmdClearPrint);
		g_pCVar = nullptr;
	}
}

CBaseEntity* UTIL_EntityByIndex(int entityIndex)
{
	if (engine == nullptr)
		return nullptr;

	CBaseEntity* entity = nullptr;

	if (entityIndex > 0)
	{
		edict_t* edict = INDEXENT(entityIndex);
		if (edict && !edict->IsFree())
		{
			entity = GetContainingEntity(edict);
		}
	}

	return entity;
}

bool UTIL_IsValidEntity(CBaseEntity* pEnt)
{
	edict_t* pEdict = pEnt->edict();
	if (!pEdict || pEdict->IsFree())
		return false;
	return true;
}

bool UTIL_IsValidEdict(edict_t& edict)
{
	if (edict.GetUnknown() == nullptr
		|| edict.IsFree())
		return false;

	return true;
}

unsigned long GMOD_GetEntHandle(GM::ILuaBase* LUA, int iStackPos)
{
	if (!LUA->IsType(iStackPos, GM::Type::Entity))
		return INVALID_EHANDLE_INDEX;

	auto* ud = reinterpret_cast<GM::ILuaBase::UserData*>(LUA->GetUserdata(iStackPos));
	CBaseHandle eh(*reinterpret_cast<unsigned int*>(ud->data));
	return eh.ToInt();
}

void GMOD_PushEntityOnStack(GM::ILuaBase* LUA, int entityIndex)
{
	LUA->PushSpecial(GM::SPECIAL_GLOB);
	{
		LUA->GetField(-1, "Entity");
		{
			if (LUA->IsType(-1, GM::Type::Function))
			{
				LUA->PushNumber(entityIndex);
				LUA->Call(1, 1);
			}
		}
	}
}