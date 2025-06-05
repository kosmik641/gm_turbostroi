--------------------------------------------------------------------------------
-- Turbostroi scripts
--------------------------------------------------------------------------------
-- NEW API
local OSes = {
    Windows = {
        x86 = "win32",
        x64 = "win64"
    },
    Linux = {
        x86 = "linux",
        x64 = "linux64"
    },
    BSD = {
        x86 = "linux",
        x64 = "linux64"
    },
    POSIX = {
        x86 = "linux",
        x64 = "linux64"
    },
    OSX = {
        x86 = "osx",
        x64 = "osx"
    },
    Other = {
        x86 = "linux",
        x64 = "linux64"
    }
}

local postfix
if OSes[jit.os] then
    postfix = OSes[jit.os][jit.arch]
end

if postfix == nil then
    print("Can't find gm_turbostroi DLL")
    return
end

local dllPath = "./garrysmod/lua/bin/gmsv_turbostroi_"..postfix..".dll"

local ffi = require("ffi")
ffi.cdef[[
bool ThreadSendMessage(void *p, int message, const char* system_name, const char* name, double index, double value);
int ThreadReadAvailable(void* p);
typedef struct {
    int message;
    char system_name[64];
    char name[64];
    double index;
    double value;
} thread_msg;
thread_msg ThreadRecvMessage(void* p);
]]

local TS = ffi.load(dllPath)

Metrostroi = {}
local dataCache = {wires = {},wiresW = {},wiresL = {}}
Metrostroi.BaseSystems = {} -- Systems that can be loaded
Metrostroi.Systems = {} -- Constructors for systems

LoadSystems = {} -- Systems that must be loaded/initialized
GlobalTrain = {} -- Train emulator
GlobalTrain.Systems = {} -- Train systems
GlobalTrain.TrainWires = {}
GlobalTrain.WriteTrainWires = {}

TimeMinus = 0
_Time = 0
function CurTime()
    --return CurrentTime-TimeMinus
    return _Time
end
--function CurTime() return os.clock() end

function Metrostroi.DefineSystem(name)
    TRAIN_SYSTEM = {}
    Metrostroi.BaseSystems[name] = TRAIN_SYSTEM

    -- Create constructor
    Metrostroi.Systems[name] = function(train,...)
        local tbl = { _base = name }
        local TRAIN_SYSTEM = Metrostroi.BaseSystems[tbl._base]
        if not TRAIN_SYSTEM then print("No system: "..tbl._base) return end
        for k,v in pairs(TRAIN_SYSTEM) do
            if type(v) == "function" then
                tbl[k] = function(...)
                    if not Metrostroi.BaseSystems[tbl._base][k] then
                        print("ERROR",k,tbl._base)
                    end
                    return Metrostroi.BaseSystems[tbl._base][k](...)
                end
            else
                tbl[k] = v
            end
        end

        tbl.Initialize = tbl.Initialize or function() end
        tbl.Think = tbl.Think or function() end
        tbl.Inputs = tbl.Inputs or function() return {} end
        tbl.Outputs = tbl.Outputs or function() return {} end
        tbl.TriggerInput = tbl.TriggerInput or function() end
        tbl.TriggerOutput = tbl.TriggerOutput or function() end

        tbl.Train = train
        tbl:Initialize(...)
        tbl.OutputsList = tbl:Outputs()
        tbl.InputsList = tbl:Inputs()
        tbl.IsInput = {}
        for k,v in pairs(tbl.InputsList) do tbl.IsInput[v] = true end
        return tbl
    end
end

