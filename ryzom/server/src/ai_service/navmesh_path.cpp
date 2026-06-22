#include "stdpch.h"

#include "navmesh_path.h"

#include "nel/misc/variable.h"
#include "nel/net/tcp_sock.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <vector>

using namespace std;
using namespace NLMISC;
using namespace NLNET;

CVariable<string> AiPathfindingNatsUrl("ai", "AiPathfindingNatsUrl",
	"NATS URL for the pathfinding-api request/reply service, e.g. nats://localhost:4222. "
	"Empty = fall back to env AI_PATHFINDING_NATS_URL, then NATS_URL.", "", 0, true);

namespace
{
	atomic<uint32> InboxCounter(0);

	string trim(const string &value)
	{
		string::size_type begin = 0;
		while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r' || value[begin] == '\n'))
			++begin;
		string::size_type end = value.size();
		while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r' || value[end - 1] == '\n'))
			--end;
		return value.substr(begin, end - begin);
	}

	string resolveNatsUrl()
	{
		if (!AiPathfindingNatsUrl.get().empty())
			return AiPathfindingNatsUrl.get();
		const char *env = getenv("AI_PATHFINDING_NATS_URL");
		if (env && *env)
			return string(env);
		env = getenv("NATS_URL");
		return env ? string(env) : string();
	}

	string natsEndpointFromUrl(const string &url)
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

