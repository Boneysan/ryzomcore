// NeL - MMORPG Framework <http://dev.ryzom.com/projects/nel/>
// Copyright (C) 2010  Winch Gate Property Limited
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.

#pragma once

#include <string>

// Export a .ig instance-group file to a Godot 4 .tscn scene.
// shapesGlbPrefix: path prefix for referencing exported .glb files,
//   e.g. "res://assets/shapes/" so the scene references
//   "res://assets/shapes/tree_01.glb".
// Returns true on success.
bool exportIG(const std::string &inputPath, const std::string &outputPath,
              const std::string &shapesGlbPrefix = "res://assets/shapes/");
