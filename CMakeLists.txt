cmake_minimum_required(VERSION 3.15)
project(ChessNextMovePredictor VERSION 1.0 LANGUAGES CXX)

# Require C++17 for high-efficiency features and chess.hpp compatibility
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optimize compilation for raw computational speed (MNC production quality)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native")

# 1. FIND HARDWARE ACCELERATED GRAPHICS (SFML)
find_package(SFML 3.0 COMPONENTS Graphics Window System REQUIRED)

# 2. CONFIGURE THIRD-PARTY ENGINE COMPONENT LIBRARIES
set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(CHESS_HPP_DIR ${THIRD_PARTY_DIR}/chess.hpp/include)
set(IMGUI_DIR ${THIRD_PARTY_DIR}/imgui)
set(IMGUI_SFML_DIR ${THIRD_PARTY_DIR}/imgui-sfml)

# Compile ImGui raw source files natively into our binary target
file(GLOB IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_SFML_DIR}/imgui-SFML.cpp
)

# 3. COLLECT ENGINE PROJECT ARCHITECTURE FILES
file(GLOB_RECURSE PROJECT_SOURCES
    "src/*.cpp"
)
# 4. BUILD SYSTEM EXECUTABLE
add_executable(${PROJECT_NAME} ${PROJECT_SOURCES} ${IMGUI_SOURCES})

# 5. LINK HEADER DIRECTORIES AND SYSTEM BINARIES
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CHESS_HPP_DIR}
    ${IMGUI_DIR}
    ${IMGUI_SFML_DIR}
)
find_package(OpenGL REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE
    SFML::Graphics
    SFML::Window
    SFML::System
    OpenGL::GL
)