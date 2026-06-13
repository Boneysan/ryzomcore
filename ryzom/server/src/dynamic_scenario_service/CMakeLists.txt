FILE(GLOB SRC *.cpp *.h)

LIST(REMOVE_ITEM SRC ${CMAKE_CURRENT_SOURCE_DIR}/service.cpp ${CMAKE_CURRENT_SOURCE_DIR}/service.h)

ADD_EXECUTABLE(ryzom_dynamic_scenario_service WIN32 ${SRC})

TARGET_INCLUDE_DIRECTORIES(ryzom_dynamic_scenario_service PRIVATE ${RZ_SERVER_SRC_DIR})

TARGET_LINK_LIBRARIES(ryzom_dynamic_scenario_service
        PRIVATE
        Ryzom::adminmodules
        Ryzom::gameshare
        Ryzom::servershare
        Ryzom::aishare
        NeL::misc
        NeL::net
        NeL::georges
        NeL::ligo
)

# Pull in EGS Lua engine logic
TARGET_SOURCES(ryzom_dynamic_scenario_service PRIVATE
        ${RZ_SERVER_SRC_DIR}/entities_game_service/egs_script/egs_lua.cpp)

TARGET_INCLUDE_DIRECTORIES(ryzom_dynamic_scenario_service PRIVATE
        ${RZ_SERVER_SRC_DIR}/entities_game_service)

FIND_PACKAGE(PostgreSQL QUIET)
IF(PostgreSQL_FOUND)
        TARGET_COMPILE_DEFINITIONS(ryzom_dynamic_scenario_service PRIVATE EGS_HAVE_PGSQL)
        TARGET_INCLUDE_DIRECTORIES(ryzom_dynamic_scenario_service PRIVATE ${PostgreSQL_INCLUDE_DIRS})
        TARGET_LINK_LIBRARIES(ryzom_dynamic_scenario_service PRIVATE ${PostgreSQL_LIBRARIES})
        MESSAGE(STATUS "DSS: PostgreSQL persistence enabled")
ENDIF()

FIND_PACKAGE(Lua QUIET)
IF(LUA_FOUND)
        TARGET_COMPILE_DEFINITIONS(ryzom_dynamic_scenario_service PRIVATE EGS_HAVE_LUA)
        TARGET_INCLUDE_DIRECTORIES(ryzom_dynamic_scenario_service PRIVATE ${LUA_INCLUDE_DIR})
        TARGET_LINK_LIBRARIES(ryzom_dynamic_scenario_service PRIVATE ${LUA_LIBRARIES})
        MESSAGE(STATUS "DSS: Lua scripting enabled")
ENDIF()

NL_DEFAULT_PROPS(ryzom_dynamic_scenario_service "Ryzom, Services: Dynamic Scenario Service (DSS)")
NL_ADD_RUNTIME_FLAGS(ryzom_dynamic_scenario_service)

INSTALL(TARGETS ryzom_dynamic_scenario_service RUNTIME DESTINATION sbin COMPONENT services)
