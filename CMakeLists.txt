cmake_minimum_required(VERSION 3.22)

project(EpollTimer)

set(WARNING_FLAGS "-Wno-unused-variable -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${WARNING_FLAGS} -std=c++17")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# set(CMAKE_INSTALL_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/installed)

if(CMAKE_INSTALL_PREFIX)
    message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
endif()
    
option(ENABLE_TEST "Enables UT." OFF)

message(STATUS "UT tests:${ENABLE_TEST}")

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Boost REQUIRED COMPONENTS system container)

file(GLOB SRC_FILES "src/*.cpp")

add_library(${PROJECT_NAME} SHARED ${SRC_FILES})
target_link_libraries(${PROJECT_NAME} ${Boost_LIBRARIES})
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>  # <prefix>/include/
)

set(EpollTimer_VERSION 1.0.0)
set(EpollTimer_MAJOR_VERSION 1)
set_property(TARGET EpollTimer PROPERTY VERSION ${EpollTimer_VERSION})
set_property(TARGET EpollTimer PROPERTY SOVERSION ${EpollTimer_MAJOR_VERSION})
set_property(TARGET EpollTimer PROPERTY INTERFACE_EpollTimer_MAJOR_VERSION ${EpollTimer_MAJOR_VERSION})
set_property(TARGET EpollTimer APPEND PROPERTY
    COMPATIBLE_INTERFACE_STRING EpollTimer_MAJOR_VERSION)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/EpollTimer/EpollTimerConfigVersion.cmake"
    VERSION ${EpollTimer_VERSION}
    COMPATIBILITY AnyNewerVersion
)

install(
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/include/EpollTimer.hpp
    "${CMAKE_CURRENT_BINARY_DIR}/epolltimer_export.h"
  DESTINATION
    include
  COMPONENT
    Devel
)

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/EpollTimer/EpollTimerConfigVersion.cmake"
    DESTINATION lib/cmake/ClimbingStats
    COMPONENT Devel
)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Util.cmake)

CreateConfigPackageFile(EpollTimer sia::epoll)

if(ENABLE_TEST)
    add_subdirectory(test)
endif()