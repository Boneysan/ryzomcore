// NeL - MMORPG Framework <http://dev.ryzom.com/projects/nel/>
// Copyright (C) 2010  Winch Gate Property Limited
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.

// Minimal glTF 2.0 .glb writer — no external dependencies.
// Coordinate system: outputs NeL native (Z-up right-handed).
// Set Godot import axes to "Up: Z, Forward: -Y" or apply the
// basis rotation (Rotate X by -90°) at the scene root.

#pragma once

#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <array>
#include <limits>

class GltfWriter
{
public:
	// glTF component type constants
	static const int CT_UNSIGNED_BYTE  = 5121;
	static const int CT_UNSIGNED_SHORT = 5123;
	static const int CT_UNSIGNED_INT   = 5125;
	static const int CT_FLOAT          = 5126;

	// Accessor type strings
	static constexpr const char *SCALAR = "SCALAR";
	static constexpr const char *VEC2   = "VEC2";
	static constexpr const char *VEC3   = "VEC3";
	static constexpr const char *VEC4   = "VEC4";
	static constexpr const char *MAT4   = "MAT4";

	// Buffer view targets
	static const int TGT_ARRAY_BUFFER         = 34962;
	static const int TGT_ELEMENT_ARRAY_BUFFER = 34963;

	// -----------------------------------------------------------------------
	// Primitive: one render pass's geometry
	struct Primitive
	{
		int posAccessor     = -1;
		int normalAccessor  = -1;
		std::vector<int> uvAccessors;   // one per UV channel
		int indexAccessor   = -1;
		int jointsAccessor  = -1;
		int weightsAccessor = -1;
		int material        = -1;
	};

	// Node: transform + optional mesh + optional skin + children
	struct NodeInfo
	{
		std::string name;
		int  mesh        = -1;
		int  skin        = -1;
		bool hasTRS      = false;
		float pos[3]     = {0, 0, 0};
		float rot[4]     = {0, 0, 0, 1};  // xyzw quaternion
		float scale[3]   = {1, 1, 1};
		std::vector<int> children;
	};

	struct AnimSampler
	{
		int  inputAccessor;
		int  outputAccessor;
		std::string interp = "LINEAR";  // LINEAR | STEP | CUBICSPLINE
	};

	struct AnimChannel
	{
		int sampler;
		int nodeTarget;
		std::string path;  // "translation" | "rotation" | "scale"
	};

	// -----------------------------------------------------------------------
	// Add raw float data and return an accessor index.
	// type: SCALAR / VEC2 / VEC3 / VEC4 / MAT4
	// isVertexData controls ARRAY_BUFFER vs no-target (for skin/anim data).
	int addFloatAccessor(const float *data, size_t count, const char *type,
	                     bool isVertexData = true)
	{
		size_t comps    = componentsOf(type);
		size_t byteLen  = count * comps * sizeof(float);
		size_t bufOff   = alignTo4(_bin.size());
		_bin.resize(bufOff + byteLen, 0);
		std::memcpy(_bin.data() + bufOff, data, byteLen);
		int bvIdx = addBV(bufOff, byteLen, isVertexData ? TGT_ARRAY_BUFFER : -1);
		int accIdx = addAcc(bvIdx, 0, CT_FLOAT, count, type);

		// Compute min/max for POSITION accessors (required by spec).
		if (std::string(type) == VEC3 && isVertexData)
		{
			float mn[3] = { std::numeric_limits<float>::max(),
			                std::numeric_limits<float>::max(),
			                std::numeric_limits<float>::max() };
			float mx[3] = { -std::numeric_limits<float>::max(),
			                -std::numeric_limits<float>::max(),
			                -std::numeric_limits<float>::max() };
			for (size_t i = 0; i < count; ++i)
			{
				for (int c = 0; c < 3; ++c)
				{
					mn[c] = std::min(mn[c], data[i * 3 + c]);
					mx[c] = std::max(mx[c], data[i * 3 + c]);
				}
			}
			_accessors[accIdx].hasMinMax = true;
			std::memcpy(_accessors[accIdx].minVals, mn, sizeof(mn));
			std::memcpy(_accessors[accIdx].maxVals, mx, sizeof(mx));
		}
		return accIdx;
	}

	// Add uint32 index buffer → returns accessor index.
	int addIndexAccessor(const uint32_t *data, size_t count)
	{
		size_t byteLen = count * sizeof(uint32_t);
		size_t bufOff  = alignTo4(_bin.size());
		_bin.resize(bufOff + byteLen, 0);
		std::memcpy(_bin.data() + bufOff, data, byteLen);
		int bvIdx = addBV(bufOff, byteLen, TGT_ELEMENT_ARRAY_BUFFER);
		return addAcc(bvIdx, 0, CT_UNSIGNED_INT, count, SCALAR);
	}

