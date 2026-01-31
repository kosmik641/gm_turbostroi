#pragma once
#include <array>
#include <edict.h>
#include <basehandle.h>
#include <GarrysMod/Lua/Interface.h>

class CBaseEntity;
class CRailNetwork
{
public:
    void AddTrain(CBaseHandle handle);


    void RemoveTrain(int entIdx)
    {
        m_Trains[entIdx].handle = INVALID_EHANDLE_INDEX;
    }

    void Think();
    
private:
    struct TTrain;

    void GetBogeys(TTrain& train);
    void UpdateTrains();

    struct TTrain
    {
        CBaseHandle handle{ INVALID_EHANDLE_INDEX };
        edict_t* edEntity = nullptr;
        CBaseEntity* pEntity = nullptr;
        //Vector vecEntity{ 0.0f, 0.0f, 0.0f };
        Vector vecFrontBogey{ 0.0f, 0.0f, 0.0f };
        Vector vecRearBogey{ 0.0f, 0.0f, 0.0f };
        bool bFrontBogey = false;
        bool bRearBogey = false;
        int iNeedFindBogeys = -1;

        void Invalidate()
        {
            handle = INVALID_EHANDLE_INDEX;
            edEntity = nullptr;
            pEntity = nullptr;
            bFrontBogey = false;
            bRearBogey = false;
            iNeedFindBogeys = -1;
        }
    };

    std::array<TTrain, MAX_EDICTS> m_Trains;
};

extern CRailNetwork g_RailNetwork;