// NeL - MMORPG Framework <http://dev.ryzom.com/projects/nel/>
// Copyright (C) 2010  Winch Gate Property Limited
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.

#pragma once

#include <string>

// Export a .shape file (any supported type) to a .glb file.
// If a matching .anim file exists alongside the input, it is embedded.
// Returns true on success.
bool exportShape(const std::string &inputPath, const std::string &outputPath);
