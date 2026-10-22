target_sources(${TRACK_NODE_TARGET} PRIVATE
    bmp280.cpp
    neo6m.cpp
    w25qxx.cpp
)

if(TRACK_NODE_ENABLE_LORA)
    target_sources(${TRACK_NODE_TARGET} PRIVATE sx126x.cpp)
endif()
if(TRACK_NODE_ENABLE_BLUETOOTH)
    target_sources(${TRACK_NODE_TARGET} PRIVATE xiao_ble_bridge.cpp)
endif()

if(NOT NO_STORAGE)
    target_sources(${TRACK_NODE_TARGET} PRIVATE sd_card.cpp)
endif()
