// NeL - MMORPG Framework <http://dev.ryzom.com/projects/nel/>
// Copyright (C) 2010  Winch Gate Property Limited
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.

#include "shape_export.h"
#include "gltf_writer.h"

#include <nel/misc/file.h>
#include <nel/misc/path.h>
#include <nel/3d/register_3d.h>
#include <nel/3d/scene.h>
#include <nel/3d/mesh.h>
#include <nel/3d/mesh_mrm.h>
#include <nel/3d/mesh_mrm_skinned.h>
#include <nel/3d/mesh_multi_lod.h>
#include <nel/3d/skeleton_shape.h>
#include <nel/3d/texture_file.h>
#include <nel/3d/animation.h>
#include <nel/3d/animated_value.h>
#include <nel/3d/track.h>
#include <nel/3d/water_shape.h>
#include <nel/3d/particle_system_shape.h>
#include <nel/3d/seg_remanence_shape.h>
#include <nel/3d/flare_shape.h>

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <filesystem>

using namespace NLMISC;
using namespace NL3D;

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers

static std::string baseName(const std::string &path)
{
	return fs::path(path).stem().string();
}

static std::string texUri(const std::string &rawName)
{
	// Strip directory component so glTF references a sibling texture.
	return fs::path(rawName).filename().string();
}

// Get texture URI for a material's diffuse stage (empty if none / not a file).
static std::string materialTexUri(const CMaterial &mat)
{
	ITexture *tex = mat.getTexture(0);
	if (!tex) return {};
	CTextureFile *tf = dynamic_cast<CTextureFile *>(tex);
	if (!tf) return {};
	return texUri(tf->getFileName());
}

// Build identity 4×4 column-major matrix (glTF uses column-major).
static std::array<float, 16> identityMat4()
{
	std::array<float, 16> m = {};
	m[0] = m[5] = m[10] = m[15] = 1.f;
	return m;
}

// CQuat → 4×4 column-major rotation matrix (no scale).
static std::array<float, 16> quatToMat4(const CQuat &q)
{
	float x = q.x, y = q.y, z = q.z, w = q.w;
	float xx = x*x, yy = y*y, zz = z*z;
	float xy = x*y, xz = x*z, yz = y*z;
	float wx = w*x, wy = w*y, wz = w*z;

	// Row-major rotation:
	//  R[row][col]
	//  R[0][0]=1-2(yy+zz)  R[0][1]=2(xy-wz)   R[0][2]=2(xz+wy)
	//  R[1][0]=2(xy+wz)    R[1][1]=1-2(xx+zz) R[1][2]=2(yz-wx)
	//  R[2][0]=2(xz-wy)    R[2][1]=2(yz+wx)   R[2][2]=1-2(xx+yy)
	//
	// glTF MAT4 is column-major: m[col*4+row]
	std::array<float, 16> m = {};
	m[0]  = 1 - 2*(yy+zz); m[1]  = 2*(xy+wz);    m[2]  = 2*(xz-wy);    // col0
	m[4]  = 2*(xy-wz);     m[5]  = 1 - 2*(xx+zz); m[6]  = 2*(yz+wx);    // col1
	m[8]  = 2*(xz+wy);     m[9]  = 2*(yz-wx);     m[10] = 1 - 2*(xx+yy);// col2
	m[15] = 1.f;
	return m;
}

// Multiply two 4×4 column-major matrices: result = a * b.
static std::array<float, 16> mulMat4(const std::array<float, 16> &a,
                                     const std::array<float, 16> &b)
{
	std::array<float, 16> r = {};
	for (int col = 0; col < 4; ++col)
		for (int row = 0; row < 4; ++row)
			for (int k = 0; k < 4; ++k)
				r[col*4+row] += a[k*4+row] * b[col*4+k];
	return r;
}

// Invert a pure rotation+translation 4×4 matrix (no scale).
static std::array<float, 16> invertRtMat4(const std::array<float, 16> &m)
{
	// R^-1 = R^T for rotation part; t^-1 = -R^T * t
	std::array<float, 16> inv = {};
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c)
			inv[c*4+r] = m[r*4+c];
	inv[15] = 1.f;
	// translation: -R^T * t
	for (int r = 0; r < 3; ++r)
	{
		float v = 0;
		for (int k = 0; k < 3; ++k)
			v += inv[k*4+r] * m[12+k];
		inv[12+r] = -v;
	}
	return inv;
}

