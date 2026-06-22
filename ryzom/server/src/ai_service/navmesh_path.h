#ifndef NAVMESH_PATH_H
#define NAVMESH_PATH_H

// Task 6.2 (Phase 6 NPC AI): hand-rolled NATS request/reply client to the
// Go pathfinding-api service, modeled on
// entities_game_service/egs_sheets/egs_sheet_nats.cpp's raw-socket style
// (same hand-rolled protocol parsing, no new C++ NATS library).
//
// Unlike egs_sheet_nats's long-lived subscriber thread, this is a
// synchronous request/reply call: it opens a connection, publishes one
// request on ai.pathfind.request with a unique reply-to inbox, waits
// (bounded by a timeout) for the MSG reply, then closes the connection.
// That is a real, known limitation for high call volumes — see
// PROGRESS.md Task 6.2 — but is the right scope for this vertical slice:
// correctness first, a pooled/async client is a follow-on once NPC AI
// actually calls this on a hot path.

#include "nel/misc/vector.h"
#include <string>
#include <vector>

namespace NavmeshPath
{
	// Issues a synchronous pathfind request to pathfinding-api. Returns
	// true and fills outPath (in mesh/world space, matching the navmesh
	// bake) on success. On failure (no NATS connection, no navmesh for
	// `zone`, no path found, timeout, ...) returns false and fills
	// errorMsg with a human-readable reason.
	bool findPath(const std::string &zone, const NLMISC::CVector &start, const NLMISC::CVector &end,
		std::vector<NLMISC::CVector> &outPath, std::string &errorMsg);
}

#endif // NAVMESH_PATH_H
