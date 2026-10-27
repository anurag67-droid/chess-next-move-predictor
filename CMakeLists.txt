cmake_minimum_required(VERSION 3.16)
project(ChessNextMovePredictor VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# SFML 3: use an installed copy if there is one, otherwise build the submodule.
find_package(SFML 3 COMPONENTS Graphics Window System QUIET)
if(NOT SFML_FOUND)
    message(STATUS "SFML 3 not found on the system, building third_party/sfml")
    set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
    add_subdirectory(${THIRD_PARTY_DIR}/sfml EXCLUDE_FROM_ALL)
endif()
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

# Dear ImGui + its SFML backend, compiled straight into the executable.
set(IMGUI_DIR ${THIRD_PARTY_DIR}/imgui)
set(IMGUI_SFML_DIR ${THIRD_PARTY_DIR}/imgui-sfml)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_SFML_DIR}/imgui-SFML.cpp
)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/core/game.cpp
    src/engine/evaluation.cpp
    src/engine/search.cpp
    src/gui/application.cpp
    src/gui/board_renderer.cpp
    src/gui/engine_worker.cpp
    src/gui/format.cpp
    src/gui/panel.cpp
    src/gui/widgets.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${THIRD_PARTY_DIR}/chess.hpp/include
)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${IMGUI_DIR} ${IMGUI_SFML_DIR})

target_compile_definitions(${PROJECT_NAME} PRIVATE
    CHESS_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(${IMGUI_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
    target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:-O3>)
elseif(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W3)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    SFML::Graphics
    SFML::Window
    SFML::System
    OpenGL::GL
    Threads::Threads
)
