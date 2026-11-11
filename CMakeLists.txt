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
# The plugin hears the pilot with whisper.cpp and speaks the controllers with
# Kokoro through sherpa-onnx, on every platform. Neither is optional: the
# console no longer transcribes audio, and nothing else in the cockpit speaks,
# so a plugin without either is one the pilot cannot use.
set(ZOAL_ATC_WHISPER_SOURCE "" CACHE PATH "whisper.cpp source tree (see scripts/ensure-whisper-src.sh)")
set(ZOAL_ATC_SHERPA_ONNX "" CACHE PATH "sherpa-onnx build (see scripts/ensure-sherpa-onnx.sh)")
set(ATC_XPLANE_SDK "${CMAKE_CURRENT_SOURCE_DIR}/sdk" CACHE PATH "Path to the X-Plane SDK root")
set(ZOAL_ATC_IMGUI_SOURCE "" CACHE PATH "Dear ImGui source tree (see scripts/ensure-imgui.sh)")
set(ZOAL_ATC_FREETYPE_SOURCE "" CACHE PATH "FreeType source tree (see scripts/ensure-freetype.sh)")
set(ZOAL_ATC_SKYSCRIPT_ROOT "" CACHE PATH "Path to the Skyscript library bundle (see scripts/ensure-skyscript-lib.sh)")

# Skyscript is not optional. The in-sim panel is the ATC GUI, so a plugin
# without it is not a lighter build, it is a broken one - and a configuration we
# never build is a configuration we never test. The SDK-free core stays
# Skyscript-free and needs none of this.

find_package(Threads REQUIRED)

