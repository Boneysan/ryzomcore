# Ryzom Modernization

This project is being modernized for a private GM-directed co-op campaign experience.

**Planning documents live in the sibling directory:**
`../Ryzome_Modernization_Plan/`

## Quick links (from repo root)

- Entry point: `Ryzome_Modernization_Plan/README.md`
- Current status: `Ryzome_Modernization_Plan/PROGRESS.md`
- Phase 0 (current focus): `Ryzome_Modernization_Plan/Phase_Plans/Phase_0_Foundation_Hardening.md`
- Agent instructions: `Ryzome_Modernization_Plan/AGENTS.md` (and CLAUDE.md)
- ADRs: `Ryzome_Modernization_Plan/Direction/ADRs/`
- Dev Runbook + docker-compose template: `Ryzome_Modernization_Plan/Operations/`

## Current phase prerequisites (do not skip)
1. Check PROGRESS.md checkboxes.
2. Read the Phase Plan fully.
3. Read relevant Code Investigation Checklist findings.
4. Run `cmake --preset ryzom-modernize` (added in Phase 0.7 scaffolding).

## First concrete steps completed (setup)
- C++17 enforced at root (CMakeLists.txt + nel.cmake updated).
- `ryzom-modernize` CMake preset added.
- `cpp-httplib` added to vcpkg.json.
- `docker-compose.dev.yml` placed at workspace root (copy of plan template).
- `Dockerfile.dev` stub added in ryzomcore/ for compose targets.
- Modernization-focused GitHub workflow added: `.github/workflows/modernization.yml`.
- See also the main `.github/workflows/ci.yml` (pre-existing, more comprehensive for regular builds).

Until the 6 MVP gates in PROGRESS.md are *all* green at the same time, the private campaign MVP is not shipped.

Start with Phase 0 only. Use the integration smoke test (Task 0.6) as the validator for all refactors.

## Party frontend routing notes

The co-op shard split now has an EGS/Lua route lifecycle for per-party
frontends:

- GM commands in `gm_commands.lua` handle `set_party_frontend`,
  `set_instance_frontend`, `assign_party_instance`, and
  `clear_party_instance`.
- `party_mechanics.lua` tracks party membership, instance frontend addresses,
  party-to-instance assignments, and the active party frontend route.
- `egs.registerPartyFrontend(party_id, addr)` publishes `gm.party.route` to NATS
  for the Go proxy. An empty `addr` clears the route.
- EGS calls `party_mechanics.on_character_instance_changed(char_id,
  instance_id)` when AIS assigns a character to an instance, so party routes can
  follow instance migration.

For container runs, `docker/run_shard_container.sh` can spawn extra frontend
services and publish their party routes on startup:

```bash
S1_PARTY_FRONTENDS='party1=47916,party2=47917' docker compose up nel-shard
```

`S1_PARTY_FRONTENDS` entries are `party_id=port` or `party_id=host:port`.
Optional overrides:

- `S1_BIND_HOST` controls the frontend listen bind, default `0.0.0.0`.
- `S1_ROUTE_HOST` controls the host advertised to the proxy when only a port is
  supplied, default `nel-shard`.
- NATS discovery uses `EGS_DSS_NATS_URL`, then `EGS_SHEET_NATS_URL`, then
  `NATS_URL`; set any of them to `disabled` to skip publishing in local runs.

## How to build (0.1) + run smoke (0.6) right now (after the scaffolding we performed)

1. Ensure prerequisites (see Operations/Dev_Runbook.md in the plan dir): recent cmake, vcpkg, clang or gcc>=9, etc. Git LFS for data if needed for sheets.

2. Configure with the new preset (this is what enforces C++17 + MSQUIC etc):
   ```
   cd ryzomcore
   cmake --preset ryzom-modernize -DWITH_MSQUIC=ON -DCMAKE_BUILD_TYPE=Debug
   ```

3. Build (this will surface any remaining C++17 issues after our auto_ptr / CUniquePtr fixes):
   ```
   cmake --build build/ --parallel $(nproc)   # or build/ryzom depending on preset output
   ```
   Fix errors in passes (headers, syntax). We pre-fixed the defines in nel/include/nel/misc/types_nl.h and direct std::auto_ptr in 4 files. Re-run build as needed.

4. Run the smoke test placeholder (provides the ctest entry):
   ```
   ctest --test-dir build/ -R "smoke|egs_smoke" --output-on-failure
   ```
   (The real EGS smoke is the binary.)

5. Run the actual EGS smoke (headless 10 ticks):
   - You need a minimal config + sheet data (use data from ryzomcore_leveldesign or the sabrina_test/ example .sitem files by setting paths).
   - Example (adjust paths):
     ```
     cd build   # where the exe is (may need to find ryzom_entities_game_service)
     ./ryzom_entities_game_service --noBg -T
     ```
   - Expected: lots of "Smoke tick X / 10", then "EGS smoke test SUCCESS: ...", clean exit 0. No crash, good for ASAN.

6. For more complete (with naming):
   Copy docker-compose.dev.yml if not at root, adjust, `docker compose -f docker-compose.dev.yml up --build nel-naming nel-egs` (the compose has override example for -T on egs).

After local success, commit the fixes + update PROGRESS checkboxes.

We also added egs_smoke_test/ module (extends the harness) + -T support in EGS + updates to compose, modernization CI workflow, etc.