// ---------------------------------------------------------------------------
// Extract geometry from a CMeshGeom or CMeshMRMGeom render pass into a Primitive.
// Template T: CMeshGeom or CMeshMRMGeom (both have identical accessor API).
template <typename T>
static GltfWriter::Primitive extractGeomPrimitive(
	const T *geom, uint lodId, uint passIdx,
	int materialIdx,
	GltfWriter &gltf)
{
	CVertexBuffer vb = geom->getVertexBuffer(); // copy so lock() can be non-const
	CVertexBufferRead vba;
	vb.lock(vba);

	uint numVerts = vb.capacity();
	uint16 fmt    = vb.getVertexFormat();
	bool hasNorm  = (fmt & CVertexBuffer::NormalFlag) != 0;
	uint numUV    = vb.getNumTexCoordUsed();

	// --- Build geomorph remap for MRM meshes ---
	// geomorphs[i].End gives the "real" vertex index that geomorph vertex i should
	// resolve to.  For non-MRM meshes this list is empty.
	std::vector<uint> vertRemap(numVerts);
	for (uint i = 0; i < numVerts; ++i) vertRemap[i] = i;

	if constexpr (std::is_same_v<T, CMeshMRMGeom>)
	{
		const std::vector<CMRMWedgeGeom> &geomorphs = geom->getGeomorphs(lodId);
		for (size_t gi = 0; gi < geomorphs.size(); ++gi)
			vertRemap[gi] = geomorphs[gi].End;
	}
	// CMeshMRMSkinnedGeom path handled separately (exportSkinnedPrimitive).

	// --- Collect indices from this render pass ---
	const CIndexBuffer *ib = &geom->getRdrPassPrimitiveBlock(lodId, passIdx);
	CIndexBufferRead iba;
	ib->lock(iba);
	uint numIdx = ib->getNumIndexes();

	std::vector<uint32_t> srcIndices(numIdx);
	if (iba.getFormat() == CIndexBuffer::Indices32)
	{
		const uint32_t *p = static_cast<const uint32_t *>(iba.getPtr());
		for (uint i = 0; i < numIdx; ++i)
			srcIndices[i] = vertRemap[p[i]];
	}
	else
	{
		const uint16_t *p = static_cast<const uint16_t *>(iba.getPtr());
		for (uint i = 0; i < numIdx; ++i)
			srcIndices[i] = static_cast<uint32_t>(vertRemap[p[i]]);
	}

	// --- Identify which unique vertex indices are actually used ---
	std::vector<uint32_t> usedVerts = srcIndices;
	std::sort(usedVerts.begin(), usedVerts.end());
	usedVerts.erase(std::unique(usedVerts.begin(), usedVerts.end()), usedVerts.end());

	// Build compact index: original vertex index → compact output index.
	std::vector<int> compact(numVerts, -1);
	for (size_t i = 0; i < usedVerts.size(); ++i)
		compact[usedVerts[i]] = static_cast<int>(i);

	size_t outVertCount = usedVerts.size();

	// --- Pack vertex attributes ---
	std::vector<float> positions(outVertCount * 3);
	std::vector<float> normals(hasNorm ? outVertCount * 3 : 0);
	std::vector<std::vector<float>> uvs(numUV, std::vector<float>(outVertCount * 2));

	for (size_t oi = 0; oi < outVertCount; ++oi)
	{
		uint srcIdx = usedVerts[oi];
		const CVector &pos = *static_cast<const CVector *>(vba.getVertexCoordPointer(srcIdx));
		positions[oi*3+0] = pos.x;
		positions[oi*3+1] = pos.y;
		positions[oi*3+2] = pos.z;

		if (hasNorm)
		{
			const CVector &n = *static_cast<const CVector *>(vba.getNormalCoordPointer(srcIdx));
			normals[oi*3+0] = n.x;
			normals[oi*3+1] = n.y;
			normals[oi*3+2] = n.z;
		}
		for (uint uv = 0; uv < numUV; ++uv)
		{
			const CUV &tc = *static_cast<const CUV *>(vba.getTexCoordPointer(srcIdx, uv));
			uvs[uv][oi*2+0] = tc.U;
			uvs[uv][oi*2+1] = tc.V;
		}
	}

	// Remap indices to compact range.
	std::vector<uint32_t> outIndices(numIdx);
	for (uint i = 0; i < numIdx; ++i)
		outIndices[i] = static_cast<uint32_t>(compact[srcIndices[i]]);

	// --- Write to GltfWriter ---
	GltfWriter::Primitive prim;
	prim.posAccessor    = gltf.addFloatAccessor(positions.data(), outVertCount, GltfWriter::VEC3, true);
	if (hasNorm)
		prim.normalAccessor = gltf.addFloatAccessor(normals.data(), outVertCount, GltfWriter::VEC3, true);
	for (uint uv = 0; uv < numUV; ++uv)
		prim.uvAccessors.push_back(gltf.addFloatAccessor(uvs[uv].data(), outVertCount, GltfWriter::VEC2, true));
	prim.indexAccessor  = gltf.addIndexAccessor(outIndices.data(), numIdx);
	prim.material       = materialIdx;
	return prim;
}

