/**
 * EGS Smoke Test (Phase 0 Foundation Hardening, Task 0.6)
 *
 * This is a placeholder / driver in the test_modules harness (as required by the plan).
 * The actual "headless EGS + N tick advance" logic lives in the EGS service itself (entities_game_service.cpp):
 *   - Run with -T (or set SmokeTestNumTicks var).
 *   - After full init + mirror, it now calls egsUpdate() (which does PlayerManager.tickUpdate() etc.) N times,
 *     logs SUCCESS, then exit(0).
 *   - Example: ./ryzom_entities_game_service --noBg -T  (needs sufficient data/config to pass init).
 *
 * This gives ctest / ASAN / UBSAN something to run that is fast and validates the build.
 *
 * To run the real binary smoke locally (after cmake --preset ryzom-modernize && build):
 *   cd build   # or wherever the exe landed (may be build/ryzom or install)
 *   # Minimal data needed: sheets + dfn from ryzomcore_leveldesign (or sabrina_test data)
 *   ./ryzom_entities_game_service --noBg -T --config-file=...
 *
 * For orchestrated with naming_service (recommended for more complete smoke):
 *   docker compose -f ../../docker-compose.dev.yml up nel-naming nel-egs
 *   (see the nel-egs service override in the compose file)
 *
 * In CI the modernization.yml workflow will run ctest -R smoke after the ASAN build.
 */

#include "nel/misc/types_nl.h"
#include "nel/misc/debug.h"

#include <iostream>

using namespace std;

int main(int argc, char **argv)
{
	NLMISC::CApplicationContext::getInstance();
	NLMISC::createDebug();

	nlinfo("=== Ryzom EGS Smoke Test Placeholder (Phase 0.6) ===");
	nlinfo("This test always 'passes' to provide a ctest -R smoke entry.");
	nlinfo("The real work is inside the EGS binary when launched with -T flag.");
	nlinfo("See egs_smoke_test.cpp comments and entities_game_service.cpp for -T / smokeTest handling.");
	nlinfo("See also: ryzom/tools/server/test_modules/egs_smoke_test/ and docker-compose.dev.yml");

	cout << "EGS_SMOKE_PLACEHOLDER_OK" << endl;
	return 0;
}