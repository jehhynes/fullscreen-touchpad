cmake_minimum_required(VERSION 3.25)
project(VirtualTouchpad VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT everywhere (app and tests), so the installed exe needs no VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(VT_BUILD_TESTS "Build unit tests" ON)

set(VT_DEFINES UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A00000C) # Windows 11 (NTDDI_WIN10_CO)
set(VT_OPTIONS /W4 /permissive- /utf-8)

# Gesture engine: pure logic behind the TouchpadHost interface, so it can be unit tested.
add_library(touchpad_core STATIC src/TouchpadEngine.cpp)
target_include_directories(touchpad_core PUBLIC src)
target_compile_definitions(touchpad_core PUBLIC ${VT_DEFINES})
target_compile_options(touchpad_core PRIVATE ${VT_OPTIONS})

add_executable(VirtualTouchpad WIN32
    src/main.cpp
    src/App.cpp
    src/Win32TouchpadHost.cpp
    src/TouchpadInjector.cpp
    src/InputSender.cpp
    src/TouchKeyboard.cpp
    src/Settings.cpp
    src/Overlays.cpp
    src/DebugLog.cpp
    src/app.manifest
)
target_compile_options(VirtualTouchpad PRIVATE ${VT_OPTIONS})

# Our manifest carries the uiAccess trustInfo; stop the linker from adding a conflicting one.
target_link_options(VirtualTouchpad PRIVATE /MANIFESTUAC:NO)

target_link_libraries(VirtualTouchpad PRIVATE touchpad_core user32 gdi32 shell32 shcore advapi32 ole32 uuid oleaut32)

# Diagnostic: injects synthetic touch and checks whether the cursor jumps to the touch point.
add_executable(touchprobe tools/touchprobe.cpp)
target_compile_options(touchprobe PRIVATE ${VT_OPTIONS})
target_compile_definitions(touchprobe PRIVATE ${VT_DEFINES})
target_link_libraries(touchprobe PRIVATE user32 gdi32 winmm)

# Diagnostic: checks whether a synthetic precision touchpad (PT_TOUCHPAD injection) works here.
add_executable(touchpadprobe tools/touchpadprobe.cpp)
target_compile_options(touchpadprobe PRIVATE ${VT_OPTIONS})
target_compile_definitions(touchpadprobe PRIVATE ${VT_DEFINES})
target_link_libraries(touchpadprobe PRIVATE user32)

if(VT_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_executable(touchpad_tests tests/TouchpadEngineTests.cpp)
    target_compile_options(touchpad_tests PRIVATE ${VT_OPTIONS})
    target_link_libraries(touchpad_tests PRIVATE touchpad_core GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(touchpad_tests)
endif()