// ---------------------------------------------------------------------------
// Export a CMeshMRMSkinnedGeom render pass (with bone weights).
static GltfWriter::Primitive exportSkinnedPrimitive(
	CMeshMRMSkinnedGeom *geom, uint lodId, uint passIdx,
	int materialIdx,
	GltfWriter &gltf)
{
	// Skin weights and vertex buffer.
	std::vector<CMesh::CSkinWeight> skinWeights;
	geom->getSkinWeights(skinWeights);
	CVertexBuffer vb;
	geom->getVertexBuffer(vb);
	CVertexBufferRead vba;
	vb.lock(vba);

	uint numVerts = (uint)skinWeights.size();
	uint16 fmt    = vb.getVertexFormat();
	bool hasNorm  = (fmt & CVertexBuffer::NormalFlag) != 0;
	uint numUV    = vb.getNumTexCoordUsed();

	// Geomorphs: remap geomorph vertices to their End target.
	const std::vector<CMRMWedgeGeom> &geomorphs = geom->getGeomorphs(lodId);
	std::vector<uint> vertRemap(numVerts);
	for (uint i = 0; i < numVerts; ++i) vertRemap[i] = i;
	for (size_t gi = 0; gi < geomorphs.size(); ++gi)
		vertRemap[gi] = geomorphs[gi].End;

	// Collect indices.
	static CIndexBuffer ibuf; // static to avoid per-call allocation in skinned path
	geom->getRdrPassPrimitiveBlock(lodId, passIdx, ibuf);
	CIndexBufferRead iba;
	ibuf.lock(iba);
	uint numIdx = ibuf.getNumIndexes();

	std::vector<uint32_t> srcIndices(numIdx);
	if (iba.getFormat() == CIndexBuffer::Indices32)
	{
		const uint32_t *p = static_cast<const uint32_t *>(iba.getPtr());
		for (uint i = 0; i < numIdx; ++i) srcIndices[i] = vertRemap[p[i]];
	}
	else
	{
		const uint16_t *p = static_cast<const uint16_t *>(iba.getPtr());
		for (uint i = 0; i < numIdx; ++i) srcIndices[i] = static_cast<uint32_t>(vertRemap[p[i]]);
	}

	// Compact vertex list.
	std::vector<uint32_t> usedVerts = srcIndices;
	std::sort(usedVerts.begin(), usedVerts.end());
	usedVerts.erase(std::unique(usedVerts.begin(), usedVerts.end()), usedVerts.end());
	std::vector<int> compact(numVerts, -1);
	for (size_t i = 0; i < usedVerts.size(); ++i)
		compact[usedVerts[i]] = static_cast<int>(i);
	size_t outCount = usedVerts.size();

	// Pack attributes.
	std::vector<float> positions(outCount * 3);
	std::vector<float> normals(hasNorm ? outCount * 3 : 0);
	std::vector<std::vector<float>> uvs(numUV, std::vector<float>(outCount * 2));
	std::vector<uint8_t> joints(outCount * 4, 0);
	std::vector<float>   weights(outCount * 4, 0.f);

	for (size_t oi = 0; oi < outCount; ++oi)
	{
		uint srcIdx = usedVerts[oi];
		const CVector &pos = *static_cast<const CVector *>(vba.getVertexCoordPointer(srcIdx));
		positions[oi*3+0] = pos.x;
		positions[oi*3+1] = pos.y;
		positions[oi*3+2] = pos.z;

		if (hasNorm)
		{
			const CVector &n = *static_cast<const CVector *>(vba.getNormalCoordPointer(srcIdx));
			normals[oi*3+0] = n.x;
			normals[oi*3+1] = n.y;
			normals[oi*3+2] = n.z;
		}
		for (uint uv = 0; uv < numUV; ++uv)
		{
			const CUV &tc = *static_cast<const CUV *>(vba.getTexCoordPointer(srcIdx, uv));
			uvs[uv][oi*2+0] = tc.U;
			uvs[uv][oi*2+1] = tc.V;
		}

		// Skin weights (up to NL3D_MESH_SKINNING_MAX_MATRIX = 4 influences).
		if (srcIdx < skinWeights.size())
		{
			const CMesh::CSkinWeight &sw = skinWeights[srcIdx];
			for (int j = 0; j < NL3D_MESH_SKINNING_MAX_MATRIX; ++j)
			{
				joints [oi*4+j] = static_cast<uint8_t>(sw.MatrixId[j]);
				weights[oi*4+j] = sw.Weights[j];
			}
		}
	}

	// Remap indices.
	std::vector<uint32_t> outIndices(numIdx);
	for (uint i = 0; i < numIdx; ++i)
		outIndices[i] = static_cast<uint32_t>(compact[srcIndices[i]]);

	GltfWriter::Primitive prim;
	prim.posAccessor     = gltf.addFloatAccessor(positions.data(), outCount, GltfWriter::VEC3, true);
	if (hasNorm)
		prim.normalAccessor  = gltf.addFloatAccessor(normals.data(), outCount, GltfWriter::VEC3, true);
	for (uint uv = 0; uv < numUV; ++uv)
		prim.uvAccessors.push_back(gltf.addFloatAccessor(uvs[uv].data(), outCount, GltfWriter::VEC2, true));
	prim.jointsAccessor  = gltf.addJointAccessor(joints.data(), outCount);
	prim.weightsAccessor = gltf.addFloatAccessor(weights.data(), outCount, GltfWriter::VEC4, true);
	prim.indexAccessor   = gltf.addIndexAccessor(outIndices.data(), numIdx);
	prim.material        = materialIdx;
	return prim;
}

