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
#include "nel/net/tcp_sock.h"

#ifdef EGS_HAVE_PGSQL
#include <libpq-fe.h>
#endif

using namespace std;
using namespace NLMISC;

CVariable<string> LuaScriptDirectory("egs", "LuaScriptDirectory", "Directory scanned for *.lua game scripts at EGS startup (empty = no startup scripts; runtime still available for gm.script.run)", "lua_scripts", 0, true);
CVariable<string> EgsDssNatsUrl("egs", "DssNatsUrl", "NATS URL for DSS", "nats://localhost:4222", 0, true);
CVariable<string> EgsDssPgConn("egs", "DssPgConn", "Postgres connection string for DSS", "", 0, true);

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

// egs.executeCommand(cmd) -> bool
sint luaEgsExecuteCommand(lua_State *state)
{
	const char *cmd = luaL_checkstring(state, 1);
	bool ok = NLMISC::CCommandRegistry::getInstance().execute(cmd, *NLMISC::InfoLog, true, false);
	lua_pushboolean(state, ok);
	return 1;
}

// egs.teleport(char_id, x, y, z) -> bool
sint luaEgsTeleport(lua_State *state)
{
	const char *charId = luaL_checkstring(state, 1);
	float x = (float)luaL_checknumber(state, 2);
	float y = (float)luaL_checknumber(state, 3);
	float z = (float)luaL_checknumber(state, 4);

	char buf[256];
	snprintf(buf, sizeof(buf), "teleportPlayerCharacter %s %f %f %f", charId, x, y, z);
	bool ok = NLMISC::CCommandRegistry::getInstance().execute(buf, *NLMISC::InfoLog, true, false);
	lua_pushboolean(state, ok);
	return 1;
}

// egs.revive(char_id) -> bool  — restores character to full HP at current position
sint luaEgsRevive(lua_State *state)
{
	const char *charId = luaL_checkstring(state, 1);
	char buf[256];
	snprintf(buf, sizeof(buf), "revive %s", charId);
	bool ok = NLMISC::CCommandRegistry::getInstance().execute(buf, *NLMISC::InfoLog, true, false);
	lua_pushboolean(state, ok);
	return 1;
}

// egs.natsPublish(subject, payload) — fire-and-forget NATS publish from Lua
sint luaEgsNatsPublish(lua_State *state)
{
	const char *subject = luaL_checkstring(state, 1);
	const char *payload = luaL_checkstring(state, 2);

	std::string host = "localhost:4222";
	std::string url = EgsDssNatsUrl.get();
	if (url.find("nats://") == 0) url = url.substr(7);
	if (!url.empty()) host = url;

	try {
		NLNET::CTcpSock sock;
		sock.connect(NLNET::CInetHost(host));
		std::string msg = std::string("PUB ") + subject + " " + toString(strlen(payload)) + "\r\n" + payload + "\r\n";
		msg += "PING\r\n";
		uint32 len = (uint32)msg.size();
		sock.send((const uint8*)msg.c_str(), len, false);
		uint8 buf[64];
		uint32 readLen = sizeof(buf);
		sock.receive(buf, readLen, true);
	} catch(std::exception &e) {
		nlwarning("<egs_lua> natsPublish to %s failed: %s", subject, e.what());
	}
	return 0;
}

// egs.registerPartyFrontend(party_id, addr)
sint luaEgsRegisterPartyFrontend(lua_State *state)
{
	const char *partyId = luaL_checkstring(state, 1);
	const char *addr = luaL_checkstring(state, 2);

	std::string host = "localhost:4222";
	std::string url = EgsDssNatsUrl.get();
	if (url.find("nats://") == 0) url = url.substr(7);
	if (!url.empty()) host = url;

	try {
		NLNET::CTcpSock sock;
		sock.connect(NLNET::CInetHost(host));
		
		char json[256];
		snprintf(json, sizeof(json), "{\"party_id\":\"%s\",\"addr\":\"%s\"}", partyId, addr);
		std::string msg = std::string("PUB gm.party.route ") + toString(strlen(json)) + "\r\n" + json + "\r\n";
		msg += "PING\r\n";
		uint32 len = (uint32)msg.size();
		sock.send((const uint8*)msg.c_str(), len, false);
		
		uint8 buf[128];
		uint32 readLen = sizeof(buf);
		sock.receive(buf, readLen, true);
	} catch(std::exception &e) {
		nlwarning("EGS NATS PUB party route failed: %s", e.what());
	}
	return 0;
}

