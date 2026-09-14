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

include(FetchContent)
find_package(EXPAT QUIET)
if(NOT EXPAT_FOUND AND NOT TARGET expat::expat)
  set(EXPAT_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(EXPAT_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(EXPAT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(EXPAT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(EXPAT_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(expat
    GIT_REPOSITORY https://github.com/libexpat/libexpat.git
    GIT_TAG R_2_6_4
    SOURCE_SUBDIR expat
  )
  FetchContent_MakeAvailable(expat)
endif()
if(NOT TARGET expat::expat)
  if(TARGET expat)
    add_library(expat::expat ALIAS expat)
  elseif(EXPAT_FOUND)
    add_library(expat::expat ALIAS EXPAT::EXPAT)
  endif()
endif()

file(GLOB OFX_HOSTSUPPORT_SRC "${CMAKE_SOURCE_DIR}/third_party/openfx/HostSupport/src/*.cpp")
add_library(OfxHostSupport STATIC ${OFX_HOSTSUPPORT_SRC})
target_include_directories(OfxHostSupport PUBLIC
  "${CMAKE_SOURCE_DIR}/third_party/openfx/include"
  "${CMAKE_SOURCE_DIR}/third_party/openfx/HostSupport/include"
)
target_link_libraries(OfxHostSupport PUBLIC expat::expat)
if(WIN32)
  target_link_libraries(OfxHostSupport PUBLIC shell32)
endif()

add_subdirectory(apps/host)
