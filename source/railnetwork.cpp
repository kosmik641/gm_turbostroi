#include "railnetwork.h"

//#include <shareddefs.h>
//#include <mathlib/vmatrix.h>
//#include <util.h>
#include <cbase.h>
#include "source_sdk.h"

namespace GM = GarrysMod::Lua;

void CRailNetwork::AddTrain(CBaseHandle handle)
{
    int entIdx = handle.GetEntryIndex();
    edict_t& edict = g_pEdictList[entIdx];

    if (!UTIL_IsValidEdict(edict))
    {
        ConColorMsg(Color(255, 255, 0, 255), "Railnetwork: Entity[%d] is not valid!\n", entIdx);
        return;
    }

    TTrain& train = m_Trains[entIdx];
    train.handle = handle;
    train.edEntity = &edict;
    train.pEntity = edict.GetIServerEntity()->GetBaseEntity();
    train.iNeedFindBogeys = 2; // Get bogeys in 2 ticks
}

void CRailNetwork::Think()
{
    UpdateTrains();
}

static inline const Vector& entGetLocalOrigin(CBaseEntity* ent)
{
#if defined(_WIN32)
    Vector* vec = reinterpret_cast<Vector*>(&reinterpret_cast<char*>(ent)[0x308-4]);
    return *vec;
#else
    ent->GetLocalOrigin();
#endif
}

static inline const QAngle& entGetLocalAngles(CBaseEntity* ent)
{
#if defined(_WIN32)
    QAngle* ang = reinterpret_cast<QAngle*>(&reinterpret_cast<char*>(ent)[0x314-4]);
    return *ang;
#else
    return ent->GetLocalAngles();
#endif
}

static inline const matrix3x4_t& entMatrix(CBaseEntity* ent)
{
#if defined(_WIN32)
    matrix3x4_t* mat = reinterpret_cast<matrix3x4_t*>(&reinterpret_cast<char*>(ent)[0x22C]);
    return *mat;
#else
    return ent->EntityToWorldTransform();
#endif
}

static inline const Vector& entWorldToLocal(CBaseEntity* ent, const Vector& wpos, Vector& out)
{
    if (entGetLocalAngles(ent) == vec3_angle)
    {
        VectorSubtract(wpos, entGetLocalOrigin(ent), out);
    }
    else
    {
        VectorITransform(wpos, entMatrix(ent), out);
    }

    return out;
}

void CRailNetwork::GetBogeys(TTrain& train)
{
    train.iNeedFindBogeys = -1;

    int entIdx = train.handle.GetEntryIndex();
    edict_t& edict = *train.edEntity;

    int top = g_Lua->Top();

    // Get train entity
    //edict.GetIServerEntity()->GetBaseEntity()->PushEntity(); // not working
    //edict.GetIServerEntity()->GetBaseEntity()->SetPhysObject(NULL, NULL); // https://github.com/danielga/sourcesdk-minimal/issues/56
    GMOD_PushEntityOnStack(g_Lua, entIdx);

    // Get front bogey
    edict_t* edFrontBogey = nullptr;
    g_Lua->GetField(-1, "FrontBogey");
    if (g_Lua->IsType(-1, GM::Type::Entity))
    {
        CBaseHandle handle = GMOD_GetEntHandle(g_Lua, -1);
        edFrontBogey = &g_pEdictList[handle.GetEntryIndex()];
    }

    if (edFrontBogey == nullptr)
    {
        ConColorMsg(Color(255, 255, 0, 255), "Railnetwork: No front bogey for Entity[%d][%s]\n", entIdx, edict.GetClassName());
    }
    g_Lua->Pop(); // train.FrontBogey

    // Get rear bogey
    edict_t* edRearBogey = nullptr;
    g_Lua->GetField(-1, "RearBogey");
    if (g_Lua->IsType(-1, GM::Type::Entity))
    {
        CBaseHandle handle = GMOD_GetEntHandle(g_Lua, -1);
        edRearBogey = &g_pEdictList[handle.GetEntryIndex()];
    }
    
    if (edRearBogey == nullptr)
    {
        ConColorMsg(Color(255, 255, 0, 255), "Railnetwork: No rear bogey for Entity[%d][%s]\n", entIdx, edict.GetClassName());
    }
    g_Lua->Pop(); // train.RearBogey

    // Get local bogeys position
    const Vector& trainPos = entGetLocalOrigin(train.pEntity);
    if (edFrontBogey != nullptr)
    {
        CBaseEntity* entBogey = edFrontBogey->GetIServerEntity() ? edFrontBogey->GetIServerEntity()->GetBaseEntity() : nullptr;
        if (entBogey)
        {
            const Vector& bogeyPos = entGetLocalOrigin(entBogey);

            //train.vecFrontBogey = entWorldToLocal(train.pEntity, bogeyPos); // broken
            entWorldToLocal(train.pEntity, bogeyPos, train.vecFrontBogey);

            train.bFrontBogey = true;

           /* Msg("Front bogey pos: [%.04f %.04f %.04f]\n",
                train.vecFrontBogey.x, train.vecFrontBogey.y, train.vecFrontBogey.z);*/
        }
    }

    if (edRearBogey != nullptr)
    {
        CBaseEntity* entBogey = edRearBogey->GetIServerEntity() ? edRearBogey->GetIServerEntity()->GetBaseEntity() : nullptr;
        if (entBogey)
        {
            const Vector& bogeyPos = entGetLocalOrigin(entBogey);

            //train.vecRearBogey = entWorldToLocal(train.pEntity, trainPos); // broken
            entWorldToLocal(train.pEntity, bogeyPos, train.vecRearBogey);

            train.bRearBogey = true;

            /*Msg("Rear bogey pos:  [%.04f %.04f %.04f]\n",
                train.vecRearBogey.x, train.vecRearBogey.y, train.vecRearBogey.z);*/
        }
    }
   
    g_Lua->Pop(g_Lua->Top() - top);
}

void CRailNetwork::UpdateTrains()
{
    for (int i = 1; i < m_Trains.size(); i++) // 0 is world
    {
        TTrain& train = m_Trains[i];

        if (train.handle == INVALID_EHANDLE_INDEX)
            continue;

        // Invalidate edicts
        if (!UTIL_IsValidEdict(*train.edEntity))
        {
            //ConMsg("Remove train #%d!\n", i);
            train.Invalidate();
            continue;
        }

        // Find bogeys after spawn
        if (train.iNeedFindBogeys > 0)
        {
            if (--train.iNeedFindBogeys == 0)
                GetBogeys(train);
        }
    }
}

// Singleton
CRailNetwork g_RailNetwork;