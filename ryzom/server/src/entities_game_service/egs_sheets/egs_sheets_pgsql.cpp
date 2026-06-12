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

// PostgreSQL sheet overlay (Phase 4, Task 4.2).
//
// Data flow:
//   Startup:   binary .packed_sheets load (unchanged, authoritative fallback)
//              then SELECT FROM bricks -> overlay live-balance fields onto
//              the already-populated CHashMap<CSheetId, CStaticBrick>
//   Runtime:   NATS "sheet.updated.<id>" (Task 4.2b) or the pgReloadBricks
//              admin command -> re-overlay that one hash map entry
//   Sheet API: separate Go service, reads PostgreSQL, serves Godot + GM
//              dashboard only. The EGS NEVER calls the Sheet API.
//
// Only fields whose PostgreSQL value is non-NULL are overlaid; everything
// else keeps its binary-sheet value. Field semantics mirror
// CStaticBrick::readGeorges() exactly (same source form atoms, preserved in
// the bricks.extras JSONB by tools/sheet_migrate/migrate_sheets.py):
//   Basics.SPCost                   -> SkillPointPrice
//   Basics.SabrinaCost              -> SabrinaValue
//   Basics.LearnRequiresOneOfSkills -> LearnRequiresOneOfSkills
//                                      (colon-separated "SKILL value" pairs)
//
// Enabled by the SheetsPgConnString config variable, or the EGS_SHEETS_DB
// environment variable if the config variable is empty. Both empty =
// overlay disabled, EGS behaves exactly as before.

#include "stdpch.h"

#include "nel/misc/algo.h"
#include "nel/misc/variable.h"

#include "egs_sheets/egs_sheets.h"

#ifdef EGS_HAVE_PGSQL
#include <libpq-fe.h>
#endif

using namespace std;
using namespace NLMISC;

CVariable<string> SheetsPgConnString("egs", "SheetsPgConnString", "PostgreSQL conninfo string for the sheet overlay (Phase 4 Task 4.2), e.g. \"host=localhost dbname=ryzom_sheets user=ryzom password=ryzom_dev\". Empty = disabled (env EGS_SHEETS_DB is used as fallback)", "", 0, true);

static string resolvePgConnString()
{
	if (!SheetsPgConnString.get().empty())
		return SheetsPgConnString.get();
	const char *env = getenv("EGS_SHEETS_DB");
	return env ? string(env) : string();
}

static PGconn *connectToPostgres()
{
	const string connString = resolvePgConnString();
	if (connString.empty())
		return NULL;

	PGconn *conn = PQconnectdb(connString.c_str());
	if (PQstatus(conn) != CONNECTION_OK)
	{
		nlwarning("<CSheets> PostgreSQL connection failed: %s", PQerrorMessage(conn));
		PQfinish(conn);
		return NULL;
	}
	return conn;
}

