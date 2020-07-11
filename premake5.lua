local workspace_name = "turbostroi"
local workspace_add_debug = false
local project_serverside = true

newoption({
    trigger = "gmcommon",
    description = "Sets the path to the garrysmod_common (https://github.com/danielga/garrysmod_common) directory",
    value = "path to garrysmod_common directory"
})

local gmcommon = assert(_OPTIONS.gmcommon or os.getenv("GARRYSMOD_COMMON"),
    "you didn't provide a path to your garrysmod_common (https://github.com/danielga/garrysmod_common) directory")
include(gmcommon .. "/generator.v2.lua")

CreateWorkspace({
    name = workspace_name,
    --allow_debug = workspace_add_debug, -- optional
    --path = workspace_path -- optional
})

CreateProject({
    serverside = project_serverside,
    --manual_files = project_manual_files, -- optional
    --source_path = project_source_path -- optional
})

files { "source/gmsv_turbostroi_win32.rc",
"external/metamod-source/core/sourcehook/sourcehook.cpp",
"external/metamod-source/core/sourcehook/sourcehook_hookmangen.cpp",
"external/metamod-source/core/sourcehook/sourcehook_impl_chookidman.cpp",
"external/metamod-source/core/sourcehook/sourcehook_impl_chookmaninfo.cpp",
"external/metamod-source/core/sourcehook/sourcehook_impl_cproto.cpp",
"external/metamod-source/core/sourcehook/sourcehook_impl_cvfnptr.cpp" }
nuget { "boost-vc142:1.71.0", "boost:1.71.0", "boost_thread-vc142:1.71.0", "boost_date_time-vc142:1.71.0", "boost_chrono-vc142:1.71.0", "boost_atomic-vc142:1.71.0" }
removefiles { "source/guicon.cpp", "source/guicon.h" }
sysincludedirs { "external/gmod-module-base/include", "external/luajit", "external/metamod-source/core/sourcehook" }

--Fix include order
removesysincludedirs { "external/garrysmod_common-x86-64/include", "external/garrysmod_common-x86-64/helpers/include" }
sysincludedirs { "external/garrysmod_common-x86-64/include", "external/garrysmod_common-x86-64/helpers/include" }

filter({"system:windows", "architecture:x86"})
	libdirs("external/luajit/x86")
	
filter({"system:windows", "architecture:x86_64"})
	libdirs("external/luajit/x64")

filter("system:windows or macosx")
	links("lua51", "luajit")

filter({"system:linux", "architecture:x86"})
	libdirs("external/luajit/linux32")
	links("lua51", "luajit")

filter({"system:linux", "architecture:x86_64"})
	links("lua51", "luajit")

filter({})

--IncludeLuaShared() -- uses this repo path
--IncludeDetouring() -- uses this repo detouring submodule
--IncludeScanning() -- uses this repo scanning submodule

IncludeHelpersExtended()
IncludeSDKCommon()
IncludeSDKTier0()
IncludeSDKTier1()
--IncludeSDKTier2()
--IncludeSDKTier3()
IncludeSDKMathlib()
--IncludeSDKRaytrace()
--IncludeSteamAPI()
