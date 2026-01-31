#pragma once
#include <iconvar.h>

struct edict_t;
namespace GarrysMod::Lua
{
    class ILuaBase;
}

bool InitSourceSDK(GarrysMod::Lua::ILuaBase* LUA);
void ShutdownSourceSDK();

void CVarMainCoresCallback(IConVar* var, const char* pOldValue, float flOldValue);
void CVarTrainCoresCallback(IConVar* var, const char* pOldValue, float flOldValue);
void ClearLoadCache(const CCommand& command);
void ClearPrintQueue(const CCommand& command);

bool UTIL_IsValidEdict(edict_t& edict);
unsigned long GMOD_GetEntHandle(GarrysMod::Lua::ILuaBase* LUA, int iStackPos);
void GMOD_PushEntityOnStack(GarrysMod::Lua::ILuaBase* LUA, int entityIndex);


extern GarrysMod::Lua::ILuaBase* g_Lua;
extern edict_t* g_pEdictList;
