#include "railnetwork.h"

#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/Lua/LuaInterface.h>
#include <nlohmann/json.hpp>
#include <cbase.h>
#include "source_sdk.h"
#include "filesystem_gmod.h"

namespace GM = GarrysMod::Lua;

using js = nlohmann::json;

static int g_TLuaHandleNode = 0; // Stores CTrackHandle
static int g_TLuaHandlePath = 0; // Stores CTrackHandle
static int g_TLuaTrain = 0;

static js loadSignalData(const char* def)
{
    ConColorMsg(Color(255, 0, 255, 255), "RailNetwork: Load %s definition...\n", def);
    std::string fileName = "metrostroi_data/";
    fileName += def;
    fileName += '_';
    fileName += g_pServerGlobalVars->mapname.ToCStr();

    // Load from txt
    std::string fileNameTxt = (fileName + ".txt");
    const char* fileDataTxt = GMOD_FileRead(g_Lua, fileNameTxt.c_str(), "DATA");
    if (fileDataTxt)
    {
        try
        {
            js data;
            data = nlohmann::json::parse(fileDataTxt);
            return data;
        }
        catch (std::exception& e)
        {
            ConColorMsg(Color(255, 0, 0, 255), "RailNetwork: Parse error in file [%s]: %s\n",
                fileNameTxt.c_str(), e.what());
        }
    }

    // Load from lua
    std::string fileNameLua = (fileName + ".lua");
    const char* fileDataLua = GMOD_FileRead(g_Lua, fileNameLua.c_str(), "lsv");
    if (fileDataLua)
    {
        try
        {
            js data;
            data = nlohmann::json::parse(fileDataLua);
            return data;
        }
        catch (std::exception& e)
        {
            ConColorMsg(Color(255, 0, 0, 255), "RailNetwork: Parse error in file [%s]: %s\n",
                fileNameLua.c_str(), e.what());
        }
    }

    ConColorMsg(Color(255, 0, 255, 255), "RailNetwork: Not found file for %s definition...\n", def);
    return js();
}

void CRailNetwork::Initialize()
{
    LoadTrack();
    PrintStatistics();
}

void CRailNetwork::Stop()
{
    m_Initialized = false;

    // RailNetwork.Paths = {}
    g_Lua->PushSpecial(GM::SPECIAL_GLOB);
    {
        g_Lua->GetField(-1, "RailNetwork");
        {
            g_Lua->CreateTable();
            g_Lua->SetField(-2, "Paths");
        }
        g_Lua->Pop();
    }
    g_Lua->Pop();
}

void CRailNetwork::Think()
{
    UpdateTrains();
}

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

static inline void entWorldToLocal(CBaseEntity* ent, const Vector& wpos, Vector& out)
{
    if (entGetLocalAngles(ent) == vec3_angle)
    {
        VectorSubtract(wpos, entGetLocalOrigin(ent), out);
    }
    else
    {
        VectorITransform(wpos, entMatrix(ent), out);
    }
}

std::vector<CTrackHandle> CRailNetwork::NearestNodes(const Vector& pos)
{
    std::vector<CTrackHandle> nodes;

    if (m_Paths.empty())
        return nodes;

    TSpacialVector spatialPos = GetSpatialPos(pos);
    int kx = spatialPos.x(), ky = spatialPos.y(), kz = spatialPos.z();
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            for (int z = -1; z <= 1; z++)
            {
                TSpacialVector spVec(kx+x, ky+y, kz+z);
                auto& hSpatials = m_SpatialLookup[spVec.val];
                if (hSpatials.empty())
                    continue;

                nodes.insert(nodes.end(), hSpatials.begin(), hSpatials.end());
            }
        }
    }

    return nodes;
}

