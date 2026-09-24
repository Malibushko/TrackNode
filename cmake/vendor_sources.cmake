# Keep vendored source trees unmodified; list the selected upstream files here.
target_sources(track_node.elf PRIVATE
    "${PROJECT_SOURCE_DIR}/lib/FreeRTOS/croutine.c"
    "${PROJECT_SOURCE_DIR}/lib/FreeRTOS/event_groups.c"
    "${PROJECT_SOURCE_DIR}/lib/FreeRTOS/list.c"
    "${PROJECT_SOURCE_DIR}/lib/FreeRTOS/queue.c"
    "${PROJECT_SOURCE_DIR}/lib/FreeRTOS/stream_buffer.c"
    "${PROJECT_SOURCE_DIR}/lib/FreeRTOS/tasks.c"
    "${PROJECT_SOURCE_DIR}/lib/FreeRTOS/timers.c"
    "${PROJECT_SOURCE_DIR}/lib/FreeRTOS/portable/ARM_CM4F/port.c"
    "${PROJECT_SOURCE_DIR}/lib/FreeRTOS/portable/MemMang/heap_4.c"
    "${PROJECT_SOURCE_DIR}/lib/fatfs/source/ff.c"
    "${PROJECT_SOURCE_DIR}/lib/fatfs/source/ffsystem.c"
    "${PROJECT_SOURCE_DIR}/lib/fatfs/source/ffunicode.c"
)
