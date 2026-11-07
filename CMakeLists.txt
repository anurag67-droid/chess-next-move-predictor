cmake_minimum_required(VERSION 3.16)
project(ChessNextMovePredictor VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# Chess logic and the player book: no graphics, shared by the app, the command-line tool and the tests.
add_library(predictor_core STATIC
    src/core/game.cpp
    src/predictor/player_book.cpp
    src/predictor/move_net.cpp
)
target_include_directories(predictor_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${THIRD_PARTY_DIR}/chess.hpp/include
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(predictor_core PRIVATE $<$<CONFIG:Release>:-O3>)
endif()

# player-book: summarise a player's games and test how well their past moves predict new ones.
add_executable(player-book src/cli/player_book_main.cpp)
target_link_libraries(player-book PRIVATE predictor_core)

include(CTest)
if(BUILD_TESTING)
    add_executable(player_book_test tests/player_book_test.cpp)
    target_link_libraries(player_book_test PRIVATE predictor_core)
    target_compile_definitions(player_book_test PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME player_book COMMAND player_book_test)

    add_executable(move_net_test tests/move_net_test.cpp)
    target_link_libraries(move_net_test PRIVATE predictor_core)
    target_compile_definitions(move_net_test PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME move_net COMMAND move_net_test)
endif()

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
    src/engine/evaluation.cpp
    src/engine/search.cpp
    src/gui/application.cpp
    src/gui/board_renderer.cpp
    src/gui/engine_worker.cpp
    src/gui/format.cpp
    src/gui/panel.cpp
    src/gui/player_worker.cpp
    src/gui/widgets.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${IMGUI_DIR} ${IMGUI_SFML_DIR})

target_compile_definitions(${PROJECT_NAME} PRIVATE
    CHESS_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    CHESS_PLAYERS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/players"
    CHESS_MODELS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ml/models"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(${IMGUI_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
    target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:-O3>)
elseif(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W3)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    predictor_core
    SFML::Graphics
    SFML::Window
    SFML::System
    OpenGL::GL
    Threads::Threads
)
