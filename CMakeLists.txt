cmake_minimum_required(VERSION 3.22)

project(EpollTimer)

set(WARNING_FLAGS "-Wno-unused-variable -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${WARNING_FLAGS} -std=c++17")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_TEST "Enable UT." ON)

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Boost REQUIRED COMPONENTS system container)

file(GLOB SRC_FILES "src/*.cpp")

add_library(${PROJECT_NAME} SHARED ${SRC_FILES})

target_include_directories(${PROJECT_NAME} PUBLIC include ${Boost_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${Boost_LIBRARIES})

if(ENABLE_TEST)
    add_subdirectory(test)
endif()