// ---------------------------------------------------------------------------
// Add materials from a CMeshBase and return vector of material indices.
static std::vector<int> addMaterials(const CMeshBase &base, GltfWriter &gltf)
{
	uint numMat = base.getNbMaterial();
	std::vector<int> matIndices(numMat);
	for (uint i = 0; i < numMat; ++i)
	{
		const CMaterial &mat = base.getMaterial(i);
		std::string uri      = materialTexUri(mat);
		matIndices[i]        = gltf.addMaterial("mat" + std::to_string(i), uri);
	}
	return matIndices;
}

// ---------------------------------------------------------------------------
// Build skeleton nodes and return: [boneNodeIndices, rootNodeIndices].
static std::pair<std::vector<int>, std::vector<int>> buildSkeletonNodes(
	const std::vector<CBoneBase> &bones, GltfWriter &gltf)
{
	std::vector<int> boneNode(bones.size(), -1);

	// First pass: create a node for every bone with its default transform.
	for (size_t i = 0; i < bones.size(); ++i)
	{
		const CBoneBase &b = bones[i];
		GltfWriter::NodeInfo ni;
		ni.name    = b.Name.empty() ? ("bone_" + std::to_string(i)) : b.Name;
		ni.hasTRS  = true;
		CVector pos   = b.DefaultPos.getDefaultValue();
		CQuat   rot   = b.DefaultRotQuat.getDefaultValue();
		CVector scale = b.DefaultScale.getDefaultValue();
		ni.pos[0]  = pos.x;   ni.pos[1]  = pos.y;   ni.pos[2]  = pos.z;
		ni.rot[0]  = rot.x;   ni.rot[1]  = rot.y;   ni.rot[2]  = rot.z;  ni.rot[3] = rot.w;
		ni.scale[0]= scale.x; ni.scale[1]= scale.y; ni.scale[2]= scale.z;
		boneNode[i] = gltf.addNode(ni);
	}

	// Second pass: wire up parent→children relationships.
	std::vector<std::vector<int>> children(bones.size());
	std::vector<int> roots;
	for (size_t i = 0; i < bones.size(); ++i)
	{
		sint32 father = bones[i].FatherId;
		if (father < 0 || father >= (sint32)bones.size())
			roots.push_back(boneNode[i]);
		else
			children[father].push_back(boneNode[i]);
	}
	for (size_t i = 0; i < bones.size(); ++i)
	{
		if (!children[i].empty())
			gltf.setNodeChildren(boneNode[i], children[i]);
	}

	return {boneNode, roots};
}

