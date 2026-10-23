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

# ---- the standalone host: T7RadiantBridge with its own minimal plugin host, so
# it runs in a Radiant that MTPlugins doesn't load into. Built from host/, forked
# from MTPlugins (GPLv3); the T7RadiantBridge target above is the MTPlugins plugin
# and is unchanged. (Milestone 1: loads, verifies, logs; no hooks or plugin yet.)

# MinHook (BSD-2, third_party/minhook), its own warning level.
add_library(minhook STATIC
  third_party/minhook/src/buffer.c
  third_party/minhook/src/hook.c
  third_party/minhook/src/trampoline.c
  third_party/minhook/src/hde/hde32.c
  third_party/minhook/src/hde/hde64.c)
target_include_directories(minhook PUBLIC third_party/minhook/include)

# the host logic that needs no Radiant, so host_test can check it
add_library(host_pure STATIC host/exe.cpp host/menukey.cpp)
target_include_directories(host_pure PUBLIC host)
target_compile_options(host_pure PRIVATE ${BRIDGE_WARNINGS})

add_library(T7RadiantBridgeStandalone SHARED
  host/dllmain.cpp
  host/log.cpp
  host/coexist.cpp)
target_link_libraries(T7RadiantBridgeStandalone PRIVATE host_pure user32)
target_compile_definitions(T7RadiantBridgeStandalone PRIVATE T7RB_STANDALONE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(T7RadiantBridgeStandalone PRIVATE ${BRIDGE_WARNINGS} /EHsc)

enable_testing()
add_executable(text_test plugin/test/text_test.cpp)
target_link_libraries(text_test PRIVATE bridge_text)
target_compile_options(text_test PRIVATE ${BRIDGE_WARNINGS} /EHsc)
add_test(NAME text COMMAND text_test)

add_executable(host_test host/test/host_test.cpp)
target_link_libraries(host_test PRIVATE host_pure)
target_compile_options(host_test PRIVATE ${BRIDGE_WARNINGS} /EHsc)
add_test(NAME host COMMAND host_test)
