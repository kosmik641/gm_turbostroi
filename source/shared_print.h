#pragma once
#include "ring_buffer.h"
#include "mutex.h"
#include "lua.hpp"
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
    RingBuffer<std::string, 256> m_Queue;
    Mutex m_Mutex{};
};

extern SharedPrint g_SharedPrint;