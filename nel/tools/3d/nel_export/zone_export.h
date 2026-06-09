// NeL - MMORPG Framework <http://dev.ryzom.com/projects/nel/>
// Copyright (C) 2010  Winch Gate Property Limited
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.

#pragma once

#include <string>

// Export a NeL .zone file (bezier patch terrain) to a glTF 2.0 .glb.
// subdivisions: number of subdivisions per patch edge (default 8 → 8×8 grid).
// Returns true on success.
//
// Task 2.2 implementation: tessellates CBezierPatch surfaces using the NeL
// CBezierPatch::eval(s, t) API.  Each patch becomes an (N+1)×(N+1) vertex grid
// triangulated with 2×N×N triangles.  No tile texture UVs yet (Task 2.3 adds those).
bool exportZone(const std::string &inputPath, const std::string &outputPath,
                int subdivisions = 8);
