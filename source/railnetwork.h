#pragma once
#include <array>
#include <edict.h>
#include <basehandle.h>
#include <utlmemory.h>
#include <vector>
#include <unordered_map>

namespace GarrysMod::Lua { class ILuaBase; }
struct lua_State;
class CBaseEntity;

// Handle for store IDs of paths and nodes
class CTrackHandle
{
public:
    using I = int16_t; // ID type
    using I2 = int32_t; // Handle type

    enum : I {
        INVALID_PATH_ID = -1,
        INVALID_NODE_ID = INVALID_PATH_ID,
        ONLY_PATH_NODE_ID = -2,
    };

    CTrackHandle() = default;
    CTrackHandle(I path, I node = ONLY_PATH_NODE_ID)
    {
        m_Value = path;
        m_Value |= int(node) << 16;
    };
    CTrackHandle(const void* v) { m_Value = v ? *reinterpret_cast<const I2*>(v) : INVALID_EHANDLE_INDEX; }
    CTrackHandle(const CTrackHandle& other) { m_Value = other.m_Value; }

    inline bool IsValidNodeID() const {
        return (path > INVALID_PATH_ID) && (node > INVALID_NODE_ID);
    }
    inline bool IsValidPathID() const {
        return (path > INVALID_PATH_ID) && (node == ONLY_PATH_NODE_ID);
    }
    inline bool IsValid() const {
        return IsValidPathID() || IsValidNodeID();
    }
    inline I PathID() const { return path; }
    inline I NodeID() const { return node; }
    inline CTrackHandle GetPathHandle() const { return path; }

    operator int() const { return m_Value; }
    operator void*() const { return reinterpret_cast<void*>(m_Value); }

    I2 operator +(const I2& v) const = delete;
    bool operator ==(const CTrackHandle& other) const
    {
        return (m_Value == other.m_Value);
    }
private:
    union
    {
        I2 m_Value = INVALID_EHANDLE_INDEX;
        struct {
            I path;
            I node;
        };
    };
};

class CRailNetwork
{
public:
    void Initialize();
    void Stop();
    void Think();
    void PrintStatistics();
    
    void AddTrain(CBaseHandle handle);
    void RemoveTrain(int entIdx) { m_Trains[entIdx].Invalidate(); }

    std::vector<CTrackHandle> NearestNodes(const Vector& pos);

    //--------------------------------------
    // GetPositionOnTrack
    //--------------------------------------
    struct TGetPosOnTrackOpt
    {
        float x_pad = 0.0f;
        union {
            float y_pad = 192.0f;
            float radius;
        };
        float z_pad = 128.0f;
        int ignore_path = -1;
    };

    struct TGetPosOnTrackRes
    {
        CTrackHandle hNode1{};
        CTrackHandle hNode2{};
        CTrackHandle hPath{};
        float angle = 0.0f;     // Angle between forward vector and axis of track
        Vector pos{ 0,0,0 };    // Local coordinates in track curvilinear coordinates
        float distance = 0.0f;  // Distance to path axis
        bool forward = false;   // Is facing forward relative to track
    };
    std::vector<TGetPosOnTrackRes> GetPositionOnTrack(const Vector& pos, const QAngle& ang = QAngle(0,0,0), const TGetPosOnTrackOpt& opts = TGetPosOnTrackOpt()); 

    //--------------------------------------
    // GetTrackPosition
    //--------------------------------------
    struct TGetTrackPosRes
    {
        Vector pos{ 0,0,0 };
        Vector ang{ 0,0,0 };
        CTrackHandle hNode{};
    };
    //TGetTrackPosRes GetTrackPos(int pathID, float x);

private:
    struct TTrain
    {
        CBaseHandle handle{ INVALID_EHANDLE_INDEX };
        edict_t* edEntity = nullptr;
        CBaseEntity* pEntity = nullptr;
        Vector pos{ 0.0f, 0.0f, 0.0f };
        Vector lposFrontBogey{ 0.0f, 0.0f, 0.0f };
        Vector lposRearBogey{ 0.0f, 0.0f, 0.0f };
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

    struct TNode
    {
        ~TNode();
        CTrackHandle id{};
        CTrackHandle hNext{};
        CTrackHandle hPrev{};
        float x = 0;
        float length = 0;
        Vector pos{ 0,0,0 };
        Vector dir{ 0,0,0 };
        Vector vec{ 0,0,0 };

        struct TJoin {
            CTrackHandle hNode;
            float x = 0;
        };

        static const int s_MaxBranches = 4;
        int branchesCount = 0;
        TJoin branches[s_MaxBranches]{};

        bool AddBranch(float brX, const CTrackHandle& node)
        {
            if (branchesCount == s_MaxBranches)
                return false;

            if (!node.IsValidNodeID())
                return false;

            auto& branch = branches[branchesCount++];
            branch = { node, brX };
            return true;
        }

        int iRef = -1; // TODO: Using CLuaObject?
    };

    struct TPath
    {
        ~TPath();
        CTrackHandle id{ CTrackHandle::INVALID_PATH_ID, CTrackHandle::ONLY_PATH_NODE_ID };
        float length = 0;
        std::vector<TNode> nodes;

        int iRef = -1; // TODO: Using CLuaObject?
    };

#pragma pack(push,1)
    struct TSpacialVector
    {
        union
        {
            int val = 0;
            struct {
                unsigned int x : 10;
                unsigned int y : 10;
                unsigned int z : 12;
            } coord;
        };
        TSpacialVector() = default;
        TSpacialVector(int x, int y, int z)
        {
            coord.x = unsigned int(x + 512) & 0x3FF;
            coord.y = unsigned int(y + 512) & 0x3FF;
            coord.z = unsigned int(z + 2048) & 0xFFF;
        }
        int x() { return int(coord.x) - 512; }
        int y() { return int(coord.y) - 512; }
        int z() { return int(coord.z) - 2048; }
    };
#pragma pack(pop)
    static inline TSpacialVector GetSpatialPos(const Vector& pos);

    void GetBogeys(TTrain& train);
    void UpdateTrains();

    void LoadTrack();
    void AddSpatial(const TNode& node);

    TGetTrackPosRes GetTrackPosition(const TPath& path, float x);

    std::array<TTrain, MAX_EDICTS> m_Trains;
    std::vector<TPath> m_Paths;

    //CUtlRBTree<TPath> mPathsList;

    std::unordered_map<int, std::vector<CTrackHandle>> m_SpatialLookup;

    bool m_Initialized = false;


//--------------------------------------
// Garry's Mod Lua
//--------------------------------------
public:
    int NearestNodes(GarrysMod::Lua::ILuaBase* LUA);
    int GetPositionOnTrack(GarrysMod::Lua::ILuaBase* LUA);
    int GetTrackPosition(GarrysMod::Lua::ILuaBase* LUA);
    int GetTrackEditorPaths(GarrysMod::Lua::ILuaBase* LUA);

    void PushPath(GarrysMod::Lua::ILuaBase* LUA);
    void PushNode(GarrysMod::Lua::ILuaBase* LUA);

    static void RegisterLuaUserData();
private:
    static int LUA_NearestNodes__call(lua_State* L);

    static int LUA_RNNode__index(lua_State* L);
    static int LUA_RNNode__eq(lua_State* L);
    static int LUA_RNNode__tostring(lua_State* L);

    static int LUA_RNPath__index(lua_State* L);
    static int LUA_RNPath__eq(lua_State* L);
    static int LUA_RNPath__len(lua_State* L);
    static int LUA_RNPath__tostring(lua_State* L);
};

extern CRailNetwork g_RailNetwork;