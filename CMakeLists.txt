cmake_minimum_required(VERSION 3.25)

project(
    ConvbasedVST3
    VERSION 0.1.0.0
    DESCRIPTION "Convbased low-latency VST3 bridge"
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "ConvbasedVST3 currently targets Windows only")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Vst3Sdk)
convbased_fetch_vst3_sdk()
smtg_enable_vst3_sdk()

add_library(convbased_bridge_core STATIC
    src/audio_ring_buffer.hpp
    src/bridge_protocol.cpp
    src/bridge_protocol.hpp
    src/loopback_bridge.cpp
    src/loopback_bridge.hpp
)

target_compile_features(convbased_bridge_core PUBLIC cxx_std_20)
target_include_directories(convbased_bridge_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(convbased_bridge_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(convbased_bridge_core PUBLIC ws2_32)

smtg_add_vst3plugin(ConvbasedVST3
    src/audio_routing.hpp
    src/controller.cpp
    src/controller.hpp
    src/factory.cpp
    src/plugin_ids.hpp
    src/plugin_messages.hpp
    src/processor.cpp
    src/processor.hpp
    src/version.hpp
    resource/editor.uidesc
)

target_compile_features(ConvbasedVST3 PRIVATE cxx_std_20)
target_compile_definitions(ConvbasedVST3 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(ConvbasedVST3 PRIVATE sdk sdk_hosting vstgui_support convbased_bridge_core)
smtg_target_configure_version_file(ConvbasedVST3)

smtg_target_add_plugin_resources(ConvbasedVST3
    RESOURCES
        resource/editor.uidesc
)

target_sources(ConvbasedVST3 PRIVATE resource/win32resource.rc)
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT ConvbasedVST3)

if(MSVC)
    target_compile_options(convbased_bridge_core PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_options(ConvbasedVST3 PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