add_library(zoal_atc_core STATIC
    src/gui/gui_bridge.cpp
    src/gui/hud_layout.cpp
    src/gui/hud_model.cpp
    src/gui/hud_session.cpp
    src/gui/panel_theme.cpp
    src/gui/font_metrics.cpp
    src/gui/gui_frames.cpp
    src/gui/panel_paths.cpp
    src/ptt/noop_audio_recorder.cpp
    src/ptt/push_to_talk_controller.cpp
    src/stt/transcript_frame.cpp
    src/tts/synthesis.cpp
    src/tts/voice_catalogue.cpp
    src/telemetry/telemetry_frame.cpp
    src/telemetry/traffic.cpp
    src/transport/async_websocket_ptt_sink.cpp
    src/transport/base64.cpp
    src/transport/endpoint_config.cpp
    src/transport/json_util.cpp
    src/transport/speech_text.cpp
    src/transport/websocket_client.cpp
    src/weather/metar.cpp
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
        src/xplane/hud_window.cpp
        src/xplane/imgui_window.cpp
        src/xplane/skyscript_panel.cpp
        src/xplane/xplane_plugin.cpp
    )

    # Dear ImGui, for the HUD. Only the core: X-Plane renders its draw lists
    # through the panel-graphics ImGui helpers and the plugin feeds it input, so
    # no backend is built. Static and position-independent, inside the .xpl.
    if(NOT EXISTS "${ZOAL_ATC_IMGUI_SOURCE}/imgui.h")
        message(FATAL_ERROR "Dear ImGui source not found at '${ZOAL_ATC_IMGUI_SOURCE}'. "
            "Run scripts/ensure-imgui.sh.")
    endif()
    # FreeType rasterizes the text, hinted to the pixel grid: stb_truetype, the
    # default, leaves small UI text thin and soft. Built from source with every
    # optional dependency off, so it needs nothing from the system.
    if(NOT EXISTS "${ZOAL_ATC_FREETYPE_SOURCE}/include/ft2build.h")
        message(FATAL_ERROR "FreeType source not found at '${ZOAL_ATC_FREETYPE_SOURCE}'. "
            "Run scripts/ensure-freetype.sh.")
    endif()
    foreach(_ft_dependency ZLIB BZIP2 PNG HARFBUZZ BROTLI)
        set(FT_DISABLE_${_ft_dependency} ON CACHE BOOL "" FORCE)
    endforeach()
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
    add_subdirectory("${ZOAL_ATC_FREETYPE_SOURCE}" "${CMAKE_BINARY_DIR}/freetype" EXCLUDE_FROM_ALL)
    set_target_properties(freetype PROPERTIES POSITION_INDEPENDENT_CODE ON)

    add_library(zoal_atc_imgui STATIC
        "${ZOAL_ATC_IMGUI_SOURCE}/imgui.cpp"
        "${ZOAL_ATC_IMGUI_SOURCE}/imgui_draw.cpp"
        "${ZOAL_ATC_IMGUI_SOURCE}/imgui_tables.cpp"
        "${ZOAL_ATC_IMGUI_SOURCE}/imgui_widgets.cpp"
        "${ZOAL_ATC_IMGUI_SOURCE}/misc/freetype/imgui_freetype.cpp"
    )
    target_include_directories(zoal_atc_imgui SYSTEM PUBLIC
        "${ZOAL_ATC_IMGUI_SOURCE}"
        "${ZOAL_ATC_IMGUI_SOURCE}/misc/freetype")
    # The demo window and the debug tools are not shipped. FreeType is the font
    # loader for every atlas, so no stb_truetype is compiled in.
    target_compile_definitions(zoal_atc_imgui PUBLIC
        IMGUI_DISABLE_DEMO_WINDOWS IMGUI_DISABLE_DEBUG_TOOLS IMGUI_ENABLE_FREETYPE)
    target_link_libraries(zoal_atc_imgui PUBLIC freetype)
    set_target_properties(zoal_atc_imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(NOT MSVC)
        target_compile_options(zoal_atc_imgui PRIVATE -fvisibility=hidden)
    endif()
    target_link_libraries(zoal_atc PRIVATE zoal_atc_imgui)

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
        XPLM410=1
        XPLM420=1
        XPLM430=1
        XPLM440=1
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

    if(NOT EXISTS "${ZOAL_ATC_WHISPER_SOURCE}/include/whisper.h")
        message(FATAL_ERROR "whisper.cpp source not found at '${ZOAL_ATC_WHISPER_SOURCE}'. "
            "Run scripts/ensure-whisper-src.sh.")
    endif()
    # The same whisper.cpp release the console's transcriber used: static, no
    # examples, portable rather than tuned to the build machine because the
    # plugin runs on other people's. On macOS Metal with its shaders embedded;
    # elsewhere the CPU, without OpenMP so there is no runtime to ship with it.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
    if(APPLE)
        set(GGML_METAL ON CACHE BOOL "" FORCE)
        set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE)
        set(GGML_BLAS ON CACHE BOOL "" FORCE)
        set(GGML_BLAS_VENDOR "Apple" CACHE STRING "" FORCE)
    else()
        set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
    endif()
    # Static, but linked into a shared module, so position-independent.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    add_subdirectory("${ZOAL_ATC_WHISPER_SOURCE}" "${CMAKE_BINARY_DIR}/whisper" EXCLUDE_FROM_ALL)
    target_sources(zoal_atc PRIVATE src/stt/whisper_transcriber.cpp)
    target_link_libraries(zoal_atc PRIVATE whisper)

    if(NOT EXISTS "${ZOAL_ATC_SHERPA_ONNX}/include/sherpa-onnx/c-api/c-api.h")
        message(FATAL_ERROR "sherpa-onnx not found at '${ZOAL_ATC_SHERPA_ONNX}'. "
            "Run scripts/ensure-sherpa-onnx.sh.")
    endif()
    target_include_directories(zoal_atc PRIVATE "${ZOAL_ATC_SHERPA_ONNX}/include")
    target_sources(zoal_atc PRIVATE src/tts/kokoro_synthesizer.cpp)
    if(APPLE)
        # Static, in link order: the C API over the core, the phonemizer and
        # its data, and ONNX Runtime underneath everything.
        set(_sherpa_libs
            sherpa-onnx-c-api sherpa-onnx-core sherpa-onnx-fstfar sherpa-onnx-fst
            sherpa-onnx-kaldifst-core kaldi-decoder-core kaldi-native-fbank-core
            piper_phonemize espeak-ng ucd onnxruntime ssentencepiece_core kissfft-float)
        foreach(_lib IN LISTS _sherpa_libs)
            target_link_libraries(zoal_atc PRIVATE "${ZOAL_ATC_SHERPA_ONNX}/lib/lib${_lib}.a")
        endforeach()
        target_link_libraries(zoal_atc PRIVATE "-framework Foundation" "-framework CoreML")
        set(ZOAL_ATC_SHERPA_RUNTIME "")
    elseif(WIN32)
        # The published MSVC build of the C API, which is a C ABI MinGW can
        # call. Its runtime is statically linked, so the DLLs need nothing but
        # each other, and they ship beside the .xpl where Windows finds them.
        target_link_libraries(zoal_atc PRIVATE "${ZOAL_ATC_SHERPA_ONNX}/lib/sherpa-onnx-c-api.lib")
        set(ZOAL_ATC_SHERPA_RUNTIME
            "${ZOAL_ATC_SHERPA_ONNX}/lib/sherpa-onnx-c-api.dll"
            "${ZOAL_ATC_SHERPA_ONNX}/lib/onnxruntime.dll"
            "${ZOAL_ATC_SHERPA_ONNX}/lib/onnxruntime_providers_shared.dll")
    else()
        # The published shared build; it finds ONNX Runtime by $ORIGIN, as the
        # .xpl finds it.
        target_link_libraries(zoal_atc PRIVATE "${ZOAL_ATC_SHERPA_ONNX}/lib/libsherpa-onnx-c-api.so")
        set(ZOAL_ATC_SHERPA_RUNTIME
            "${ZOAL_ATC_SHERPA_ONNX}/lib/libsherpa-onnx-c-api.so"
            "${ZOAL_ATC_SHERPA_ONNX}/lib/libonnxruntime.so")
    endif()
    # The runtime libraries land beside the built .xpl, where packaging picks
    # them up with it.
    foreach(_runtime IN LISTS ZOAL_ATC_SHERPA_RUNTIME)
        add_custom_command(TARGET zoal_atc POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_runtime}" "$<TARGET_FILE_DIR:zoal_atc>")
    endforeach()

    target_include_directories(zoal_atc PRIVATE ${ZOAL_ATC_SKYSCRIPT_INCLUDE_DIR})
    target_link_libraries(zoal_atc PRIVATE ${ZOAL_ATC_SKYSCRIPT_LIBRARY})
    # The Skyscript library ships beside the .xpl. There is no browser runtime
    # to find: X-Plane renders the panel itself.
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
    zoal_atc_add_test(zoal_atc_metar_tests tests/metar_test.cpp)
    zoal_atc_add_test(zoal_atc_speech_text_tests tests/speech_text_test.cpp)
    zoal_atc_add_test(zoal_atc_endpoint_config_tests tests/endpoint_config_test.cpp)
    zoal_atc_add_test(zoal_atc_reconnect_gate_tests tests/reconnect_gate_test.cpp)
    zoal_atc_add_test(zoal_atc_websocket_client_tests tests/websocket_client_test.cpp)
    zoal_atc_add_test(zoal_atc_bounded_queue_tests tests/bounded_queue_test.cpp)
    zoal_atc_add_test(zoal_atc_json_util_tests tests/json_util_test.cpp)
    zoal_atc_add_test(zoal_atc_gui_panel_path_tests tests/gui_panel_paths_test.cpp)
    zoal_atc_add_test(zoal_atc_gui_frames_tests tests/gui_frames_test.cpp)
    zoal_atc_add_test(zoal_atc_gui_bridge_tests tests/gui_bridge_test.cpp)
    zoal_atc_add_test(zoal_atc_hud_layout_tests tests/hud_layout_test.cpp)
    zoal_atc_add_test(zoal_atc_font_metrics_tests tests/font_metrics_test.cpp)
    zoal_atc_add_test(zoal_atc_hud_model_tests tests/hud_model_test.cpp)
    zoal_atc_add_test(zoal_atc_hud_session_tests tests/hud_session_test.cpp)
    zoal_atc_add_test(zoal_atc_panel_theme_tests tests/panel_theme_test.cpp)
    zoal_atc_add_test(zoal_atc_local_stt_tests tests/local_stt_test.cpp)
    zoal_atc_add_test(zoal_atc_tts_tests tests/tts_test.cpp)
    zoal_atc_add_test(zoal_atc_voice_catalogue_tests tests/voice_catalogue_test.cpp)
endif()
