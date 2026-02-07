if not (SERVER and Metrostroi and RailNetwork) then return end

-- Save original functions
_RN = _RN or {}

function _RN.SaveMSFuncs()
    if _RN.MS then print("_RN.SaveMSFuncs(): Functions already saved") return end
    _RN.MS = {}

    _RN.MS.NearestNodes             = _RN.MS.NearestNodes           or Metrostroi.NearestNodes
    _RN.MS.GetPositionOnTrack       = _RN.MS.GetPositionOnTrack     or Metrostroi.GetPositionOnTrack
    _RN.MS.GetTrackPosition         = _RN.MS.GetTrackPosition       or Metrostroi.GetTrackPosition
    _RN.MS.UpdateSignalNames        = _RN.MS.UpdateSignalNames      or Metrostroi.UpdateSignalNames
    _RN.MS.UpdateSignalEntities     = _RN.MS.UpdateSignalEntities   or Metrostroi.UpdateSignalEntities
    _RN.MS.PostSignalInitialize     = _RN.MS.PostSignalInitialize   or Metrostroi.PostSignalInitialize
    _RN.MS.UpdateSwitchEntities     = _RN.MS.UpdateSwitchEntities   or Metrostroi.UpdateSwitchEntities
    _RN.MS.AddARSSubSection         = _RN.MS.AddARSSubSection       or Metrostroi.AddARSSubSection
    _RN.MS.UpdateARSSections        = _RN.MS.UpdateARSSections      or Metrostroi.UpdateARSSections
    _RN.MS.ScanTrack                = _RN.MS.ScanTrack              or Metrostroi.ScanTrack
    _RN.MS.GetSignalByName          = _RN.MS.GetSignalByName        or Metrostroi.GetSignalByName
    _RN.MS.GetSwitchByName          = _RN.MS.GetSwitchByName        or Metrostroi.GetSwitchByName
    _RN.MS.GetNextTrafficLight      = _RN.MS.GetNextTrafficLight    or Metrostroi.GetNextTrafficLight
    _RN.MS.GetARSJoint              = _RN.MS.GetARSJoint            or Metrostroi.GetARSJoint
    _RN.MS.GetTrackSwitches         = _RN.MS.GetTrackSwitches       or Metrostroi.GetTrackSwitches
    _RN.MS.IsTrackOccupied          = _RN.MS.IsTrackOccupied        or Metrostroi.IsTrackOccupied
    _RN.MS.PredictTrainPositions    = _RN.MS.PredictTrainPositions  or Metrostroi.PredictTrainPositions
    _RN.MS.UpdateTrainPositions     = _RN.MS.UpdateTrainPositions   or Metrostroi.UpdateTrainPositions
    _RN.MS.UpdateStations           = _RN.MS.UpdateStations         or Metrostroi.UpdateStations
    _RN.MS.GetTravelTime            = _RN.MS.GetTravelTime          or Metrostroi.GetTravelTime
    _RN.MS.Load                     = _RN.MS.Load                   or Metrostroi.Load
    _RN.MS.Save                     = _RN.MS.Save                   or Metrostroi.Save
    _RN.MS.PrintStatistics          = _RN.MS.PrintStatistics        or Metrostroi.PrintStatistics
end