std::vector<CRailNetwork::TGetPosOnTrackRes> CRailNetwork::GetPositionOnTrack(const Vector& pos, const QAngle& ang, const TGetPosOnTrackOpt& opts)
{
    std::vector<TGetPosOnTrackRes> out;
    if (m_Paths.empty())
        return out;

    float X_PAD = opts.x_pad;
    float Y_PAD = opts.y_pad;
    float Z_PAD = opts.z_pad;

    auto nearestNodes = NearestNodes(pos);
    for (auto& hNode : nearestNodes)
    {
        auto& node = m_Paths[hNode.PathID()].nodes[hNode.NodeID()];
        // Get local coordinate system of a section
        const Vector& forward = node.dir;
        const Vector up{ 0,0,1 };
        const Vector right = forward.Cross(up);

        // Transform position into local coordinates
        Vector local_pos = pos - node.pos;
        float local_x = local_pos.Dot(forward);
        float local_y = local_pos.Dot(right);
        float local_z = local_pos.Dot(up);
        float yz_delta = sqrt(powf(local_y, 2) + powf(local_z, 2));

        // Determine if facing forward or backward
        Vector local_dir{ 0,0,0 };
        AngleVectors(ang, &local_dir, nullptr, nullptr);
        float dir_delta = local_dir.Dot(forward);
        bool dir_forward = dir_delta > 0;
        float dir_angle = 90-RAD2DEG(acos(dir_delta));

        if (node.id.PathID() != opts.ignore_path
            && (local_x > -X_PAD) && (local_x < node.vec.Length() + X_PAD)
            && (local_y > -Y_PAD) && (local_y < Y_PAD)
            && (local_z > -Z_PAD) && (local_z < Z_PAD))
        {
            TGetPosOnTrackRes res;
            res.hNode1 = node.id;
            res.hNode2 = node.hNext;
            res.hPath = node.id.GetPathHandle();

            res.angle = dir_angle;
            res.forward = dir_forward;
            res.pos.x = local_x * 0.01905 + node.x;
            res.pos.y = local_y * 0.01905;
            res.pos.z = local_z * 0.01905;

            res.distance = yz_delta;

            out.push_back(res);
            /*ConColorMsg(Color(255, 0, 255, 255), "[Cpp] Found track %d_%d for [%.04f %.04f %.04f]\n",
                node.id.pathID+1, node.id.nodeID+1, pos.x, pos.y, pos.z);*/
        }
    }

    // Sort results by distance
    std::sort(out.begin(), out.end(),
        [&](const TGetPosOnTrackRes& a, const TGetPosOnTrackRes& b)
        {
            return a.distance < b.distance;
        });

    return out;
}

void CRailNetwork::GetBogeys(TTrain& train)
{
    train.iNeedFindBogeys = -1;

    int entIdx = train.handle.GetEntryIndex();
    edict_t& edict = *train.edEntity;

    int top = g_Lua->Top();

    /* Get train Lua entity
    * https://github.com/danielga/sourcesdk-minimal/issues/56
    * edict.GetIServerEntity()->GetBaseEntity()->PushEntity();
    * edict.GetIServerEntity()->GetBaseEntity()->SetPhysObject(NULL, NULL);
    */
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
    if (edFrontBogey != nullptr)
    {
        CBaseEntity* entBogey = edFrontBogey->GetIServerEntity() ? edFrontBogey->GetIServerEntity()->GetBaseEntity() : nullptr;
        if (entBogey)
        {
            const Vector& bogeyPos = entGetLocalOrigin(entBogey);
            entWorldToLocal(train.pEntity, bogeyPos, train.lposFrontBogey);

            train.bFrontBogey = true;

            /*Msg("Front bogey pos: [%.04f %.04f %.04f]\n",
                train.lposFrontBogey.x, train.lposFrontBogey.y, train.lposFrontBogey.z);*/
        }
    }

    if (edRearBogey != nullptr)
    {
        CBaseEntity* entBogey = edRearBogey->GetIServerEntity() ? edRearBogey->GetIServerEntity()->GetBaseEntity() : nullptr;
        if (entBogey)
        {
            const Vector& bogeyPos = entGetLocalOrigin(entBogey);
            entWorldToLocal(train.pEntity, bogeyPos, train.lposRearBogey);

            train.bRearBogey = true;

            /*Msg("Rear bogey pos:  [%.04f %.04f %.04f]\n",
                train.lposRearBogey.x, train.lposRearBogey.y, train.lposRearBogey.z);*/
        }
    }
   
    g_Lua->Pop(g_Lua->Top() - top);
}