static int luaDssJournalPublish(lua_State *state) {
	const char *json = luaL_checkstring(state, 1);
	nlinfo("DSS: Publishing quest.journal.all via NATS: %s", json);
	
	std::string host = "localhost:4222";
	std::string url = EgsDssNatsUrl.get();
	if (url.find("nats://") == 0) url = url.substr(7);
	if (!url.empty()) host = url;

	try {
		NLNET::CTcpSock sock;
		sock.connect(NLNET::CInetHost(host));
		std::string msg = std::string("PUB quest.journal.all ") + toString(strlen(json)) + "\r\n" + json + "\r\n";
		msg += "PING\r\n";
		uint32 len = (uint32)msg.size();
		sock.send((const uint8*)msg.c_str(), len, false);
		
		uint8 buf[128];
		uint32 readLen = sizeof(buf);
		// block until we read something (like PONG) to ensure it was flushed
		sock.receive(buf, readLen, true);
	} catch(std::exception &e) {
		nlwarning("DSS NATS PUB failed: %s", e.what());
	}
	return 0;
}

static int luaDssSaveProgress(lua_State *state) {
	const char *questId = luaL_checkstring(state, 1);
	const char *progressJson = luaL_checkstring(state, 2);
	nlinfo("DSS: Saving progress to PostgreSQL for quest %s: %s", questId, progressJson);
#ifdef EGS_HAVE_PGSQL
	if (!EgsDssPgConn.get().empty()) {
		PGconn *conn = PQconnectdb(EgsDssPgConn.get().c_str());
		if (PQstatus(conn) == CONNECTION_OK) {
			const char *paramValues[2] = { questId, progressJson };
			PGresult *res = PQexecParams(conn, "INSERT INTO quest_progress (quest_id, data) VALUES ($1, $2) ON CONFLICT (quest_id) DO UPDATE SET data = $2", 2, NULL, paramValues, NULL, NULL, 0);
			PQclear(res);
		}
		PQfinish(conn);
	}
#endif
	return 0;
}

static int luaDssSaveChronicleChoice(lua_State *state) {
	const char *storyline = luaL_checkstring(state, 1);
	const char *questId = luaL_checkstring(state, 2);
	const char *objective = luaL_checkstring(state, 3);
	const char *choiceId = luaL_checkstring(state, 4);
	const char *accountId = luaL_checkstring(state, 5);

	nlinfo("DSS: Saving chronicle choice: %s/%s/%s -> %s (account %s)", storyline, questId, objective, choiceId, accountId);
#ifdef EGS_HAVE_PGSQL
	if (!EgsDssPgConn.get().empty()) {
		PGconn *conn = PQconnectdb(EgsDssPgConn.get().c_str());
		if (PQstatus(conn) == CONNECTION_OK) {
			const char *paramValues[5] = { storyline, questId, objective, choiceId, accountId };
			PGresult *res = PQexecParams(conn, "INSERT INTO chronicle_choices (storyline, quest, objective, choice_id, account_id) VALUES ($1, $2, $3, $4, $5::bigint) ON CONFLICT (storyline, quest, objective, account_id) DO UPDATE SET choice_id = EXCLUDED.choice_id, decided_at = CURRENT_TIMESTAMP", 5, NULL, paramValues, NULL, NULL, 0);
			PQclear(res);
		}
		PQfinish(conn);
	}
#endif
	return 0;
}

