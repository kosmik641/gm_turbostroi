if SERVER and Turbostroi then
--------------------------------------------------------------------------------
-- Serverside script
--------------------------------------------------------------------------------
Turbostroi.SpawnedTrains = {}

local tsTriggerInput = Turbostroi.TriggerInput
local tsReadAvailable = Turbostroi.ReadAvailable
local tsRecvMessage = Turbostroi.RecvMessage
local tsSendMessage = Turbostroi.SendMessage

--------------------------------------------------------------------------------
-- Preccess incoming thread messages
--------------------------------------------------------------------------------
local processMsgTbl = {
    -----------------------------------
    -- OutputsList values
    -----------------------------------
    [1] = function(train,system,name,index,value)
        if train.Systems[system] then
            train.Systems[system][name] = value
            train:TriggerTurbostroiInput(system,name,value)
        end
    end,

    -----------------------------------
    -- WriteTrainWires values
    -----------------------------------
    [2] = function(train,system,name,index,value)
        if not train.TrainWireWritersID[index] then train.TrainWireWritersID[index] = true end
        train.TrainWireTurbostroi[index] = value
        train:TriggerTurbostroiInput("TrainWire",index,value)
    end,

    -----------------------------------
    -- TriggerInput for non accelereted system
    -----------------------------------
    [3] = function(train,system,name,index,value)
        if train.Systems[system] then
            train.Systems[system]:TriggerInput(name,value)
        end
    end,

    -----------------------------------
    -- ENT:PlayOnce()
    -----------------------------------
    [4] = function(train,system,name,index,value)
        train:PlayOnce(system,name,index,value)
    end,

    -----------------------------------
    -- print() data from lua_runstring in Turbostroi environment
    -----------------------------------
    [5] = function(train,system,name,index,value)
        local ply = Player(index)
        if not IsValid(ply) then return end
        ply:PrintMessage(HUD_PRINTCONSOLE, "metrostroi_turbostroi_run:" )
        ply:PrintMessage(HUD_PRINTCONSOLE, system )
    end,
}

--------------------------------------------------------------------------------
-- Read from thread data
--------------------------------------------------------------------------------
local function tsReadData(train, ud)
    local msg_count = tsReadAvailable(ud)

    for i=1,msg_count do
        local id,system,name,index,value = tsRecvMessage(ud)

        processMsgTbl[id](train,system,name,index,value)
    end
end

--------------------------------------------------------------------------------
-- Write to thread data
--------------------------------------------------------------------------------
local function tsWriteData(train, ud)
    -- Send wires
    for i,v in pairs(train.TrainWires) do
        if train._WireOut[i] ~= v then
            if tsSendMessage(ud, 2, "", "", i, v) then
                train._WireOut[i] = v
            -- else
            --     print("Fail to send Wire item: "..i)
            end
        end
    end

    -- Send outputs
    for sys_name,sys in pairs(train.Systems) do
        if sys.OutputsList and sys.DontAccelerateSimulation then
            for _,name in ipairs(sys.OutputsList) do
                if train._DataOut[sys_name][name] ~= sys[name] then
                    local value = (sys[name]==true) and 1 or (sys[name]==false) and 0 or tonumber(sys[name]) or 0
                    if tsSendMessage(ud, 1, sys_name, name, 0, value) then
                        train._DataOut[sys_name][name] = sys[name]
                    -- else
                    --     print("Fail to send OutputsList item: "..sys_name..name)
                    end
                end
            end
        end
    end
end

--------------------------------------------------------------------------------
-- Hook from Turbostroi.InitializeTrain
--------------------------------------------------------------------------------
local function tsWagonCreate(ent)
    if ent.DontAccelerateSimulation then return end

    ent._DataOut = {}
    ent._WireOut = {}
    
    for sys_name,sys in pairs(ent.Systems) do
        -- Remove empty list
        if sys.OutputsList and #sys.OutputsList == 0 then sys.OutputsList = nil end 

        -- Initialize data cache
        if sys.OutputsList then
            ent._DataOut[sys_name] = {}
            for i,k in ipairs(sys.OutputsList) do
                ent._DataOut[sys_name][k] = 0
            end
        end
    end

    -- Add data exchange funcs
    local ud
    local oldThink = ent.Think
    function ent.Think(self)
        ud = self._CWagon
        tsReadData(self,ud)
        oldThink(self)
        tsWriteData(self,ud)
        return true
    end

    table.insert(Turbostroi.SpawnedTrains, ent)