void CRailNetwork::UpdateTrains()
{
    if (!m_Initialized) return;

    for (size_t i = 1; i < m_Trains.size(); i++) // 0 is world
    {
        TTrain& train = m_Trains[i];

        if (train.handle == INVALID_EHANDLE_INDEX)
            continue;

        // Invalidate edicts
        if (!UTIL_IsValidEdict(*train.edEntity))
        {
            //ConMsg("CRailNetwork::UpdateTrains(): Invalidate train [%d]!\n", i);
            train.Invalidate();
            continue;
        }

        // Find bogeys after spawn
        if (train.iNeedFindBogeys > 0)
        {
            if (--train.iNeedFindBogeys == 0)
                GetBogeys(train);
        }

        train.pos = entGetLocalOrigin(train.pEntity);
    }
}

void CRailNetwork::LoadTrack()
{
    m_Paths.clear();
    m_SpatialLookup.clear();
    const js& j = loadSignalData("track");

    if (j.empty())
    {
        m_Initialized = false;
        return;
    }

    // Allocate memory for paths and nodes (TODO?)
    /*size_t pathsCount = j.size();
    size_t nodesCount = 0;
    for (size_t pathID = 0; pathID < pathsCount; pathID++)
    {
        const auto& jsonPath = j[pathID];
        if (!jsonPath.is_array())
            continue;

        nodesCount += jsonPath.size();
    }*/

    //Msg("pathsCount=%u    nodesCount=%u\n", pathsCount, nodesCount);

    // Load paths
    for (size_t pathID = 0; pathID < j.size(); pathID++)
    {
        const auto& jsonPath = j[pathID];
        if (!jsonPath.is_array())
            continue;

        TPath trackPath{ CTrackHandle(pathID) };
        for (size_t nodeID = 0; nodeID < jsonPath.size(); nodeID++)
        {
            const auto& jsonNode = jsonPath[nodeID];
            if (!jsonNode.is_string())
                continue;

            const std::string &str = jsonNode.get<js::string_t>();
            Vector nodePos{ 0, 0, 0 };
            if (sscanf(str.c_str(), "[%f %f %f]", &nodePos.x, &nodePos.y, &nodePos.z) != 3)
                continue;

            TNode pathNode{ CTrackHandle(pathID, nodeID) };

            if (trackPath.nodes.size() > 0)
            {
                TNode& prevNode = trackPath.nodes.back();
                
                // Save prev and next
                pathNode.hPrev = prevNode.id;
                prevNode.hNext = pathNode.id;

                // Calc distances for prev node
                float len = prevNode.pos.DistTo(nodePos) * 0.01905f;
                prevNode.dir = (nodePos - prevNode.pos).Normalized();
                prevNode.vec = nodePos - prevNode.pos;
                prevNode.length = len;

                // Calc path length
                trackPath.length += len;
            }

            pathNode.pos = nodePos;
            pathNode.x = trackPath.length;

            trackPath.nodes.push_back(pathNode);
            AddSpatial(pathNode);
        }

        m_Paths.push_back(trackPath);
    }

    // Find places where tracks link up together
    bool isOrange = std::string(g_pServerGlobalVars->mapname.ToCStr()).find("orange") != std::string::npos;
    for (auto& path : m_Paths)
    {
        if (path.nodes.size() == 0)
            continue;

        auto& nodeFirst = path.nodes.front();
        auto& nodeLast = path.nodes.back();

        TGetPosOnTrackOpt opt;
        opt.ignore_path = (isOrange && path.id.PathID() == 0) ? -1 : path.id;
        auto pos1 = GetPositionOnTrack(nodeFirst.pos, vec3_angle, opt);
        auto pos2 = GetPositionOnTrack(nodeLast.pos, vec3_angle, opt);

        if (pos1.size() || pos2.size()) Msg("\nJoints for path(%d):\n", path.id.PathID() + 1);

        if (pos1.size() > 0)
        {
            CTrackHandle hJoin1 = pos1[0].hNode1;
            TNode& join1 = m_Paths[hJoin1.PathID()].nodes[hJoin1.NodeID()];

            Msg("\tjoin1(%d).branches = {x=%.03f  id=%d}\n", join1.id.NodeID() + 1, pos1[0].pos.x, nodeFirst.id.NodeID() + 1);
            Msg("\tnode1(%d).branches = {x=%.03f  id=%d}\n", nodeFirst.id.NodeID() + 1, nodeFirst.x, join1.id.NodeID() + 1);
            
            join1.AddBranch(pos1[0].pos.x, nodeFirst.id);
            nodeFirst.AddBranch(nodeFirst.x, join1.id);
        }

        if (pos2.size() > 0)
        {
            CTrackHandle hJoin2 = pos2[0].hNode1;
            auto& join2 = m_Paths[hJoin2.PathID()].nodes[hJoin2.NodeID()];

            Msg("\tjoin2(%d).branches = {x=%.03f  id=%d}\n", join2.id.NodeID() + 1, pos2[0].pos.x, nodeLast.id.NodeID() + 1);
            Msg("\tnode2(%d).branches = {x=%.03f  id=%d}\n", nodeLast.id.NodeID() + 1, nodeLast.x, join2.id.NodeID() + 1);

            join2.AddBranch(pos2[0].pos.x, nodeLast.id);
            nodeLast.AddBranch(nodeLast.x, join2.id);
        }
    }

    // Fill Lua table
    g_Lua->PushSpecial(GM::SPECIAL_GLOB);
    {
        g_Lua->GetField(-1, "RailNetwork");
        {
            g_Lua->CreateTable();
            {
                int i = 0;
                for (TPath& path : g_RailNetwork.m_Paths)
                {
                    // Save path reference
                    if (path.iRef == -1)
                    {
                        g_Lua->PushUserType(path.id, g_TLuaHandlePath);
                        path.iRef = g_Lua->ReferenceCreate();
                    }

                    // Save nodes references
                    for (TNode& node : path.nodes)
                    {
                        if (node.iRef != -1)
                            continue;

                        g_Lua->PushUserType(node.id, g_TLuaHandleNode);
                        node.iRef = g_Lua->ReferenceCreate();
                    }

                    g_Lua->ReferencePush(path.iRef);
                    GM::p_lua_rawseti(g_Lua->GetState(), -2, ++i);
                }
            }
            g_Lua->SetField(-2, "Paths");
        }
        g_Lua->Pop();
    }
    g_Lua->Pop();

    m_Initialized = true;
}

