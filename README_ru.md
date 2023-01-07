# Turbostroi V2 with cross-compile support
- [x] Обратная совместимость
  - С фиксом в `sv_turbostroi_v2.lua` для загрузки через FFI
- [x] Компиляция под Linux
- [x] Использовать Think от Source engine вместо своего потока
- [ ] Стабильная работа на Linux 
  - не гарантировано, нужно тестировать
- [ ] Библиотека Boost
  - для lock_free queue, spsc_queue
- [ ] Оптимизация
- [ ] Чистка кода 

# Установка фикса
Замените оригинальный `sv_turbostroi_v2.lua` из этого репозитория. Без этого оно крашит на Linux.

1. Создайте новую папку в `garrysmod/addons`
2. Скопируйте `lua` файл в эту папку по пути `lua/metrostroi` *(эти папки надо создать)*

# Компиляция под Windows MSVC:
1. Установите Visual Studio 2015 или новее
2. [Скачайте](https://premake.github.io/download) `premake5.exe` для Windows
3. Скопируйте и запустите `premake5.exe` в папке с этим репозиторием:
```
premake5.exe vs2022
```
- `vs2015` для Visual Studio 2015
- `vs2017` для Visual Studio 2017
- `vs2019` для Visual Studio 2019
- `vs2022` для Visual Studio 2022
4. Запустите `x86 Native Tools Command Prompt for VS`
5. Перейдите в этой консолие в `external\luajit\src` 
6. Введите команду:
```
msvcbuild.bat static
```
7. Скопируйте `lua51.lib` в `external\luajit\x86`
8. Откройте `projects\windows\vs2022\turbostroi.sln`
9. Скомпилируте с `Release/Win32` конфигурацией
10. Скопируйте `projects\windows\vs2022\x86\Release\gmsv_turbostroi_win32.dll` в `GarrysModDS\garrysmod\lua\bin` (создайте папку `bin`, если её нет)

# Компиляция под Linux GCC:
1. Установите пакет `gcc-multilib`
```
apt install gcc-multilib
```
2. [Скачайте](https://premake.github.io/download) `premake5.exe` для Linux
3. Скопируйте и запустите `premake5` в папке с этим репозиторием:
```
chmod +x ./premake5
./premake5 gmake
```
4. В файле `external/luajit/src/Makefile` найдите строчку
```
CC= $(DEFAULT_CC)
```
и в конец добавьте `-m32`:
```
CC= $(DEFAULT_CC) -m32
```
5. Запустите `make` в папке `external/luajit`
6. Скопируйте `external/luajit/src/libluajit.a` в `external/luajit/linux32`
7. Откройте терминал в `projects/linux/gmake`
8. Запустите компиляцию
```
make config=release_x86
```
9. Скопируйте `projects/linux/gmake/x86/Release/gmsv_turbostroi_linux.dll` в `GarrysModDS\garrysmod\lua\bin` (создайте папку `bin`, если её нет)