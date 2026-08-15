set(MINIAUDIO_VERSION "0.11.22" CACHE STRING "miniaudio version")
set(MINIAUDIO_URL "https://raw.githubusercontent.com/mackron/miniaudio/${MINIAUDIO_VERSION}/miniaudio.h" CACHE STRING "miniaudio header URL")
set(MINIAUDIO_SHA256 "9019743287e443c55e5737a7297f38e5e358561701d6db2d905afb114390c410" CACHE STRING "miniaudio header sha256")
set(MINIAUDIO_DIR "${CMAKE_CURRENT_LIST_DIR}/../vendor/miniaudio" CACHE PATH "miniaudio vendor directory")
set(MINIAUDIO_HEADER "${MINIAUDIO_DIR}/miniaudio.h")

file(MAKE_DIRECTORY "${MINIAUDIO_DIR}")
file(DOWNLOAD
    "${MINIAUDIO_URL}"
    "${MINIAUDIO_HEADER}"
    EXPECTED_HASH "SHA256=${MINIAUDIO_SHA256}"
    SHOW_PROGRESS
    STATUS download_status
)

list(GET download_status 0 status_code)
list(GET download_status 1 status_message)
if(NOT status_code EQUAL 0)
    file(REMOVE "${MINIAUDIO_HEADER}")
    message(FATAL_ERROR "Failed to download miniaudio: ${status_message}")
endif()

message(STATUS "miniaudio ready at ${MINIAUDIO_HEADER}")