CRailNetwork::TGetTrackPosRes CRailNetwork::GetTrackPosition(const TPath& path, float x)
{
    TGetTrackPosRes out;
    if (m_Paths.empty() || !path.id.IsValidPathID())
        return out;

    for (auto& node : path.nodes)
    {
        if (node.hNext.NodeID() == CTrackHandle::INVALID_NODE_ID)
            break;

        auto& nextNode = path.nodes[node.hNext.NodeID()];
        if (node.x < x && x < nextNode.x)
        {
            const Vector& dir1 = node.dir;
            const Vector& dir2 = nextNode.dir;

            float t = (x - node.x) / node.length;
            out.pos = node.pos + node.vec * t;
            out.ang = dir1 * (1 - t) + dir2 * t;
            out.hNode = node.id;
            break;
        }
    }

    return out;
}

inline CRailNetwork::TSpacialVector CRailNetwork::GetSpatialPos(const Vector& pos)
{
    constexpr float SPATIAL_CELL_WIDTH = 1024;
    constexpr float SPATIAL_CELL_HEIGHT = 256;

    int kx = floor(pos.x / SPATIAL_CELL_WIDTH);
    int ky = floor(pos.y / SPATIAL_CELL_WIDTH);
    int kz = floor(pos.z / SPATIAL_CELL_HEIGHT);

    return TSpacialVector(kx, ky, kz);
}

void CRailNetwork::AddSpatial(const TNode& node)
{
    m_SpatialLookup[GetSpatialPos(node.pos).val].push_back(node.id);

    //Msg("AddSpatial(): [%.04f %.04f %.04f]    %d    %d    %d\n", node.pos.x, node.pos.y, node.pos.z, kx, ky, kz);
}