//---------------------------------------------------
// applyPgBrickOverlay :
//---------------------------------------------------
uint32 CSheets::applyPgBrickOverlay(const std::string &brickIdFilter)
{
#ifndef EGS_HAVE_PGSQL
	if (!resolvePgConnString().empty())
		nlwarning("<CSheets::applyPgBrickOverlay> SheetsPgConnString is set but the EGS was built without libpq — overlay skipped");
	return 0;
#else
	PGconn *conn = connectToPostgres();
	if (!conn)
		return 0;

	TTime startTime = CTime::getLocalTime();

	static const char *baseQuery =
		"SELECT id,"
		" extras->'Basics'->>'SPCost',"
		" extras->'Basics'->>'SabrinaCost',"
		" extras->'Basics'->>'LearnRequiresOneOfSkills'"
		" FROM bricks";

	PGresult *res;
	if (brickIdFilter.empty())
	{
		res = PQexec(conn, baseQuery);
	}
	else
	{
		const string query = string(baseQuery) + " WHERE id = $1";
		const char *params[1] = { brickIdFilter.c_str() };
		res = PQexecParams(conn, query.c_str(), 1, NULL, params, NULL, NULL, 0);
	}

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		nlwarning("<CSheets::applyPgBrickOverlay> bricks query failed: %s", PQerrorMessage(conn));
		PQclear(res);
		PQfinish(conn);
		return 0;
	}

	// index the loaded bricks by sheet name without extension ("bczaca01.sbrick" -> "bczaca01"),
	// so unknown PostgreSQL rows never go through CSheetId construction
	map<string, CStaticBrick *> bricksByName;
	for (CAllStaticBricks::iterator it = _StaticSheets._SBrickSheets.begin(); it != _StaticSheets._SBrickSheets.end(); ++it)
	{
		string name = (*it).first.toString();
		string::size_type dot = name.rfind('.');
		bricksByName[name.substr(0, dot)] = &(*it).second;
	}

	uint32 applied = 0;
	uint32 unmatched = 0;
	const int rows = PQntuples(res);
	for (int i = 0; i < rows; ++i)
	{
		map<string, CStaticBrick *>::iterator itb = bricksByName.find(PQgetvalue(res, i, 0));
		if (itb == bricksByName.end())
		{
			++unmatched;
			continue;
		}
		CStaticBrick &brick = *(*itb).second;
		bool touched = false;

		if (!PQgetisnull(res, i, 1))
		{
			fromString(PQgetvalue(res, i, 1), brick.SkillPointPrice);
			touched = true;
		}
		if (!PQgetisnull(res, i, 2))
		{
			fromString(PQgetvalue(res, i, 2), brick.SabrinaValue);
			touched = true;
		}
		if (!PQgetisnull(res, i, 3))
		{
			// same parsing as CStaticBrick::readGeorges: colon-separated "SKILL value" pairs,
			// unknown skills discarded with a warning
			vector<string> skillsAndValues;
			explode(string(PQgetvalue(res, i, 3)), string(":"), skillsAndValues, true);
			vector<CPlayerSkill> learnRequires;
			for (vector<string>::const_iterator isv = skillsAndValues.begin(); isv != skillsAndValues.end(); ++isv)
			{
				CPlayerSkill ps;
				if (ps.initFromString(*isv))
					learnRequires.push_back(ps);
				else
					nlwarning("<CSheets::applyPgBrickOverlay> Invalid LearnRequiresOneOfSkills value '%s' for brick %s", (*isv).c_str(), PQgetvalue(res, i, 0));
			}
			brick.LearnRequiresOneOfSkills = learnRequires;
			touched = true;
		}

		if (touched)
			++applied;
	}

	PQclear(res);
	PQfinish(conn);

	nlinfo("<CSheets::applyPgBrickOverlay> overlaid %u of %d PostgreSQL brick rows in %u ms (%u rows had no loaded sheet)",
		applied, rows, (uint32)(CTime::getLocalTime() - startTime), unmatched);
	return applied;
#endif
}

//---------------------------------------------------
// applyPgItemOverlay :
//---------------------------------------------------
uint32 CSheets::applyPgItemOverlay(const std::string &itemIdFilter)
{
#ifndef EGS_HAVE_PGSQL
	if (!resolvePgConnString().empty())
		nlwarning("<CSheets::applyPgItemOverlay> SheetsPgConnString is set but the EGS was built without libpq — overlay skipped");
	return 0;
#else
	PGconn *conn = connectToPostgres();
	if (!conn)
		return 0;

	TTime startTime = CTime::getLocalTime();

	static const char *baseQuery = "SELECT id, weight, bulk, price FROM items";

	PGresult *res;
	if (itemIdFilter.empty())
	{
		res = PQexec(conn, baseQuery);
	}
	else
	{
		const string query = string(baseQuery) + " WHERE id = $1";
		const char *params[1] = { itemIdFilter.c_str() };
		res = PQexecParams(conn, query.c_str(), 1, NULL, params, NULL, NULL, 0);
	}

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		nlwarning("<CSheets::applyPgItemOverlay> items query failed: %s", PQerrorMessage(conn));
		PQclear(res);
		PQfinish(conn);
		return 0;
	}

	map<string, CStaticItem *> itemsByName;
	for (CAllStaticItems::iterator it = _StaticSheets._StaticItems.begin(); it != _StaticSheets._StaticItems.end(); ++it)
	{
		string name = (*it).first.toString();
		string::size_type dot = name.rfind('.');
		itemsByName[name.substr(0, dot)] = &(*it).second;
	}

	uint32 applied = 0;
	uint32 unmatched = 0;
	const int rows = PQntuples(res);
	for (int i = 0; i < rows; ++i)
	{
		map<string, CStaticItem *>::iterator itb = itemsByName.find(PQgetvalue(res, i, 0));
		if (itb == itemsByName.end())
		{
			++unmatched;
			continue;
		}
		CStaticItem &item = *(*itb).second;
		bool touched = false;

		if (!PQgetisnull(res, i, 1))
		{
			float v; fromString(PQgetvalue(res, i, 1), v);
			item.Weight = (uint32)v;
			touched = true;
		}
		if (!PQgetisnull(res, i, 2))
		{
			float v; fromString(PQgetvalue(res, i, 2), v);
			item.Bulk = (uint32)v;
			touched = true;
		}
		if (!PQgetisnull(res, i, 3))
		{
			float v; fromString(PQgetvalue(res, i, 3), v);
			item.ItemPrice = (uint32)v;
			touched = true;
		}

		if (touched)
			++applied;
	}

	PQclear(res);
	PQfinish(conn);

	nlinfo("<CSheets::applyPgItemOverlay> overlaid %u of %d PostgreSQL item rows in %u ms (%u rows had no loaded sheet)",
		applied, rows, (uint32)(CTime::getLocalTime() - startTime), unmatched);
	return applied;
