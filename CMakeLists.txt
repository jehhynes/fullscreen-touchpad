cmake_minimum_required(VERSION 3.25)
project(VirtualTouchpad VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(VirtualTouchpad WIN32
    src/main.cpp
    src/App.cpp
    src/TouchpadEngine.cpp
    src/InputSender.cpp
    src/Settings.cpp
    src/Overlays.cpp
    src/app.manifest
)

target_compile_definitions(VirtualTouchpad PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000000
)

target_compile_options(VirtualTouchpad PRIVATE /W4 /permissive- /utf-8)

# Our manifest carries the uiAccess trustInfo; stop the linker from adding a conflicting one.
target_link_options(VirtualTouchpad PRIVATE /MANIFESTUAC:NO)

target_link_libraries(VirtualTouchpad PRIVATE user32 gdi32 shell32 shcore advapi32 ole32)

# Static CRT so the installed exe has no Visual C++ redistributable dependency.
set_property(TARGET VirtualTouchpad PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
