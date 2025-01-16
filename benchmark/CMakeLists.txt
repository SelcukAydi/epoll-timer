cmake_minimum_required(VERSION 3.22)

project(EpollTimerBenchmark)

file(GLOB SRC_FILES "src/*.cpp")

set(BENCHMARK_DOWNLOAD_DEPENDENCIES ON)

include(FetchContent)

FetchContent_Declare(
    google_benchmark
    GIT_REPOSITORY      https://github.com/google/benchmark.git
    GIT_TAG             v1.9.1
    GIT_PROGRESS        True
    OVERRIDE_FIND_PACKAGE
)

FetchContent_Declare(
    google_test
    GIT_REPOSITORY      https://github.com/google/googletest.git
    GIT_TAG             v1.15.2
    GIT_PROGRESS        True
    OVERRIDE_FIND_PACKAGE
)

FetchContent_Declare(
    folly
    GIT_REPOSITORY      https://github.com/facebook/folly
    GIT_TAG             v2025.01.06.00
    GIT_PROGRESS        True
)

FetchContent_MakeAvailable(google_test google_benchmark folly)

set(FOLLY_BUILD_PATH ${CMAKE_SOURCE_DIR}/build/_deps/folly-src/build)

add_custom_target(folly_build DEPENDS folly
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/build/_deps/folly-src
    # COMMAND python3 ${FOLLY_BUILD_PATH}/fbcode_builder/getdeps.py --allow-system-packages --install-prefix ${CMAKE_SOURCE_DIR}/build/_deps/folly-installed build
    COMMAND python3 ./build/fbcode_builder/getdeps.py --allow-system-packages --install-prefix ${CMAKE_SOURCE_DIR}/build/_deps/folly-installed build)

add_executable(${PROJECT_NAME} ${SRC_FILES})

target_link_libraries(${PROJECT_NAME} PUBLIC EpollTimer GTest::gtest_main benchmark::benchmark benchmark::benchmark_main folly)
target_include_directories(${PROJECT_NAME} PUBLIC include ${GTEST_INCLUDE_DIRS})