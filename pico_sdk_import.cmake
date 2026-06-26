# Minimal Pico SDK import helper for this project.
# Set PICO_SDK_PATH, or set PICO_SDK_FETCH_FROM_GIT=ON to fetch the SDK.

if (DEFINED ENV{PICO_SDK_PATH} AND (NOT PICO_SDK_PATH))
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif ()

if (DEFINED ENV{PICO_SDK_FETCH_FROM_GIT} AND (NOT PICO_SDK_FETCH_FROM_GIT))
    set(PICO_SDK_FETCH_FROM_GIT $ENV{PICO_SDK_FETCH_FROM_GIT})
endif ()

if (DEFINED ENV{PICO_SDK_FETCH_FROM_GIT_TAG} AND (NOT PICO_SDK_FETCH_FROM_GIT_TAG))
    set(PICO_SDK_FETCH_FROM_GIT_TAG $ENV{PICO_SDK_FETCH_FROM_GIT_TAG})
endif ()

if (PICO_SDK_FETCH_FROM_GIT AND NOT PICO_SDK_FETCH_FROM_GIT_TAG)
    set(PICO_SDK_FETCH_FROM_GIT_TAG "2.2.0")
endif ()

set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "Path to the Raspberry Pi Pico SDK")
set(PICO_SDK_FETCH_FROM_GIT "${PICO_SDK_FETCH_FROM_GIT}" CACHE BOOL "Fetch SDK from git when PICO_SDK_PATH is not set")
set(PICO_SDK_FETCH_FROM_GIT_TAG "${PICO_SDK_FETCH_FROM_GIT_TAG}" CACHE STRING "Pico SDK git ref")

if (NOT PICO_SDK_PATH)
    if (PICO_SDK_FETCH_FROM_GIT)
        include(FetchContent)
        FetchContent_Declare(
            pico_sdk
            GIT_REPOSITORY https://github.com/raspberrypi/pico-sdk.git
            GIT_TAG ${PICO_SDK_FETCH_FROM_GIT_TAG}
        )
        FetchContent_MakeAvailable(pico_sdk)
        set(PICO_SDK_PATH ${pico_sdk_SOURCE_DIR})
    else ()
        message(FATAL_ERROR "Set PICO_SDK_PATH or PICO_SDK_FETCH_FROM_GIT=ON")
    endif ()
endif ()

get_filename_component(PICO_SDK_PATH "${PICO_SDK_PATH}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
set(PICO_SDK_INIT_CMAKE_FILE ${PICO_SDK_PATH}/pico_sdk_init.cmake)
if (NOT EXISTS ${PICO_SDK_INIT_CMAKE_FILE})
    message(FATAL_ERROR "PICO_SDK_PATH='${PICO_SDK_PATH}' does not contain pico_sdk_init.cmake")
endif ()

set(PICO_SDK_PATH ${PICO_SDK_PATH} CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)
include(${PICO_SDK_INIT_CMAKE_FILE})
