#include "filesystem_gmod.h"
namespace GM = GarrysMod::Lua;

const char* GMOD_FileRead(GarrysMod::Lua::ILuaBase* LUA, const char* fileName, const char* pathID)
{
    const char* data = nullptr;
    int top = LUA->Top();
    LUA->PushSpecial(GM::SPECIAL_GLOB);
    {
        LUA->GetField(-1, "file");
        if (LUA->IsType(-1, GM::Type::Table))
        {
            LUA->GetField(-1, "Read");
            {
                if (LUA->IsType(-1, GM::Type::Function))
                {
                    LUA->PushString(fileName);
                    LUA->PushString(pathID);
                    LUA->Call(2, 1);
                    if (LUA->IsType(-1, GM::Type::String))
                    {
                        data = LUA->GetString(-1);
                    }
                }
            }
        }
    }
    LUA->Pop(LUA->Top() - top);

    return data;
}

bool GMOD_FileWrite(GarrysMod::Lua::ILuaBase* LUA, const char* fileName, const char* data)
{
    bool success = false;
    int top = LUA->Top();
    LUA->PushSpecial(GM::SPECIAL_GLOB);
    {
        LUA->GetField(-1, "file");
        if (LUA->IsType(-1, GM::Type::Table))
        {
            LUA->GetField(-1, "Write");
            {
                if (LUA->IsType(-1, GM::Type::Function))
                {
                    LUA->PushString(fileName);
                    LUA->PushString(data);
                    LUA->Call(2, 1);
                    if (LUA->IsType(-1, GM::Type::Bool))
                    {
                        success = LUA->GetBool(-1);
                    }
                }
            }
        }
    }

    LUA->Pop(LUA->Top() - top);
    return success;
}

int64_t GMOD_FileSize(GarrysMod::Lua::ILuaBase* LUA, const char* fileName, const char* pathID)
{
    int64_t size = -1;
    int top = LUA->Top();
    LUA->PushSpecial(GM::SPECIAL_GLOB);
    {
        LUA->GetField(-1, "file");
        if (LUA->IsType(-1, GM::Type::Table))
        {
            LUA->GetField(-1, "Size");
            {
                if (LUA->IsType(-1, GM::Type::Function))
                {
                    LUA->PushString(fileName);
                    LUA->PushString(pathID);
                    LUA->Call(2, 1);
                    if (LUA->IsType(-1, GM::Type::Number))
                    {
                        size = (int64_t)LUA->GetNumber(-1);
                    }
                }
            }
        }
    }
    LUA->Pop(LUA->Top() - top);

    return size;
}
