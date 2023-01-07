PROJECT_GENERATOR_VERSION = 3
include("external/garrysmod_common")

CreateWorkspace({name = "turbostroi", abi_compatible = false})
	CreateProject({serverside = true})
		IncludeSDKCommon()
		IncludeSDKTier0()
		IncludeSDKTier1()

		includedirs("external/luajit/src")
		
		files({
			"source/include/**.h",
			"source/include/**.hpp",
			"source/*.cpp"
		})
		vpaths({
			["Header files/*"] = {"**.h", "**.hpp"},
			["Source files/*"] = "source/*.cpp"
		})
		

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