	// Deadline-bounded read primitives (egs_sheet_nats.cpp's equivalents
	// loop until StopRequested instead, since that client is a long-lived
	// subscriber; this one is a single bounded request/reply call).
	bool readByte(CTcpSock &sock, char &out, const chrono::steady_clock::time_point &deadline)
	{
		while (chrono::steady_clock::now() < deadline)
		{
			if (!sock.dataAvailable())
			{
				nlSleep(5);
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

	bool readLine(CTcpSock &sock, string &line, const chrono::steady_clock::time_point &deadline)
	{
		line.clear();
		while (chrono::steady_clock::now() < deadline)
		{
			char ch = 0;
			if (!readByte(sock, ch, deadline))
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
				nlwarning("<navmesh_path> NATS protocol line exceeded 8192 bytes");
				return false;
			}
		}
		return false;
	}

	bool readBytes(CTcpSock &sock, uint32 count, string &data, const chrono::steady_clock::time_point &deadline)
	{
		data.clear();
		data.reserve(count);
		for (uint32 i = 0; i < count; ++i)
		{
			char ch = 0;
			if (!readByte(sock, ch, deadline))
				return false;
			data += ch;
		}
		return true;
	}

	bool sendAll(CTcpSock &sock, const string &data)
	{
		const uint8 *ptr = reinterpret_cast<const uint8 *>(data.data());
		uint32 remaining = (uint32)data.size();
		while (remaining > 0)
		{
			uint32 sent = remaining;
			CSock::TSockResult result = sock.send(ptr, sent, false);
			if (result != CSock::Ok)
				return false;
			if (sent == 0)
			{
				nlSleep(5);
				continue;
			}
			ptr += sent;
			remaining -= sent;
		}
		return true;
	}

	vector<string> splitWords(const string &line)
	{
		istringstream input(line);
		vector<string> words;
		string word;
		while (input >> word)
			words.push_back(word);
		return words;
	}

	// Minimal hand-rolled JSON value extraction, same style as
	// egs_sheet_nats.cpp's extractJsonString — this project intentionally
	// avoids pulling in a JSON library for these small, fixed-shape
	// request/response payloads.
	string extractJsonRaw(const string &payload, const string &key, string::size_type from = 0)
	{
		const string needle = "\"" + key + "\"";
		string::size_type pos = payload.find(needle, from);
		if (pos == string::npos)
			return string();
		pos = payload.find(':', pos + needle.size());
		if (pos == string::npos)
			return string();
		++pos;
		while (pos < payload.size() && (payload[pos] == ' ' || payload[pos] == '\t'))
			++pos;
		return payload.substr(pos);
	}

	string extractJsonString(const string &payload, const string &key)
	{
		string rest = extractJsonRaw(payload, key);
		if (rest.empty() || rest[0] != '"')
			return string();

		string value;
		bool escaped = false;
		for (string::size_type i = 1; i < rest.size(); ++i)
		{
			const char ch = rest[i];
			if (escaped) { value += ch; escaped = false; continue; }
			if (ch == '\\') { escaped = true; continue; }
			if (ch == '"') return value;
			value += ch;
		}
		return string();
	}

	bool extractJsonNumber(const string &payload, const string &key, float &out)
	{
		string rest = extractJsonRaw(payload, key);
		if (rest.empty())
			return false;
		try
		{
			out = stof(rest);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	// Parses {"path":[{"x":..,"y":..,"z":..}, ...]} into outPath. No
	// nested arrays/objects appear in this fixed response shape, so a
	// simple brace-matched scan of the "path" array is sufficient.
	bool parsePathResponse(const string &payload, vector<CVector> &outPath, string &errorMsg)
	{
		string err = extractJsonString(payload, "error");
		if (!err.empty())
		{
			errorMsg = err;
			return false;
		}

		string::size_type arrayPos = payload.find("\"path\"");
		if (arrayPos == string::npos)
		{
			errorMsg = "response had neither 'error' nor 'path'";
			return false;
		}
		arrayPos = payload.find('[', arrayPos);
		string::size_type arrayEnd = payload.find(']', arrayPos);
		if (arrayPos == string::npos || arrayEnd == string::npos)
		{
			errorMsg = "malformed 'path' array";
			return false;
		}

		string::size_type pos = arrayPos;
		while (true)
		{
			string::size_type objStart = payload.find('{', pos);
			if (objStart == string::npos || objStart > arrayEnd)
				break;
			string::size_type objEnd = payload.find('}', objStart);
			if (objEnd == string::npos || objEnd > arrayEnd)
				break;

			string obj = payload.substr(objStart, objEnd - objStart + 1);
			CVector v;
			extractJsonNumber(obj, "x", v.x);
			extractJsonNumber(obj, "y", v.y);
			extractJsonNumber(obj, "z", v.z);
			outPath.push_back(v);

			pos = objEnd + 1;
		}
		return true;
	}
}

bool NavmeshPath::findPath(const string &zone, const CVector &start, const CVector &end,
	vector<CVector> &outPath, string &errorMsg)
{
	outPath.clear();
	errorMsg.clear();

	const string endpoint = natsEndpointFromUrl(resolveNatsUrl());
	if (endpoint.empty())
	{
		errorMsg = "pathfinding NATS disabled (AiPathfindingNatsUrl / AI_PATHFINDING_NATS_URL / NATS_URL not set)";
		return false;
	}

	const auto deadline = chrono::steady_clock::now() + chrono::milliseconds(2000);

	try
	{
		CTcpSock sock;
		sock.setTimeOutValue(0, 200000);
		sock.connect(CInetHost(endpoint));
		sock.setTimeOutValue(0, 200000);

		string line;
		if (!readLine(sock, line, deadline) || line.compare(0, 4, "INFO") != 0)
		{
			errorMsg = "did not receive NATS INFO from " + endpoint;
			return false;
		}

		const string connectMsg = "CONNECT {\"verbose\":false,\"pedantic\":false,\"lang\":\"ryzom-ai_service\",\"version\":\"0.1\"}\r\n";
		if (!sendAll(sock, connectMsg))
		{
			errorMsg = "failed to send CONNECT";
			return false;
		}

		const string inbox = "_INBOX.ai_service." + toString(InboxCounter.fetch_add(1));
		if (!sendAll(sock, "SUB " + inbox + " 1\r\n"))
		{
			errorMsg = "failed to send SUB";
			return false;
		}

		ostringstream payload;
		payload << "{\"zone\":\"" << zone << "\","
			<< "\"start\":{\"x\":" << start.x << ",\"y\":" << start.y << ",\"z\":" << start.z << "},"
			<< "\"end\":{\"x\":" << end.x << ",\"y\":" << end.y << ",\"z\":" << end.z << "}}";
		const string body = payload.str();

		ostringstream pub;
		pub << "PUB ai.pathfind.request " << inbox << " " << body.size() << "\r\n" << body << "\r\n";
		if (!sendAll(sock, pub.str()))
		{
			errorMsg = "failed to send PUB";
			return false;
		}

		while (chrono::steady_clock::now() < deadline)
		{
			if (!readLine(sock, line, deadline))
			{
				errorMsg = "timed out waiting for pathfinding-api reply on " + inbox;
				return false;
			}
			if (line.empty() || line.compare(0, 4, "INFO") == 0 || line == "+OK")
				continue;
			if (line == "PING")
			{
				sendAll(sock, "PONG\r\n");
				continue;
			}
			if (line.compare(0, 4, "-ERR") == 0)
			{
				errorMsg = "NATS error: " + line;
				return false;
			}

			vector<string> words = splitWords(line);
			if (words.empty() || words[0] != "MSG")
				continue;
			if (words.size() != 4 && words.size() != 5)
			{
				errorMsg = "malformed MSG line from NATS";
				return false;
			}

			uint32 payloadSize = 0;
			fromString(words.back(), payloadSize);
			string replyPayload;
			if (!readBytes(sock, payloadSize, replyPayload, deadline))
			{
				errorMsg = "failed reading MSG payload";
				return false;
			}
			string crlf;
			readBytes(sock, 2, crlf, deadline);

			return parsePathResponse(replyPayload, outPath, errorMsg);
		}

		errorMsg = "timed out waiting for pathfinding-api reply on " + inbox;
		return false;
	}
	catch (const Exception &e)
	{
		errorMsg = string("NATS connection to ") + endpoint + " failed: " + e.what();
		return false;
	}
	catch (const std::exception &e)
	{
		errorMsg = string("NATS connection to ") + endpoint + " failed: " + e.what();
		return false;
	}
}