function GlobalTrain.LoadSystem(self,a,b,...)
    local name
    local sys_name
    if b then
        name = b
        sys_name = a
    else
        name = a
        sys_name = a
    end

    if not Metrostroi.Systems[name] then print("Error: No system defined: "..name) return end
    if self.Systems[sys_name] then print("Error: System already defined: "..sys_name)  return end

    self[sys_name] = Metrostroi.Systems[name](self,...)
    self[sys_name].Name = sys_name
    self[sys_name].BaseName = name
    self.Systems[sys_name] = self[sys_name]

    -- Don't simulate on here
    local no_acceleration = Metrostroi.BaseSystems[name].DontAccelerateSimulation
    if no_acceleration then
        self.Systems[sys_name].Think = function() end
        self.Systems[sys_name].TriggerInput = function(train,name,value)
            local v = value or 0
            if type(value) == "boolean" then v = value and 1 or 0 end
            TS.ThreadSendMessage(_userdata, 4,sys_name,name,0,v)
        end -- replace with new api

    --Precache values
    elseif self[sys_name].OutputsList then
        dataCache[sys_name] = {}
        for _,name in pairs(self[sys_name].OutputsList) do
            dataCache[sys_name][name] = 0--self[sys_name][name] or 0
        end
    end
end

function GlobalTrain.PlayOnce(self,soundid,location,range,pitch)
    TS.ThreadSendMessage(_userdata, 2,soundid or "",location or "",range or 0,pitch or 0) -- replace with new api
end

function GlobalTrain.ReadTrainWire(self,n)
    return self.TrainWires[n] or 0
end

function GlobalTrain.WriteTrainWire(self,n,v)
    self.WriteTrainWires[n] = v
end


GlobalTrain.DeltaTime = 0.33

--------------------------------------------------------------------------------
-- Main train code (turbostroi side)
--------------------------------------------------------------------------------
print("[!] Train initialized!")
function Think(skipped)
    -- This is just blatant copy paste from init.lua of base train entity
    local self = GlobalTrain

    -- Is initialized?
    if not self.Initialized then
        Initialize()
        return
    end

    self.DeltaTime = (CurrentTime - self.PrevTime)--self.DeltaTime+math.min(0.02,((CurrentTime - self.PrevTime)-self.DeltaTime)*0.1)
    self.PrevTime = CurrentTime
    if skipped or self.DeltaTime<=0 then return end
    _Time = _Time+self.DeltaTime

    -- Perform data exchange
    DataExchange()

    -- Simulate according to schedule
    for i,s in ipairs(self.Schedule) do
        for k,v in ipairs(s) do
            v:Think(self.DeltaTime / (v.SubIterations or 1),i)
        end
    end
end

function Initialize()
    if not CurrentTime then return end
    print("[!] Loading systems")
    local time = os.clock()
    for k,v in ipairs(LoadSystems) do
        GlobalTrain:LoadSystem(v[1],v[2])
    end
    print(string.format("[!] -Took %.2fs",os.clock()-time))
    GlobalTrain.PrevTime = CurrentTime
    local iterationsCount = 1
    if (not GlobalTrain.Schedule) or (iterationsCount ~= GlobalTrain.Schedule.IterationsCount) then
        GlobalTrain.Schedule = { IterationsCount = iterationsCount }
        local SystemIterations = {}

        -- Find max number of iterations
        local maxIterations = 0
        for k,v in pairs(GlobalTrain.Systems) do
            SystemIterations[k] = (v.SubIterations or 1)
            maxIterations = math.max(maxIterations,(v.SubIterations or 1))
        end

        -- Create a schedule of simulation
        for iteration=1,maxIterations do
            GlobalTrain.Schedule[iteration] = {}
            -- Populate schedule
            for k,v in pairs(GlobalTrain.Systems) do
                if ((iteration)%(maxIterations/(v.SubIterations or 1))) == 0 then
                    table.insert(GlobalTrain.Schedule[iteration],v)
                end

            end
        end
    end
    GlobalTrain.Initialized = true
