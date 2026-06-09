// NeL - MMORPG Framework <http://dev.ryzom.com/projects/nel/>
// Copyright (C) 2010  Winch Gate Property Limited
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.

// Tessellates Ryzom .zone files (cubic bezier patch terrain) into glTF 2.0 .glb.
// Reference: NeL CBezierPatch::eval(s, t) evaluates position; evalNormal(s, t)
// evaluates the surface normal.  Subdivisions default to 8 per edge (plan spec).

#include "zone_export.h"
#include "gltf_writer.h"

#include <nel/misc/file.h>
#include <nel/misc/path.h>
#include <nel/3d/zone.h>
#include <nel/3d/patch.h>
#include <nel/3d/bezier_patch.h>

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <cmath>

using namespace NLMISC;
using namespace NL3D;

namespace fs = std::filesystem;

bool exportZone(const std::string &inputPath, const std::string &outputPath,
                int subdivisions)
{
	if (subdivisions < 1) subdivisions = 1;

	CIFile ifile;
	if (!ifile.open(inputPath))
	{
		std::cerr << "Cannot open zone: " << inputPath << "\n";
		return false;
	}

	CZone zone;
	try { zone.serial(ifile); }
	catch (const Exception &e)
	{
		std::cerr << "Error reading zone " << inputPath << ": " << e.what() << "\n";
		return false;
	}

	// Retrieve patch info (decompresses quantized coordinates via PatchBias/PatchScale).
	std::vector<CPatchInfo>    patches;
	std::vector<CBorderVertex> borderVerts;
	zone.retrieve(patches, borderVerts);

	int numPatches = (int)patches.size();
	std::cout << "[zone] " << fs::path(inputPath).filename().string()
	          << " — " << numPatches << " patches, sub=" << subdivisions << "\n";

	if (numPatches == 0)
	{
		std::cerr << "  [warn] zone has no patches, writing empty glb\n";
		GltfWriter gltf;
		GltfWriter::NodeInfo ni;
		ni.name = fs::path(inputPath).stem().string();
		gltf.addNode(ni);
		gltf.setSceneNodes({0});
		return gltf.write(outputPath);
	}

	// Tessellate all patches into one combined mesh.
	// Per patch: (subdivisions+1)^2 vertices, subdivisions^2 * 2 triangles.
	int vPerEdge = subdivisions + 1;
	int vPerPatch = vPerEdge * vPerEdge;
	int triPerPatch = subdivisions * subdivisions * 2;
	int idxPerPatch = triPerPatch * 3;

	size_t totalVerts   = (size_t)numPatches * vPerPatch;
	size_t totalIndices = (size_t)numPatches * idxPerPatch;

	std::vector<float>    positions(totalVerts * 3);
	std::vector<float>    normals  (totalVerts * 3);
	std::vector<uint32_t> indices  (totalIndices);

	for (int pi = 0; pi < numPatches; ++pi)
	{
		const CBezierPatch &bp = patches[pi].Patch;

		uint32_t baseVert = (uint32_t)(pi * vPerPatch);
		size_t   vOff     = (size_t)pi * vPerPatch * 3;
		size_t   iOff     = (size_t)pi * idxPerPatch;

		// Evaluate (subdivisions+1)^2 vertices across the patch surface.
		for (int ti = 0; ti < vPerEdge; ++ti)
		{
			float t = (float)ti / (float)subdivisions;
			for (int si = 0; si < vPerEdge; ++si)
			{
				float s = (float)si / (float)subdivisions;
				int vi  = ti * vPerEdge + si;

				CVector pos = bp.eval(s, t);
				CVector nrm = bp.evalNormal(s, t);
				// Normalize normal (evalNormal may not return unit length).
				float len = std::sqrt(nrm.x*nrm.x + nrm.y*nrm.y + nrm.z*nrm.z);
				if (len > 1e-6f) { nrm.x /= len; nrm.y /= len; nrm.z /= len; }

				positions[vOff + vi*3+0] = pos.x;
				positions[vOff + vi*3+1] = pos.y;
				positions[vOff + vi*3+2] = pos.z;
				normals  [vOff + vi*3+0] = nrm.x;
				normals  [vOff + vi*3+1] = nrm.y;
				normals  [vOff + vi*3+2] = nrm.z;
			}
		}

		// Build triangle indices for the patch quad grid.
		// For each (ti, si) quad: two triangles.
		int ii = 0;
		for (int ti = 0; ti < subdivisions; ++ti)
		{
			for (int si = 0; si < subdivisions; ++si)
			{
				uint32_t v00 = baseVert + (uint32_t)(ti     * vPerEdge + si);
				uint32_t v10 = baseVert + (uint32_t)((ti+1) * vPerEdge + si);
				uint32_t v01 = baseVert + (uint32_t)(ti     * vPerEdge + si + 1);
				uint32_t v11 = baseVert + (uint32_t)((ti+1) * vPerEdge + si + 1);

				indices[iOff + ii++] = v00;
				indices[iOff + ii++] = v10;
				indices[iOff + ii++] = v11;

				indices[iOff + ii++] = v00;
				indices[iOff + ii++] = v11;
				indices[iOff + ii++] = v01;
			}
		}
	}

	// Write glTF.
	GltfWriter gltf;

	GltfWriter::Primitive prim;
	prim.posAccessor    = gltf.addFloatAccessor(positions.data(), totalVerts, GltfWriter::VEC3, true);
	prim.normalAccessor = gltf.addFloatAccessor(normals.data(),   totalVerts, GltfWriter::VEC3, true);
	prim.indexAccessor  = gltf.addIndexAccessor(indices.data(),   totalIndices);

	std::string name = fs::path(inputPath).stem().string();
	int meshIdx = gltf.addMesh(name, {prim});

	GltfWriter::NodeInfo ni;
	ni.name = name;
	ni.mesh = meshIdx;
	gltf.addNode(ni);
	gltf.setSceneNodes({0});

	std::cout << "  " << totalVerts << " vertices, " << totalIndices/3 << " triangles -> "
	          << outputPath << "\n";
	return gltf.write(outputPath);
}