	// Add JOINTS_0: array of uint8[4] per vertex → returns accessor index.
	int addJointAccessor(const uint8_t *data, size_t vertCount)
	{
		size_t byteLen = vertCount * 4;
		size_t bufOff  = alignTo4(_bin.size());
		_bin.resize(bufOff + byteLen, 0);
		std::memcpy(_bin.data() + bufOff, data, byteLen);
		int bvIdx = addBV(bufOff, byteLen, TGT_ARRAY_BUFFER);
		return addAcc(bvIdx, 0, CT_UNSIGNED_BYTE, vertCount, VEC4);
	}

	// Add MAT4 accessor for inverse-bind matrices.
	int addMat4Accessor(const float *data, size_t matCount)
	{
		size_t byteLen = matCount * 16 * sizeof(float);
		size_t bufOff  = alignTo4(_bin.size());
		_bin.resize(bufOff + byteLen, 0);
		std::memcpy(_bin.data() + bufOff, data, byteLen);
		int bvIdx = addBV(bufOff, byteLen, -1);
		return addAcc(bvIdx, 0, CT_FLOAT, matCount, MAT4);
	}

	// Add a material. Returns material index.
	// textureUri may be empty (no texture, white base color).
	int addMaterial(const std::string &name, const std::string &textureUri = "")
	{
		int texIdx = -1;
		if (!textureUri.empty())
		{
			_images.push_back(textureUri);
			_textures.push_back((int)_images.size() - 1);
			texIdx = (int)_textures.size() - 1;
		}
		_materials.push_back({name, texIdx});
		return (int)_materials.size() - 1;
	}

	// Add a mesh (list of primitives). Returns mesh index.
	int addMesh(const std::string &name, const std::vector<Primitive> &prims)
	{
		_meshes.push_back({name, prims});
		return (int)_meshes.size() - 1;
	}

	// Add a node. Returns node index.
	int addNode(const NodeInfo &ni)
	{
		_nodes.push_back(ni);
		return (int)_nodes.size() - 1;
	}

	// Update an existing node's children list (used after all nodes are added).
	void setNodeChildren(int nodeIdx, const std::vector<int> &children)
	{
		_nodes[nodeIdx].children = children;
	}

	// Add a skin. Returns skin index.
	int addSkin(const std::string &name, const std::vector<int> &joints,
	            int ibmAccessor = -1)
	{
		_skins.push_back({name, joints, ibmAccessor});
		return (int)_skins.size() - 1;
	}

	// Add an animation. Returns animation index.
	int addAnimation(const std::string &name,
	                 const std::vector<AnimSampler> &samplers,
	                 const std::vector<AnimChannel> &channels)
	{
		_animations.push_back({name, samplers, channels});
		return (int)_animations.size() - 1;
	}

	// Set the root nodes for scene 0.
	void setSceneNodes(const std::vector<int> &roots)
	{
		_sceneNodes = roots;
	}

	// Write .glb file. Returns true on success.
	bool write(const std::string &outPath)
	{
		std::string json = buildJson();

		// Pad JSON to 4-byte boundary with spaces (as per glTF spec).
		while (json.size() % 4 != 0) json += ' ';

		// Pad binary to 4-byte boundary with zeros.
		while (_bin.size() % 4 != 0) _bin.push_back(0);

		uint32_t jsonLen = (uint32_t)json.size();
		uint32_t binLen  = (uint32_t)_bin.size();
		uint32_t total   = 12 + 8 + jsonLen + (_bin.empty() ? 0 : 8 + binLen);

		std::ofstream out(outPath, std::ios::binary);
		if (!out) return false;

		write32(out, 0x46546C67u); // magic 'glTF'
		write32(out, 2u);          // version
		write32(out, total);

		write32(out, jsonLen);
		write32(out, 0x4E4F534Au); // chunk type 'JSON'
		out.write(json.data(), (std::streamsize)json.size());

		if (!_bin.empty())
		{
			write32(out, binLen);
			write32(out, 0x004E4942u); // chunk type 'BIN\0'
			out.write(reinterpret_cast<const char *>(_bin.data()),
			          (std::streamsize)_bin.size());
		}

		return out.good();
	}

private:
	struct AccInfo
	{
		int    bufferView;
		size_t byteOffset;
		int    componentType;
		size_t count;
		std::string type;
		bool  hasMinMax = false;
		float minVals[3] = {};
		float maxVals[3] = {};
	};
	struct BVInfo { size_t byteOffset, byteLength; int target; };
	struct MatInfo { std::string name; int textureIdx; };
	struct MeshInfo { std::string name; std::vector<Primitive> prims; };
	struct SkinInfo { std::string name; std::vector<int> joints; int ibmAcc; };
	struct AnimInfo
	{
		std::string name;
		std::vector<AnimSampler> samplers;
		std::vector<AnimChannel> channels;
	};

