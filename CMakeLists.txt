cmake_minimum_required(VERSION 3.21)

project(obs-auto-stop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

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

find_package(Qt6 QUIET COMPONENTS Widgets)
if(NOT Qt6_FOUND)
    find_package(Qt5 QUIET COMPONENTS Widgets)
endif()

if(NOT OBS_AUTOSTOP_BUILD_PLUGIN)
    message(STATUS "OBS Auto Stop plugin build disabled")
elseif(NOT OBS_LIBOBS_INCLUDE_DIR OR NOT OBS_LIBOBS_LIBRARY)
    message(WARNING "OBS SDK not found. Configure with -DOBS_SOURCE_DIR=... and -DOBS_BUILD_DIR=... to build the plugin.")
elseif(NOT Qt6_FOUND AND NOT Qt5_FOUND)
    message(WARNING "Qt Widgets not found. Install Qt6 Widgets to build the OBS Auto Stop dock UI.")
else()
    add_library(obs-auto-stop MODULE
        src/auto-stop-dock.cpp
        src/region-select-dialog.cpp
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

    find_library(OBS_FRONTEND_API_LIBRARY NAMES obs-frontend-api HINTS ${_obs_build_hints} PATH_SUFFIXES libUI/obs-frontend-api lib)
    if(NOT OBS_FRONTEND_API_LIBRARY)
        find_library(OBS_FRONTEND_API_LIBRARY NAMES obs-frontend-api)
    endif()

    if(Qt6_FOUND)
        target_link_libraries(obs-auto-stop PRIVATE
            "${OBS_LIBOBS_LIBRARY}"
            ${OBS_FRONTEND_API_LIBRARY}
            Qt6::Widgets
        )
    else()
        target_link_libraries(obs-auto-stop PRIVATE
            "${OBS_LIBOBS_LIBRARY}"
            ${OBS_FRONTEND_API_LIBRARY}
            Qt5::Widgets
        )
    endif()

    set_target_properties(obs-auto-stop PROPERTIES
        PREFIX ""
        OUTPUT_NAME "obs-auto-stop"
    )

    set(_obs_data_dir "${CMAKE_BINARY_DIR}/obs-auto-stop/data")
    add_custom_command(TARGET obs-auto-stop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_obs_data_dir}/locale"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/data/locale/en-US.ini"
            "${_obs_data_dir}/locale/en-US.ini"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/data/locale/ja-JP.ini"
            "${_obs_data_dir}/locale/ja-JP.ini"
        COMMENT "Copying OBS Auto Stop locale data"
    )

    include(GNUInstallDirs)
    install(TARGETS obs-auto-stop
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/obs-plugins
    )
    install(DIRECTORY data/
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/obs-plugins/obs-auto-stop/data
    )
endif()
