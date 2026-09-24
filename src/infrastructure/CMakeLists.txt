target_sources(track_node.elf PRIVATE
    console_service.cpp
    command_parser.cpp
    track_node_control_protocol.cpp
    indicator_service.cpp
    logger_service.cpp
    sensor_service.cpp
    storage_service.cpp
    supervisor.cpp
)
