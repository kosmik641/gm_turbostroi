local LUA_FILENAME = "lib_turbostroi_v2.lua"
local HEADER_FILENAME = "source/lib_turbostroi_lua.h"

newaction({
	trigger = "lualib2header",
	description = "Generate header file with \"" .. LUA_FILENAME .. "\" content",
	execute = function()
        local f = io.open(LUA_FILENAME, "rb")
        if f == nil then
            error("Fail to open ".. LUA_FILENAME .. "!")
            return
        end
        
        local luaDataStr = string.gsub(f:read("*all"), "\r\n", "\n")
        f:close()
        
        local tbl = {}
        local strLen = string.len(luaDataStr)
        local outStr = ""
        for c=1, strLen do
            outStr = outStr..string.format("\\x%02X", string.byte(luaDataStr, c, c+1))
            
            if (c % 16) == 0 or c == strLen then
                tbl[#tbl+1] = "\""..outStr.."\""
                outStr = ""
            end
        end

        local header = [[
#ifndef LIB_TURBOSTROI_LUA_H
#define LIB_TURBOSTROI_LUA_H

const char g_LibTurbostroiLua[] =
%s;

#endif // LIB_TURBOSTROI_LUA_H
]]
        header = string.format(header, table.concat(tbl, "\n"))
        
        local f = io.open(HEADER_FILENAME, "wb")
        if f == nil then
            error("Fail to open ".. HEADER_FILENAME .. "!")
            return
        end
        
        f:write(header)
        f:close()
	end
})