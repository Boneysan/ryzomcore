#ifndef AI_LUA_H
#define AI_LUA_H

#include "nel/misc/types_nl.h"
#include <string>

// Lua runtime for the AI service
// Encapsulates the Lua state and luabind registration

struct lua_State;

class CAiLua
{
public:
    static CAiLua& getInstance();

    void init();
    void release();

    lua_State* getState() const { return _LuaState; }
    
    // Load a lua file from the behaviors directory
    bool loadBehavior(const std::string& filename);

private:
    CAiLua();
    ~CAiLua();

    lua_State* _LuaState;
};

#endif // AI_LUA_H