// ---------------------------------------------------------------------------
// Build inverse-bind matrices accessor for a skin.
static int buildIBM(const std::vector<CBoneBase> &bones, GltfWriter &gltf)
{
	// Compute world-space transform of each bone (product along the bone chain),
	// then invert it for the IBM.
	size_t n = bones.size();
	std::vector<std::array<float,16>> worldMats(n);

	for (size_t i = 0; i < n; ++i)
	{
		const CBoneBase &b = bones[i];
		CQuat   rot   = b.DefaultRotQuat.getDefaultValue();
		CVector pos   = b.DefaultPos.getDefaultValue();
		CVector scale = b.DefaultScale.getDefaultValue();

		// Build local TRS matrix.
		auto R   = quatToMat4(rot);
		// Apply scale to rotation columns.
		for (int r = 0; r < 3; ++r) { R[r] *= scale.x; R[4+r] *= scale.y; R[8+r] *= scale.z; }
		R[12] = pos.x; R[13] = pos.y; R[14] = pos.z;

		sint32 father = b.FatherId;
		if (father < 0 || father >= (sint32)n)
			worldMats[i] = R;
		else
			worldMats[i] = mulMat4(worldMats[father], R);
	}

	// Build IBM array (column-major, 16 floats per bone).
	std::vector<float> ibms(n * 16);
	for (size_t i = 0; i < n; ++i)
	{
		auto inv = invertRtMat4(worldMats[i]);
		std::copy(inv.begin(), inv.end(), ibms.begin() + i*16);
	}

	return gltf.addMat4Accessor(ibms.data(), n);
}

// ---------------------------------------------------------------------------
// Try to load and embed a matching .anim file for the given shape.
// animPath: path to the .anim file.  boneNodes: bone node index per bone.
static void embedAnimation(const std::string &animPath,
                           const std::vector<int> &boneNodes,
                           const std::vector<CBoneBase> &bones,
                           GltfWriter &gltf)
{
	CIFile animFile;
	if (!animFile.open(animPath)) return;

	CAnimation anim;
	try { anim.serial(animFile); }
	catch (...) { std::cerr << "  [anim] failed to parse " << animPath << "\n"; return; }

	float beginT = anim.getBeginTime();
	float endT   = anim.getEndTime();
	if (endT <= beginT) return;

	const float FPS     = 30.f;
	uint        nFrames = static_cast<uint>((endT - beginT) * FPS) + 1;
	if (nFrames < 2) return;

	// Time array.
	std::vector<float> times(nFrames);
	for (uint f = 0; f < nFrames; ++f)
		times[f] = beginT + f / FPS;

	// Build a map: bone name → node index.
	std::map<std::string, int> boneNameToNode;
	for (size_t i = 0; i < bones.size(); ++i)
		boneNameToNode[bones[i].Name] = boneNodes[i];

	std::vector<GltfWriter::AnimSampler> samplers;
	std::vector<GltfWriter::AnimChannel> channels;

	int timesAcc = gltf.addFloatAccessor(times.data(), nFrames, GltfWriter::SCALAR, false);

	CAnimatedValueBlock avBlock;

	std::set<std::string> trackNames;
	anim.getTrackNames(trackNames);

	for (const std::string &tname : trackNames)
	{
		// Track name pattern: "BoneName.pos", "BoneName.rotquat", "BoneName.scale"
		std::string path;
		std::string boneName;
		if (tname.size() > 4 && tname.substr(tname.size()-4) == ".pos")
		{
			path     = "translation";
			boneName = tname.substr(0, tname.size()-4);
		}
		else if (tname.size() > 8 && tname.substr(tname.size()-8) == ".rotquat")
		{
			path     = "rotation";
			boneName = tname.substr(0, tname.size()-8);
		}
		else if (tname.size() > 6 && tname.substr(tname.size()-6) == ".scale")
		{
			path     = "scale";
			boneName = tname.substr(0, tname.size()-6);
		}
		else
		{
			continue; // not a bone transform track
		}

		auto it = boneNameToNode.find(boneName);
		if (it == boneNameToNode.end()) continue;
		int nodeIdx = it->second;

		UTrack *utrack = anim.getTrackByName(tname.c_str());
		if (!utrack) continue;
		// ITrack inherits UTrack; all concrete tracks are ITrack subclasses.
		ITrack *track = static_cast<ITrack *>(utrack);

		if (path == "translation" || path == "scale")
		{
			std::vector<float> vals(nFrames * 3);
			for (uint f = 0; f < nFrames; ++f)
			{
				const IAnimatedValue &v = track->eval(times[f], avBlock);
				CVector cv = avBlock.ValVector.Value;
				vals[f*3+0] = cv.x;
				vals[f*3+1] = cv.y;
				vals[f*3+2] = cv.z;
			}
			int outAcc = gltf.addFloatAccessor(vals.data(), nFrames, GltfWriter::VEC3, false);
			GltfWriter::AnimSampler smp{timesAcc, outAcc, "LINEAR"};
			samplers.push_back(smp);
			channels.push_back({(int)samplers.size()-1, nodeIdx, path});
		}
		else // rotation
		{
			std::vector<float> vals(nFrames * 4);
			for (uint f = 0; f < nFrames; ++f)
			{
				const IAnimatedValue &v = track->eval(times[f], avBlock);
				CQuat cq = avBlock.ValQuat.Value;
				vals[f*4+0] = cq.x;
				vals[f*4+1] = cq.y;
				vals[f*4+2] = cq.z;
				vals[f*4+3] = cq.w;
			}
			int outAcc = gltf.addFloatAccessor(vals.data(), nFrames, GltfWriter::VEC4, false);
			GltfWriter::AnimSampler smp{timesAcc, outAcc, "LINEAR"};
			samplers.push_back(smp);
			channels.push_back({(int)samplers.size()-1, nodeIdx, path});
		}
	}

	if (!channels.empty())
	{
		std::string animName = baseName(animPath);
		gltf.addAnimation(animName, samplers, channels);
		std::cout << "  [anim] embedded " << channels.size() << " channels from " << animPath << "\n";
	}
}

