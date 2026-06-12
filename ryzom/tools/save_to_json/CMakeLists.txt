ADD_EXECUTABLE(save_to_json save_to_json.cpp)

TARGET_LINK_LIBRARIES(save_to_json
        PRIVATE
        Ryzom::gameshare
        NeL::misc
        NeL::net
        NeL::ligo
        NeL::georges
)

NL_DEFAULT_PROPS(save_to_json "Ryzom, Tools: Save To JSON (Task 4.2c)")
NL_ADD_RUNTIME_FLAGS(save_to_json)

INSTALL(TARGETS save_to_json RUNTIME DESTINATION ${RYZOM_BIN_PREFIX} COMPONENT tools)
