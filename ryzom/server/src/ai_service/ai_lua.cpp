#include "stdpch.h"
#include "ai_lua.h"

#ifdef EGS_HAVE_LUA
extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}
#else
// We'll use the local system lua or the one from EGS.
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}
#endif

#include <luabind/luabind.hpp>

using namespace std;
using namespace NLMISC;

// Dummy namespace for native AI functions
namespace aiNative {
    void spawn() { nlinfo("aiNative::spawn called from Lua"); }
    void despawn() { nlinfo("aiNative::despawn called from Lua"); }
    void setTimer(int id, float time) { nlinfo("aiNative::setTimer(%d, %f) called", id, time); }
    void postNextState() { nlinfo("aiNative::postNextState called"); }
    void setAgro() { nlinfo("aiNative::setAgro called"); }
    void moveToZone(int zoneId) { nlinfo("aiNative::moveToZone(%d) called", zoneId); }
}

CAiLua& CAiLua::getInstance()
{
    static CAiLua instance;
    return instance;
}

CAiLua::CAiLua() : _LuaState(NULL)
{
}

CAiLua::~CAiLua()
{
    release();
}

void CAiLua::init()
{
    if (_LuaState) return;

    _LuaState = luaL_newstate();
    if (!_LuaState)
    {
        nlwarning("Failed to initialize Lua state for AI service");
        return;
    }

    luaL_openlibs(_LuaState);

    // Initialize luabind
    luabind::open(_LuaState);

    // Register Group 1 & 2 functions as required by the plan
    luabind::module(_LuaState, "ai") [
        luabind::def("spawn",        &aiNative::spawn),
        luabind::def("despawn",      &aiNative::despawn),
        luabind::def("setTimer",     &aiNative::setTimer),
        luabind::def("postNextState",&aiNative::postNextState),
        luabind::def("setAgro",      &aiNative::setAgro),
        luabind::def("moveToZone",   &aiNative::moveToZone)
    ];

    nlinfo("AI service Lua state initialized with luabind native bindings.");
}

void CAiLua::release()
{
    if (_LuaState)
    {
        lua_close(_LuaState);
        _LuaState = NULL;
    }
}

bool CAiLua::loadBehavior(const std::string& filename)
{
    if (!_LuaState) return false;

    if (luaL_dofile(_LuaState, filename.c_str()) != 0)
    {
        nlwarning("Lua error loading behavior file %s: %s", filename.c_str(), lua_tostring(_LuaState, -1));
        lua_pop(_LuaState, 1);
        return false;
    }
    return true;
}