function _RN.ReplaceFunctions(rn)
    Metrostroi.NearestNodes             = rn and RailNetwork.NearestNodes           or _RN.MS.NearestNodes
    Metrostroi.GetPositionOnTrack       = rn and RailNetwork.GetPositionOnTrack     or _RN.MS.GetPositionOnTrack
    Metrostroi.GetTrackPosition         = rn and RailNetwork.GetTrackPosition       or _RN.MS.GetTrackPosition
    -- Metrostroi.UpdateSignalNames        = rn and RailNetwork.UpdateSignalNames      or _RN.MS.UpdateSignalNames
    -- Metrostroi.UpdateSignalEntities     = rn and RailNetwork.UpdateSignalEntities   or _RN.MS.UpdateSignalEntities
    -- Metrostroi.PostSignalInitialize     = rn and RailNetwork.PostSignalInitialize   or _RN.MS.PostSignalInitialize
    -- Metrostroi.UpdateSwitchEntities     = rn and RailNetwork.UpdateSwitchEntities   or _RN.MS.UpdateSwitchEntities
    -- Metrostroi.AddARSSubSection         = rn and RailNetwork.AddARSSubSection       or _RN.MS.AddARSSubSection
    -- Metrostroi.UpdateARSSections        = rn and RailNetwork.UpdateARSSections      or _RN.MS.UpdateARSSections
    -- Metrostroi.ScanTrack                = rn and RailNetwork.ScanTrack              or _RN.MS.ScanTrack
    -- Metrostroi.GetSignalByName          = rn and RailNetwork.GetSignalByName        or _RN.MS.GetSignalByName
    -- Metrostroi.GetSwitchByName          = rn and RailNetwork.GetSwitchByName        or _RN.MS.GetSwitchByName
    -- Metrostroi.GetNextTrafficLight      = rn and RailNetwork.GetNextTrafficLight    or _RN.MS.GetNextTrafficLight
    -- Metrostroi.GetARSJoint              = rn and RailNetwork.GetARSJoint            or _RN.MS.GetARSJoint
    -- Metrostroi.GetTrackSwitches         = rn and RailNetwork.GetTrackSwitches       or _RN.MS.GetTrackSwitches
    -- Metrostroi.IsTrackOccupied          = rn and RailNetwork.IsTrackOccupied        or _RN.MS.IsTrackOccupied
    -- Metrostroi.PredictTrainPositions    = rn and RailNetwork.PredictTrainPositions  or _RN.MS.PredictTrainPositions
    -- Metrostroi.UpdateTrainPositions     = rn and RailNetwork.UpdateTrainPositions   or _RN.MS.UpdateTrainPositions
    -- Metrostroi.UpdateStations           = rn and RailNetwork.UpdateStations         or _RN.MS.UpdateStations // Need to replace?
    -- Metrostroi.GetTravelTime            = rn and RailNetwork.GetTravelTime          or _RN.MS.GetTravelTime  // ...
    Metrostroi.Load                     = rn and RailNetwork.Load                   or _RN.MS.Load           
    Metrostroi.Save                     = rn and RailNetwork.Save                   or _RN.MS.Save
    Metrostroi.PrintStatistics          = rn and RailNetwork.PrintStatistics        or _RN.MS.PrintStatistics
end



function RailNetwork.PredictTrainPositions()

end

function RailNetwork.UpdateTrainPositions()

end

function RailNetwork.Load(name,keep_signs)
    name = name or game.GetMap()

     -- Load tracks
    RailNetwork.Initialize()
    if RailNetwork.Paths then
        if Metrostroi.TrackEditor then
            Metrostroi.TrackEditor.Paths = RailNetwork.GetTrackEditorPaths()
        end
        Metrostroi.Paths = RailNetwork.Paths
    end

    -- Initialize stations list
    Metrostroi.UpdateStations()

    -- Ignore updates to prevent created/removed switches from constantly updating table of positions
    Metrostroi.IgnoreEntityUpdates = true
    Metrostroi.LoadSigns(name,keep_signs)
    Metrostroi.LoadAutoSigns(name,keep_signs)
    Metrostroi.LoadPAData(name)
    timer.Simple(0.05,function()
        -- No more ignoring updates
        Metrostroi.IgnoreEntityUpdates = false
        -- Load ARS entities
        Metrostroi.UpdateSignalEntities()
        -- Load switches
        Metrostroi.UpdateSwitchEntities()
        -- Add additional ARS sections
        Metrostroi.UpdateARSSections()
    end)

    -- Load schedules data
    Metrostroi.LoadSchedules(name)
    
    -- Initialize signs
    print("Metrostroi: Initializing signs...")
    Metrostroi.InitializeSigns()
end

function RailNetwork.Save()

end

hook.Add("PreGamemodeLoaded", "RN_GMLoad", function()
    MsgC(Color(255,0,255,255), "RailNetwork: Replace Metrostroi's railnetwork\n")
    hook.Remove("Initialize", "Metrostroi_MapInitialize")
    _RN.SaveMSFuncs()
    _RN.ReplaceFunctions(true)
end)

-- hook.Add("InitPostEntity", "RN_InitPE", function()
    -- Metrostroi.Load()
-- end)