static int luaDssSaveFactionStanding(lua_State *state) {
	const char *accountId = luaL_checkstring(state, 1);
	const char *faction = luaL_checkstring(state, 2);
	const char *deltaStr = luaL_checkstring(state, 3);

	nlinfo("DSS: Modifying faction standing for %s/%s by %s", accountId, faction, deltaStr);
#ifdef EGS_HAVE_PGSQL
	if (!EgsDssPgConn.get().empty()) {
		PGconn *conn = PQconnectdb(EgsDssPgConn.get().c_str());
		if (PQstatus(conn) == CONNECTION_OK) {
			const char *paramValues[3] = { accountId, faction, deltaStr };
			PGresult *res = PQexecParams(conn, "INSERT INTO faction_standings (account_id, faction, standing) VALUES ($1::bigint, $2, $3::integer) ON CONFLICT (account_id, faction) DO UPDATE SET standing = faction_standings.standing + $3::integer", 3, NULL, paramValues, NULL, NULL, 0);
			PQclear(res);
		}
		PQfinish(conn);
	}
#endif
	return 0;
}

void registerBindings(lua_State *state)
{
	lua_createtable(state, 0, 5);

	lua_pushcfunction(state, luaEgsInfo);
	lua_setfield(state, -2, "info");
	lua_pushcfunction(state, luaEgsWarning);
	lua_setfield(state, -2, "warning");
	lua_pushcfunction(state, luaEgsGameCycle);
	lua_setfield(state, -2, "gameCycle");
	lua_pushcfunction(state, luaEgsJsonGet);
	lua_setfield(state, -2, "jsonGet");
	lua_pushcfunction(state, luaEgsExecuteCommand);
	lua_setfield(state, -2, "executeCommand");

	lua_pushcfunction(state, luaEgsTeleport);
	lua_setfield(state, -2, "teleport");

	lua_pushcfunction(state, luaEgsRevive);
	lua_setfield(state, -2, "revive");

	lua_pushcfunction(state, luaEgsNatsPublish);
	lua_setfield(state, -2, "natsPublish");

	lua_pushcfunction(state, luaEgsRegisterPartyFrontend);
	lua_setfield(state, -2, "registerPartyFrontend");

	lua_setglobal(state, "egs");

	lua_pushcfunction(state, luaDssJournalPublish);
	lua_setglobal(state, "dss_journalPublish");

	lua_pushcfunction(state, luaDssSaveProgress);
	lua_setglobal(state, "dss_saveProgress");

	lua_pushcfunction(state, luaDssSaveChronicleChoice);
	lua_setglobal(state, "dss_saveChronicleChoice");

	lua_pushcfunction(state, luaDssSaveFactionStanding);
	lua_setglobal(state, "dss_saveFactionStanding");
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

bool runFile(lua_State *state, const string &path, string &errorMsg, const string &moduleName = "")
{
	if (luaL_loadfile(state, path.c_str()) != LUA_OK)
	{
		const char *err = lua_tostring(state, -1);
		errorMsg = err ? err : "unknown Lua load error";
		lua_pop(state, 1);
		return false;
	}
	
	int nresults = moduleName.empty() ? 0 : 1;
	if (lua_pcall(state, 0, nresults, 0) != LUA_OK)
	{
		const char *err = lua_tostring(state, -1);
		errorMsg = err ? err : "unknown Lua error";
		lua_pop(state, 1);
		return false;
	}

	if (!moduleName.empty())
	{
		lua_getglobal(state, "package");
		if (lua_istable(state, -1))
		{
			lua_getfield(state, -1, "loaded");
			lua_pushvalue(state, -3); // push result
			lua_setfield(state, -2, moduleName.c_str());
			lua_pop(state, 2); // pop 'loaded' and 'package'
		}
		else
		{
			lua_pop(state, 1);
		}
		lua_pop(state, 1); // pop result
	}

	return true;
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

	string standardizedDir = CPath::standardizePath(dir);
	lua_getglobal(state, "package");
	if (lua_istable(state, -1))
	{
		lua_getfield(state, -1, "path");
		string pkgPath = lua_tostring(state, -1);
		lua_pop(state, 1);
		pkgPath = pkgPath + ";" + standardizedDir + "?.lua";
		lua_pushstring(state, pkgPath.c_str());
		lua_setfield(state, -2, "path");
	}
	lua_pop(state, 1);

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
		string filename = scripts[i].substr(0, scripts[i].find_last_of('.'));
		string::size_type sep = filename.find_last_of("/\\");
		if (sep != string::npos)
			filename = filename.substr(sep + 1);
		
		if (runFile(state, scripts[i], errorMsg, filename))
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
