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

#ifndef EGS_LUA_H
#define EGS_LUA_H

#include <string>
#include <vector>

struct lua_State;

// Phase 4.4: embedded Lua runtime for hot-reloadable game logic.
//
// Scripts load from the LuaScriptDirectory config variable (cwd-relative or
// absolute). GM commands arriving over NATS (gm.*) dispatch to the Lua hook
// on_gm_command(command, payload_json); gm.script.run executes a raw snippet
// and gm.script.reload re-runs a single named file without a service restart.
//
// All entry points must be called from the service update thread (they share
// one lua_State and the game state they touch is not thread-safe).
namespace EGSLUA
{
	// true when the binary was built with Lua support (EGS_HAVE_LUA)
	bool isAvailable();

	// true once init() created the runtime
	bool isInitialized();

	// create the lua_State, register the egs.* bindings and run every *.lua
	// in LuaScriptDirectory (sorted by filename). Idempotent.
	void init();

	// destroy the runtime (safe to call when never initialized)
	void release();

	// execute a Lua snippet; on failure returns false and fills errorMsg
	bool runString(const std::string &code, std::string &errorMsg);

	// re-run one script file from LuaScriptDirectory. name is a bare filename
	// with or without the .lua extension; path separators are rejected.
	bool reloadScript(const std::string &name, std::string &errorMsg);

	enum THookResult
	{
		HookMissing,	// global function not defined — caller decides fallback
		HookOk,
		HookError		// hook raised; errorMsg filled
	};

	// call global fn(args...) with string arguments
	THookResult callHook(const std::string &fn, const std::vector<std::string> &args, std::string &errorMsg);

	// Expose lua_State for additional bindings
	lua_State* getState();

} // namespace EGSLUA

#endif // EGS_LUA_H
