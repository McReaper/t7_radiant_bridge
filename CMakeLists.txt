cmake_minimum_required(VERSION 3.24)
project(t7_radiant_bridge LANGUAGES C CXX)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Radiant is a 64-bit MSVC program: build with MSVC for x64")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# the static CRT: nothing to install next to Radiant
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(BRIDGE_WARNINGS /W4 /WX /permissive- /utf-8)

# the protocol's text, apart so the tests need no Radiant
add_library(bridge_text STATIC plugin/src/text.cpp)
target_include_directories(bridge_text PUBLIC plugin/src)
target_compile_options(bridge_text PRIVATE ${BRIDGE_WARNINGS})

add_library(T7RadiantBridge SHARED
  plugin/src/bridge.cpp
  plugin/src/commands.cpp
  plugin/src/pipe.cpp
  plugin/src/radiant.cpp)
target_include_directories(T7RadiantBridge SYSTEM PRIVATE third_party/mtplugins_sdk)
target_link_libraries(T7RadiantBridge PRIVATE bridge_text user32 gdi32)
target_compile_definitions(T7RadiantBridge PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(T7RadiantBridge PRIVATE ${BRIDGE_WARNINGS} /EHsc /external:W0)

enable_testing()
add_executable(text_test plugin/test/text_test.cpp)
target_link_libraries(text_test PRIVATE bridge_text)
target_compile_options(text_test PRIVATE ${BRIDGE_WARNINGS} /EHsc)
add_test(NAME text COMMAND text_test)