end
hook.Add("TurbostroiWagonCreate", "Turbostroi_Create", tsWagonCreate)

--------------------------------------------------------------------------------
-- Hook from Turbostroi.DeinitializeTrain
--------------------------------------------------------------------------------
local function tsWagonRemove(ent)
    for i,t in ipairs(Turbostroi.SpawnedTrains) do
        if ent == t then
            table.remove(Turbostroi.SpawnedTrains, i)
            break
        end
    end
end
hook.Add("TurbostroiWagonRemove", "Turbostroi_Remove", tsWagonRemove)

--------------------------------------------------------------------------------
-- Replace sv_turbostroi_v2.lua things
--------------------------------------------------------------------------------
hook.Add("MetrostroiLoaded", "Turbostroi_Loaded", function()
    Turbostroi.TriggerInput = tsTriggerInput -- Replace replaced...

    hook.Remove("Think", "Turbostroi_Think")
    hook.Remove("OnEntityCreated", "Turbostroi")
    hook.Remove("EntityRemoved", "Turbostroi")
    concommand.Remove("metrostroi_turbostroi_run")

    concommand.Add("metrostroi_turbostroi_run",function(ply,cmd,args,argStr)
        if IsValid(ply) and not ply:IsSuperAdmin() then return end

        local train
        local entIdx = tonumber(args[1])
        if entIdx then
            train = Entity(entIdx)
            argStr = string.Replace(argStr,args[1].." ","")
        elseif IsValid(ply) and #argStr > 0 then
            train = ply:GetTrain()
        end

        if not IsValid(train) or train.Base ~= "gmod_subway_base" then
            if entIdx then
                if IsValid(ply) then
                    ply:PrintMessage(HUD_PRINTCONSOLE, "Train ["..entIdx.."] not found")
                else
                    MsgC(Color(255,0,0), "Turbostroi: Train ["..entIdx.."] not found\n")
                end
            else
                if IsValid(ply) then
                    ply:PrintMessage(HUD_PRINTCONSOLE, "Command usage:")
                    ply:PrintMessage(HUD_PRINTCONSOLE, "\tmetrostroi_turbostroi_run [Code]")
                    ply:PrintMessage(HUD_PRINTCONSOLE, "\tmetrostroi_turbostroi_run [Entity index] [Code]")
                else
                    print("Command usage: metrostroi_turbostroi_run [Entity index] [Code]")
                end
            end
            return
        end

        tsSendMessage(train._CWagon, 5, argStr:sub(1,255), argStr:sub(256,511), IsValid(ply) and ply:UserID() or -1, train:EntIndex())
    end, nil, "Run lua string in turbostroi train thread.")
end)

return end -- SERVER and Turbostroi


if not TURBOSTROI then return end
--------------------------------------------------------------------------------
-- Turbostroi side script
--------------------------------------------------------------------------------
Turbostroi = {}
Turbostroi.Schedule = {}
Turbostroi.ScheduleIter = 0

-- Train data
TRAIN = {}
TRAIN.Systems = {}
TRAIN._WiresR = {}
TRAIN._WiresW = {}

-- Data values buffer for send only when updated
TRAIN._DataOut = {}
TRAIN._WireOut = {}

-- Metrostroi
Metrostroi = {}
Metrostroi.BaseSystems = {}
Metrostroi.CtorSystems = {}

--------------------------------------------------------------------------------
-- Load FFI
--------------------------------------------------------------------------------
local OSes = {
    Windows     = {x86 = "win32",x64 = "win64"},
    Linux       = {x86 = "linux",x64 = "linux64"},
    BSD         = {x86 = "linux",x64 = "linux64"},
    POSIX       = {x86 = "linux",x64 = "linux64"},
    OSX         = {x86 = "osx",  x64 = "osx"},
    Other       = {x86 = "linux",x64 = "linux64"}
}

