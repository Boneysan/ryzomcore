// NeL - MMORPG Framework <http://dev.ryzom.com/projects/nel/>
// Copyright (C) 2010  Winch Gate Property Limited
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.

// nel-export — NeL asset → glTF 2.0 / Godot .tscn converter.
//
// Usage:
//   nel-export --shape  input.shape  [-o output.glb]
//   nel-export --ig     input.ig     [-o output.tscn]  [--shapes-prefix res://assets/shapes/]
//   nel-export --zone   input.zone   [-o output.glb]   [--subdivisions 8]
//   nel-export --batch-shapes  input_dir/ --output-dir out/
//   nel-export --batch-zones   input_dir/ --output-dir out/
//   nel-export --batch-ig      input_dir/ --output-dir out/

#include "shape_export.h"
#include "ig_export.h"
#include "zone_export.h"

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

static void usage()
{
	std::cout <<
		"nel-export — NeL asset to glTF 2.0 / Godot .tscn converter\n"
		"\n"
		"Single file:\n"
		"  nel-export --shape  input.shape [-o output.glb]\n"
		"  nel-export --ig     input.ig    [-o output.tscn] [--shapes-prefix res://assets/shapes/]\n"
		"  nel-export --zone   input.zone  [-o output.glb]  [--subdivisions 8]\n"
		"\n"
		"Batch (converts all matching files in a directory):\n"
		"  nel-export --batch-shapes input_dir/ --output-dir out/\n"
		"  nel-export --batch-zones  input_dir/ --output-dir out/ [--subdivisions 8]\n"
		"  nel-export --batch-ig     input_dir/ --output-dir out/ [--shapes-prefix res://assets/shapes/]\n"
		"\n"
		"Output coordinate system: NeL native (Z-up, right-handed).\n"
		"Godot import: set Up Axis = Z (or rotate scene root by -90° on X).\n";
}

static std::string replaceExt(const std::string &path, const std::string &ext)
{
	return fs::path(path).replace_extension(ext).string();
}

static std::string outputFor(const std::string &inputPath, const std::string &outDir,
                             const std::string &ext)
{
	fs::path stem = fs::path(inputPath).stem();
	return (fs::path(outDir) / stem).string() + ext;
}

// Collect all files with the given extension from a directory tree (recursive).
static std::vector<std::string> listFiles(const std::string &dir, const std::string &ext)
{
	std::vector<std::string> result;
	try
	{
		for (const auto &entry : fs::recursive_directory_iterator(dir))
		{
			if (!entry.is_regular_file()) continue;
			std::string e = entry.path().extension().string();
			// Case-insensitive extension match.
			std::transform(e.begin(), e.end(), e.begin(), ::tolower);
			if (e == ext)
				result.push_back(entry.path().string());
		}
	}
	catch (const fs::filesystem_error &err)
	{
		std::cerr << "Directory error: " << err.what() << "\n";
	}
	std::sort(result.begin(), result.end());
	return result;
}

int main(int argc, char *argv[])
{
	if (argc < 2) { usage(); return 1; }

	std::string mode;
	std::string inputPath;
	std::string outputPath;
	std::string outputDir;
	std::string shapesPrefix = "res://assets/shapes/";
	int subdivisions = 8;

	for (int i = 1; i < argc; ++i)
	{
		std::string arg = argv[i];
		if (arg == "--shape" || arg == "--ig" || arg == "--zone" ||
		    arg == "--batch-shapes" || arg == "--batch-zones" || arg == "--batch-ig")
		{
			mode = arg;
			if (i + 1 < argc) inputPath = argv[++i];
		}
		else if (arg == "-o" && i + 1 < argc)       outputPath  = argv[++i];
		else if (arg == "--output-dir" && i+1<argc)  outputDir   = argv[++i];
		else if (arg == "--shapes-prefix" && i+1<argc) shapesPrefix = argv[++i];
		else if (arg == "--subdivisions" && i+1<argc) subdivisions = std::stoi(argv[++i]);
		else if (arg == "--help" || arg == "-h") { usage(); return 0; }
		else { std::cerr << "Unknown argument: " << arg << "\n"; return 1; }
	}

	if (mode.empty() || inputPath.empty())
	{
		std::cerr << "Error: mode and input path required.\n";
		usage();
		return 1;
	}

	// --- Single file modes ---
	if (mode == "--shape")
	{
		if (outputPath.empty()) outputPath = replaceExt(inputPath, ".glb");
		return exportShape(inputPath, outputPath) ? 0 : 1;
	}
	if (mode == "--ig")
	{
		if (outputPath.empty()) outputPath = replaceExt(inputPath, ".tscn");
		return exportIG(inputPath, outputPath, shapesPrefix) ? 0 : 1;
	}
	if (mode == "--zone")
	{
		if (outputPath.empty()) outputPath = replaceExt(inputPath, ".glb");
		return exportZone(inputPath, outputPath, subdivisions) ? 0 : 1;
	}

	// --- Batch modes ---
	if (outputDir.empty())
	{
		std::cerr << "Error: --output-dir required for batch modes.\n";
		return 1;
	}
	fs::create_directories(outputDir);

	int ok = 0, fail = 0;

	if (mode == "--batch-shapes")
	{
		for (const auto &f : listFiles(inputPath, ".shape"))
		{
			std::string out = outputFor(f, outputDir, ".glb");
			if (exportShape(f, out)) ++ok; else ++fail;
		}
	}
	else if (mode == "--batch-zones")
	{
		for (const auto &f : listFiles(inputPath, ".zone"))
		{
			std::string out = outputFor(f, outputDir, ".glb");
			if (exportZone(f, out, subdivisions)) ++ok; else ++fail;
		}
	}
	else if (mode == "--batch-ig")
	{
		for (const auto &f : listFiles(inputPath, ".ig"))
		{
			std::string out = outputFor(f, outputDir, ".tscn");
			if (exportIG(f, out, shapesPrefix)) ++ok; else ++fail;
		}
	}
	else
	{
		std::cerr << "Unknown mode: " << mode << "\n";
		return 1;
	}

	std::cout << "\nDone: " << ok << " ok, " << fail << " failed.\n";
	return fail > 0 ? 1 : 0;
}
