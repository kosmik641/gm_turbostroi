# IMPORTANT for gmsv_turbostroi_win64 (Gmod x86-64 beta branch):
Version for Gmod x86-64 beta branch
IMPORTANT: Only for x86-64 beta branch! Can crash server with public branch!

IMPORTANT: For gmsv_turbostroi_win64 (Gmod x86-64 beta branch):
To work correctly, you need to unpack and change dll names in metrostroi files:
`lua\autorun\metrostroi.lua` (line 289):
`"win32"` -> `"win64"`

`lua\metrostroi\sv_turbostroi_railnetwork.lua` (line 15):
`"gmsv_turbostroi_win32"` -> `"gmsv_turbostroi_win64"`

`lua\metrostroi\sv_turbostroi_v2.lua` (line 224):
`"gmsv_turbostroi_win32"` -> `"gmsv_turbostroi_win64"`

-------------------------------------------------------------------

Версия для ветки Gmod x86-64 beta
ВАЖНО: Только для бета ветки x86-64! Может вылететь сервер с публичной веткой!

ВАЖНО: Для gmsv_turbostroi_win64 (бета-ветка Gmod x86-64):
Для корректной работы нужно распаковать и изменить имена dll в файлах метростроя:
`lua\autorun\metrostroi.lua` (строка 289):
`"win32"` -> `"win64"`

`lua\metrostroi\sv_turbostroi_railnetwork.lua` (строка 15):
`"gmsv_turbostroi_win32"` -> `"gmsv_turbostroi_win64"`

`lua\metrostroi\sv_turbostroi_v2.lua` (строка 224):
`"gmsv_turbostroi_win32"` -> `"gmsv_turbostroi_win64"`
