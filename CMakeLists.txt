cmake_minimum_required(VERSION 3.24)

project(zoal_atc
    VERSION 0.1.0
    DESCRIPTION "zoal-atc X-Plane edge plugin"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ZOAL_ATC_BUILD_TESTS "Build SDK-free unit tests" ON)
option(ZOAL_ATC_BUILD_PLUGIN "Build the X-Plane plugin module" OFF)
set(ATC_XPLANE_SDK "${CMAKE_CURRENT_SOURCE_DIR}/sdk" CACHE PATH "Path to the X-Plane SDK root")
set(ZOAL_ATC_SKYSCRIPT_ROOT "" CACHE PATH "Path to the Skyscript library bundle (see scripts/ensure-skyscript-lib.sh)")

# Skyscript is not optional. The in-sim panel is the ATC GUI, so a plugin
# without it is not a lighter build, it is a broken one - and a configuration we
# never build is a configuration we never test. The SDK-free core stays
# Skyscript-free and needs none of this.

find_package(Threads REQUIRED)

add_library(zoal_atc_core STATIC
    src/gui/gui_bridge.cpp
    src/gui/gui_frames.cpp
    src/gui/notification.cpp
    src/gui/panel_paths.cpp
    src/navdata/md5.cpp
    src/navdata/navdata_service.cpp
    src/ptt/noop_audio_recorder.cpp
    src/ptt/push_to_talk_controller.cpp
    src/telemetry/telemetry_frame.cpp
    src/telemetry/traffic.cpp
    src/transport/async_websocket_ptt_sink.cpp
    src/transport/base64.cpp
    src/transport/endpoint_config.cpp
    src/transport/json_util.cpp
    src/transport/speech_text.cpp
    src/transport/websocket_client.cpp
)

target_include_directories(zoal_atc_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(zoal_atc_core PUBLIC cxx_std_17)
set_target_properties(zoal_atc_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(zoal_atc_core PUBLIC Threads::Threads)

if(WIN32)
    target_link_libraries(zoal_atc_core PUBLIC ws2_32)
endif()

if(MSVC)
    target_compile_options(zoal_atc_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(zoal_atc_core PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
    )
endif()

if(ZOAL_ATC_BUILD_PLUGIN)
    include(cmake/XPlaneSDK.cmake)
    include(cmake/SkyScript.cmake)

    set(ATC_MINIAUDIO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/miniaudio" CACHE PATH "Path containing miniaudio.h")
    if(NOT EXISTS "${ATC_MINIAUDIO_INCLUDE_DIR}/miniaudio.h")
        message(FATAL_ERROR "miniaudio.h not found. Run `make -C atc setup` first.")
    endif()

    add_library(zoal_atc MODULE
        src/ptt/miniaudio_audio_recorder.cpp
        src/xplane/skyscript_panel.cpp
        src/xplane/xplane_plugin.cpp
    )

    target_link_libraries(zoal_atc PRIVATE zoal_atc_core)
    target_include_directories(zoal_atc PRIVATE
        ${ATC_XPLANE_INCLUDE_DIRS}
        ${ATC_MINIAUDIO_INCLUDE_DIR}
    )
    target_compile_definitions(zoal_atc PRIVATE
        XPLM200=1
        XPLM210=1
        XPLM300=1
        XPLM301=1
        XPLM400=1
        ZOAL_ATC_VERSION="${PROJECT_VERSION}"
    )

    if(APPLE)
        target_compile_definitions(zoal_atc PRIVATE APL=1 IBM=0 LIN=0)
    elseif(WIN32)
        target_compile_definitions(zoal_atc PRIVATE APL=0 IBM=1 LIN=0)
    elseif(UNIX)
        target_compile_definitions(zoal_atc PRIVATE APL=0 IBM=0 LIN=1)
    endif()

    if(MSVC)
        target_compile_options(zoal_atc PRIVATE /W4 /permissive-)
    else()
        target_compile_options(zoal_atc PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wno-unused-parameter
            -fvisibility=hidden
        )
    endif()

    target_link_libraries(zoal_atc PRIVATE ${ATC_XPLANE_LIBRARIES})

    target_include_directories(zoal_atc PRIVATE ${ZOAL_ATC_SKYSCRIPT_INCLUDE_DIR})
    target_link_libraries(zoal_atc PRIVATE ${ZOAL_ATC_SKYSCRIPT_LIBRARY})
    # The Skyscript library ships beside the .xpl. CEF is not ours to find: the
    # library resolves it against X-Plane's own install.
    if(APPLE)
        set_target_properties(zoal_atc PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            BUILD_RPATH "@loader_path"
            INSTALL_RPATH "@loader_path"
        )
    elseif(UNIX)
        set_target_properties(zoal_atc PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            BUILD_RPATH "\$ORIGIN"
            INSTALL_RPATH "\$ORIGIN"
        )
    endif()

    # X-Plane loads the plugin directly, so a MinGW build must not require the
    # MinGW runtime DLLs to be installed beside the .xpl.
    if(WIN32 AND MINGW)
        target_link_options(zoal_atc PRIVATE
            -static-libgcc
            -static-libstdc++
            -static
        )
    endif()

    if(APPLE)
        target_link_libraries(zoal_atc PRIVATE
            "-framework CoreAudio"
            "-framework CoreFoundation"
            "-framework AudioToolbox"
            "-framework AudioUnit"
        )
    elseif(WIN32)
        target_link_libraries(zoal_atc PRIVATE ole32 uuid winmm)
    elseif(UNIX)
        target_link_libraries(zoal_atc PRIVATE pthread dl m)
    endif()

    set_target_properties(zoal_atc PROPERTIES
        OUTPUT_NAME "zoal_atc"
        PREFIX ""
        SUFFIX ".xpl"
    )
endif()

if(ZOAL_ATC_BUILD_TESTS)
    enable_testing()

    function(zoal_atc_add_test name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE zoal_atc_core)
        if(MSVC)
            target_compile_options(${name} PRIVATE /W4 /permissive-)
        else()
            target_compile_options(${name} PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion
            )
        endif()
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    zoal_atc_add_test(zoal_atc_tests tests/push_to_talk_controller_test.cpp)
    zoal_atc_add_test(zoal_atc_telemetry_tests tests/telemetry_frame_test.cpp)
    zoal_atc_add_test(zoal_atc_traffic_tests tests/traffic_test.cpp)
    zoal_atc_add_test(zoal_atc_navdata_tests tests/navdata_service_test.cpp)
    zoal_atc_add_test(zoal_atc_speech_text_tests tests/speech_text_test.cpp)
    zoal_atc_add_test(zoal_atc_endpoint_config_tests tests/endpoint_config_test.cpp)
    zoal_atc_add_test(zoal_atc_reconnect_gate_tests tests/reconnect_gate_test.cpp)
    zoal_atc_add_test(zoal_atc_bounded_queue_tests tests/bounded_queue_test.cpp)
    zoal_atc_add_test(zoal_atc_json_util_tests tests/json_util_test.cpp)
    zoal_atc_add_test(zoal_atc_gui_panel_path_tests tests/gui_panel_paths_test.cpp)
    zoal_atc_add_test(zoal_atc_gui_frames_tests tests/gui_frames_test.cpp)
    zoal_atc_add_test(zoal_atc_gui_bridge_tests tests/gui_bridge_test.cpp)
    zoal_atc_add_test(zoal_atc_gui_notification_tests tests/gui_notification_test.cpp)
endif()