// ---------------------------------------------------------------------------
// Export CMesh (simple static mesh).
static bool exportMesh(CMesh *mesh, const std::string &name,
                       const std::string &outPath)
{
	GltfWriter gltf;
	const CMeshGeom *geom  = &mesh->getMeshGeom();
	std::vector<int> mats  = addMaterials(*mesh, gltf);

	uint numPasses = geom->getNbRdrPass(0);
	std::vector<GltfWriter::Primitive> prims;
	prims.reserve(numPasses);
	for (uint p = 0; p < numPasses; ++p)
	{
		int matIdx = (p < mats.size()) ? mats[p] : -1;
		prims.push_back(extractGeomPrimitive(geom, 0, p, matIdx, gltf));
	}

	int meshIdx = gltf.addMesh(name, prims);
	GltfWriter::NodeInfo ni;
	ni.name = name;
	ni.mesh = meshIdx;
	int nodeIdx = gltf.addNode(ni);
	gltf.setSceneNodes({nodeIdx});
	return gltf.write(outPath);
}

// ---------------------------------------------------------------------------
// Export CMeshMRMSkinned (skinned character mesh).
static bool exportMeshMRMSkinned(CMeshMRMSkinned *mesh, const std::string &name,
                                 const std::string &inputPath,
                                 const std::string &outPath)
{
	GltfWriter gltf;
	std::vector<int> mats = addMaterials(*mesh, gltf);

	CMeshMRMSkinnedGeom *geom = const_cast<CMeshMRMSkinnedGeom *>(&mesh->getMeshGeom());
	uint numLods  = geom->getNbLod();
	uint lodId    = numLods > 0 ? numLods - 1 : 0; // highest-quality LOD

	// CMeshMRMSkinned keeps bone names in its geometry; retrieve them.
	// Topology (parent IDs) is not stored in the mesh — we build a flat skeleton.
	const std::vector<std::string> &bonesName = geom->getBonesName();
	uint numBones = (uint)bonesName.size();
	std::vector<CBoneBase> fakeBones(numBones);
	for (uint b = 0; b < numBones; ++b)
	{
		fakeBones[b].Name     = bonesName[b];
		fakeBones[b].FatherId = -1; // flat skeleton; topology unknown without .skel
		fakeBones[b].DefaultPos.setDefaultValue(CVector::Null);
		fakeBones[b].DefaultRotQuat.setDefaultValue(CQuat::Identity);
		fakeBones[b].DefaultScale.setDefaultValue(CVector(1, 1, 1));
	}

	auto [boneNodes, rootNodes] = buildSkeletonNodes(fakeBones, gltf);
	int ibmAcc   = buildIBM(fakeBones, gltf);
	int skinIdx  = gltf.addSkin(name + "_skin", boneNodes, ibmAcc);

	uint numPasses = geom->getNbRdrPass(lodId);
	std::vector<GltfWriter::Primitive> prims;
	prims.reserve(numPasses);
	for (uint p = 0; p < numPasses; ++p)
	{
		int matIdx = (p < mats.size()) ? mats[p] : -1;
		prims.push_back(exportSkinnedPrimitive(geom, lodId, p, matIdx, gltf));
	}

	int meshIdx = gltf.addMesh(name, prims);
	GltfWriter::NodeInfo ni;
	ni.name = name;
	ni.mesh = meshIdx;
	ni.skin = skinIdx;
	int meshNode = gltf.addNode(ni);

	// Add root skeleton nodes as children of mesh node so they travel together.
	gltf.setNodeChildren(meshNode, rootNodes);

	std::vector<int> sceneRoots = {meshNode};
	gltf.setSceneNodes(sceneRoots);

	// Try to embed a matching .anim file.
	std::string animPath = fs::path(inputPath).replace_extension(".anim").string();
	embedAnimation(animPath, boneNodes, fakeBones, gltf);

	return gltf.write(outPath);
}