#endif
}

//---------------------------------------------------
// applyPgCreatureOverlay :
//---------------------------------------------------
uint32 CSheets::applyPgCreatureOverlay(const std::string &creatureIdFilter)
{
#ifndef EGS_HAVE_PGSQL
	if (!resolvePgConnString().empty())
		nlwarning("<CSheets::applyPgCreatureOverlay> SheetsPgConnString is set but the EGS was built without libpq — overlay skipped");
	return 0;
#else
	PGconn *conn = connectToPostgres();
	if (!conn)
		return 0;

	TTime startTime = CTime::getLocalTime();

	static const char *baseQuery = "SELECT id, creature_level FROM creatures";

	PGresult *res;
	if (creatureIdFilter.empty())
	{
		res = PQexec(conn, baseQuery);
	}
	else
	{
		const string query = string(baseQuery) + " WHERE id = $1";
		const char *params[1] = { creatureIdFilter.c_str() };
		res = PQexecParams(conn, query.c_str(), 1, NULL, params, NULL, NULL, 0);
	}

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		nlwarning("<CSheets::applyPgCreatureOverlay> creatures query failed: %s", PQerrorMessage(conn));
		PQclear(res);
		PQfinish(conn);
		return 0;
	}

	map<string, CStaticCreatures *> creaturesByName;
	for (CAllStaticCreatures::iterator it = _StaticSheets._StaticCreatures.begin(); it != _StaticSheets._StaticCreatures.end(); ++it)
	{
		string name = (*it).first.toString();
		string::size_type dot = name.rfind('.');
		creaturesByName[name.substr(0, dot)] = &(*it).second;
	}

	uint32 applied = 0;
	uint32 unmatched = 0;
	const int rows = PQntuples(res);
	for (int i = 0; i < rows; ++i)
	{
		map<string, CStaticCreatures *>::iterator itb = creaturesByName.find(PQgetvalue(res, i, 0));
		if (itb == creaturesByName.end())
		{
			++unmatched;
			continue;
		}
		CStaticCreatures &creature = *(*itb).second;
		bool touched = false;

		// Note: We bypass the private member _Level by using a const cast if needed,
		// but IStaticCreatures has getLevel(), not setLevel(). Since we are in CSheets, 
		// if _Level is private, we'll see if it compiles.
		// Wait, CStaticCreatures might not be a friend of CSheets!
		// If it fails, we will need to update CStaticCreatures.

		// For now we just print it to avoid compilation error until we check.
		// Actually, let's just do it and fix the header.
		// We'll update the header in the next tool call.
		
		if (!PQgetisnull(res, i, 1))
		{
			float v; fromString(PQgetvalue(res, i, 1), v);
			creature.setLevel((uint16)v);
			touched = true;
		}

		if (touched)
			++applied;
	}

	PQclear(res);
	PQfinish(conn);

	nlinfo("<CSheets::applyPgCreatureOverlay> overlaid %u of %d PostgreSQL creature rows in %u ms (%u rows had no loaded sheet)",
		applied, rows, (uint32)(CTime::getLocalTime() - startTime), unmatched);
	return applied;
#endif
}

NLMISC_COMMAND(pgReloadBricks, "re-overlay brick live-balance fields from PostgreSQL", "[<brickId>]")
{
	if (args.size() > 1)
		return false;
	uint32 count = CSheets::applyPgBrickOverlay(args.empty() ? string() : args[0]);
	log.displayNL("%u brick(s) updated from PostgreSQL", count);
	return true;
}

NLMISC_COMMAND(pgReloadItems, "re-overlay item live-balance fields from PostgreSQL", "[<itemId>]")
{
	if (args.size() > 1)
		return false;
	uint32 count = CSheets::applyPgItemOverlay(args.empty() ? string() : args[0]);
	log.displayNL("%u item(s) updated from PostgreSQL", count);
	return true;
}

NLMISC_COMMAND(pgReloadCreatures, "re-overlay creature live-balance fields from PostgreSQL", "[<creatureId>]")
{
	if (args.size() > 1)
		return false;
	uint32 count = CSheets::applyPgCreatureOverlay(args.empty() ? string() : args[0]);
	log.displayNL("%u creature(s) updated from PostgreSQL", count);
	return true;
}
