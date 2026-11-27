# XPMP2 (https://github.com/TwinFan/XPMP2), pinned by commit.
#
# Built here from its sources rather than through its own CMakeLists, which
# looks for the SDK in the official CHeaders/ layout; zoal-atc's SDK tree is
# flattened, and ATC_XPLANE_INCLUDE_DIRS already resolves both.
#
# Bump by changing both lines together; the tag is for humans, the commit is
# what is fetched.
set(ZOAL_MP_XPMP2_TAG "v3.7.0")
set(ZOAL_MP_XPMP2_COMMIT "fb543f9d063e7fc61e08f9d21c6e68136606fc23")

include(FetchContent)
FetchContent_Declare(xpmp2_src
    GIT_REPOSITORY https://github.com/TwinFan/XPMP2.git
    GIT_TAG        ${ZOAL_MP_XPMP2_COMMIT}
    GIT_SHALLOW    FALSE
    SOURCE_SUBDIR  _no_cmake_please   # populate only; do not add_subdirectory
)
FetchContent_MakeAvailable(xpmp2_src)
set(XPMP2_SOURCE_DIR "${xpmp2_src_SOURCE_DIR}" CACHE INTERNAL "")

file(GLOB _xpmp2_sources CONFIGURE_DEPENDS "${XPMP2_SOURCE_DIR}/src/*.cpp")
add_library(xpmp2 STATIC ${_xpmp2_sources})
target_include_directories(xpmp2
    PUBLIC  "${XPMP2_SOURCE_DIR}/inc"
    PRIVATE "${XPMP2_SOURCE_DIR}/src" "${XPMP2_SOURCE_DIR}/lib/fmod/inc" ${ATC_XPLANE_INCLUDE_DIRS})
target_compile_definitions(xpmp2
    PUBLIC  XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM303=1 XPLM400=1
            APL=$<BOOL:${APPLE}> IBM=$<BOOL:${WIN32}> LIN=$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${APPLE}>>>
    PRIVATE XPMP2_VERSION="3.7.0" XPMP2_VER_MAJOR=3 XPMP2_VER_MINOR=7 XPMP2_VER_PATCH=0
            $<IF:$<CONFIG:Debug>,DEBUG=1,NDEBUG=1>)
if(WIN32)
    target_compile_definitions(xpmp2 PRIVATE _WIN32_WINNT=0x0601)
    # Network.cpp enumerates interfaces for its remote-client multicast:
    # GetAdaptersAddresses and if_indextoname are iphlpapi's, the sockets
    # ws2_32's. A static library carries no link of its own, so without this
    # the Windows plugin failed to link from the day XPMP2 came in.
    target_link_libraries(xpmp2 PUBLIC ws2_32 iphlpapi)
endif()
if(NOT MSVC)
    target_compile_options(xpmp2 PRIVATE -w -fexceptions -fvisibility=hidden)
endif()
set_target_properties(xpmp2 PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 17)
target_precompile_headers(xpmp2 PRIVATE "${XPMP2_SOURCE_DIR}/src/XPMP2.h")