local postfix = OSes[jit.os] and OSes[jit.os][jit.arch]
if postfix == nil then
    print("Can't find gm_turbostroi DLL")
    return
end

local ffi = require("ffi")
ffi.cdef[[
typedef struct {
    int id;
    const char* system;
    const char* name;
    double index;
    double value;
} TThreadMsg;
int ThreadReadAvailable(void* p);
TThreadMsg& ThreadRecvMessage(void* p);
bool ThreadSendMessage(void *p, int message, const char* system_name, const char* name, double index, double value);
]]

local TS = ffi.load("./garrysmod/lua/bin/gmsv_turbostroi_"..postfix..".dll")
print("[!] Train initialized!")

local tsReadAvailable = TS.ThreadReadAvailable
local tsRecvMessage = TS.ThreadRecvMessage
local tsSendMessage = TS.ThreadSendMessage

local ud = _CWagon
--------------------------------------------------------------------------------
-- DLL functions
--------------------------------------------------------------------------------
LoadSystems = {}
function Initialize()
    print("[!] Loading systems")
    local time = SysTime()

    -- Load train systems
    for i,v in ipairs(LoadSystems) do
        TRAIN:LoadSystem(v[1],v[2])
    end

    -- Build schedule
    local max_iter = Turbostroi.ScheduleIter
    for iteration=1,max_iter do
        local tbl = {}

        for sys_name,sys in pairs(TRAIN.Systems) do
            local sys_iter = sys.SubIterations or 1
            if ((iteration)%(max_iter/sys_iter)) == 0 then
                table.insert(tbl, sys)
            end
        end

        table.insert(Turbostroi.Schedule, tbl)
    end

    Turbostroi.Initialized = true
    print(string.format("[!] -Took %.2fms",(SysTime()-time)*1000))
end


local tsReadData
local tsWriteData
local tsRunString
function Think(dT)
    tsReadData()
    
    -- Run train systems
    for iter,sch in ipairs(Turbostroi.Schedule) do
        for _,sys in ipairs(sch) do
            sys:Think(dT/(sys.SubIterations or 1), iter)
        end
    end

    tsWriteData()
end

--------------------------------------------------------------------------------
-- Preccess incoming engine messages
--------------------------------------------------------------------------------
local processMsgTbl = {
    -----------------------------------
    -- OutputsList values
    -----------------------------------
    [1] = function(train,system,name,index,value)
        if train.Systems[system] then
            train.Systems[system][name] = value
        -- else
        --     print("[TThreadMsg: 1] No system defined: "..system)
        end
    end,

    -----------------------------------
    -- ReadTrainWires values
    -----------------------------------
    [2] = function(train,system,name,index,value)
        train._WiresR[index] = value
    end,

    -----------------------------------
    -- TriggerInput for accelereted system
    -----------------------------------
    [3] = function(train,system,name,index,value)
        if train.Systems[system] then
            train.Systems[system]:TriggerInput(name,value)
        -- else
        --     print("[TThreadMsg: 3] No system defined: "..system)
        end
    end,

    -----------------------------------
    -- Not used
    -----------------------------------
    [4] = function(train,system,name,index,value) end,

    -----------------------------------
    -- lua_runstring in Turbostroi environment
    -----------------------------------
    [5] = function(train,system,name,index,value)
        tsRunString(system..name, index, value)
    end,
}

--------------------------------------------------------------------------------
-- Read from engine data
--------------------------------------------------------------------------------
local train = TRAIN
function tsReadData()
    local msg_count = tsReadAvailable(ud)

    for i=1,msg_count do
        local msg = tsRecvMessage(ud)
        local id = msg.id
        local system = ffi.string(msg.system)
        local name = ffi.string(msg.name)
        local index = msg.index
        local value = msg.value

        processMsgTbl[id](train,system,name,index,value)
    end
end

