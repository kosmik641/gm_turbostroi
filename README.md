# Turbostroi V2 with cross-compile support
- [ ] Backwards compatibility
  - With small fix for FFI load
- [ ] Linux compile
- [ ] Use Source Engine think instead of our thread for `CurTime()`
- [ ] Linux stable work
  - Need to long work test
- [ ] Boost library for queue, spsc_queue
- [ ] Optimization
- [ ] Code refactoring 

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
4. Open `projects\windows\vs2022\turbostroi.sln`
5. Compile with `Release/Win32` configuration
6. Copy `projects\windows\vs2022\x86\Release\gmsv_turbostroi_win32.dll` to `GarrysModDS\garrysmod\lua\bin` (create `bin` folder if it doesn't exist) 

# Manual for Linux GCC compile:
1. Install `gcc-multilib`
```
apt install gcc-multilib
```
2. [Get](https://premake.github.io/download) `premake5.exe` for Linux
3. Place and run `premake5` in this folder:
```
./premake5.exe gmake
```
4. Open terminal in `projects/linux/gmake`
5. Run compile
```
make config=release_x86
```
6. Copy `projects/linux/gmake/x86/Release/gmsv_turbostroi_linux.dll` to `GarrysModDS\garrysmod\lua\bin` (create `bin` folder if it doesn't exist) 