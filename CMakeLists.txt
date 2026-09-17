cmake_minimum_required(VERSION 3.21)

project(obs-auto-stop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OBS_SOURCE_DIR "" CACHE PATH "OBS Studio source directory")
set(OBS_BUILD_DIR "" CACHE PATH "OBS Studio build directory")
option(OBS_AUTOSTOP_BUILD_PLUGIN "Build the plugin when OBS headers and libraries are available" ON)

set(_obs_source_hints "${OBS_SOURCE_DIR}" "$ENV{OBS_SOURCE_DIR}")
set(_obs_build_hints "${OBS_BUILD_DIR}" "$ENV{OBS_BUILD_DIR}")

find_path(OBS_LIBOBS_INCLUDE_DIR
    NAMES obs-module.h
    HINTS ${_obs_source_hints}
    PATH_SUFFIXES libobs
)
find_path(OBS_GENERATED_INCLUDE_DIR
    NAMES obs-config.h
    HINTS ${_obs_build_hints}
    PATH_SUFFIXES config libobs
)
find_library(OBS_LIBOBS_LIBRARY
    NAMES obs libobs
    HINTS ${_obs_build_hints}
    PATH_SUFFIXES libobs
)

if(NOT OBS_AUTOSTOP_BUILD_PLUGIN)
    message(STATUS "OBS Auto Stop plugin build disabled")
elseif(NOT OBS_LIBOBS_INCLUDE_DIR OR NOT OBS_LIBOBS_LIBRARY)
    message(WARNING "OBS SDK not found. Configure with -DOBS_SOURCE_DIR=... and -DOBS_BUILD_DIR=... to build the plugin.")
else()
    add_library(obs-auto-stop MODULE
        src/auto-stop-dock.cpp
        src/motion-detector.cpp
        src/plugin-main.cpp
        src/recording-monitor.cpp
        src/stop-controller.cpp
    )

    target_include_directories(obs-auto-stop PRIVATE
        "${OBS_LIBOBS_INCLUDE_DIR}"
        "${OBS_GENERATED_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(obs-auto-stop PRIVATE "${OBS_LIBOBS_LIBRARY}")
    set_target_properties(obs-auto-stop PROPERTIES
        PREFIX ""
        OUTPUT_NAME "obs-auto-stop"
    )
endif()
