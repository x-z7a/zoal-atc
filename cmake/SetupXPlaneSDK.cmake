cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED ATC_XPLANE_SDK OR ATC_XPLANE_SDK STREQUAL "")
    get_filename_component(_atc_dir "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    set(ATC_XPLANE_SDK "${_atc_dir}/sdk")
endif()

if(NOT DEFINED SDK_VERSION OR SDK_VERSION STREQUAL "")
    set(SDK_VERSION "440-b5")
endif()

if(NOT DEFINED SDK_ZIP_URL OR SDK_ZIP_URL STREQUAL "")
    set(SDK_ZIP_URL "https://www.x-plane.com/wp-content/uploads/2026/10/XPSDK440-b5.zip")
endif()

get_filename_component(ATC_XPLANE_SDK "${ATC_XPLANE_SDK}" ABSOLUTE)
# A 4.4-only header: the HUD needs SDK 4.4.0's panel-graphics ImGui helpers,
# so an older SDK already in the tree is replaced rather than accepted.
set(_sentinel "${ATC_XPLANE_SDK}/XPLM/XPLMPanelGraphics.h")

if(EXISTS "${_sentinel}")
    message(STATUS "X-Plane SDK already present at ${ATC_XPLANE_SDK}")
    return()
endif()

message(STATUS "Downloading X-Plane SDK ${SDK_VERSION} from ${SDK_ZIP_URL}")

set(_tmp_root "${CMAKE_BINARY_DIR}/_zoal_atc_xpsdk")
set(_zip_path "${_tmp_root}/xpsdk.zip")
set(_extract_dir "${_tmp_root}/extracted")

file(REMOVE_RECURSE "${_tmp_root}")
file(MAKE_DIRECTORY "${_tmp_root}" "${_extract_dir}")

file(DOWNLOAD
    "${SDK_ZIP_URL}"
    "${_zip_path}"
    STATUS _download_status
    SHOW_PROGRESS
    TLS_VERIFY ON
)

list(GET _download_status 0 _download_code)
list(GET _download_status 1 _download_message)
if(NOT _download_code EQUAL 0)
    message(FATAL_ERROR "Failed to download X-Plane SDK: ${_download_message}")
endif()

file(ARCHIVE_EXTRACT INPUT "${_zip_path}" DESTINATION "${_extract_dir}")

file(MAKE_DIRECTORY
    "${ATC_XPLANE_SDK}/XPLM"
    "${ATC_XPLANE_SDK}/XPWidgets"
    "${ATC_XPLANE_SDK}/Libraries/Win"
    "${ATC_XPLANE_SDK}/Libraries/Mac"
    "${ATC_XPLANE_SDK}/Libraries/Lin"
)

file(GLOB_RECURSE _xplm_headers
    LIST_DIRECTORIES false
    "${_extract_dir}/*/CHeaders/XPLM/*.h"
)
file(GLOB_RECURSE _widget_headers
    LIST_DIRECTORIES false
    "${_extract_dir}/*/CHeaders/Widgets/*.h"
)
file(GLOB_RECURSE _win_libs
    LIST_DIRECTORIES false
    "${_extract_dir}/*/Libraries/Win/*.lib"
)
file(GLOB_RECURSE _linux_libs
    LIST_DIRECTORIES false
    "${_extract_dir}/*/Libraries/Lin/*.so"
)
file(GLOB _mac_frameworks
    LIST_DIRECTORIES true
    "${_extract_dir}/*/Libraries/Mac/*.framework"
)

foreach(_file IN LISTS _xplm_headers)
    file(COPY "${_file}" DESTINATION "${ATC_XPLANE_SDK}/XPLM")
endforeach()
foreach(_file IN LISTS _widget_headers)
    file(COPY "${_file}" DESTINATION "${ATC_XPLANE_SDK}/XPWidgets")
endforeach()
foreach(_file IN LISTS _win_libs)
    file(COPY "${_file}" DESTINATION "${ATC_XPLANE_SDK}/Libraries/Win")
endforeach()
foreach(_file IN LISTS _linux_libs)
    file(COPY "${_file}" DESTINATION "${ATC_XPLANE_SDK}/Libraries/Lin")
endforeach()
foreach(_framework IN LISTS _mac_frameworks)
    if(IS_DIRECTORY "${_framework}")
        file(COPY "${_framework}" DESTINATION "${ATC_XPLANE_SDK}/Libraries/Mac")
    endif()
endforeach()

file(REMOVE_RECURSE "${_tmp_root}")

if(NOT EXISTS "${_sentinel}")
    message(FATAL_ERROR "X-Plane SDK setup finished without ${_sentinel}")
endif()

message(STATUS "X-Plane SDK installed at ${ATC_XPLANE_SDK}")
