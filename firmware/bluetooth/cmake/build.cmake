if(NOT WEST OR WEST MATCHES "-NOTFOUND$")
    message(FATAL_ERROR "Run CMake in the nRF Connect SDK toolchain environment or set TRACK_NODE_WEST_EXECUTABLE")
endif()

if(NOT EXISTS "${SDK_ROOT}/.west/config" OR NOT EXISTS "${SDK_ROOT}/zephyr/CMakeLists.txt")
    message(FATAL_ERROR "Set TRACK_NODE_BLUETOOTH_SDK_ROOT to an initialized nRF Connect SDK west workspace")
endif()

set(ENV{ZEPHYR_BASE} "${SDK_ROOT}/zephyr")
get_filename_component(west_directory "${WEST}" DIRECTORY)
get_filename_component(cmake_directory "${CMAKE_COMMAND}" DIRECTORY)
get_filename_component(ninja_directory "${NINJA}" DIRECTORY)

if(CMAKE_HOST_WIN32)
    set(path_separator ";")
else()
    set(path_separator ":")
endif()

set(ENV{PATH} "${west_directory}${path_separator}${cmake_directory}${path_separator}${ninja_directory}${path_separator}$ENV{PATH}")

if(GNUARMEMB_ROOT)
    if(NOT EXISTS "${GNUARMEMB_ROOT}/bin")
        message(FATAL_ERROR "TRACK_NODE_BLUETOOTH_GNUARMEMB_ROOT must contain the toolchain bin directory")
    endif()

    set(ENV{ZEPHYR_TOOLCHAIN_VARIANT} "gnuarmemb")
    set(ENV{GNUARMEMB_TOOLCHAIN_PATH} "${GNUARMEMB_ROOT}")
endif()

execute_process(
    COMMAND "${WEST}" build --no-sysbuild --board "${BOARD}" --build-dir "${BUILD_DIR}" "${FIRMWARE_DIR}"
    WORKING_DIRECTORY "${SDK_ROOT}"
    RESULT_VARIABLE build_result
)

if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Bluetooth firmware build failed: ${build_result}")
endif()
