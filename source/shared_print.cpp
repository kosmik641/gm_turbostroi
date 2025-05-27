#include "shared_print.h"
#include <color.h>
#include <dbg.h>

void SharedPrint::Push(std::string& str)
{
	m_Mutex.lock();
	m_Queue.push(str);
	m_Mutex.unlock();
}

void SharedPrint::Push(const char* str)
{
	std::string sstr(str);
	Push(sstr);
}

void SharedPrint::PrintAvailable()
{
	m_Mutex.lock();
	if (!m_Queue.empty())
	{
		std::string msg = m_Queue.front();
		ConColorMsg(Color(255, 0, 255, 255), msg.c_str());
		m_Queue.pop();
	}
	m_Mutex.unlock();
}

void SharedPrint::ClearPrintQueue()
{
	m_Mutex.lock();
	while (!m_Queue.empty()) m_Queue.pop();
	m_Mutex.unlock();
}

// Based on source: https://www.lua.org/source/5.4/lauxlib.c.html#luaL_tolstring
static const char* lj_tolstring(lua_State* L, int idx, size_t* len)
{
    if (luaL_callmeta(L, idx, "__tostring")) {  /* metafield? */
        if (!lua_isstring(L, -1))
            luaL_error(L, "'__tostring' must return a string");
    }
    else {
        switch (lua_type(L, idx)) {
        case LUA_TNUMBER:
            lua_pushfstring(L, "%f", lua_tonumber(L, idx));
            break;
        case LUA_TSTRING:
            lua_pushvalue(L, idx);
            break;
        case LUA_TBOOLEAN:
            lua_pushstring(L, (lua_toboolean(L, idx) ? "true" : "false"));
            break;
        case LUA_TNIL:
            lua_pushliteral(L, "nil");
            break;
        default: {
            int tt = luaL_getmetafield(L, idx, "__name");  /* try name */
            const char* kind = (tt == LUA_TSTRING) ? lua_tostring(L, -1) : luaL_typename(L, idx);
            lua_pushfstring(L, "%s: %p", kind, lua_topointer(L, idx));
            if (tt != LUA_TNIL)
                lua_remove(L, -2);  /* remove '__name' */
            break;
        }
        }
    }
    return lua_tolstring(L, -1, len);
}

// Based on source: https://www.lua.org/source/5.4/lbaselib.c.html#luaB_print
int SharedPrint::PrintL(lua_State* L)
{
    std::string msg;
    int n = lua_gettop(L);  /* number of arguments */
    for (int i = 1; i <= n; i++) {  /* for each argument */
        if (i > 1)  /* not the first element? */
            msg += '\t';  /* add a tab before it */

        msg += lj_tolstring(L, i, nullptr);  /* add it */
    }
    msg += '\n'; /* add newline */
    g_SharedPrint.Push(msg);
    return 0;
}
