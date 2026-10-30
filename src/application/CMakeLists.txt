target_sources(${TRACK_NODE_TARGET} PRIVATE
    node_controller.cpp
    log_record.cpp
)

if(TRACK_NODE_ENABLE_LORA)
    target_sources(${TRACK_NODE_TARGET} PRIVATE radio_packet.cpp)
endif()
