cmake_minimum_required(VERSION 3.22)

project(EpollTimerUTs)

file(GLOB SRC_FILES "src/*.cpp")

find_package(GTest REQUIRED)

add_executable(${PROJECT_NAME} ${SRC_FILES})

target_link_libraries(${PROJECT_NAME} EpollTimer GTest::GTest GTest::Main)
target_include_directories(${PROJECT_NAME} PUBLIC include ${GTEST_INCLUDE_DIRS})