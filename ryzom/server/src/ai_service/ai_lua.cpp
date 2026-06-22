#include "stdpch.h"
#include "ai_lua.h"
#include "navmesh_path.h"

#include "nel/misc/vector.h"
#include <vector>

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

// The installed libluabind.so only exports the std::unique_ptr overload of
// luabind::scope's constructor; without this define, scope.hpp declares (and
// function.hpp's luabind::def<> calls) the std::auto_ptr overload instead,
// which fails to link. Same guard as lua_helper.cpp/lua_ihm.cpp/lua_ihm_ryzom.cpp.
#ifndef LUABIND_USE_CXX11
#define LUABIND_USE_CXX11
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

    // Task 6.2 (Phase 6 NPC AI): requests a Detour navmesh path from
    // pathfinding-api over NATS (see navmesh_path.cpp) and returns the
    // waypoint count (0 on failure, logged via nlwarning). Wiring the
    // returned waypoints into actual NPC movement (path_behaviors.cpp) is
    // a follow-on — this binding proves the Lua->NATS->Go->Detour round
    // trip, which is this vertical slice's scope.
    int findPath(const std::string& zone, float startX, float startY, float startZ, float endX, float endY, float endZ)
    {
        NLMISC::CVector start(startX, startY, startZ);
        NLMISC::CVector end(endX, endY, endZ);
        std::vector<NLMISC::CVector> path;
        std::string errorMsg;
        if (!NavmeshPath::findPath(zone, start, end, path, errorMsg))
        {
            nlwarning("aiNative::findPath(%s) failed: %s", zone.c_str(), errorMsg.c_str());
            return 0;
        }
        nlinfo("aiNative::findPath(%s) returned %u waypoints", zone.c_str(), (unsigned)path.size());
        return (int)path.size();
    }
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
        luabind::def("moveToZone",   &aiNative::moveToZone),
        luabind::def("findPath",     &aiNative::findPath)
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
