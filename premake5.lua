PROJECT_GENERATOR_VERSION = 3
include("external/garrysmod_common")

CreateWorkspace({name = "turbostroi", abi_compatible = false})
	CreateProject({serverside = true})
        cppdialect("C++14")
		IncludeSDKCommon()
		IncludeSDKTier0()
		IncludeSDKTier1()

		externalincludedirs("external/boost/config/include")
		externalincludedirs("external/boost/multiprecision/include")
		externalincludedirs("external/luajit/src")
		
		files({
			"source/include/**.h",
			"source/include/**.hpp",
			"source/*.cpp"
		})
		vpaths({
			["Header files/*"] = {"**.h", "**.hpp"},
			["Source files/*"] = "source/*.cpp"
		})
		
		-- TODO: Autocompile luajit in MSVC/GCC
		
		-- Windows
		filter("system:windows")
			links("lua51", "luajit")
            defines({"_SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING"})
			
		filter({"system:windows", "architecture:x86"})
			libdirs("external/luajit/x86")
			
		filter({"system:windows", "architecture:x86_64"})
			libdirs("external/luajit/x64")

		-- Linux GCC
		filter("system:linux")
			links("dl", "stdc++fs")
			linkoptions("-pthread")
			
		filter({"system:linux", "architecture:x86"})
			links("luajit")
			libdirs("external/luajit/linux32")

		filter({"system:linux", "architecture:x86_64"})
			links("luajit")
			libdirs("external/luajit/linux64")
			

		filter({})