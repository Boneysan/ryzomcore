// NeL - MMORPG Framework <http://dev.ryzom.com/projects/nel/>
// Copyright (C) 2010  Winch Gate Property Limited
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.

// Exports a NeL CInstanceGroup (.ig) to a Godot 4 .tscn file.
// Each CInstance becomes a Node3D with a MeshInstance3D child referencing
// the already-exported .glb asset.

#include "ig_export.h"

#include <nel/misc/file.h>
#include <nel/misc/path.h>
#include <nel/misc/quat.h>
#include <nel/misc/vector.h>
#include <nel/3d/scene_group.h>

#include <fstream>
#include <iostream>
#include <filesystem>
#include <map>
#include <string>
#include <cmath>

using namespace NLMISC;
using namespace NL3D;

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Quaternion + scale → Godot Transform3D string.
//
// Godot .tscn Transform3D format:
//   Transform3D(b00,b01,b02, b10,b11,b12, b20,b21,b22, ox,oy,oz)
// where b0x/b1x/b2x are the three BASIS COLUMNS (X, Y, Z), and o is origin.
//
// The basis columns equal the rotation matrix columns scaled by (sx, sy, sz).
// R is derived from the quaternion q (x, y, z, w):
//   Column 0 (X basis): (1-2(yy+zz), 2(xy+wz),   2(xz-wy))   * sx
//   Column 1 (Y basis): (2(xy-wz),   1-2(xx+zz),  2(yz+wx))   * sy
//   Column 2 (Z basis): (2(xz+wy),   2(yz-wx),    1-2(xx+yy)) * sz

static std::string toTransform3D(const CQuat &q, const CVector &pos, const CVector &scale)
{
	float x = q.x, y = q.y, z = q.z, w = q.w;
	float xx = x*x, yy = y*y, zz = z*z;
	float xy = x*y, xz = x*z, yz = y*z;
	float wx = w*x, wy = w*y, wz = w*z;

	float sx = scale.x, sy = scale.y, sz = scale.z;

	// Three basis columns, each scaled.
	float b00 = (1-2*(yy+zz))*sx, b01 = 2*(xy+wz)*sx,    b02 = 2*(xz-wy)*sx;
	float b10 = 2*(xy-wz)*sy,     b11 = (1-2*(xx+zz))*sy, b12 = 2*(yz+wx)*sy;
	float b20 = 2*(xz+wy)*sz,     b21 = 2*(yz-wx)*sz,     b22 = (1-2*(xx+yy))*sz;

	char buf[512];
	std::snprintf(buf, sizeof(buf),
	    "Transform3D(%g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g)",
	    b00, b01, b02,
	    b10, b11, b12,
	    b20, b21, b22,
	    pos.x, pos.y, pos.z);
	return buf;
}

// Strip directory and extension from shape instance name to get base key.
static std::string shapeKey(const std::string &instanceName)
{
	return fs::path(instanceName).stem().string();
}

// Make a safe Godot node name (no slashes, colons, etc.).
static std::string safeNodeName(const std::string &s)
{
	std::string r;
	r.reserve(s.size());
	for (char c : s)
	{
		if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
			r += c;
		else
			r += '_';
	}
	return r.empty() ? "node" : r;
}

bool exportIG(const std::string &inputPath, const std::string &outputPath,
              const std::string &shapesGlbPrefix)
{
	CIFile ifile;
	if (!ifile.open(inputPath))
	{
		std::cerr << "Cannot open IG: " << inputPath << "\n";
		return false;
	}

	CInstanceGroup ig;
	try { ig.serial(ifile); }
	catch (const Exception &e)
	{
		std::cerr << "Error reading IG " << inputPath << ": " << e.what() << "\n";
		return false;
	}

	uint numInst = ig.getNumInstance();
	std::cout << "[ig] " << fs::path(inputPath).filename().string()
	          << " — " << numInst << " instances\n";

	// --- Collect unique shape names and assign ext_resource IDs ---
	// resource ID 1-based.
	std::map<std::string, int> shapeResId;
	int nextResId = 1;
	for (uint i = 0; i < numInst; ++i)
	{
		std::string key = shapeKey(ig.getInstanceName(i));
		if (key.empty()) continue;
		if (shapeResId.find(key) == shapeResId.end())
			shapeResId[key] = nextResId++;
	}

	// --- Write .tscn ---
	std::ofstream out(outputPath);
	if (!out)
	{
		std::cerr << "Cannot write: " << outputPath << "\n";
		return false;
	}

	int loadSteps = (int)shapeResId.size() + 1; // +1 for the scene itself
	out << "[gd_scene format=3 uid=\"\" load_steps=" << loadSteps << "]\n\n";

	// External resources (one per unique shape glb).
	for (const auto &[key, resId] : shapeResId)
	{
		out << "[ext_resource type=\"PackedScene\" uid=\"\" "
		    << "path=\"" << shapesGlbPrefix << key << ".glb\" "
		    << "id=\"" << resId << "\"]\n";
	}
	out << "\n";

	// Root node.
	std::string sceneName = safeNodeName(fs::path(inputPath).stem().string());
	out << "[node name=\"" << sceneName << "\" type=\"Node3D\"]\n\n";

	// One Node3D per instance.
	// For parent resolution, build node name array.
	std::vector<std::string> nodeNames(numInst);
	std::map<std::string, int> nameCount;
	for (uint i = 0; i < numInst; ++i)
	{
		std::string key = safeNodeName(ig.getInstanceName(i));
		if (key.empty()) key = "inst";
		int cnt = ++nameCount[key];
		nodeNames[i] = (cnt == 1) ? key : (key + "_" + std::to_string(cnt));
	}

	for (uint i = 0; i < numInst; ++i)
	{
		std::string rawName = ig.getInstanceName(i);
		if (rawName.empty()) continue;

		std::string key    = shapeKey(rawName);
		std::string nname  = nodeNames[i];
		CVector  pos       = ig.getInstancePos(i);
		CQuat    rot       = ig.getInstanceRot(i);
		CVector  scale     = ig.getInstanceScale(i);
		sint32   parentIdx = ig.getInstanceParent(i);

		std::string parentPath = ".";
		if (parentIdx >= 0 && parentIdx < (sint32)numInst)
			parentPath = nodeNames[parentIdx];

		out << "[node name=\"" << nname << "\" type=\"Node3D\" parent=\""
		    << parentPath << "\"]\n";
		out << "transform = " << toTransform3D(rot, pos, scale) << "\n";

		// Add a MeshInstance3D child for the shape reference if we have it.
		auto it = shapeResId.find(key);
		if (it != shapeResId.end())
		{
			out << "\n[node name=\"mesh\" type=\"MeshInstance3D\" parent=\""
			    << nname << "\"]\n";
			out << "// scene resource reference — use preload in GDScript or\n";
			out << "// PackedScene.instantiate() at runtime\n";
			// In Godot .tscn, to instantiate a packed scene as a child:
			out << "[node name=\"" << nname << "_scene\" "
			    << "instance=ExtResource(\"" << it->second << "\") "
			    << "parent=\"" << nname << "\"]\n";
		}
		out << "\n";
	}

	std::cout << "  -> " << outputPath << "\n";
	return true;
}