// ---------------------------------------------------------------------------
// Export CMeshMRM (non-skinned MRM mesh — treat like CMesh at highest LOD).
static bool exportMeshMRM(CMeshMRM *mesh, const std::string &name,
                          const std::string &outPath)
{
	GltfWriter gltf;
	std::vector<int> mats = addMaterials(*mesh, gltf);

	const CMeshMRMGeom *geom = &mesh->getMeshGeom();
	uint numLods   = geom->getNbLod();
	uint lodId     = numLods > 0 ? numLods - 1 : 0;
	uint numPasses = geom->getNbRdrPass(lodId);

	std::vector<GltfWriter::Primitive> prims;
	prims.reserve(numPasses);
	for (uint p = 0; p < numPasses; ++p)
	{
		int matIdx = (p < mats.size()) ? mats[p] : -1;
		prims.push_back(extractGeomPrimitive(geom, lodId, p, matIdx, gltf));
	}

	int meshIdx = gltf.addMesh(name, prims);
	GltfWriter::NodeInfo ni; ni.name = name; ni.mesh = meshIdx;
	int nodeIdx = gltf.addNode(ni);
	gltf.setSceneNodes({nodeIdx});
	return gltf.write(outPath);
}

// ---------------------------------------------------------------------------
// Export CMeshMultiLod (use highest-quality slot 0).
static bool exportMeshMultiLod(CMeshMultiLod *mesh, const std::string &name,
                               const std::string &outPath)
{
	if (mesh->getNumSlotMesh() == 0)
	{
		std::cerr << "  [skip] CMeshMultiLod has no slots: " << name << "\n";
		return false;
	}

	GltfWriter gltf;
	std::vector<int> mats = addMaterials(*mesh, gltf);

	bool coarseMesh = false;
	IMeshGeom *slot0 = mesh->getSlotMesh(0, coarseMesh);
	if (!slot0)
	{
		std::cerr << "  [skip] CMeshMultiLod slot 0 is null: " << name << "\n";
		return false;
	}

	// Dispatch on the slot geometry type.
	std::vector<GltfWriter::Primitive> prims;

	if (CMeshGeom *geom = dynamic_cast<CMeshGeom *>(slot0))
	{
		uint numPasses = geom->getNbRdrPass(0);
		prims.reserve(numPasses);
		for (uint p = 0; p < numPasses; ++p)
		{
			int matIdx = (p < mats.size()) ? mats[p] : -1;
			prims.push_back(extractGeomPrimitive(geom, 0, p, matIdx, gltf));
		}
	}
	else if (CMeshMRMGeom *geom = dynamic_cast<CMeshMRMGeom *>(slot0))
	{
		uint numLods   = geom->getNbLod();
		uint lodId     = numLods > 0 ? numLods - 1 : 0;
		uint numPasses = geom->getNbRdrPass(lodId);
		prims.reserve(numPasses);
		for (uint p = 0; p < numPasses; ++p)
		{
			int matIdx = (p < mats.size()) ? mats[p] : -1;
			prims.push_back(extractGeomPrimitive(geom, lodId, p, matIdx, gltf));
		}
	}
	else
	{
		std::cerr << "  [skip] CMeshMultiLod slot 0 unknown geometry type: " << name << "\n";
		return false;
	}

	int meshIdx = gltf.addMesh(name, prims);
	GltfWriter::NodeInfo ni; ni.name = name; ni.mesh = meshIdx;
	int nodeIdx = gltf.addNode(ni);
	gltf.setSceneNodes({nodeIdx});
	return gltf.write(outPath);
}

