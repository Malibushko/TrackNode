target_sources(${TRACK_NODE_TARGET} PRIVATE
    telemetry_journal_service.cpp
    console_service.cpp
    command_parser.cpp
    track_node_control_protocol.cpp
    indicator_service.cpp
    control_button_service.cpp
    logger_service.cpp
    sensor_service.cpp
    board_state_service.cpp
    supervisor.cpp
    service_status.cpp
    diagnostic_log.cpp
    gnss_service.cpp
)

if(TRACK_NODE_ENABLE_LORA)
    target_sources(${TRACK_NODE_TARGET} PRIVATE radio_service.cpp)
endif()
if(TRACK_NODE_ENABLE_BLUETOOTH)
    target_sources(${TRACK_NODE_TARGET} PRIVATE bluetooth_service.cpp telemetry_history_protocol.cpp)
endif()

if(NOT NO_STORAGE)
    target_sources(${TRACK_NODE_TARGET} PRIVATE storage_service.cpp)
endif()
