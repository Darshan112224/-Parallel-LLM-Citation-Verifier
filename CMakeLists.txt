cmake_minimum_required(VERSION 3.20)
project(citeverify VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language standard: C++20 (we use std::span, std::string_view lookups in
# unordered_map, designated initializers, and later std::jthread/semaphores).
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # for clangd / IDEs

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Optional sanitizers.  Usage:
#   cmake -B build-tsan -DCITEVERIFY_SANITIZE=thread
#   cmake -B build-asan -DCITEVERIFY_SANITIZE=address
set(CITEVERIFY_SANITIZE "" CACHE STRING "Sanitizer to enable: thread | address | (empty)")

# -Wno-missing-field-initializers: GCC warns on C++20 designated initializers
# like Result{.index = i}, even though omitted fields use their defaults (by design).
add_compile_options(-Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
if(CITEVERIFY_SANITIZE)
  add_compile_options(-fsanitize=${CITEVERIFY_SANITIZE} -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=${CITEVERIFY_SANITIZE})
endif()

find_package(Threads REQUIRED)   # needed from Week 2 (std::thread)

# --- Core library: everything except main(), so tests and benches can link it.
add_library(citeverify_core
  src/core/text.cpp
  src/core/paper_db.cpp
  src/core/reference_parser.cpp
  src/core/verifier.cpp
  src/core/report.cpp
)
target_include_directories(citeverify_core PUBLIC include)
target_link_libraries(citeverify_core PUBLIC Threads::Threads)

# --- Command-line program
add_executable(citeverify src/main.cpp)
target_link_libraries(citeverify PRIVATE citeverify_core)

# --- Tests (plain asserts, no external framework needed)
enable_testing()
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE citeverify_core)
add_test(NAME core COMMAND test_core)
