cmake_minimum_required(VERSION 3.16)
project(TSPStudio VERSION 1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt specific settings
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
find_package(OpenMP)

set(SOURCES
    src/main.cpp
    src/ui/GuiController.cpp
    src/ui/TSPStudioMapView.cpp
    src/core/TSPStudioManager.cpp
    src/solvers/SolverWorker.cpp
    include/ui/GuiController.h
    include/ui/TSPStudioMapView.h
    include/solvers/SolverWorker.h
    include/core/TSPStudioManager.h
)

# In Windows, use WIN32 to avoid a console popup
add_executable(TSPStudio WIN32
    ${SOURCES}
    resources.qrc
)

# Target-specific include directories (Modern CMake)
target_include_directories(TSPStudio PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core/cdt
)

# Target-specific libraries
target_link_libraries(TSPStudio PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(TSPStudio PUBLIC OpenMP::OpenMP_CXX)
endif()

if(MSVC)
    target_compile_options(TSPStudio PRIVATE /MP)
    target_link_libraries(TSPStudio PRIVATE user32)
    target_compile_definitions(TSPStudio PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Visual Studio specific: set TSPStudio as the startup project
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT TSPStudio)