	std::vector<uint8_t>  _bin;
	std::vector<AccInfo>  _accessors;
	std::vector<BVInfo>   _bufferViews;
	std::vector<MatInfo>  _materials;
	std::vector<std::string> _images;
	std::vector<int>      _textures;  // index into _images
	std::vector<MeshInfo> _meshes;
	std::vector<NodeInfo> _nodes;
	std::vector<SkinInfo> _skins;
	std::vector<AnimInfo> _animations;
	std::vector<int>      _sceneNodes;

	static size_t alignTo4(size_t n) { return (n + 3u) & ~3u; }

	int addBV(size_t off, size_t len, int target)
	{
		_bufferViews.push_back({off, len, target});
		return (int)_bufferViews.size() - 1;
	}

	int addAcc(int bvIdx, size_t byteOff, int compType, size_t count, const char *type)
	{
		_accessors.push_back({bvIdx, byteOff, compType, count, type});
		return (int)_accessors.size() - 1;
	}

	static size_t componentsOf(const char *type)
	{
		if (!std::strcmp(type, "SCALAR")) return 1;
		if (!std::strcmp(type, "VEC2"))   return 2;
		if (!std::strcmp(type, "VEC3"))   return 3;
		if (!std::strcmp(type, "VEC4"))   return 4;
		if (!std::strcmp(type, "MAT4"))   return 16;
		return 1;
	}

	static void write32(std::ofstream &out, uint32_t v)
	{
		out.write(reinterpret_cast<const char *>(&v), 4);
	}

	static std::string esc(const std::string &s)
	{
		std::string r;
		r.reserve(s.size());
		for (char c : s)
		{
			if (c == '"')       r += "\\\"";
			else if (c == '\\') r += "\\\\";
			else                r += c;
		}
		return r;
	}

