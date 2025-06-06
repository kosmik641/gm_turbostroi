> [Русская версия этого файла](README_ru.md)
# Turbostroi V2 with cross-compile support
- [x] Backwards compatibility
  - With small `sv_turbostroi_v2.lua` fix for FFI load
- [x] Linux compile
- [x] Use Source Engine think instead of our thread
- [ ] Linux stable work not guaranteed
  - Need testing
- [ ] Optimization
- [x] Code refactoring
- [ ] Remove turbostroi side from sv_turbostroi_v2.lua
  - Need to be independent

# Install fix
Install `lib_turbostroi_v2.lua` from this repository. It's need to work with module.

1. Create new folder in `garrysmod/addons` (for example `garrysmod/addons/new_turbostroi`)
2. Place `lua` file to `lua/metrostroi` in your new folder *(you need to create subdirectories)*

# Manual for Windows MSVC compile:
1. Install Visual Studio 2015 or newer
2. [Get](https://premake.github.io/download) `premake5.exe` for Windows
3. Place and run `premake5.exe` in this folder:
```
premake5.exe vs2022
```
- `vs2015` for Visual Studio 2015
- `vs2017` for Visual Studio 2017
- `vs2019` for Visual Studio 2019
- `vs2022` for Visual Studio 2022
4. Open `x86 Native Tools Command Prompt for VS` command prompt
5. Go to `external\luajit\src` folder
6. Run command:
```
msvcbuild.bat static
```
7. Copy `lua51.lib` to `external\luajit\x86`
8. Open `projects\windows\vs2022\turbostroi.sln`
9. Compile with `Release/Win32` configuration
10.  Copy `projects\windows\vs2022\x86\Release\gmsv_turbostroi_win32.dll` to `GarrysModDS\garrysmod\lua\bin` (create `bin` folder if it doesn't exist) 

# Manual for Linux GCC compile:
1. Install `gcc-multilib`
```
apt install gcc-multilib
```
2. [Get](https://premake.github.io/download) `premake5` for Linux
3. Place and run `premake5` in this folder:
```
chmod +x ./premake5
./premake5 gmake
```
4. In `external/luajit/src/Makefile` find
```
CC= $(DEFAULT_CC)
```
and add `-m32` parameter:
```
CC= $(DEFAULT_CC) -m32
```
5. Run `make` in `external/luajit` directory
6. Copy `external/luajit/src/libluajit.a` to `external/luajit/linux32`
7. Open terminal in `projects/linux/gmake`
8. Run compilation
```
make config=release_x86
```
9. Copy `projects/linux/gmake/x86/Release/gmsv_turbostroi_linux.dll` to `GarrysModDS\garrysmod\lua\bin` (create `bin` folder if it doesn't exist) 