cmake_minimum_required(VERSION 3.16)
project(CabinetCore VERSION 0.9 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(SDL2_INCLUDE "${CMAKE_SOURCE_DIR}/third_party/SDL2")
set(SDL2_LIB "/usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0")
if(NOT EXISTS "${SDL2_LIB}")
    message(FATAL_ERROR "SDL2 shared library not found at ${SDL2_LIB}")
endif()

find_package(Threads REQUIRED)

set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/third_party/imgui")

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends ${SDL2_INCLUDE})
target_compile_definitions(imgui PUBLIC SDL_MAIN_HANDLED)

add_library(ccore STATIC
    src/common/Canvas.cpp
    src/common/Project.cpp
    src/common/Synth.cpp
    src/common/Export.cpp
    src/common/AudioDevice.cpp
    src/player/Engine.cpp
    src/demo/Demo.cpp
    src/plugin/Plugins.cpp
)
target_include_directories(ccore PUBLIC src ${SDL2_INCLUDE} ${CMAKE_SOURCE_DIR}/third_party)
target_compile_definitions(ccore PUBLIC SDL_MAIN_HANDLED)
target_link_libraries(ccore PUBLIC ${SDL2_LIB} Threads::Threads ${CMAKE_DL_LIBS})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ccore PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_compile_options(imgui PRIVATE -w)
endif()

add_executable(cabinet src/player/main.cpp)
target_link_libraries(cabinet PRIVATE ccore)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cabinet PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
set_target_properties(cabinet PROPERTIES OUTPUT_NAME "cabinet.elf")

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/cabinet_player_path.h"
    CONTENT "#pragma once\n#define CABINET_PLAYER_PATH \"$<TARGET_FILE:cabinet>\"\n")

add_executable(cabinetcore src/editor/main.cpp src/editor/Editor.cpp src/editor/BlockEditor.cpp src/editor/SelfTest.cpp)
target_include_directories(cabinetcore PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(cabinetcore PRIVATE ccore imgui)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cabinetcore PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_dependencies(cabinetcore cabinet)

add_executable(cabinet-plugin src/plugin/Create.cpp)
target_link_libraries(cabinet-plugin PRIVATE ccore)
target_compile_definitions(cabinet-plugin PRIVATE "CABINET_ROOT=\"${CMAKE_SOURCE_DIR}\"")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cabinet-plugin PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
