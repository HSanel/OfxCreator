cmake_minimum_required(VERSION 3.24)

project(NROfxHost
  VERSION 0.1.0
  DESCRIPTION "OFX authoring host"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets OpenGL)

add_library(OpenFXHeaders INTERFACE)
add_library(OpenFX::Headers ALIAS OpenFXHeaders)
target_include_directories(OpenFXHeaders INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openfx/include"
)

set(BUILD_TESTING OFF CACHE BOOL "QtNodes tests" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "QtNodes examples" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "QtNodes docs" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Link QtNodes statically into the host" FORCE)
set(USE_QT6 ON CACHE BOOL "QtNodes uses Qt6" FORCE)

add_subdirectory(third_party/nodeeditor)

add_subdirectory(apps/host)
