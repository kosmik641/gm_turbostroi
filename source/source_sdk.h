#pragma once

#include <eiface.h>
#include <filesystem.h>

bool GetGlobalVars();
bool InitFileSystem();
bool RegisterConCommands();
void SaveSettings();
bool InitSourceSDK();
void ShutdownSourceSDK();

void ClearLoadCache(const CCommand& command);
void ClearPrintQueue(const CCommand& command);
void CVarMainCoresCallback(IConVar* var, const char* pOldValue, float flOldValue);
void CVarTrainCoresCallback(IConVar* var, const char* pOldValue, float flOldValue);