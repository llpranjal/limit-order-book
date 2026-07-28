cmake_minimum_required(VERSION 3.16)
project(orderbook CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Same flags for both implementations. A benchmark comparing code built with
# different optimization settings measures the compiler, not the design.
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

add_library(ob STATIC src/map_book.cpp src/array_book.cpp)
target_include_directories(ob PUBLIC include)

add_executable(test_correctness tests/test_correctness.cpp)
target_link_libraries(test_correctness PRIVATE ob)

add_executable(test_differential tests/test_differential.cpp)
target_link_libraries(test_differential PRIVATE ob)

add_executable(bench bench/bench_main.cpp)
target_link_libraries(bench PRIVATE ob)

enable_testing()
add_test(NAME correctness COMMAND test_correctness)
add_test(NAME differential COMMAND test_differential)
