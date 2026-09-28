cmake_minimum_required(VERSION 3.20)
project(orda-book LANGUAGES CXX)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LOB_ENABLE_ASAN "Enable address sanitizer" OFF)
option(LOB_ENABLE_UBSAN "Enable undefined behavior sanitizer" OFF)
option(LOB_ENABLE_TSAN "Enable thread sanitizer" OFF)
option(LOB_BUILD_FUZZER "Build the libFuzzer differential target" OFF)

set(LOB_WARNING_FLAGS
  -Wall
  -Wextra
  -Wpedantic
  -Wconversion
  -Wshadow
  -Werror=return-type
)

if(LOB_ENABLE_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()

if(LOB_ENABLE_UBSAN)
  add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=undefined)
endif()

if(LOB_ENABLE_TSAN)
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
endif()

add_library(orda_core
  src/order_book.cpp
  src/pooled_order_book.cpp
  src/ladder_order_book.cpp
  src/event_parser.cpp
  src/binary_event_codec.cpp
)

target_include_directories(orda_core PUBLIC src)

target_compile_options(orda_core PRIVATE ${LOB_WARNING_FLAGS})

add_executable(orda_replay src/replay_main.cpp)
target_link_libraries(orda_replay PRIVATE orda_core)

add_executable(orda_benchmark src/benchmark_main.cpp)
target_link_libraries(orda_benchmark PRIVATE orda_core)

add_executable(orda_ingress_benchmark src/ingress_benchmark_main.cpp)
target_link_libraries(orda_ingress_benchmark PRIVATE orda_core Threads::Threads)

add_executable(orda_allocation_benchmark src/allocation_benchmark_main.cpp)
target_link_libraries(orda_allocation_benchmark PRIVATE orda_core)

add_executable(orda_tail_benchmark src/tail_benchmark_main.cpp)
target_link_libraries(orda_tail_benchmark PRIVATE orda_core)

add_executable(orda_partition_lab src/partition_lab_main.cpp)
target_link_libraries(orda_partition_lab PRIVATE orda_core Threads::Threads)

add_executable(orda_tests
  tests/test_main.cpp
  tests/order_book_tests.cpp
  tests/abuse_tests.cpp
  tests/spsc_queue_tests.cpp
  tests/property_tests.cpp
  tests/reference_order_book.cpp
  tests/differential_tests.cpp
  tests/replay_tests.cpp
  tests/partition_tests.cpp
)
target_link_libraries(orda_tests PRIVATE orda_core)
target_link_libraries(orda_tests PRIVATE Threads::Threads)
target_compile_options(orda_tests PRIVATE ${LOB_WARNING_FLAGS})

include(CTest)
enable_testing()
add_test(NAME orda_tests COMMAND orda_tests)
add_test(
  NAME visualizer_trace_fixture
  COMMAND ${CMAKE_COMMAND}
    "-DREPLAY=$<TARGET_FILE:orda_replay>"
    "-DEVENTS=${CMAKE_CURRENT_SOURCE_DIR}/visualizer/scenarios/price-time-priority.events"
    "-DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/visualizer/data/price-time-priority.json"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_visualizer_trace.cmake"
)

# A scenario the engine refuses, so the page's rejection rendering is guarded
# by a checked fixture rather than by a scenario nobody can reach.
add_test(
  NAME visualizer_rejection_fixture
  COMMAND ${CMAKE_COMMAND}
    "-DREPLAY=$<TARGET_FILE:orda_replay>"
    "-DEVENTS=${CMAKE_CURRENT_SOURCE_DIR}/visualizer/scenarios/rejections.events"
    "-DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/visualizer/data/rejections.json"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_visualizer_trace.cmake"
)

# The counterfactual behind the parameter panel's modify toggle. Re-sending the
# older bid at the same price and size moves it to the back of its queue.
add_test(
  NAME visualizer_queue_position_fixture
  COMMAND ${CMAKE_COMMAND}
    "-DREPLAY=$<TARGET_FILE:orda_replay>"
    "-DEVENTS=${CMAKE_CURRENT_SOURCE_DIR}/visualizer/scenarios/queue-position.events"
    "-DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/visualizer/data/queue-position.json"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_visualizer_trace.cmake"
)

# The depth chart is the one place the page derives its own numbers, so its
# arithmetic is checked against the trace. Skipped where Node is absent rather
# than failing a C++-only runner.
find_program(LOB_NODE node)
if(LOB_NODE)
  add_test(
    NAME visualizer_depth_chart
    COMMAND ${LOB_NODE}
      "${CMAKE_CURRENT_SOURCE_DIR}/visualizer/check-depth.mjs"
      "${CMAKE_CURRENT_SOURCE_DIR}/visualizer/data/price-time-priority.json"
      "${CMAKE_CURRENT_SOURCE_DIR}/visualizer/data/rejections.json"
  )

  # The lesson itself, and the one assertion a fixture test cannot make: that
  # the two runs differ in fill order, rather than in price or size.
  add_test(
    NAME visualizer_priority_order
    COMMAND ${LOB_NODE}
      "${CMAKE_CURRENT_SOURCE_DIR}/visualizer/check-priority.mjs"
      "${CMAKE_CURRENT_SOURCE_DIR}/visualizer/data/price-time-priority.json"
      "${CMAKE_CURRENT_SOURCE_DIR}/visualizer/data/queue-position.json"
  )

  # app.js is the page's only script and nothing else in the suite parses it, so
  # a syntax error there would pass every other test and publish a blank page.
  add_test(
    NAME visualizer_script_syntax
    COMMAND ${LOB_NODE} --check "${CMAKE_CURRENT_SOURCE_DIR}/visualizer/app.js"
  )
else()
  message(STATUS "node not found, skipping the visualizer depth, priority, and syntax tests")
endif()

if(LOB_BUILD_FUZZER)
  add_executable(orda_fuzz
    tests/fuzz_target.cpp
    tests/reference_order_book.cpp
  )
  target_link_libraries(orda_fuzz PRIVATE orda_core)
  target_compile_options(orda_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(orda_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
endif()
