if(NOT NO_STORAGE)
    target_sources(${TRACK_NODE_TARGET} PRIVATE sd_diskio.cpp)
endif()
