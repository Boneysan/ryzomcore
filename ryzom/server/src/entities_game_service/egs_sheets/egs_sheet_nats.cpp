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

#include "egs_sheets/egs_sheet_nats.h"
#include "egs_sheets/egs_sheets.h"

#include "nel/misc/variable.h"
#include "nel/net/tcp_sock.h"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

using namespace std;
using namespace NLMISC;
using namespace NLNET;

CVariable<bool> EnableSheetNatsInvalidation("egs", "EnableSheetNatsInvalidation", "Enable the Phase 4.2b NATS sheet.updated.* subscriber", true, 0, true);
CVariable<string> SheetNatsUrl("egs", "SheetNatsUrl", "NATS URL for sheet invalidation, e.g. nats://localhost:4222. Empty = disabled (env EGS_SHEET_NATS_URL, then NATS_URL, are used as fallback)", "", 0, true);

namespace
{

struct CPendingSheetInvalidation
{
	string Table;
	string SheetId;
	bool FullReload;
};

struct CPendingGmCommand
{
	string Subject;
	string Command;
	string Payload;
};

mutex PendingMutex;
vector<CPendingSheetInvalidation> PendingInvalidations;
vector<CPendingGmCommand> PendingGmCommands;

mutex ThreadMutex;
thread NatsThread;
atomic<bool> StopRequested(false);
atomic<bool> ThreadRunning(false);

static string trim(const string &value)
{
	string::size_type begin = 0;
	while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r' || value[begin] == '\n'))
		++begin;

	string::size_type end = value.size();
	while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r' || value[end - 1] == '\n'))
		--end;

	return value.substr(begin, end - begin);
}

static string resolveNatsUrl()
{
	if (!SheetNatsUrl.get().empty())
		return SheetNatsUrl.get();
	const char *env = getenv("EGS_SHEET_NATS_URL");
	if (env && *env)
		return string(env);
	env = getenv("NATS_URL");
	return env ? string(env) : string();
}

static string natsEndpointFromUrl(const string &url)
{
	string endpoint = trim(url);
	if (endpoint.empty() || endpoint == "disabled")
		return string();

	static const string scheme = "nats://";
	if (endpoint.compare(0, scheme.size(), scheme) == 0)
		endpoint = endpoint.substr(scheme.size());

	string::size_type at = endpoint.rfind('@');
	if (at != string::npos)
		endpoint = endpoint.substr(at + 1);

	string::size_type slash = endpoint.find('/');
	if (slash != string::npos)
		endpoint = endpoint.substr(0, slash);

	if (endpoint.find(':') == string::npos)
		endpoint += ":4222";

	return endpoint;
}

static bool sendAll(CTcpSock &sock, const string &data)
{
	const uint8 *ptr = reinterpret_cast<const uint8 *>(data.data());
	uint32 remaining = (uint32)data.size();
	while (remaining > 0 && !StopRequested)
	{
		uint32 sent = remaining;
		CSock::TSockResult result = sock.send(ptr, sent, false);
		if (result != CSock::Ok)
			return false;
		if (sent == 0)
		{
			nlSleep(10);
			continue;
		}
		ptr += sent;
		remaining -= sent;
	}
	return remaining == 0;
}

static bool readByte(CTcpSock &sock, char &out)
{
	while (!StopRequested)
	{
		if (!sock.dataAvailable())
		{
			nlSleep(10);
			continue;
		}

		uint8 value = 0;
		uint32 len = 1;
		CSock::TSockResult result = sock.receive(&value, len, false);
		if (result == CSock::WouldBlock || len == 0)
			continue;
		if (result != CSock::Ok)
			return false;

		out = (char)value;
		return true;
	}
	return false;
}

static bool readLine(CTcpSock &sock, string &line)
{
	line.clear();
	while (!StopRequested)
	{
		char ch = 0;
		if (!readByte(sock, ch))
			return false;
		if (ch == '\n')
		{
			if (!line.empty() && line[line.size() - 1] == '\r')
				line.resize(line.size() - 1);
			return true;
		}
		line += ch;
		if (line.size() > 8192)
		{
			nlwarning("<egs_sheet_nats> NATS protocol line exceeded 8192 bytes");
			return false;
		}
	}
	return false;
}

static bool readBytes(CTcpSock &sock, uint32 count, string &data)
{
	data.clear();
	data.reserve(count);
	for (uint32 i = 0; i < count; ++i)
	{
		char ch = 0;
		if (!readByte(sock, ch))
			return false;
		data += ch;
	}
	return true;
}

static vector<string> splitWords(const string &line)
{
	istringstream input(line);
	vector<string> words;
	string word;
	while (input >> word)
		words.push_back(word);
	return words;
}

static string subjectSheetId(const string &subject)
{
	static const string prefix = "sheet.updated.";
	if (subject.compare(0, prefix.size(), prefix) != 0)
		return string();
	return subject.substr(prefix.size());
}

static string extractJsonString(const string &payload, const string &key)
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

static void queueInvalidation(const string &table, const string &sheetId, bool fullReload)
{
	CPendingSheetInvalidation update;
	update.Table = table;
	update.SheetId = sheetId;
	update.FullReload = fullReload;

	lock_guard<mutex> guard(PendingMutex);
	PendingInvalidations.push_back(update);
}

static bool handleMsg(CTcpSock &sock, const vector<string> &words)
{
	if (words.size() != 4 && words.size() != 5)
	{
		nlwarning("<egs_sheet_nats> malformed MSG line from NATS");
		return false;
	}

	const string subject = words[1];
	uint32 payloadSize = 0;
	fromString(words.back(), payloadSize);

	string payload;
	if (!readBytes(sock, payloadSize, payload))
		return false;

	string crlf;
	if (!readBytes(sock, 2, crlf))
		return false;

	if (subject.compare(0, 14, "sheet.updated.") == 0)
	{
		string table = extractJsonString(payload, "table");
		string sheetId = extractJsonString(payload, "sheet_id");
		if (sheetId.empty())
			sheetId = subjectSheetId(subject);

		const bool fullReload = sheetId.empty() || sheetId == "*" || sheetId == "all";
		nlinfo("<egs_sheet_nats> queued %s invalidation for table '%s' sheet '%s'",
			fullReload ? "full" : "single",
			table.c_str(),
			sheetId.c_str());
		queueInvalidation(table, sheetId, fullReload);
		return true;
	}
	else if (subject.compare(0, 3, "gm.") == 0)
	{
		string command = extractJsonString(payload, "command");
		if (command.empty()) return true;

		CPendingGmCommand cmd;
		cmd.Subject = subject;
		cmd.Command = command;
		cmd.Payload = payload;

		lock_guard<mutex> guard(PendingMutex);
		PendingGmCommands.push_back(cmd);
		nlinfo("<egs_sheet_nats> queued GM command: %s", command.c_str());
		return true;
	}

	return true;
}

static bool handleNatsLine(CTcpSock &sock, const string &line)
{
	if (line.empty() || line == "+OK" || line == "PONG")
		return true;
	if (line.compare(0, 4, "INFO") == 0)
		return true;
	if (line == "PING")
		return sendAll(sock, "PONG\r\n");
	if (line.compare(0, 4, "-ERR") == 0)
	{
		nlwarning("<egs_sheet_nats> NATS error: %s", line.c_str());
		return false;
	}

	vector<string> words = splitWords(line);
	if (!words.empty() && words[0] == "MSG")
		return handleMsg(sock, words);

	nlwarning("<egs_sheet_nats> unexpected NATS protocol line: %s", line.c_str());
	return true;
}

static bool connectAndSubscribe(const string &endpoint)
{
	CTcpSock sock;
	sock.setTimeOutValue(1, 0);
	sock.connect(CInetHost(endpoint));
	sock.setTimeOutValue(1, 0);

	string line;
	if (!readLine(sock, line))
		return false;
	if (line.compare(0, 4, "INFO") != 0)
		nlwarning("<egs_sheet_nats> expected NATS INFO, got: %s", line.c_str());

	const string connectMsg = "CONNECT {\"verbose\":false,\"pedantic\":false,\"lang\":\"ryzom-egs\",\"version\":\"0.1\"}\r\n";
	if (!sendAll(sock, connectMsg))
		return false;
	if (!sendAll(sock, "PING\r\n"))
		return false;
	if (!sendAll(sock, "SUB sheet.updated.* 1\r\n"))
		return false;
	if (!sendAll(sock, "SUB gm.* 2\r\n"))
		return false;

	nlinfo("<egs_sheet_nats> subscribed to sheet.updated.* and gm.* on %s", endpoint.c_str());
	queueInvalidation("bricks", string(), true);

	while (!StopRequested)
	{
		if (!readLine(sock, line))
			return false;
		if (!handleNatsLine(sock, line))
			return false;
	}
	return true;
}

static void natsThreadMain(string endpoint)
{
	CSock::initNetwork();

	while (!StopRequested)
	{
		try
		{
			connectAndSubscribe(endpoint);
		}
		catch (const Exception &e)
		{
			if (!StopRequested)
				nlwarning("<egs_sheet_nats> NATS connection to %s failed: %s", endpoint.c_str(), e.what());
		}
		catch (const std::exception &e)
		{
			if (!StopRequested)
				nlwarning("<egs_sheet_nats> NATS connection to %s failed: %s", endpoint.c_str(), e.what());
		}

		for (uint i = 0; i < 20 && !StopRequested; ++i)
			nlSleep(100);
	}

	ThreadRunning = false;
}

} // anonymous namespace