--------------------------------------------------------------------------------
-- Write to engine data
--------------------------------------------------------------------------------
function tsWriteData()
    -- Send wires
    for i,v in pairs(train._WiresW) do
        if train._WireOut[i] ~= v then
            if tsSendMessage(ud, 2, "", "", i, v) then
                train._WireOut[i] = v
            -- else
            --     print("Fail to send Wire item: "..i)
            end
        end
    end

    -- Send outputs
    for sys_name,sys in pairs(train.Systems) do
        if sys.OutputsList and (not sys.DontAccelerateSimulation) then
            for _,name in ipairs(sys.OutputsList) do
                if train._DataOut[sys_name][name] ~= sys[name] then
                    local value = (sys[name]==true) and 1 or (sys[name]==false) and 0 or tonumber(sys[name]) or 0
                    if tsSendMessage(ud, 1, sys_name, name, 0, value) then
                        train._DataOut[sys_name][name] = sys[name]
                    -- else
                    --     print("Fail to send OutputsList item: "..sys_name..name)
                    end
                end
            end
        end
    end
end

--------------------------------------------------------------------------------
-- lua_runstring in Turbostroi environment
--------------------------------------------------------------------------------
function tsRunString(str, userid, tid)
    local scr = [[
    local _retdata=""
    local print = function(...)
        for k,v in ipairs({...}) do _retdata = _retdata..tostring(v).."\t" end
        _retdata = _retdata.."\n"
    end
    ]]
    scr = scr..str.."\n"
    scr = scr.."return _retdata"
    local data,err = loadstring(scr)
    if data then
        local ret = tostring(data()) or "N\\A"
        if userid == -1 then print(ret) return end
        tsSendMessage(ud, 5, ret, "", userid, tid)
    else
        if userid == -1 then print(err) return end
        tsSendMessage(ud, 5, tostring(err), "", userid, tid)
    end
end

--------------------------------------------------------------------------------
-- Train functions
--------------------------------------------------------------------------------
function TRAIN:ReadTrainWire(n)
    return self._WiresR[n] or 0
end

function TRAIN:WriteTrainWire(n,v)
    self._WiresW[n] = v
end

function TRAIN:PlayOnce(soundid,location,range,pitch)
    tsSendMessage(ud, 4, soundid or "", location or "", range or 0.8, pitch or 1)
end

function TRAIN:LoadSystem(sys_name,name,...)
    name = name or sys_name

    if not Metrostroi.CtorSystems[name] then print("Error: No system defined: "..name) return end
    if self[sys_name] then print("Error: System already defined: "..sys_name) end
    local sys = Metrostroi.CtorSystems[name](self, ...)
    sys.Name = sys_name
    sys.BaseName = name
    
    if sys.DontAccelerateSimulation then
        sys.Think = function() end -- Do nothing
        sys.TriggerInput = function(train,name,value)
            tsSendMessage(ud, 3, sys_name, name, 0,
            (value==true) and 1 or (value==false) and 0 or tonumber(value) or 0)
        end
    else
        if sys.OutputsList then
            self._DataOut[sys_name] = {}
            for i,k in ipairs(sys.OutputsList) do
                self._DataOut[sys_name][k] = 0
            end
        end
    end

    -- Keep max iterations
    Turbostroi.ScheduleIter = math.max(Turbostroi.ScheduleIter, sys.SubIterations or 1)

    -- Save to train
    self[sys_name] = sys
    self.Systems[sys_name] = sys
end

--------------------------------------------------------------------------------
-- Metrostroi functions
--------------------------------------------------------------------------------
function Metrostroi.DefineSystem(name)
    if not name then return end
    TRAIN_SYSTEM = {}
    Metrostroi.BaseSystems[name] = TRAIN_SYSTEM
    Metrostroi.CtorSystems[name] = function(train, ...)
        local base_sys = Metrostroi.BaseSystems[name]
        if not base_sys then print("No system: "..name) return end

        local sys = { _base = name, Train = train }

        -- Copy data and functions from base system
        for k,v in pairs(base_sys) do sys[k]=v end
        sys.Initialize = sys.Initialize or function() end
        sys.Think = sys.Think or function() end
        sys.TriggerInput = sys.TriggerInput or function() end
        sys.TriggerOutput = sys.TriggerOutput or function() end
        
        -- Get outputs
        if sys.Outputs then sys.OutputsList = sys:Outputs() end
        if sys.OutputsList and #sys.OutputsList == 0 then sys.OutputsList = nil end

        -- Initialize system
        sys:Initialize(...)
        return sys
    end
end

LIB_TURBOSTROI_VERSION = "v2.6.1"
