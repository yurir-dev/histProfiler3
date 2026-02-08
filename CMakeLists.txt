cmake_minimum_required(VERSION 3.10)

set (PROJECT_NAME "profiler")
set (EXE_NAME "profiledApp")

project(${PROJECT_NAME})

if(MSVC)
    add_compile_options("/W4" "/WX" "$<$<CONFIG:RELEASE>:/O2>")
else()
    add_compile_options("-Wall" "-Wextra" "-Werror" "-pedantic-errors" "-Werror=unused-result" "$<$<CONFIG:RELEASE>:-O3>")
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        add_compile_options("-stdlib=libc++")
    else()
        # nothing special for gcc at the moment
    endif()
endif()

include_directories(${CMAKE_SOURCE_DIR} histProfiler)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set (HIST_PROFILER 	histProfiler/histogram.h)
set (SOURCES main.cpp  ${HIST_PROFILER})

set(ENABLE_HIST_PROFILER "true" CACHE BOOL "enables the profiler")
if(ENABLE_HIST_PROFILER)
	add_compile_definitions(ENABLE_HIST_PROFILER)
endif()

add_executable(${EXE_NAME} ${SOURCES})

if (UNIX)
target_link_libraries(${EXE_NAME} pthread)
endif()

enable_testing()
add_subdirectory(tests)

