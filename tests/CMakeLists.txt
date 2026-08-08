add_executable(convbased_bridge_tests bridge_tests.cpp)
target_compile_features(convbased_bridge_tests PRIVATE cxx_std_20)
target_compile_definitions(convbased_bridge_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(convbased_bridge_tests PRIVATE convbased_bridge_core)

if(MSVC)
    target_compile_options(convbased_bridge_tests PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

add_test(NAME convbased_bridge_tests COMMAND convbased_bridge_tests)

add_executable(convbased_ui_tests ui_tests.cpp)
target_compile_features(convbased_ui_tests PRIVATE cxx_std_20)
target_compile_definitions(convbased_ui_tests PRIVATE
    CONVBASED_EDITOR_UIDESC="${CMAKE_SOURCE_DIR}/resource/editor.uidesc"
)
target_link_libraries(convbased_ui_tests PRIVATE vstgui_support)

if(MSVC)
    target_compile_options(convbased_ui_tests PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

add_test(NAME convbased_ui_tests COMMAND convbased_ui_tests)