void CRailNetwork::PrintStatistics()
{
    if (!m_Initialized)
    {
        ConColorMsg(Color(255, 0, 0, 255), "RailNetwork: Not initialized\n");
        return;
    }

    float totalL = 0;
    for (auto& path : m_Paths)
    {
        totalL += path.length;
    }

    ConColorMsg(Color(255, 0, 255, 255), "RailNetwork: Total %.03f km of paths defined:\n", totalL / 1000.0f);
    for (auto& path : m_Paths)
    {
        ConColorMsg(Color(255, 0, 255, 255), "    [%d] %.03f km (%d nodes)\n",
            path.id.PathID() + 1, path.length / 1000.0f, path.nodes.size());
    }

    size_t cellCount = m_SpatialLookup.size();
    size_t maxn = 0, avgn = 0;
    for (auto& node : m_SpatialLookup)
    {
        size_t cellSize = node.second.size();
        maxn = std::max(maxn, cellSize);
        avgn += cellSize;
    }

    ConColorMsg(Color(255, 0, 255, 255), "RailNetwork: %d cells used for spatial lookup\n", cellCount);
    ConColorMsg(Color(255, 0, 255, 255), "RailNetwork: Most nodes in cell: %d, average nodes in cell: %.2f\n", maxn, (float)avgn/(float)cellCount);

}

namespace NearestNodesLUA {
    static std::vector<CTrackHandle> nodes;
    static size_t i = 0;
}
    
int CRailNetwork::LUA_NearestNodes__call(lua_State* L) // (for generator)
{
    using namespace NearestNodesLUA;
    GM::ILuaInterface* LUA = reinterpret_cast<GM::ILuaInterface*>(L->luabase);

    if (i >= nodes.size())
    {
        LUA->PushNil();
        return 1;
    }
    auto hNode = nodes[i++];
    auto& node = g_RailNetwork.m_Paths[hNode.PathID()].nodes[hNode.NodeID()];

    LUA->PushNumber(hNode.NodeID() + 1);
    LUA->ReferencePush(node.iRef);
    return 2;
}

int CRailNetwork::NearestNodes(GarrysMod::Lua::ILuaBase* LUA)
{
    if (m_Paths.empty())
        return 0;

    LUA->CheckType(1, GM::Type::Vector);
    const Vector& pos = LUA->GetVector(1);

    NearestNodesLUA::nodes = g_RailNetwork.NearestNodes(pos);
    NearestNodesLUA::i = 0;

    LUA->PushCFunction(LUA_NearestNodes__call);
    return 1;
}

//------------------------------------------------------------------------------
// Garry's Mod Lua
//------------------------------------------------------------------------------
int CRailNetwork::GetPositionOnTrack(GarrysMod::Lua::ILuaBase* LUA)
{
    if (m_Paths.empty())
        return 0;

    // Get args from Lua
    LUA->CheckType(1, GM::Type::Vector);

    Vector pos = LUA->GetVector(1);
    QAngle ang{ 0, 0, 0 };
    if (LUA->IsType(2, GM::Type::Angle))
        ang = LUA->GetAngle(2);

    CRailNetwork::TGetPosOnTrackOpt opt;
    if (LUA->IsType(3, GM::Type::Table))
    {
        // opt.y_pad
        LUA->GetField(3, "y_pad");
        if (LUA->IsType(-1, GM::Type::Number))
        {
            opt.y_pad = LUA->GetNumber(-1);
        }
        LUA->Pop();

        // opt.radius
        LUA->GetField(3, "radius");
        if (LUA->IsType(-1, GM::Type::Number))
        {
            opt.radius = LUA->GetNumber(-1);
        }
        LUA->Pop();

        // opt.z_pad
        LUA->GetField(3, "z_pad");
        if (LUA->IsType(-1, GM::Type::Number))
        {
            opt.z_pad = LUA->GetNumber(-1);
        }
        LUA->Pop();

        // opt.ignore_path
        LUA->GetField(3, "ignore_path");
        if (LUA->IsType(-1, g_TLuaHandlePath))
        {
            CTrackHandle hPath = LUA->GetUserdata(-1);
            opt.ignore_path = hPath.PathID();
        }
        else if (LUA->IsType(-1, GM::Type::Number))
        {
            opt.ignore_path = LUA->GetNumber(-1);
        }
        LUA->Pop();
    }

    // Calculate
    auto results = GetPositionOnTrack(pos, ang, opt);

    // Return to Lua
    LUA->CreateTable();
    {
        int i = 0;
        for (auto& res : results)
        {
            LUA->CreateTable();
            {
                auto& node1 = m_Paths[res.hNode1.PathID()].nodes[res.hNode1.NodeID()];
                LUA->ReferencePush(node1.iRef);
                LUA->SetField(-2, "node1");

                if (res.hNode2.IsValidNodeID())
                {
                    auto& node2 = m_Paths[res.hNode1.PathID()].nodes[res.hNode1.NodeID()];
                    LUA->ReferencePush(node2.iRef);
                    LUA->SetField(-2, "node2");
                }

                auto& path = m_Paths[res.hNode1.PathID()];
                LUA->ReferencePush(path.iRef);
                LUA->SetField(-2, "path");

                LUA->PushNumber(res.angle);
                LUA->SetField(-2, "angle");

                LUA->PushBool(res.forward);
                LUA->SetField(-2, "forward");

                LUA->PushNumber(res.pos.x);
                LUA->SetField(-2, "x");

                LUA->PushNumber(res.pos.y);
                LUA->SetField(-2, "y");

                LUA->PushNumber(res.pos.z);
                LUA->SetField(-2, "z");

                LUA->PushNumber(res.distance);
                LUA->SetField(-2, "distance");
            }
            GM::p_lua_rawseti(LUA->GetState(), -2, ++i);
        }
    }

    return 1;
}

