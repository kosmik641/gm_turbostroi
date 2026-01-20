#include "source_sdk.h"
#include <utlbuffer.h>
#include <GarrysMod/FactoryLoader.hpp>
#include <game/server/iplayerinfo.h>

ConVar g_CVarDisableCache("turbostroi_disable_cache", "0", FCVAR_NONE, "Disable scripts cache for development");
ConVar g_CVarMainCores("turbostroi_main_cores", "0", FCVAR_NONE, "Set affinity mask for main thread", CVarMainCoresCallback);
ConVar g_CVarTrainCores("turbostroi_train_cores", "0", FCVAR_NONE, "Set affinity mask for train threads", CVarTrainCoresCallback);
ConCommand g_CmdClearCache("turbostroi_clear_cache", ClearLoadCache, "Clear cache for reload systems");
ConCommand g_CmdClearPrint("turbostroi_clear_print", ClearPrintQueue, "Clear print queue");

CGlobalVars* g_pServerGlobalVars = nullptr;
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

IFileSystem* g_pFileSystem = nullptr;
bool InitFileSystem()
{
	Sys_LoadInterface("filesystem_stdio", FILESYSTEM_INTERFACE_VERSION, nullptr, (void**)&g_pFileSystem);
	if (g_pFileSystem == nullptr)
	{
		ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: Unable to load FileSystem Interface!\n");
		return false;
	}

	if (!g_pFileSystem->Connect(Sys_GetFactory("vstdlib")))
	{
		ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: Unable to connect FileSystem Interface!\n");
		g_pFileSystem->Shutdown();
		g_pFileSystem = nullptr;
		return false;
	};

	if (!g_pFileSystem->Init())
	{
		ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: Unable to init FileSystem Interface!\n");
		g_pFileSystem->Shutdown();
		g_pFileSystem = nullptr;
		return false;
	}

	g_pFileSystem->AddSearchPath("garrysmod", "MOD");
	g_pFileSystem->AddSearchPath("garrysmod/data", "DATA");

	return true;
}

KeyValues* g_pKVTurbostroi = nullptr;
void SaveSettings()
{
	if (g_pFileSystem == nullptr || g_pKVTurbostroi == nullptr)
		return;

	// Save to KeyValues
	if (g_CVarMainCores.IsRegistered())
		g_pKVTurbostroi->SetString(g_CVarMainCores.GetName(), g_CVarMainCores.GetString());

	if (g_CVarTrainCores.IsRegistered())
		g_pKVTurbostroi->SetString(g_CVarTrainCores.GetName(), g_CVarTrainCores.GetString());

	// Write to file
	FileHandle_t f = g_pFileSystem->Open("cfg/turbostroi.cfg", "wb", "MOD");
	if (f != FILESYSTEM_INVALID_HANDLE)
	{
		CUtlBuffer buf;
		g_pKVTurbostroi->RecursiveSaveToFile(buf, 0, false, false);
		g_pFileSystem->Write(buf.Base(), buf.TellPut(), f);
		g_pFileSystem->Close(f);
	}
	else
		ConColorMsg(Color(255, 255, 0, 255), "Turbostroi: Fail to save settings.\n");
}

bool RegisterConCommands()
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

	// Load values (FCVAR_ARCHIVE not working)
	g_pKVTurbostroi = new KeyValues("Turbostroi");
	if (g_pKVTurbostroi->LoadFromFile(g_pFileSystem, "cfg/turbostroi.cfg", "MOD"))
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
		if (kvLuaCVars->LoadFromFile(g_pFileSystem, "cfg/server.vdf", "MOD"))
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

	return true;
}

bool InitSourceSDK()
{
	if (!GetGlobalVars())
		return false;

	if (!InitFileSystem())
		return false;

	if (!RegisterConCommands())
		return false;

	return true;
}

void ShutdownSourceSDK()
{
	SaveSettings();

	if (g_pKVTurbostroi != nullptr)
	{
		g_pKVTurbostroi->deleteThis();
		g_pKVTurbostroi = nullptr;
	}

	if (g_pFileSystem != nullptr)
	{
		g_pFileSystem->Shutdown();
		g_pFileSystem->Disconnect();
		g_pFileSystem = nullptr;
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