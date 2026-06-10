# Standalone test project for the engine-independent logic in
# src/Core/StageLogic.h. Builds with any C++20 compiler on any platform:
#
#   cmake -S tests -B build-tests
#   cmake --build build-tests
#   ctest --test-dir build-tests --output-on-failure

cmake_minimum_required(VERSION 3.16)

project(QuestTrackerLogicTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(stage_logic_tests StageLogicTests.cpp)
target_include_directories(stage_logic_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

if(MSVC)
	target_compile_options(stage_logic_tests PRIVATE /W4 /WX)
else()
	target_compile_options(stage_logic_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME stage_logic COMMAND stage_logic_tests)
