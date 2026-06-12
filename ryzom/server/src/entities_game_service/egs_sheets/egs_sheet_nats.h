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

#ifndef EGS_SHEET_NATS_H
#define EGS_SHEET_NATS_H

// Phase 4.2b: optional NATS sheet invalidation subscriber.
//
// The socket thread only receives NATS messages and queues sheet IDs. The
// The EGS service update path calls serviceSheetNatsInvalidations(), which
// applies the PostgreSQL overlay to the loaded CSheets maps.

void startSheetNatsInvalidationThread();
void serviceSheetNatsInvalidations();
void stopSheetNatsInvalidationThread();

#endif
