// Ryzom - MMORPG Framework <http://dev.ryzom.com/projects/ryzom/>
// Copyright (C) 2010  Winch Gate Property Limited
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "stdpch.h"

#include "egs_script/egs_lua.h"

#include "nel/misc/command.h"
#include "nel/misc/path.h"
#include "nel/misc/variable.h"

#include "game_share/tick_event_handler.h"

#include <algorithm>

using namespace std;
using namespace NLMISC;

CVariable<string> LuaScriptDirectory("egs", "LuaScriptDirectory", "Directory scanned for *.lua game scripts at EGS startup (empty = no startup scripts; runtime still available for gm.script.run)", "lua_scripts", 0, true);

#ifdef EGS_HAVE_LUA

extern "C"
{
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

// Lua 5.1 has no LUA_OK; every 5.x returns 0 for success
#ifndef LUA_OK
#define LUA_OK 0
#endif

namespace
{

lua_State *LuaState = NULL;

// Same minimal extraction as egs_sheet_nats.cpp: enough for the flat JSON
// envelopes the Go services publish, no external JSON dependency.
string extractJsonString(const string &payload, const string &key)
{
	const string needle = "\"" + key + "\"";
	string::size_type pos = payload.find(needle);
	if (pos == string::npos)
		return string();
	pos = payload.find(':', pos + needle.size());
	if (pos == string::npos)
		return string();
	pos = payload.find('"', pos + 1);
	if (pos == string::npos)
		return string();

	string value;
	bool escaped = false;
	for (++pos; pos < payload.size(); ++pos)
	{
		const char ch = payload[pos];
		if (escaped)
		{
			value += ch;
			escaped = false;
			continue;
		}
		if (ch == '\\')
		{
			escaped = true;
			continue;
		}
		if (ch == '"')
			return value;
		value += ch;
	}
	return string();
}

// --- egs.* bindings ---------------------------------------------------------

sint luaEgsInfo(lua_State *state)
{
	const char *msg = luaL_checkstring(state, 1);
	nlinfo("<egs_lua> %s", msg);
	return 0;
}

sint luaEgsWarning(lua_State *state)
{
	const char *msg = luaL_checkstring(state, 1);
	nlwarning("<egs_lua> %s", msg);
	return 0;
}

sint luaEgsGameCycle(lua_State *state)
{
	lua_pushnumber(state, (lua_Number)CTickEventHandler::getGameCycle());
	return 1;
}

// egs.jsonGet(json, key) -> string value or nil (flat string fields only)
sint luaEgsJsonGet(lua_State *state)
{
	const char *json = luaL_checkstring(state, 1);
	const char *key = luaL_checkstring(state, 2);
	const string value = extractJsonString(json, key);
	if (value.empty())
		lua_pushnil(state);
	else
		lua_pushstring(state, value.c_str());
	return 1;
}

void registerBindings(lua_State *state)
{
	lua_createtable(state, 0, 4);

	lua_pushcfunction(state, luaEgsInfo);
	lua_setfield(state, -2, "info");
	lua_pushcfunction(state, luaEgsWarning);
	lua_setfield(state, -2, "warning");
	lua_pushcfunction(state, luaEgsGameCycle);
	lua_setfield(state, -2, "gameCycle");
	lua_pushcfunction(state, luaEgsJsonGet);
	lua_setfield(state, -2, "jsonGet");

	lua_setglobal(state, "egs");
}

// run the chunk sitting on top of the stack; pops it either way
bool pcallTop(lua_State *state, int nargs, string &errorMsg)
{
	if (lua_pcall(state, nargs, 0, 0) != LUA_OK)
	{
		const char *err = lua_tostring(state, -1);
		errorMsg = err ? err : "unknown Lua error";
		lua_pop(state, 1);
		return false;
	}
	return true;
}

bool runFile(lua_State *state, const string &path, string &errorMsg)
{
	if (luaL_loadfile(state, path.c_str()) != LUA_OK)
	{
		const char *err = lua_tostring(state, -1);
		errorMsg = err ? err : "unknown Lua load error";
		lua_pop(state, 1);
		return false;
	}
	return pcallTop(state, 0, errorMsg);
}

// bare filename with optional .lua extension; no path separators allowed
bool resolveScriptPath(const string &name, string &path, string &errorMsg)
{
	if (name.empty() || name.find('/') != string::npos || name.find('\\') != string::npos || name.find("..") != string::npos)
	{
		errorMsg = "invalid script name '" + name + "' (bare filename expected)";
		return false;
	}

	string filename = name;
	if (filename.size() < 4 || filename.substr(filename.size() - 4) != ".lua")
		filename += ".lua";

	const string dir = LuaScriptDirectory.get();
	if (dir.empty())
	{
		errorMsg = "LuaScriptDirectory is not set";
		return false;
	}

	path = CPath::standardizePath(dir) + filename;
	if (!CFile::fileExists(path))
	{
		errorMsg = "script not found: " + path;
		return false;
	}
	return true;
}

void loadStartupScripts(lua_State *state)
{
	const string dir = LuaScriptDirectory.get();
	if (dir.empty())
	{
		nlinfo("<egs_lua> LuaScriptDirectory empty — no startup scripts");
		return;
	}
	if (!CFile::isDirectory(dir))
	{
		nlinfo("<egs_lua> script directory '%s' does not exist — no startup scripts", dir.c_str());
		return;
	}

	vector<string> files;
	CPath::getPathContent(dir, false, false, true, files);

	vector<string> scripts;
	for (uint i = 0; i < files.size(); ++i)
	{
		if (files[i].size() >= 4 && files[i].substr(files[i].size() - 4) == ".lua")
			scripts.push_back(files[i]);
	}
	std::sort(scripts.begin(), scripts.end());

	uint loaded = 0;
	for (uint i = 0; i < scripts.size(); ++i)
	{
		string errorMsg;
		if (runFile(state, scripts[i], errorMsg))
			++loaded;
		else
			nlwarning("<egs_lua> failed to load %s: %s", scripts[i].c_str(), errorMsg.c_str());
	}
	nlinfo("<egs_lua> loaded %u of %u scripts from '%s'", loaded, (uint)scripts.size(), dir.c_str());
}

} // anonymous namespace

namespace EGSLUA
{

bool isAvailable()
{
	return true;
}

bool isInitialized()
{
	return LuaState != NULL;
}

void init()
{
	if (LuaState)
		return;

	LuaState = luaL_newstate();
	if (!LuaState)
	{
		nlwarning("<egs_lua> luaL_newstate failed — Lua runtime disabled");
		return;
	}

	luaL_openlibs(LuaState);
	registerBindings(LuaState);
	nlinfo("<egs_lua> Lua runtime initialized (%s)", LUA_RELEASE);

	loadStartupScripts(LuaState);
}

void release()
{
	if (!LuaState)
		return;
	lua_close(LuaState);
	LuaState = NULL;
}

bool runString(const string &code, string &errorMsg)
{
	if (!LuaState)
	{
		errorMsg = "Lua runtime not initialized";
		return false;
	}
	if (luaL_loadbuffer(LuaState, code.data(), code.size(), "gm.script.run") != LUA_OK)
	{
		const char *err = lua_tostring(LuaState, -1);
		errorMsg = err ? err : "unknown Lua load error";
		lua_pop(LuaState, 1);
		return false;
	}
	return pcallTop(LuaState, 0, errorMsg);
}

bool reloadScript(const string &name, string &errorMsg)
{
	if (!LuaState)
	{
		errorMsg = "Lua runtime not initialized";
		return false;
	}
	string path;
	if (!resolveScriptPath(name, path, errorMsg))
		return false;
	if (!runFile(LuaState, path, errorMsg))
		return false;
	nlinfo("<egs_lua> reloaded script %s", path.c_str());
	return true;
}

THookResult callHook(const string &fn, const vector<string> &args, string &errorMsg)
{
	if (!LuaState)
	{
		errorMsg = "Lua runtime not initialized";
		return HookError;
	}

	lua_getglobal(LuaState, fn.c_str());
	if (!lua_isfunction(LuaState, -1))
	{
		lua_pop(LuaState, 1);
		return HookMissing;
	}

	for (uint i = 0; i < args.size(); ++i)
		lua_pushlstring(LuaState, args[i].data(), args[i].size());

	return pcallTop(LuaState, (int)args.size(), errorMsg) ? HookOk : HookError;
}

lua_State* getState()
{
	return LuaState;
}

} // namespace EGSLUA

#else // !EGS_HAVE_LUA

namespace EGSLUA
{

bool isAvailable() { return false; }
bool isInitialized() { return false; }

void init()
{
	nlinfo("<egs_lua> built without Lua support — runtime disabled");
}

void release() {}

bool runString(const std::string &/* code */, std::string &errorMsg)
{
	errorMsg = "EGS built without Lua support";
	return false;
}

bool reloadScript(const std::string &/* name */, std::string &errorMsg)
{
	errorMsg = "EGS built without Lua support";
	return false;
}

THookResult callHook(const std::string &/* fn */, const std::vector<std::string> &/* args */, std::string &errorMsg)
{
	errorMsg = "EGS built without Lua support";
	return HookError;
}

} // namespace EGSLUA

#endif // EGS_HAVE_LUA

// --- admin commands ----------------------------------------------------------

NLMISC_COMMAND(luaRun, "execute a Lua snippet in the EGS runtime", "<lua code>")
{
	if (args.empty())
		return false;

	string code = args[0];
	for (uint i = 1; i < args.size(); ++i)
		code += " " + args[i];

	string errorMsg;
	if (EGSLUA::runString(code, errorMsg))
		log.displayNL("lua: ok");
	else
		log.displayNL("lua error: %s", errorMsg.c_str());
	return true;
}

NLMISC_COMMAND(luaReload, "reload a named Lua script from LuaScriptDirectory", "<script name>")
{
	if (args.size() != 1)
		return false;

	string errorMsg;
	if (EGSLUA::reloadScript(args[0], errorMsg))
		log.displayNL("reloaded '%s'", args[0].c_str());
	else
		log.displayNL("reload failed: %s", errorMsg.c_str());
	return true;
}
