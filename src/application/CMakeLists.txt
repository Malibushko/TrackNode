target_sources(${TRACK_NODE_TARGET} PRIVATE
    node_controller.cpp
	radio_packet.cpp
)

if(NOT NO_STORAGE)
    target_sources(${TRACK_NODE_TARGET} PRIVATE log_record.cpp)
endif()