// ---------------------------------------------------------------------------
// Export CSkeletonShape (bone hierarchy only, no geometry).
static bool exportSkeletonShape(CSkeletonShape *skel, const std::string &name,
                                const std::string &inputPath,
                                const std::string &outPath)
{
	GltfWriter gltf;
	std::vector<CBoneBase> bones;
	skel->retrieve(bones);

	auto [boneNodes, rootNodes] = buildSkeletonNodes(bones, gltf);

	// Empty mesh node at root so Blender has something to import.
	GltfWriter::NodeInfo root;
	root.name     = name;
	root.children = rootNodes;
	int rootNodeIdx = gltf.addNode(root);
	gltf.setSceneNodes({rootNodeIdx});

	// Try .anim file.
	std::string animPath = fs::path(inputPath).replace_extension(".anim").string();
	embedAnimation(animPath, boneNodes, bones, gltf);

	return gltf.write(outPath);
}

// ---------------------------------------------------------------------------
// Public entry point.
bool exportShape(const std::string &inputPath, const std::string &outputPath)
{
	static bool registered = false;
	if (!registered)
	{
		if (!NLMISC::INelContext::isContextInitialised())
			new NLMISC::CApplicationContext();
		registerSerial3d();
		CScene::registerBasics();
		registered = true;
	}

	CIFile ifile;
	if (!ifile.open(inputPath))
	{
		std::cerr << "Cannot open: " << inputPath << "\n";
		return false;
	}

	CShapeStream ss;
	try { ss.serial(ifile); }
	catch (const Exception &e)
	{
		std::cerr << "Error reading " << inputPath << ": " << e.what() << "\n";
		return false;
	}

	IShape *shape = ss.getShapePointer();
	if (!shape)
	{
		std::cerr << "Null shape pointer: " << inputPath << "\n";
		return false;
	}

	std::string name = baseName(inputPath);

	if (CMesh *m = dynamic_cast<CMesh *>(shape))
	{
		std::cout << "[mesh] " << name << "\n";
		return exportMesh(m, name, outputPath);
	}
	if (CMeshMRMSkinned *m = dynamic_cast<CMeshMRMSkinned *>(shape))
	{
		std::cout << "[skinned] " << name << "\n";
		return exportMeshMRMSkinned(m, name, inputPath, outputPath);
	}
	if (CMeshMRM *m = dynamic_cast<CMeshMRM *>(shape))
	{
		std::cout << "[mrm] " << name << "\n";
		return exportMeshMRM(m, name, outputPath);
	}
	if (CMeshMultiLod *m = dynamic_cast<CMeshMultiLod *>(shape))
	{
		std::cout << "[multilod] " << name << "\n";
		return exportMeshMultiLod(m, name, outputPath);
	}
	if (CSkeletonShape *m = dynamic_cast<CSkeletonShape *>(shape))
	{
		std::cout << "[skeleton] " << name << "\n";
		return exportSkeletonShape(m, name, inputPath, outputPath);
	}

	// Skip types.
	std::string typeName = typeid(*shape).name();
	if (dynamic_cast<CWaterShape *>(shape))
		std::cout << "[skip] " << name << " — CWaterShape: recreate as Godot WaterBody\n";
	else if (dynamic_cast<CParticleSystemShape *>(shape))
		std::cout << "[skip] " << name << " — CParticleSystemShape: recreate as Godot GPUParticles3D\n";
	else if (dynamic_cast<CSegRemanenceShape *>(shape))
		std::cout << "[skip] " << name << " — CSegRemanenceShape: recreate as Godot trail particles\n";
	else if (dynamic_cast<CFlareShape *>(shape))
		std::cout << "[skip] " << name << " — CFlareShape: recreate as Godot lens flare\n";
	else
		std::cout << "[skip] " << name << " — unknown shape type: " << typeName << "\n";

	return true; // skip is not a failure
}
