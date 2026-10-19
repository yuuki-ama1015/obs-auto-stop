cmake_minimum_required(VERSION 3.21)

project(obs-auto-stop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Single-config generators (Makefiles/Ninja) default to an unoptimized build
# when no type is given; default to Release so local Linux builds are small.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(OBS_SOURCE_DIR "" CACHE PATH "OBS Studio source directory")
set(OBS_BUILD_DIR "" CACHE PATH "OBS Studio build directory")
option(OBS_AUTOSTOP_BUILD_PLUGIN "Build the plugin when OBS headers and libraries are available" ON)
option(OBS_AUTOSTOP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(OBS_AUTOSTOP_USE_PACKAGES "Prefer installed libobs / obs-frontend-api CMake packages" ON)

# 1) Prefer installed CMake packages (e.g. libobs-dev on Linux, or an OBS
#    install prefix on CMAKE_PREFIX_PATH). 2) Otherwise fall back to an OBS
#    source + build tree. Every path below is a cache variable, so it can be
#    passed directly with -D<VAR>=... (no CMakeLists patching needed).
set(_obs_use_packages OFF)
if(OBS_AUTOSTOP_USE_PACKAGES AND NOT OBS_SOURCE_DIR)
    find_package(libobs QUIET CONFIG)
    find_package(obs-frontend-api QUIET CONFIG)
    if(TARGET OBS::libobs AND TARGET OBS::obs-frontend-api)
        set(_obs_use_packages ON)
    endif()
endif()

if(NOT _obs_use_packages)
    set(_obs_source_hints "${OBS_SOURCE_DIR}" "$ENV{OBS_SOURCE_DIR}")
    set(_obs_build_hints "${OBS_BUILD_DIR}" "$ENV{OBS_BUILD_DIR}")

    find_path(OBS_LIBOBS_INCLUDE_DIR
        NAMES obs-module.h
        HINTS ${_obs_source_hints}
        PATH_SUFFIXES libobs obs
    )
    find_path(OBS_GENERATED_INCLUDE_DIR
        NAMES obsconfig.h obs-config.h
        HINTS ${_obs_build_hints}
        PATH_SUFFIXES config libobs obs
    )
    find_path(OBS_FRONTEND_API_INCLUDE_DIR
        NAMES obs-frontend-api.h
        HINTS ${_obs_source_hints}
        PATH_SUFFIXES frontend/api UI/obs-frontend-api obs
    )
    find_library(OBS_LIBOBS_LIBRARY
        NAMES obs libobs
        HINTS ${_obs_build_hints}
        PATH_SUFFIXES libobs libobs/Release lib
    )
    find_library(OBS_FRONTEND_API_LIBRARY
        NAMES obs-frontend-api
        HINTS ${_obs_build_hints}
        PATH_SUFFIXES frontend/api frontend/api/Release UI/obs-frontend-api UI/obs-frontend-api/Release lib
    )
endif()

find_package(Qt6 QUIET COMPONENTS Widgets)
if(NOT Qt6_FOUND)
    find_package(Qt5 QUIET COMPONENTS Widgets)
endif()

if(NOT OBS_AUTOSTOP_BUILD_PLUGIN)
    message(STATUS "OBS Auto Stop plugin build disabled")
    return()
endif()
if(NOT _obs_use_packages AND (NOT OBS_LIBOBS_INCLUDE_DIR OR NOT OBS_LIBOBS_LIBRARY
        OR NOT OBS_FRONTEND_API_INCLUDE_DIR OR NOT OBS_FRONTEND_API_LIBRARY))
    message(WARNING "OBS SDK not found. Install libobs-dev, or configure with -DOBS_SOURCE_DIR=... and -DOBS_BUILD_DIR=... to build the plugin.")
    return()
endif()
if(NOT Qt6_FOUND AND NOT Qt5_FOUND)
    message(WARNING "Qt Widgets not found. Install Qt6 Widgets to build the OBS Auto Stop dock UI.")
    return()
endif()

add_library(obs-auto-stop MODULE
    src/auto-stop-dock.cpp
    src/region-select-dialog.cpp
    src/motion-detector.cpp
    src/media-end-watcher.cpp
    src/silence-detector.cpp
    src/plugin-main.cpp
    src/recording-monitor.cpp
    src/stop-controller.cpp
)

target_include_directories(obs-auto-stop PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(_obs_use_packages)
    message(STATUS "OBS Auto Stop: using libobs / obs-frontend-api CMake packages")
    target_link_libraries(obs-auto-stop PRIVATE OBS::libobs OBS::obs-frontend-api)
else()
    message(STATUS "OBS Auto Stop: using OBS tree (${OBS_LIBOBS_INCLUDE_DIR})")
    # SYSTEM so OBS headers never trip the plugin's own warning flags.
    target_include_directories(obs-auto-stop SYSTEM PRIVATE
        "${OBS_LIBOBS_INCLUDE_DIR}"
        "${OBS_GENERATED_INCLUDE_DIR}"
        "${OBS_FRONTEND_API_INCLUDE_DIR}"
    )
    target_link_libraries(obs-auto-stop PRIVATE "${OBS_LIBOBS_LIBRARY}" "${OBS_FRONTEND_API_LIBRARY}")
endif()

if(Qt6_FOUND)
    target_link_libraries(obs-auto-stop PRIVATE Qt6::Widgets)
else()
    target_link_libraries(obs-auto-stop PRIVATE Qt5::Widgets)
endif()

if(MSVC)
    # Sources contain UTF-8 (Japanese) text; without /utf-8 MSVC reads them
    # in the system code page.
    target_compile_options(obs-auto-stop PRIVATE /utf-8 /W4 /permissive- /Gy /Gw)
    target_link_options(obs-auto-stop PRIVATE $<$<CONFIG:Release,MinSizeRel>:/OPT:REF /OPT:ICF>)
    if(OBS_AUTOSTOP_WARNINGS_AS_ERRORS)
        target_compile_options(obs-auto-stop PRIVATE /WX)
    endif()
else()
    target_compile_options(obs-auto-stop PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion)
    if(OBS_AUTOSTOP_WARNINGS_AS_ERRORS)
        target_compile_options(obs-auto-stop PRIVATE -Werror)
    endif()
    # Only obs_module_* (declared MODULE_EXPORT in obs-module.h) needs to be
    # visible; hide everything else and let the linker drop unused sections.
    set_target_properties(obs-auto-stop PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_compile_options(obs-auto-stop PRIVATE $<$<CONFIG:Release,MinSizeRel>:-ffunction-sections -fdata-sections>)
    if(NOT APPLE)
        target_link_options(obs-auto-stop PRIVATE $<$<CONFIG:Release,MinSizeRel>:-Wl,--gc-sections -Wl,--as-needed>)
    endif()
endif()

set_target_properties(obs-auto-stop PROPERTIES
    PREFIX ""
    OUTPUT_NAME "obs-auto-stop"
)

set(_obs_data_dir "${CMAKE_BINARY_DIR}/obs-auto-stop/data")
add_custom_command(TARGET obs-auto-stop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_obs_data_dir}/locale"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/data/locale"
        "${_obs_data_dir}/locale"
    COMMENT "Copying OBS Auto Stop locale data"
)

include(GNUInstallDirs)
install(TARGETS obs-auto-stop
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/obs-plugins
)
# libobs looks for module data in <datadir>/obs/obs-plugins/<module>/
install(DIRECTORY data/
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/obs-auto-stop
)
