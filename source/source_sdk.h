#pragma once
#include <iconvar.h>

bool InitSourceSDK();
void ShutdownSourceSDK();

void CVarMainCoresCallback(IConVar* var, const char* pOldValue, float flOldValue);
void CVarTrainCoresCallback(IConVar* var, const char* pOldValue, float flOldValue);
void ClearLoadCache(const CCommand& command);
void ClearPrintQueue(const CCommand& command);
