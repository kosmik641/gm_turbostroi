#include "source_sdk.h"

#include "gmsv_turbostroi.h"
#include "filesystem_stl.h"
#include "affinity.h"
#include "shared_print.h"
#include <GarrysMod/FactoryLoader.hpp>
#include <GarrysMod/Lua/LuaInterface.h>
#include <cbase.h>
#include <utlbuffer.h>
#include <edict.h>
#include <globalvars_base.h>
#include <convar.h>
#include <keyvalues.h>
#include <game/server/iplayerinfo.h>
#include <dbg.h>
#include <color.h>
#undef GetClassName

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
CGlobalVars* g_pServerGlobalVars = nullptr;
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
namespace GarrysMod::Lua
{
	fn_lua_rawseti p_lua_rawseti = nullptr;
}
bool InitSourceSDK(GM::ILuaBase* LUA)
{
	g_Lua = LUA;
	SourceSDK::ModuleLoader lua_shared_loader("lua_shared");
	GM::p_lua_rawseti = (GM::fn_lua_rawseti)lua_shared_loader.GetSymbol("lua_rawseti");

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

// https://github.com/garrynewman/bootil/blob/master/src/3rdParty/globber.cpp
static bool globber(const char* wild, const char* string)
{
	const char* cp = 0, * mp = 0;

	while ((*string) && (*wild != '*')) {
		if ((*wild != *string) && (*wild != '?')) {
			return false;
		}
		wild++;
		string++;
	}

	while (*string) {
		if (*wild == '*') {
			if (!*++wild) {
				return true;
			}
			mp = wild;
			cp = string + 1;
		}
		else if ((*wild == *string) || (*wild == '?')) {
			wild++;
			string++;
		}
		else {
			wild = mp;
			string = cp++;
		}
	}

	while (*wild == '*') {
		wild++;
	}
	return !*wild;
}

CBaseEntity* UTIL_FindEntityByClassname(CBaseEntity* pStartEntity, const char* query)
{
	if (query == nullptr)
		return nullptr;

	int startIdx = pStartEntity ? pStartEntity->entindex() : 0;

	for (int i = startIdx; i < MAX_EDICTS; i++)
	{
		edict_t& edict = g_pEdictList[i];
		if (!UTIL_IsValidEdict(edict))
			continue;

		CBaseEntity* ent = edict.GetUnknown()->GetBaseEntity();
		const char* pClassName = ent->GetClassname();
		if (globber(query, pClassName))
			return ent;
	}
	
	return nullptr;
}

unsigned long GMOD_GetEntHandle(GM::ILuaBase* LUA, int iStackPos)
{
	if (!LUA->IsType(iStackPos, GM::Type::Entity))
		return INVALID_EHANDLE_INDEX;

	auto* ud = reinterpret_cast<GM::ILuaBase::UserData*>(LUA->GetUserdata(iStackPos));
	CBaseHandle eh((ud && ud->data) ? *reinterpret_cast<unsigned int*>(ud->data) : INVALID_EHANDLE_INDEX);
	return eh.ToInt();
}

void GMOD_PushEntityOnStack(GM::ILuaBase* LUA, int entityIndex)
{
	static GM::CFunc Entity = nullptr;
	if (Entity == nullptr)
	{
		LUA->PushSpecial(GM::SPECIAL_GLOB);
		{
			LUA->GetField(-1, "Entity");
			if (LUA->IsType(-1, GM::Type::Function))
			{
				Entity = LUA->GetCFunction();
			}
			LUA->Pop();
		}
		LUA->Pop();
	}

	if (Entity == nullptr)
		return;

	LUA->PushCFunction(Entity);
	LUA->PushNumber(entityIndex);
	LUA->Call(1, 1);
}

void GMOD_Include(GarrysMod::Lua::ILuaBase* LUA, const char* filename)
{
	static GM::CFunc include = nullptr;
	if (include == nullptr)
	{
		LUA->PushSpecial(GM::SPECIAL_GLOB);
		{
			LUA->GetField(-1, "include");
			if (LUA->IsType(-1, GM::Type::Function))
			{
				include = LUA->GetCFunction(-1);
			}
			LUA->Pop();
		}
		LUA->Pop();
	}

	if (include == nullptr)
		return;

	LUA->PushCFunction(include);
	LUA->PushString(filename);
	LUA->Call(1, 0);
}

CBaseEntity* GMOD_EntsCreate(GM::ILuaBase* LUA, const char* className)
{
	static GM::CFunc ents_Create = nullptr;
	if (ents_Create == nullptr)
	{
		LUA->PushSpecial(GM::SPECIAL_GLOB);
		{
			LUA->GetField(-1, "ents");
			if (LUA->IsType(-1, GM::Type::Table))
			{
				LUA->GetField(-1, "Create");
				if (LUA->IsType(-1, GM::Type::Function))
				{
					ents_Create = LUA->GetCFunction(-1);
				}
				LUA->Pop();
			}
			LUA->Pop();
		}
		LUA->Pop();
	}

	if (ents_Create == nullptr)
		return nullptr;

	CBaseEntity* ent = nullptr;

	LUA->PushCFunction(ents_Create);
	LUA->PushString(className);
	LUA->Call(1, 1);

	if (LUA->IsType(-1, GM::Type::Entity))
	{
		CBaseHandle hEnt = GMOD_GetEntHandle(LUA, -1);
		if (hEnt != INVALID_EHANDLE_INDEX)
			ent = g_pEdictList[hEnt.GetEntryIndex()].GetIServerEntity()->GetBaseEntity();
	}

	return ent;
}

void GMOD_EntityRemove(GM::ILuaBase* LUA, int entityIndex)
{
	static GM::CFunc EntityRemove = nullptr;
	if (EntityRemove == nullptr)
	{
		LUA->PushSpecial(GM::SPECIAL_GLOB);
		{
			LUA->GetField(-1, "Entity");
			if (LUA->IsType(-1, GM::Type::Function))
			{
				// Get pointer to Entity.Remove from Entity(0)
				LUA->PushNumber(0);
				LUA->Call(1, 1);

				LUA->GetField(-1, "Remove");
				if (LUA->IsType(-1, GM::Type::Function))
				{
					EntityRemove = LUA->GetCFunction(-1);
				}
				LUA->Pop();
			}
			LUA->Pop();
		}
		LUA->Pop();
	}

	if (EntityRemove == nullptr)
		return;

	LUA->PushCFunction(EntityRemove);
	GMOD_PushEntityOnStack(LUA, entityIndex);
	if (LUA->PCall(1, 0, 0))
	{
		const char* err = LUA->GetString(-1);
		ConColorMsg(Color(255, 0, 0, 255), "GMOD_EntityRemove(): %s\n", err);
		LUA->Pop();
	}
}

//UTIL_Find