int CRailNetwork::GetTrackPosition(GarrysMod::Lua::ILuaBase* LUA)
{
    if (m_Paths.empty())
       return 0;

    LUA->CheckType(1, g_TLuaHandlePath);
    float x = LUA->CheckNumber(2);

    CTrackHandle hPath = LUA->GetUserdata(1);
    if (!hPath.IsValidPathID())
        return 0;

    TPath& path = m_Paths[hPath.PathID()];
    TGetTrackPosRes res = GetTrackPosition(path, x);
    if (!res.hNode.IsValidNodeID())
        return 0;

    auto& node = m_Paths[res.hNode.PathID()].nodes[res.hNode.NodeID()];

    LUA->PushVector(res.pos);
    LUA->PushVector(res.ang);
    LUA->ReferencePush(node.iRef);
    return 3;
}

int CRailNetwork::GetTrackEditorPaths(GarrysMod::Lua::ILuaBase* LUA)
{
    if (m_Paths.empty())
    {
        LUA->CreateTable();
        return 1;
    }

    LUA->CreateTable();
    {
        int i = 0;
        for (auto& path : m_Paths)
        {
            LUA->CreateTable();
            {
                int j = 0;
                for (auto& node : path.nodes)
                {
                    LUA->PushVector(node.pos);
                    GM::p_lua_rawseti(LUA->GetState(), -2, ++j);
                }
            }
            GM::p_lua_rawseti(LUA->GetState(), -2, ++i);
        }
        
    }
    return 1;
}

void CRailNetwork::PushPath(GarrysMod::Lua::ILuaBase* LUA)
{
    if (m_Paths.empty())
    {
        LUA->PushNil();
        return;
    }
        
    int pathID = LUA->CheckNumber(1);
    pathID -= 1;
    if (pathID < 0)
    {
        LUA->PushNil();
        return;
    }

    try
    {
        TPath& path = m_Paths.at(pathID);
        LUA->ReferencePush(path.iRef);
    }
    catch (std::out_of_range&)
    {
        LUA->PushNil();
    }
}

void CRailNetwork::PushNode(GarrysMod::Lua::ILuaBase* LUA)
{
    if (m_Paths.empty())
    {
        LUA->PushNil();
        return;
    }

    int pathID = LUA->CheckNumber(1);
    int nodeID = LUA->CheckNumber(2);

    pathID -= 1;
    nodeID -= 1;
    if (pathID < 0 || nodeID < 0)
    {
        LUA->PushNil();
        return;
    }

    try
    {
        auto& path = m_Paths.at(pathID);
        TNode& node = path.nodes.at(nodeID);
        LUA->ReferencePush(node.iRef);
    }
    catch (std::out_of_range&)
    {
        LUA->PushNil();
    }
}

