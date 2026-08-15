# Locate the Skyscript library bundle the plugin links against.
#
# Skyscript is not optional: the in-sim panel is the ATC GUI, so a plugin build
# that cannot find it must fail here rather than quietly produce a plugin with
# no GUI.
#
# There is deliberately no CEF check. Since Skyscript v0.5.0 the library loads
# the CEF runtime that ships with X-Plane, so no CEF tree is distributed with
# the library bundle and none is packaged beside the plugin. Requiring one is
# what used to reject a bundle that was correct.

if(NOT ZOAL_ATC_SKYSCRIPT_ROOT)
    message(FATAL_ERROR "ZOAL_ATC_SKYSCRIPT_ROOT is unset. Run `make -C atc skyscript-lib` (or scripts/ensure-skyscript-lib.sh) to provision the pinned Skyscript release.")
endif()

if(APPLE)
    set(ZOAL_ATC_SKYSCRIPT_PLATFORM "mac_x64")
elseif(WIN32)
    set(ZOAL_ATC_SKYSCRIPT_PLATFORM "win_x64")
elseif(UNIX)
    set(ZOAL_ATC_SKYSCRIPT_PLATFORM "lin_x64")
else()
    message(FATAL_ERROR "Unsupported platform for Skyscript.")
endif()

unset(ZOAL_ATC_SKYSCRIPT_INCLUDE_DIR)
unset(ZOAL_ATC_SKYSCRIPT_INCLUDE_DIR CACHE)
unset(ZOAL_ATC_SKYSCRIPT_LIBRARY)
unset(ZOAL_ATC_SKYSCRIPT_LIBRARY CACHE)
unset(ZOAL_ATC_SKYSCRIPT_ASSETS_DIR)
unset(ZOAL_ATC_SKYSCRIPT_ASSETS_DIR CACHE)

find_path(ZOAL_ATC_SKYSCRIPT_INCLUDE_DIR
    NAMES skyscript_c.h
    PATHS
        "${ZOAL_ATC_SKYSCRIPT_ROOT}/include"
        "${ZOAL_ATC_SKYSCRIPT_ROOT}/src"
    NO_DEFAULT_PATH
    NO_CACHE
)

if(NOT ZOAL_ATC_SKYSCRIPT_INCLUDE_DIR)
    message(FATAL_ERROR "skyscript_c.h not found under ${ZOAL_ATC_SKYSCRIPT_ROOT}. Run `make -C atc skyscript-lib` to provision the pinned Skyscript release.")
endif()

# On Windows the plugin links against the import library; the DLL is what ships.
if(WIN32)
    set(ZOAL_ATC_SKYSCRIPT_LIBRARY_NAMES SkyScriptLib.lib SkyScriptLib)
else()
    set(ZOAL_ATC_SKYSCRIPT_LIBRARY_NAMES SkyScriptLib libSkyScriptLib)
endif()

find_library(ZOAL_ATC_SKYSCRIPT_LIBRARY
    NAMES ${ZOAL_ATC_SKYSCRIPT_LIBRARY_NAMES}
    PATHS
        "${ZOAL_ATC_SKYSCRIPT_ROOT}/lib/${ZOAL_ATC_SKYSCRIPT_PLATFORM}"
    NO_DEFAULT_PATH
    NO_CACHE
)

if(NOT ZOAL_ATC_SKYSCRIPT_LIBRARY)
    message(FATAL_ERROR "SkyScriptLib not found under ${ZOAL_ATC_SKYSCRIPT_ROOT}/lib/${ZOAL_ATC_SKYSCRIPT_PLATFORM}. Run `make -C atc skyscript-lib` to provision the pinned Skyscript release.")
endif()

find_path(ZOAL_ATC_SKYSCRIPT_ASSETS_DIR
    NAMES icons/x-circle.svg
    PATHS
        "${ZOAL_ATC_SKYSCRIPT_ROOT}/assets"
    NO_DEFAULT_PATH
    NO_CACHE
)

if(NOT ZOAL_ATC_SKYSCRIPT_ASSETS_DIR)
    message(FATAL_ERROR "Skyscript assets not found under ${ZOAL_ATC_SKYSCRIPT_ROOT}/assets. Run `make -C atc skyscript-lib` to provision the pinned Skyscript release.")
endif()

if(EXISTS "${ZOAL_ATC_SKYSCRIPT_ROOT}/SKYSCRIPT_VERSION")
    file(READ "${ZOAL_ATC_SKYSCRIPT_ROOT}/SKYSCRIPT_VERSION" ZOAL_ATC_SKYSCRIPT_VERSION)
    string(STRIP "${ZOAL_ATC_SKYSCRIPT_VERSION}" ZOAL_ATC_SKYSCRIPT_VERSION)
    message(STATUS "Skyscript version: ${ZOAL_ATC_SKYSCRIPT_VERSION}")
endif()

message(STATUS "Skyscript include: ${ZOAL_ATC_SKYSCRIPT_INCLUDE_DIR}")
message(STATUS "Skyscript library: ${ZOAL_ATC_SKYSCRIPT_LIBRARY}")
message(STATUS "Skyscript assets: ${ZOAL_ATC_SKYSCRIPT_ASSETS_DIR}")
message(STATUS "Skyscript CEF: provided by X-Plane at runtime")
