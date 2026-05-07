#pragma once
#include <iconvar.h>

struct lua_State;
namespace GarrysMod::Lua
{
    typedef void (*fn_lua_rawseti)(lua_State* L, int idx, int n);
    extern fn_lua_rawseti lua_rawseti;
}

bool InitSourceSDK();
void ShutdownSourceSDK();

void CVarMainCoresCallback(IConVar* var, const char* pOldValue, float flOldValue);
void CVarTrainCoresCallback(IConVar* var, const char* pOldValue, float flOldValue);
void ClearLoadCache(const CCommand& command);
void ClearPrintQueue(const CCommand& command);
void RunStringDisable(const CCommand& command);
