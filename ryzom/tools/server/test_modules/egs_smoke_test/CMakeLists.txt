# EGS smoke test module (Phase 0.6)
# Extends the existing test_modules harness as required by the plan.
# This provides a ctest entry point. The real validation is:
#   1. Build with ryzom-modernize preset
#   2. Run the entities_game_service binary with -T (smoke) + --noBg + suitable minimal cfg
#   3. It should init, advance SmokeTestNumTicks (default 10), then exit(0) cleanly.
#
# For full integration (naming + egs): use docker compose -f docker-compose.dev.yml up nel-naming nel-egs
#   (override command for nel-egs to include -T)

add_test_executable(ryzom_egs_smoke_test
        egs_smoke_test.cpp
)

target_link_libraries(ryzom_egs_smoke_test
        PRIVATE
        NeL::misc
        NeL::net
)

# Also register a named test for easy ctest -R smoke
# The binary-driven smoke is invoked manually or via CI script because it
# requires the full built ryzom_entities_game_service + data.
add_test(NAME ryzom_egs_smoke_placeholder COMMAND ryzom_egs_smoke_test)
set_tests_properties(ryzom_egs_smoke_placeholder PROPERTIES
        LABELS "smoke;egs;phase0"
)