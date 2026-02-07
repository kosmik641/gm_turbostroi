#pragma once
#include <iconvar.h>

struct edict_t;
class CGlobalVars;
class CBaseEntity;
struct lua_State;
namespace GarrysMod::Lua
{
    class ILuaBase;

    typedef void (*fn_lua_rawseti)(lua_State* L, int idx, int n);
    extern fn_lua_rawseti p_lua_rawseti;
}

bool InitSourceSDK(GarrysMod::Lua::ILuaBase* LUA);
void ShutdownSourceSDK();

void CVarMainCoresCallback(IConVar* var, const char* pOldValue, float flOldValue);
void CVarTrainCoresCallback(IConVar* var, const char* pOldValue, float flOldValue);
void ClearLoadCache(const CCommand& command);
void ClearPrintQueue(const CCommand& command);

bool UTIL_IsValidEdict(edict_t& edict);
CBaseEntity* UTIL_FindEntityByClassname(CBaseEntity* pStartEntity, const char* query);
unsigned long GMOD_GetEntHandle(GarrysMod::Lua::ILuaBase* LUA, int iStackPos);
void GMOD_PushEntityOnStack(GarrysMod::Lua::ILuaBase* LUA, int entityIndex);
void GMOD_Include(GarrysMod::Lua::ILuaBase* LUA, const char* filename);
CBaseEntity* GMOD_EntsCreate(GarrysMod::Lua::ILuaBase* LUA, const char* className);
void GMOD_EntityRemove(GarrysMod::Lua::ILuaBase* LUA, int entityIndex);

extern GarrysMod::Lua::ILuaBase* g_Lua;
extern CGlobalVars* g_pServerGlobalVars;
extern edict_t* g_pEdictList;
