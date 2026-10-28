cmake_minimum_required(VERSION 3.20)
project(gamepad_macro_tests LANGUAGES CXX)

enable_testing()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(macro_core STATIC
    ../../src/macro/macro_engine.cpp
    ../../src/macro/macro_recorder.cpp)
target_include_directories(macro_core PUBLIC ../../src/macro)

add_executable(macro_engine_tests engine_tests.cpp)
add_executable(macro_recording_tests recording_tests.cpp)
add_executable(macro_format_tests ../macro_format_test.cpp)
add_executable(macro_runtime_tests runtime_tests.cpp)
target_include_directories(macro_runtime_tests BEFORE PRIVATE stubs)
target_compile_definitions(macro_runtime_tests PRIVATE ENABLE_GAMEPAD_MACRO=1 GAMEPAD_MACRO_HOST_TEST=1)

foreach(test macro_engine_tests macro_recording_tests macro_format_tests macro_runtime_tests)
    target_link_libraries(${test} PRIVATE macro_core)
    if(MSVC)
        target_compile_options(${test} PRIVATE /W4 /UNDEBUG)
    else()
        target_compile_options(${test} PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    endif()
    add_test(NAME ${test} COMMAND ${test})
endforeach()
