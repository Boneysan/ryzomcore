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

// Task 4.2c Step 1: PDR binary save file -> JSON on stdout.
//
// Walks the CPersistentDataRecord token stream (same traversal as
// CPersistentDataRecord::toXML) and emits a uniform JSON tree:
//   leaf:   {"n":"<token>","t":"<type>","v":"<value-as-string>"}
//   struct: {"n":"<token>","c":[ ...children... ]}
// All values are emitted as JSON strings — uint64 fields (e.g. _Money) would
// lose precision as JSON numbers; the importer converts what it needs.

#include "nel/misc/types_nl.h"
#include "nel/misc/path.h"
#include "nel/misc/sheet_id.h"
#include <stdio.h>
#include <string>
#include <vector>
#include "game_share/persistent_data.h"

using namespace std;
using namespace NLMISC;

static string jsonEscape(const string &in)
{
	string out;
	out.reserve(in.size() + 8);
	for (uint i = 0; i < in.size(); ++i)
	{
		const unsigned char ch = (unsigned char)in[i];
		switch (ch)
		{
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\b': out += "\\b"; break;
		case '\f': out += "\\f"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (ch < 0x20)
			{
				char buf[8];
				sprintf(buf, "\\u%04x", ch);
				out += buf;
			}
			else
			{
				out += (char)ch;
			}
		}
	}
	return out;
}

int main(int argc, char *argv[])
{
	NLMISC::CApplicationContext context;

	// stdout must carry only the JSON document — silence NeL's default
	// stdout displayers (errors still reach stderr via fprintf below)
	createDebug();
	DebugLog->removeDisplayer("DEFAULT_SD");
	InfoLog->removeDisplayer("DEFAULT_SD");
	WarningLog->removeDisplayer("DEFAULT_SD");

	string sheetIdPath;
	string fileName;

	for (int i = 1; i < argc; ++i)
	{
		const string arg = argv[i];
		if (arg.size() >= 2 && arg[0] == '-' && arg[1] == 's')
			sheetIdPath = arg.substr(2);
		else if (!arg.empty() && arg[0] != '-')
			fileName = arg;
		else
		{
			fprintf(stderr, "Unknown parameter '%s'\n", arg.c_str());
			return -1;
		}
	}

	if (fileName.empty())
	{
		printf("Usage: %s [-s<sheet_id_path>] <save_file.bin>\n", argv[0]);
		printf("  Reads a PDR save file (binary or xml/txt) and writes JSON to stdout.\n");
		printf("  -s : path where sheet_id.bin can be found (default: current directory)\n");
		return -1;
	}

	if (!sheetIdPath.empty())
		CPath::addSearchPath(sheetIdPath, false, false);

	CSheetId::init(false);

	if (!CFile::isExists(fileName))
	{
		fprintf(stderr, "File not found: '%s'\n", fileName.c_str());
		return -1;
	}

	static CPersistentDataRecord pdr;
	if (!pdr.readFromFile(fileName))
	{
		fprintf(stderr, "Failed to read PDR file: '%s'\n", fileName.c_str());
		return -1;
	}

	string out;
	out.reserve(4 * 1024 * 1024);
	out += "{\"file\":\"" + jsonEscape(CFile::getFilename(fileName)) + "\",\"format\":\"pdr-json-v1\",\"root\":[";

	// per-depth "need a comma before the next sibling" flags (depth 0 = root)
	vector<bool> needComma;
	needComma.push_back(false);

	pdr.rewind();
	while (!pdr.isEndOfData())
	{
		if (pdr.isStartOfStruct())
		{
			if (needComma.back()) out += ',';
			needComma.back() = true;
			out += "{\"n\":\"" + jsonEscape(pdr.peekNextTokenName()) + "\",\"c\":[";
			pdr.popStructBegin(pdr.peekNextToken());
			needComma.push_back(false);
		}
		else if (pdr.isEndOfStruct())
		{
			out += "]}";
			needComma.pop_back();
			pdr.popStructEnd(pdr.peekNextToken());
		}
		else if (pdr.isTokenWithNoData())
		{
			if (needComma.back()) out += ',';
			needComma.back() = true;
			out += "{\"n\":\"" + jsonEscape(pdr.peekNextTokenName()) + "\",\"t\":\"FLAG\",\"v\":\"1\"}";
			pdr.pop(pdr.peekNextToken());
		}
		else
		{
			const string token = pdr.peekNextTokenName();
			const string argType = pdr.peekNextArg().typeName();
			string argTxt;
			pdr.pop(pdr.peekNextToken(), argTxt);

			if (needComma.back()) out += ',';
			needComma.back() = true;
			out += "{\"n\":\"" + jsonEscape(token) + "\",\"t\":\"" + jsonEscape(argType)
				+ "\",\"v\":\"" + jsonEscape(argTxt) + "\"}";
		}
	}

	if (needComma.size() != 1)
	{
		fprintf(stderr, "Malformed PDR: %u unterminated struct(s) in '%s'\n",
			(uint)(needComma.size() - 1), fileName.c_str());
		return -1;
	}

	out += "]}\n";
	fwrite(out.data(), 1, out.size(), stdout);

	// exit before static destructors — NeL's instance-leak detector prints
	// to stdout at teardown and would corrupt the JSON stream
	fflush(stdout);
	fflush(stderr);
	_exit(0);
}