--------------------------------------------------------------------------------
-- Copy of local Metrostroi functions
--------------------------------------------------------------------------------
local function getFile(path,name,id)
    local data,found
    if file.Exists(Format(path..".txt",name),"DATA") then
        print(Format("Metrostroi: Loading %s definition...",id))
        data= util.JSONToTable(file.Read(Format(path..".txt",name),"DATA"))
        found = true
    end
    if not data and file.Exists(Format(path..".lua",name),"LUA") then
        print(Format("Metrostroi: Loading default %s definition...",id))
        data= util.JSONToTable(file.Read(Format(path..".lua",name),"LUA"))
        found = true
    end
    if not found then
        print(Format("%s definition file not found: %s",id,Format(path,name)))
        return
    elseif not data then
        print(Format("Parse error in %s %s definition JSON",id,Format(path,name)))
        return
    end
    return data
end

function Metrostroi.LoadSigns(name,keep)
    if keep then return end
    print("Metrostroi: Loading signs, signals, switches...")
    local signs = getFile("metrostroi_data/signs_%s",name,"Signal")

    if not signs then print("Metrostroi: Loading canceled, no file found") return end

    local signals_ents = ents.FindByClass("gmod_track_signal")
    for k,v in pairs(signals_ents) do SafeRemoveEntity(v) end
    local switch_ents = ents.FindByClass("gmod_track_switch")
    for k,v in pairs(switch_ents) do SafeRemoveEntity(v) end
    local signs_ents = ents.FindByClass("gmod_track_signs")
    for k,v in pairs(signs_ents) do SafeRemoveEntity(v) end

    --Some compatibility checks
    local version
    version = signs.Version
    if not version then
        print("Metrostroi: This signs file is incompatible with signs version")
        signs = nil
    else
        signs.Version = nil
    end
    local TwoToSix = false
    if version ~= 1.2 then
        print(Format("Metrostroi: !!Converting from version %.1f!! signals converted to %s.",version,TwoToSix and "2/6" or "1/5"))
        if game.GetMap():find("gm_mus_loop") then
            TwoToSix = true
        end
    end

    -- Create new entities (add a delay so the old entities clean up)
    for k,v in pairs(signs) do
        local ent = ents.Create(v.Class)
        if IsValid(ent) then
            ent:SetPos(v.Pos)
            ent:SetAngles(v.Angles)
            if v.Class == "gmod_track_switch" then
                ---CHANGE
                ent:SetChannel(v.Channel or 1)
                ent.LockedSignal = v.LockedSignal
                ent.NotChangePos = v.NotChangePos
                ent.Invertred = v.Invertred
                ent.Name = v.Name,
                ent:Spawn()
            end
            if v.Class == "gmod_track_signal" and v.Routes then
                ent.SignalType = v.SignalType
                ent.Name = v.Name
                ent.RouteNumberSetup = v.RouteNumberSetup
                ent.LensesStr = v.LensesStr
                ent.Lenses = string.Explode("-",v.LensesStr)
                ent.RouteNumber = v.RouteNumber
                ent.IsolateSwitches = v.IsolateSwitches
                ent.Routes = v.Routes
                ent.ARSOnly = v.ARSOnly
                ent.Left = v.Left
                ent.Double = v.Double
                ent.DoubleL = v.DoubleL
                ent.Approve0 = v.Approve0
                ent.TwoToSix = v.TwoToSix
                ent.NonAutoStop = v.NonAutoStop
                ent.PassOcc = v.PassOcc
                ent.Lenses = string.Explode("-",ent.LensesStr)
                ent.InS = nil
                for i = 1,#ent.Lenses do
                    if ent.Lenses[i]:find("W") then
                        ent.InS = i
                    end
                end
                if version == 1 and ent.Left then
                    print(Format("Metrostroi: !!Converting from version %.1f!! signal %s rotated.",version,ent.Name))
                    ent:SetAngles(ent:LocalToWorldAngles(ent:WorldToLocalAngles(ent:GetAngles())+Angle(0,180,0)))
                end
                if version ~= 1.2 then ent.TwoToSix = TwoToSix end
                ent:Spawn()
            elseif v.Class == "gmod_track_signs" then
                ent.SignType = v.SignType
                ent.YOffset = v.YOffset
                ent.ZOffset = v.ZOffset
                ent.Left = v.Left,
                ent:Spawn()
                ent:SendUpdate()
            elseif v.Class == "gmod_track_signal" then ent:Remove() end
        end
    end
end