end
local msg_data
local msg_count = 0
local id,system,name,index,value
function DataExchange()
    -- Get data packets
    msg_count = TS.ThreadReadAvailable(_userdata)
    for i = 1, msg_count do
        msg_data = TS.ThreadRecvMessage(_userdata)
        if msg_data.message == 1 then
            local system_name = ffi.string(msg_data.system_name)
            if GlobalTrain.Systems[system_name] then
                GlobalTrain.Systems[system_name][ffi.string(msg_data.name)] = msg_data.value
            end
        end
        if msg_data.message == 3 then
            dataCache["wiresW"][msg_data.index] = msg_data.value
        end
        if msg_data.message == 4 then
            local system_name = ffi.string(msg_data.system_name)
            if GlobalTrain.Systems[system_name] then
                GlobalTrain.Systems[system_name]:TriggerInput(ffi.string(msg_data.name),msg_data.value)
            end
        end
        if msg_data.message == 5 then
            dataCache["wiresL"] = {}
        end
        if msg_data.message == 6 then
            local scr = [[
            local _retdata=""
            local print = function(...)
                for k,v in ipairs({...}) do _retdata = _retdata..tostring(v).."\t" end
                _retdata = _retdata.."\n"
            end
            ]]
            scr = scr..ffi.string(msg_data.system_name)..ffi.string(msg_data.name).."\n"
            scr = scr.."return _retdata"
            local data,err = loadstring(scr)
            if data then
                local ret = tostring(data()) or "N\\A"
                for i=0,math.ceil(#ret/63) do
                    TS.ThreadSendMessage(_userdata, 6, ret:sub(i*63,(i+1)*63-1), "",msg_data.index,i)
                end
            else
                print(err)
                TS.ThreadSendMessage(_userdata, 6, tostring(err), "",msg_data.index,0)
            end
            --Turbostroi.SendMessage(train,6,cmd:sub(1,255),cmd:sub(256,511),ply:UserID(),0)
        end
    end
    for twid,value in pairs(dataCache["wiresW"]) do
        GlobalTrain.TrainWires[twid] = value
    end

    -- Output all variable values
    for sys_name,system in pairs(GlobalTrain.Systems) do
        if system.OutputsList and (not system.DontAccelerateSimulation) then
            for _,name in pairs(system.OutputsList) do
                local value = (system[name] or 0)
                --if type(value) == "boolean" then value = value and 1 or 0 end
                if not dataCache[sys_name] then print(sys_name) end
                if dataCache[sys_name][name] ~= value then
                    --print(sys_name,name,value)
                    --if SendMessage(1,sys_name,name,0,tonumber(value) or 0) then -- OLD API
                    if TS.ThreadSendMessage(_userdata, 1, sys_name , name, 0, tonumber(value) or 0) then -- NEW API
                        dataCache[sys_name][name] = value
                    end
                end
            end
        end
    end

    -- Output train wire writes
    for twID,value in pairs(GlobalTrain.WriteTrainWires) do
        --local value = tonumber(value) or 0
        if dataCache["wires"][twID] ~= value then
            dataCache["wires"][twID] = value
            dataCache["wiresL"][twID] = false
        end
        if not dataCache["wiresL"][twID] or dataCache["wiresL"][twID]~=GlobalTrain.PrevTime then
            --SendMessage(3,"","on",tonumber(twID) or 0,dataCache["wires"][twID]) -- OLD API
            TS.ThreadSendMessage(_userdata, 3, "", "on", tonumber(twID) or 0, dataCache["wires"][twID]) -- NEW API
            --print("[!]Wire "..twID.." starts update! Value "..dataCache["wires"][twID])
        end
        GlobalTrain.WriteTrainWires[twID] = nil
        dataCache["wiresL"][twID] = CurTime()
    end
    for twID,time in pairs(dataCache["wiresL"]) do
        if time~=CurTime() then
            TS.ThreadSendMessage(_userdata,3, "", "off", tonumber(twID) or 0, 0)
            --print("[!]Wire "..twID.." stops update!")
            dataCache["wiresL"][twID] = nil
        end
    end
    --SendMessage(5,"","",0,0) -- OLD API
    --C.ThreadSendMessage(_userdata, 5,"","",0,0) -- NEW API
    --print(string.format("%s %s",count,#msgCache))
    --count = 0

end
