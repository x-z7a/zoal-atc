if(NOT ATC_XPLANE_SDK)
    message(FATAL_ERROR
        "ZOAL_ATC_BUILD_PLUGIN=ON requires -DATC_XPLANE_SDK=/path/to/X-Plane-SDK"
    )
endif()

get_filename_component(ATC_XPLANE_SDK "${ATC_XPLANE_SDK}" ABSOLUTE)

set(_atc_xplm_header_candidates
    "${ATC_XPLANE_SDK}/CHeaders/XPLM/XPLMPlugin.h"
    "${ATC_XPLANE_SDK}/XPLM/XPLMPlugin.h"
    "${ATC_XPLANE_SDK}/XPLMPlugin.h"
)

set(ATC_XPLANE_INCLUDE_DIRS "")
foreach(_candidate IN LISTS _atc_xplm_header_candidates)
    if(EXISTS "${_candidate}")
        get_filename_component(_header_dir "${_candidate}" DIRECTORY)
        list(APPEND ATC_XPLANE_INCLUDE_DIRS "${_header_dir}")
    endif()
endforeach()

if(EXISTS "${ATC_XPLANE_SDK}/CHeaders/Widgets")
    list(APPEND ATC_XPLANE_INCLUDE_DIRS "${ATC_XPLANE_SDK}/CHeaders/Widgets")
endif()
if(EXISTS "${ATC_XPLANE_SDK}/XPWidgets")
    list(APPEND ATC_XPLANE_INCLUDE_DIRS "${ATC_XPLANE_SDK}/XPWidgets")
endif()

list(REMOVE_DUPLICATES ATC_XPLANE_INCLUDE_DIRS)

if(NOT ATC_XPLANE_INCLUDE_DIRS)
    message(FATAL_ERROR "Could not find XPLMPlugin.h under ${ATC_XPLANE_SDK}")
endif()

set(ATC_XPLANE_LIBRARIES "")

if(APPLE)
    set(_xplm_framework "${ATC_XPLANE_SDK}/Libraries/Mac/XPLM.framework/Versions/Current/XPLM")
    if(NOT EXISTS "${_xplm_framework}")
        message(FATAL_ERROR "Could not find macOS XPLM framework under ${ATC_XPLANE_SDK}/Libraries/Mac")
    endif()
    list(APPEND ATC_XPLANE_LIBRARIES "${_xplm_framework}")
elseif(WIN32)
    find_library(_xplm_win
        NAMES XPLM_64 XPLM
        PATHS "${ATC_XPLANE_SDK}/Libraries/Win"
        NO_DEFAULT_PATH
    )
    if(NOT _xplm_win)
        message(FATAL_ERROR "Could not find Windows XPLM library under ${ATC_XPLANE_SDK}/Libraries/Win")
    endif()
    list(APPEND ATC_XPLANE_LIBRARIES "${_xplm_win}")
elseif(UNIX)
    set(_xplm_linux "${ATC_XPLANE_SDK}/Libraries/Lin/XPLM_64.so")
    if(NOT EXISTS "${_xplm_linux}")
        message(FATAL_ERROR "Could not find Linux XPLM_64.so under ${ATC_XPLANE_SDK}/Libraries/Lin")
    endif()
    list(APPEND ATC_XPLANE_LIBRARIES "${_xplm_linux}")
else()
    message(FATAL_ERROR "Unsupported platform for X-Plane plugin build")
endif()