function Metrostroi.LoadAutoSigns(name,keep)
if keep then return end
    local auto = getFile("metrostroi_data/auto_%s",name,"Autodrive")

    if not auto then return end
    local auto_ents = ents.FindByClass("gmod_track_autodrive_plate")
    for _,v in pairs(auto_ents) do SafeRemoveEntity(v) end
    Metrostroi.HaveSBPP = false
    Metrostroi.HaveAuto = false
    for k,v in pairs(auto) do
        local ent = ents.Create("gmod_track_autodrive_plate")
        if IsValid(ent) and v.Model then
            ent:SetPos(v.Pos)
            ent:SetAngles(v.Angles)
            ent.PlateType = v.Type
            ent.Right = v.Right
            ent.Mode = v.Mode
            ent.Model = v.Model
            ent.StationID = v.StationID
            ent.StationPath = v.StationPath
            ent.UPPS = v.UPPS
            ent.DistanceToOPV = v.DistanceToOPV

            ent.SBPPType = v.SBPPType
            ent.IsDeadlock = v.IsDeadlock
            ent.DriveMode = v.DriveMode
            ent.RightDoors = v.RightDoors
            ent.WTime = v.WTime
            ent.RKPos = v.RKPos

            ent:SetModel(ent.Model)
            ent:Spawn()
            --[[ if ent.PlateType <= 2 then
                Metrostroi.HaveAuto = true
            end--]]
            if ent.SBPPType==3 and not ent.BrakeProps then
                ent.BrakeProps = {}
                for i=-1,1,2 do
                    local entL = ents.Create("gmod_track_autodrive_plate")
                    entL.Model = "models/metrostroi/signals/autodrive/rfid.mdl"
                    entL:SetPos(v.Pos + (v.Angles:Right()*(-1.5*i)/0.01905))
                    entL:SetModel(v.Model)
                    entL:SetAngles(v.Angles)
                    entL:Spawn()
                    entL.Linked = ent
                    entL.SBPPType = ent.SBPPType
                    entL.PlateType = METROSTROI_SBPPSENSOR
                    table.insert(ent.BrakeProps,entL)
                end
            end
        end
    end
end

function Metrostroi.LoadPAData(name)
    local pa = getFile("metrostroi_data/pa_%s",name,"PAData")

    if not pa then return end
    local pa_ents = ents.FindByClass("gmod_track_pa_marker")
    for _,v in pairs(pa_ents) do SafeRemoveEntity(v) end
    Metrostroi.PAMConfTest = pa
    if pa.markers then
        for k,v in pairs(pa.markers) do
            if not v.TrackPath or not v.TrackX then continue end
            local ent = ents.Create("gmod_track_pa_marker")
            if IsValid(ent) then
                ent:SetPos(v.Pos)
                ent:SetAngles(v.Angles)
                if Metrostroi.Paths[v.TrackPath] then
                    ent:SetTrackPosition(Metrostroi.Paths[v.TrackPath],v.TrackX)
                end
                ent.TrackPath = v.TrackPath
                ent.TrackX = v.TrackX
                ent.PAType = v.PAType
                if ent.PAType == 1 then
                    ent.PAStationPath = tonumber(v.PAStationPath)
                    ent.PAStationID = tonumber(v.PAStationID)
                    ent.PAStationName = v.PAStationName
                    ent.PALastStation = v.PALastStation
                    ent.PAStationRightDoors = v.PAStationRightDoors
                    ent.PAStationHorlift = v.PAStationHorlift
                    ent.PAStationHasSwtiches = v.PAStationHasSwtiches
                    ent.PAStationCorrection = tonumber(v.PAStationCorrection)
                    if ent.PALastStation then
                        ent.PALastStationName = v.PALastStationName
                        ent.PAWrongPath = v.PAWrongPath
                        ent.PADeadlockStart = tonumber(v.PADeadlockStart)
                        ent.PADeadlockEnd = tonumber(v.PADeadlockEnd)
                        ent.PALineChange = v.PALineChange
                        if ent.PALineChange then
                            ent.PALineChangeStationPath = tonumber(v.PALineChangeStationPath)
                            ent.PALineChangeStationID = tonumber(v.PALineChangeStationID)
                        end
                    end
                end
                ent:Spawn()
            end
        end
    end
    Metrostroi.PARebuildStations()
end

function Metrostroi.LoadSchedules(name)
    local sched_data = getFile("metrostroi_data/sched_%s",name,"schedules")
    if sched_data then
        Metrostroi.LoadSchedulesData(sched_data)
    else
        print("Metrostroi: Could not load schedules configuration!")
    end
end