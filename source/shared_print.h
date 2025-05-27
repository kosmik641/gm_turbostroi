#pragma once
#include "mutex.h"
#include "lua.hpp"
#include <queue>
#include <string>

class SharedPrint
{
public:
    SharedPrint() = default;

    void Push(std::string& str);
    void Push(const char* str);
    
    void PrintAvailable();
    void ClearPrintQueue();

    static int PrintL(lua_State* L);

private:
    std::queue<std::string> m_Queue;
    Mutex m_Mutex{};
};

extern SharedPrint g_SharedPrint;