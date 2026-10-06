cmake_minimum_required(VERSION 3.20)
project(RockRaiders LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL2 REQUIRED CONFIG)
find_package(OpenGL REQUIRED)

file(GLOB_RECURSE RR_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.h)

add_executable(RockRaiders ${RR_SOURCES})
target_include_directories(RockRaiders PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(RockRaiders PRIVATE SDL2::SDL2main SDL2::SDL2 OpenGL::GL)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(RockRaiders PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

if(WIN32)
    # Console subsystem in Debug so log output is visible; GUI subsystem otherwise.
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        set_target_properties(RockRaiders PROPERTIES WIN32_EXECUTABLE ON)
    endif()
    target_link_options(RockRaiders PRIVATE -static)
    add_custom_command(TARGET RockRaiders POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:RockRaiders>)
endif()

enable_testing()
file(GLOB RR_TESTS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.cpp)
if(RR_TESTS)
    set(RR_LIB_SOURCES ${RR_SOURCES})
    list(FILTER RR_LIB_SOURCES EXCLUDE REGEX ".*/src/main\\.cpp$")
    add_executable(rr_tests ${RR_TESTS} ${RR_LIB_SOURCES})
    target_include_directories(rr_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(rr_tests PRIVATE SDL2::SDL2 OpenGL::GL)
    target_link_options(rr_tests PRIVATE -static)
    add_test(NAME rr_tests COMMAND rr_tests)
endif()