void CRailNetwork::RegisterLuaUserData()
{
    // Track node
    g_TLuaHandleNode = g_Lua->CreateMetaTable("RNNode");
    {
        g_Lua->PushCFunction(LUA_RNNode__index);
        g_Lua->SetField(-2, "__index");

        g_Lua->PushCFunction(LUA_RNNode__eq);
        g_Lua->SetField(-2, "__eq");

        g_Lua->PushCFunction(LUA_RNNode__tostring);
        g_Lua->SetField(-2, "__tostring");
    }
    g_Lua->Pop();

    // Track path
    g_TLuaHandlePath = g_Lua->CreateMetaTable("RNPath");
    {
        g_Lua->PushCFunction(LUA_RNPath__index);
        g_Lua->SetField(-2, "__index");

        g_Lua->PushCFunction(LUA_RNPath__eq);
        g_Lua->SetField(-2, "__eq");

        g_Lua->PushCFunction(LUA_RNPath__len);
        g_Lua->SetField(-2, "__len");

        g_Lua->PushCFunction(LUA_RNPath__tostring);
        g_Lua->SetField(-2, "__tostring");
    }
    g_Lua->Pop();
}

int CRailNetwork::LUA_RNNode__index(lua_State* L)
{
    GM::ILua* LUA = L->luabase;
    CTrackHandle hNode = LUA->GetUserdata(1);
    if (!hNode.IsValidNodeID())
        return 0;

    if (!LUA->IsType(2, GM::Type::String))
        return 0;

    const TNode& node = g_RailNetwork.m_Paths[hNode.PathID()].nodes[hNode.NodeID()];

    const char* id = LUA->GetString(2);
    if (strcmp(id, "x") == 0) // node.x
    {
        LUA->PushNumber(node.x);
        return 1;
    }

    if (strcmp(id, "length") == 0) // node.length
    {
        LUA->PushNumber(node.length);
        return 1;
    }

    if (strcmp(id, "id") == 0) // node.id
    {
        LUA->PushNumber(node.id.NodeID() + 1);
        return 1;
    }

    if (strcmp(id, "pos") == 0) // node.pos
    {
        LUA->PushVector(node.pos);
        return 1;
    }

    if (strcmp(id, "dir") == 0) // node.dir
    {
        LUA->PushVector(node.dir);
        return 1;
    }

    if (strcmp(id, "vec") == 0) // node.vec
    {
        LUA->PushVector(node.vec);
        return 1;
    }

    if (strcmp(id, "next") == 0) // node.next
    {
        auto& hNextNode = node.hNext;
        if (!hNextNode.IsValidNodeID())
            return 0;

        auto& nextNode = g_RailNetwork.m_Paths[hNextNode.PathID()].nodes[hNextNode.NodeID()];
        LUA->ReferencePush(nextNode.iRef);
        return 1;
    }

    if (strcmp(id, "prev") == 0) // node.prev
    {
        auto& hPrevNode = node.hPrev;
        if (!hPrevNode.IsValidNodeID())
            return 0;

        auto& prevNode = g_RailNetwork.m_Paths[hPrevNode.PathID()].nodes[hPrevNode.NodeID()];
        LUA->ReferencePush(prevNode.iRef);
        return 1;
    }

    if (strcmp(id, "path") == 0) // node.path
    {
        auto& path = g_RailNetwork.m_Paths[node.id.PathID()];
        LUA->ReferencePush(path.iRef);
        return 1;
    }

    if (strcmp(id, "branches") == 0)  // node.branches
    {
        if (node.branchesCount == 0)
        {
            LUA->PushNil();
            return 1;
        }

        LUA->CreateTable();
        for (int i = 0; i < node.branchesCount; i++)
        {
            const auto& branch = node.branches[i];
            LUA->CreateTable();
            {
                LUA->PushNumber(branch.x);
                GM::p_lua_rawseti(LUA->GetState(), -2, 1);

                auto& node = g_RailNetwork.m_Paths[branch.hNode.PathID()].nodes[branch.hNode.NodeID()];
                LUA->ReferencePush(node.iRef);
                GM::p_lua_rawseti(LUA->GetState(), -2, 2);
            }
            GM::p_lua_rawseti(LUA->GetState(), -2, i+1);
        }

        return 1;
    }

    return 0;
}

int CRailNetwork::LUA_RNNode__eq(lua_State* L)
{
    GM::ILua* LUA = L->luabase;
    if (!LUA->IsType(1, g_TLuaHandleNode) || !LUA->IsType(2, g_TLuaHandleNode))
    {
        LUA->PushBool(false);
        return 1;
    }

    CTrackHandle hNode1 = LUA->GetUserdata(1);
    CTrackHandle hNode2 = LUA->GetUserdata(2);

    if (!hNode1.IsValidNodeID() || !hNode2.IsValidNodeID())
    {
        LUA->PushBool(false);
        return 1;
    }

    LUA->PushBool(hNode1 == hNode2);
    return 1;
}