void startSheetNatsInvalidationThread()
{
	if (!EnableSheetNatsInvalidation)
		return;

	const string endpoint = natsEndpointFromUrl(resolveNatsUrl());
	if (endpoint.empty())
	{
		nlinfo("<egs_sheet_nats> disabled: SheetNatsUrl / EGS_SHEET_NATS_URL / NATS_URL not set");
		return;
	}

	lock_guard<mutex> guard(ThreadMutex);
	if (ThreadRunning || NatsThread.joinable())
		return;

	StopRequested = false;
	ThreadRunning = true;
	NatsThread = thread([endpoint]() { natsThreadMain(endpoint); });
}

void serviceSheetNatsInvalidations()
{
	vector<CPendingSheetInvalidation> updates;
	vector<CPendingGmCommand> gmCmds;
	{
		lock_guard<mutex> guard(PendingMutex);
		updates.swap(PendingInvalidations);
		gmCmds.swap(PendingGmCommands);
	}

	for (vector<CPendingGmCommand>::const_iterator it = gmCmds.begin(); it != gmCmds.end(); ++it)
	{
		nlinfo("<egs_sheet_nats> executing GM command '%s' on subject '%s' (payload: %s)",
			it->Command.c_str(), it->Subject.c_str(), it->Payload.c_str());
		// TODO: PlayerManager/CEntityBase hooks (Phase 4.5/5.1)
	}

	if (updates.empty())
		return;

	bool reloadAllBricks = false;
	bool reloadAllItems = false;
	bool reloadAllCreatures = false;
	set<string> brickIds;
	set<string> itemIds;
	set<string> creatureIds;
	for (vector<CPendingSheetInvalidation>::const_iterator it = updates.begin(); it != updates.end(); ++it)
	{
		if (it->Table == "bricks")
		{
			if (it->FullReload)
				reloadAllBricks = true;
			else if (!it->SheetId.empty())
				brickIds.insert(it->SheetId);
		}
		else if (it->Table == "items")
		{
			if (it->FullReload)
				reloadAllItems = true;
			else if (!it->SheetId.empty())
				itemIds.insert(it->SheetId);
		}
		else if (it->Table == "creatures")
		{
			if (it->FullReload)
				reloadAllCreatures = true;
			else if (!it->SheetId.empty())
				creatureIds.insert(it->SheetId);
		}
		else if (!it->Table.empty())
		{
			nlwarning("<egs_sheet_nats> sheet.updated for table '%s' is queued, but only bricks, items and creatures overlay is implemented in this slice", it->Table.c_str());
		}
	}

	if (reloadAllBricks)
		CSheets::applyPgBrickOverlay();
	else
		for (set<string>::const_iterator it = brickIds.begin(); it != brickIds.end(); ++it)
			CSheets::applyPgBrickOverlay(*it);

	if (reloadAllItems)
		CSheets::applyPgItemOverlay();
	else
		for (set<string>::const_iterator it = itemIds.begin(); it != itemIds.end(); ++it)
			CSheets::applyPgItemOverlay(*it);

	if (reloadAllCreatures)
		CSheets::applyPgCreatureOverlay();
	else
		for (set<string>::const_iterator it = creatureIds.begin(); it != creatureIds.end(); ++it)
			CSheets::applyPgCreatureOverlay(*it);
}

void stopSheetNatsInvalidationThread()
{
	thread threadToJoin;
	{
		lock_guard<mutex> guard(ThreadMutex);
		StopRequested = true;
		if (NatsThread.joinable())
			threadToJoin.swap(NatsThread);
	}

	if (threadToJoin.joinable())
		threadToJoin.join();
}
