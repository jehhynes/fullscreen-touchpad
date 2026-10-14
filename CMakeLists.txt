cmake_minimum_required(VERSION 3.25)
project(VirtualTouchpad VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT everywhere (app and tests), so the exe needs no VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(VT_BUILD_TESTS "Build unit tests" ON)

set(VT_DEFINES UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A00000C) # NTDDI_WIN10_NI (Windows 11 22H2 SDK level)
set(VT_OPTIONS /W4 /permissive- /utf-8)

# Pure logic (window layout, tap detection), unit tested.
add_library(touchpad_core STATIC src/Layout.cpp src/TapDetector.cpp)
target_include_directories(touchpad_core PUBLIC src)
target_compile_definitions(touchpad_core PUBLIC ${VT_DEFINES})
target_compile_options(touchpad_core PRIVATE ${VT_OPTIONS})
target_link_libraries(touchpad_core PUBLIC gdi32)

add_executable(VirtualTouchpad WIN32
    src/main.cpp
    src/App.cpp
    src/BuiltinTouchpad.cpp
    src/TextFieldKeyboard.cpp
    src/RawTouch.cpp
    src/Osd.cpp
    src/Settings.cpp
    src/app.manifest
)
target_compile_options(VirtualTouchpad PRIVATE ${VT_OPTIONS})
target_link_options(VirtualTouchpad PRIVATE /MANIFESTUAC:NO) # trustInfo comes from app.manifest
target_link_libraries(VirtualTouchpad PRIVATE touchpad_core user32 gdi32 shell32 shcore advapi32 ole32 oleaut32 uuid hid dwmapi)

if(VT_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_executable(touchpad_tests tests/LayoutTests.cpp tests/TapDetectorTests.cpp)
    target_compile_options(touchpad_tests PRIVATE ${VT_OPTIONS})
    target_link_libraries(touchpad_tests PRIVATE touchpad_core GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(touchpad_tests)
endif()