int CRailNetwork::LUA_RNNode__tostring(lua_State* L)
{
    GM::ILua* LUA = L->luabase;
    CTrackHandle hNode = LUA->GetUserdata(1);
    if (!hNode.IsValidNodeID())
    {
        LUA->PushString("TrackNode [NULL]");
        return 1;
    }

    const TNode& node = g_RailNetwork.m_Paths[hNode.PathID()].nodes[hNode.NodeID()];

    char buf[128]{};
    sprintf_s(buf, sizeof(buf), "TrackNode [%d][%d][%.04f %.04f %.04f][L=%.04f m][p=0x%p]",
        node.id.PathID() + 1, node.id.NodeID() + 1,
        node.pos.x, node.pos.y, node.pos.z,
        node.length, LUA->GetUserdata(1));

    LUA->PushString(buf);
    return 1;
}

int CRailNetwork::LUA_RNPath__index(lua_State* L)
{
    GM::ILua* LUA = L->luabase;
    CTrackHandle hPath = LUA->GetUserdata(1);
    if (!hPath.IsValidPathID())
        return 0;

    TPath& path = g_RailNetwork.m_Paths[hPath.PathID()];

    if (LUA->IsType(2, GM::Type::Number))
    {
        int nodeIdx = LUA->GetNumber(2) - 1;
        try
        {
            TNode& node = path.nodes.at(nodeIdx);
            LUA->ReferencePush(node.iRef);
        }
        catch (std::out_of_range&)
        {
            LUA->PushNil();
        }
        return 1;
    }

    if (LUA->IsType(2, GM::Type::String))
    {
        const char* id = LUA->GetString(2);
        if (strcmp(id, "id") == 0) // path.id
        {
            LUA->PushNumber(hPath.PathID() + 1);
            return 1;
        }

        if (strcmp(id, "length") == 0) // path.length
        {
            LUA->PushNumber(path.length);
            return 1;
        }

        if (strcmp(id, "GetTrackPosition") == 0) // path.GetTrackPosition
        {
            LUA->PushBool(true);
            return 1;
        }
    }

    return 0;
}

int CRailNetwork::LUA_RNPath__eq(lua_State* L)
{
    GM::ILua* LUA = L->luabase;
    if (!LUA->IsType(1, g_TLuaHandlePath) || !LUA->IsType(2, g_TLuaHandlePath))
    {
        LUA->PushBool(false);
        return 1;
    }

    CTrackHandle hPath1 = LUA->GetUserdata(1);
    CTrackHandle hPath2 = LUA->GetUserdata(2);
    
    LUA->PushBool(hPath1 == hPath2);
    return 1;
}

int CRailNetwork::LUA_RNPath__len(lua_State* L)
{
    GM::ILua* LUA = L->luabase;
    CTrackHandle hPath = LUA->GetUserdata(1);
    if (!hPath.IsValidPathID())
        return 0;

    LUA->PushNumber(g_RailNetwork.m_Paths[hPath.PathID()].nodes.size());
    return 1;
}

int CRailNetwork::LUA_RNPath__tostring(lua_State* L)
{
    GM::ILua* LUA = L->luabase;
    CTrackHandle hPath = LUA->GetUserdata(1);
    if (!hPath.IsValidPathID())
    {
        LUA->PushString("TrackPath [NULL]");
        return 1;
    }

    const TPath& path = g_RailNetwork.m_Paths[hPath.PathID()];

    char buf[64]{};
    sprintf_s(buf, sizeof(buf), "TrackPath [%d][L=%.04f m][%d nodes][p=0x%p]",
        path.id.PathID()+1, path.length, path.nodes.size(), LUA->GetUserdata(1));

    LUA->PushString(buf);
    return 1;
}

// Clear Lua references
CRailNetwork::TNode::~TNode()
{
    if (iRef != -1)
    {
        g_Lua->ReferenceFree(iRef);
        iRef = -1;
    }
}

CRailNetwork::TPath::~TPath()
{
    if (iRef != -1)
    {
        g_Lua->ReferenceFree(iRef);
        iRef = -1;
    }
}

// Singleton
CRailNetwork g_RailNetwork;