	// Build the JSON string (all arrays, even if empty).
	std::string buildJson() const
	{
		std::ostringstream j;
		j.precision(7);

		j << "{";
		j << "\"asset\":{\"version\":\"2.0\",\"generator\":\"nel-export\"},";
		j << "\"scene\":0,";

		// scenes
		j << "\"scenes\":[{\"name\":\"Scene\",\"nodes\":[";
		for (size_t i = 0; i < _sceneNodes.size(); ++i)
		{ if (i) j << ","; j << _sceneNodes[i]; }
		j << "]}],";

		// nodes
		j << "\"nodes\":[";
		for (size_t i = 0; i < _nodes.size(); ++i)
		{
			if (i) j << ",";
			const NodeInfo &n = _nodes[i];
			j << "{\"name\":\"" << esc(n.name) << "\"";
			if (n.mesh >= 0) j << ",\"mesh\":" << n.mesh;
			if (n.skin >= 0) j << ",\"skin\":" << n.skin;
			if (!n.children.empty())
			{
				j << ",\"children\":[";
				for (size_t c = 0; c < n.children.size(); ++c)
				{ if (c) j << ","; j << n.children[c]; }
				j << "]";
			}
			if (n.hasTRS)
			{
				j << ",\"translation\":[" << n.pos[0] << "," << n.pos[1] << "," << n.pos[2] << "]";
				j << ",\"rotation\":["    << n.rot[0] << "," << n.rot[1] << "," << n.rot[2] << "," << n.rot[3] << "]";
				j << ",\"scale\":["       << n.scale[0] << "," << n.scale[1] << "," << n.scale[2] << "]";
			}
			j << "}";
		}
		j << "],";

		// meshes
		j << "\"meshes\":[";
		for (size_t i = 0; i < _meshes.size(); ++i)
		{
			if (i) j << ",";
			const MeshInfo &m = _meshes[i];
			j << "{\"name\":\"" << esc(m.name) << "\",\"primitives\":[";
			for (size_t p = 0; p < m.prims.size(); ++p)
			{
				if (p) j << ",";
				const Primitive &pr = m.prims[p];
				j << "{\"attributes\":{";
				bool first = true;
				auto attr = [&](const char *k, int v)
				{
					if (v < 0) return;
					if (!first) j << ",";
					j << "\"" << k << "\":" << v;
					first = false;
				};
				attr("POSITION",   pr.posAccessor);
				attr("NORMAL",     pr.normalAccessor);
				for (size_t uv = 0; uv < pr.uvAccessors.size(); ++uv)
				{
					std::string key = "TEXCOORD_" + std::to_string(uv);
					attr(key.c_str(), pr.uvAccessors[uv]);
				}
				attr("JOINTS_0",   pr.jointsAccessor);
				attr("WEIGHTS_0",  pr.weightsAccessor);
				j << "}";
				if (pr.indexAccessor >= 0) j << ",\"indices\":"  << pr.indexAccessor;
				if (pr.material      >= 0) j << ",\"material\":" << pr.material;
				j << "}";
			}
			j << "]}";
		}
		j << "],";

		// materials
		j << "\"materials\":[";
		for (size_t i = 0; i < _materials.size(); ++i)
		{
			if (i) j << ",";
			const MatInfo &mat = _materials[i];
			j << "{\"name\":\"" << esc(mat.name) << "\",\"pbrMetallicRoughness\":{";
			if (mat.textureIdx >= 0)
				j << "\"baseColorTexture\":{\"index\":" << mat.textureIdx << "},";
			j << "\"metallicFactor\":0.0,\"roughnessFactor\":0.8}}";
		}
		j << "],";

		// textures
		j << "\"textures\":[";
		for (size_t i = 0; i < _textures.size(); ++i)
		{ if (i) j << ","; j << "{\"source\":" << _textures[i] << "}"; }
		j << "],";

		// images
		j << "\"images\":[";
		for (size_t i = 0; i < _images.size(); ++i)
		{ if (i) j << ","; j << "{\"uri\":\"" << esc(_images[i]) << "\"}"; }
		j << "],";

		// skins
		j << "\"skins\":[";
		for (size_t i = 0; i < _skins.size(); ++i)
		{
			if (i) j << ",";
			const SkinInfo &sk = _skins[i];
			j << "{\"name\":\"" << esc(sk.name) << "\",\"joints\":[";
			for (size_t ji = 0; ji < sk.joints.size(); ++ji)
			{ if (ji) j << ","; j << sk.joints[ji]; }
			j << "]";
			if (sk.ibmAcc >= 0) j << ",\"inverseBindMatrices\":" << sk.ibmAcc;
			j << "}";
		}
		j << "],";

		// animations
		j << "\"animations\":[";
		for (size_t i = 0; i < _animations.size(); ++i)
		{
			if (i) j << ",";
			const AnimInfo &an = _animations[i];
			j << "{\"name\":\"" << esc(an.name) << "\",\"samplers\":[";
			for (size_t s = 0; s < an.samplers.size(); ++s)
			{
				if (s) j << ",";
				const AnimSampler &sp = an.samplers[s];
				j << "{\"input\":" << sp.inputAccessor
				  << ",\"output\":" << sp.outputAccessor
				  << ",\"interpolation\":\"" << sp.interp << "\"}";
			}
			j << "],\"channels\":[";
			for (size_t c = 0; c < an.channels.size(); ++c)
			{
				if (c) j << ",";
				const AnimChannel &ch = an.channels[c];
				j << "{\"sampler\":" << ch.sampler
				  << ",\"target\":{\"node\":" << ch.nodeTarget
				  << ",\"path\":\"" << ch.path << "\"}}";
			}
			j << "]}";
		}
		j << "],";

		// accessors
		j << "\"accessors\":[";
		for (size_t i = 0; i < _accessors.size(); ++i)
		{
			if (i) j << ",";
			const AccInfo &a = _accessors[i];
			j << "{\"bufferView\":"   << a.bufferView
			  << ",\"byteOffset\":"   << a.byteOffset
			  << ",\"componentType\":" << a.componentType
			  << ",\"count\":"        << a.count
			  << ",\"type\":\""       << a.type << "\"";
			if (a.hasMinMax)
			{
				j << ",\"min\":[" << a.minVals[0] << "," << a.minVals[1] << "," << a.minVals[2] << "]";
				j << ",\"max\":[" << a.maxVals[0] << "," << a.maxVals[1] << "," << a.maxVals[2] << "]";
			}
			j << "}";
		}
		j << "],";

		// bufferViews
		j << "\"bufferViews\":[";
		for (size_t i = 0; i < _bufferViews.size(); ++i)
		{
			if (i) j << ",";
			const BVInfo &bv = _bufferViews[i];
			j << "{\"buffer\":0,\"byteOffset\":" << bv.byteOffset
			  << ",\"byteLength\":" << bv.byteLength;
			if (bv.target >= 0) j << ",\"target\":" << bv.target;
			j << "}";
		}
		j << "],";

		// buffers
		j << "\"buffers\":[{\"byteLength\":" << _bin.size() << "}]";

		j << "}";
		return j.str();
	}
};
