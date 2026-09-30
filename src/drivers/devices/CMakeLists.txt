target_sources(${TRACK_NODE_TARGET} PRIVATE
    bmp280.cpp
    neo6m.cpp
    sx126x.cpp
)

if(NOT NO_STORAGE)
    target_sources(${TRACK_NODE_TARGET} PRIVATE sd_card.cpp)
endif()
