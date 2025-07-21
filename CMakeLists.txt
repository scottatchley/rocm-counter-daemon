cmake_minimum_required(VERSION 3.21.0 FATAL_ERROR)

if(NOT CMAKE_HIP_COMPILER)
    find_program(
        amdclangpp_EXECUTABLE
        NAMES amdclang++
        HINTS ${ROCM_PATH} ENV ROCM_PATH /opt/rocm-6.4.1/
        PATHS ${ROCM_PATH} ENV ROCM_PATH /opt/rocm-6.4.1/
        PATH_SUFFIXES bin llvm/bin NO_CACHE)
    mark_as_advanced(amdclangpp_EXECUTABLE)

    if(amdclangpp_EXECUTABLE)
        set(CMAKE_HIP_COMPILER "${amdclangpp_EXECUTABLE}")
    endif()
endif()

project(rocm-counter-daemon LANGUAGES CXX HIP)

find_package(rocprofiler-sdk REQUIRED)
find_package(HIP REQUIRED)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNDEBUG -g")

set_source_files_properties(daemon.cpp PROPERTIES LANGUAGE HIP)

add_executable(rocm-counter-daemon)
target_sources(rocm-counter-daemon PRIVATE daemon.cpp)
target_link_libraries(
    rocm-counter-daemon
    PRIVATE rocprofiler-sdk::rocprofiler-sdk Threads::Threads)

set_property(TARGET rocm-counter-daemon PROPERTY CXX_STANDARD 17)
