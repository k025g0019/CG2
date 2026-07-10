# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause


####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was ovphysxConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

# ovphysx CMake Configuration File
# This file provides configuration for projects that want to use ovphysx via find_package()

# Check if the package has already been found
if(ovphysx_FOUND)
    return()
endif()

# Set version information
set(ovphysx_VERSION "0.4.13")

# Include the targets file
include("${CMAKE_CURRENT_LIST_DIR}/ovphysxTargets.cmake")

# Resolve package prefix from install lib dir (relocatable)
set(ovphysx_LIB_DIR "${PACKAGE_PREFIX_DIR}/lib")
get_filename_component(OVPHYSX_PACKAGE_PREFIX "${ovphysx_LIB_DIR}/.." ABSOLUTE)

# Define the main target alias for convenience
# Set up variables that consuming projects might need
get_target_property(ovphysx_INCLUDE_DIRS ovphysx::ovphysx INTERFACE_INCLUDE_DIRECTORIES)

# PhysX SDK headers for physx pointer interop (headers only, no linking needed)
set(ovphysx_PHYSX_INCLUDE_DIR "${OVPHYSX_PACKAGE_PREFIX}/include/physx")

# For legacy projects that use variables instead of targets
# Modern CMake projects should just use: target_link_libraries(myapp ovphysx::ovphysx)
set(ovphysx_LIBRARIES ovphysx::ovphysx)

# Add plugins directory to link search path
# libovphysx.so depends on libcarb.so which lives in plugins/
set(ovphysx_PLUGINS_DIR "${PACKAGE_PREFIX_DIR}/plugins")
if(EXISTS "${ovphysx_PLUGINS_DIR}")
    set_property(TARGET ovphysx::ovphysx APPEND PROPERTY
        INTERFACE_LINK_DIRECTORIES "${ovphysx_PLUGINS_DIR}"
    )
endif()

# Runtime search paths for consumers (no PATH/LD_LIBRARY_PATH required)
if(UNIX AND NOT APPLE)
    set(_ovphysx_runtime_rpath
        "${PACKAGE_PREFIX_DIR}/lib"
        "${ovphysx_PLUGINS_DIR}"
        "${ovphysx_PLUGINS_DIR}/gpu"
    )
    foreach(_dir IN LISTS _ovphysx_runtime_rpath)
        set_property(TARGET ovphysx::ovphysx APPEND PROPERTY
            INTERFACE_LINK_OPTIONS "-Wl,-rpath,${_dir}"
        )
    endforeach()
endif()

# Windows: helper to copy runtime DLLs next to a target
if(WIN32)
    function(ovphysx_copy_runtime_dlls target)
        if(NOT TARGET ${target})
            message(FATAL_ERROR "ovphysx_copy_runtime_dlls: target not found: ${target}")
        endif()

        # Runtime layout (Windows SDK):
        # - Core DLLs (e.g. ovphysx.dll, carb.dll, etc.) are installed to <prefix>/bin
        #   via CMake's RUNTIME DESTINATION.
        # - Carbonite/PhysX plugins are packaged under <prefix>/plugins (flat).
        # - GPU-only plugins live under <prefix>/plugins/gpu and are copied as
        #   part of the plugins tree. Do not copy them beside the executable;
        #   CarboniteLoader only makes that directory visible when GPU is enabled.
        # - Some third-party runtime deps may live under <prefix>/plugins/bin/deps.
        set(_ovphysx_runtime_dirs
            "${OVPHYSX_PACKAGE_PREFIX}/bin"
            "${ovphysx_PLUGINS_DIR}"
            "${ovphysx_PLUGINS_DIR}/bin/deps"
        )
        foreach(_dir IN LISTS _ovphysx_runtime_dirs)
            if(EXISTS "${_dir}")
                if(_dir STREQUAL "${ovphysx_PLUGINS_DIR}")
                    # Keep the installed plugins/ layout next to the consumer
                    # executable so Carbonite can discover plugins the same way
                    # ovphysx expects them in the packaged SDK.
                    add_custom_command(TARGET ${target} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E make_directory
                            "$<TARGET_FILE_DIR:${target}>/plugins"
                        COMMAND ${CMAKE_COMMAND} -E copy_directory
                            "${_dir}" "$<TARGET_FILE_DIR:${target}>/plugins"
                        COMMENT "ovphysx: copy plugins from ${_dir}"
                    )

                    # Windows also needs the top-level plugin DLLs beside the
                    # executable itself. The loader resolves dependent DLLs
                    # before main() starts and does not search the plugins/
                    # subdirectory automatically, so copy those DLLs into the
                    # executable directory in addition to the full plugins tree.
                    # Evaluated at configure time; downstream projects must
                    # reconfigure if the installed plugin DLL set changes.
                    file(GLOB _ovphysx_plugin_dlls "${_dir}/*.dll")
                    foreach(_plugin_dll IN LISTS _ovphysx_plugin_dlls)
                        add_custom_command(TARGET ${target} POST_BUILD
                            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                                "${_plugin_dll}" "$<TARGET_FILE_DIR:${target}>"
                            COMMENT "ovphysx: copy top-level plugin DLLs from ${_dir}"
                        )
                    endforeach()
                else()
                    add_custom_command(TARGET ${target} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_directory
                            "${_dir}" "$<TARGET_FILE_DIR:${target}>"
                        COMMENT "ovphysx: copy runtime DLLs from ${_dir}"
                    )
                endif()
            endif()
        endforeach()
    endfunction()
endif()

# Check that all required components are available
set(ovphysx_FOUND TRUE)

# Provide information about what was found
if(NOT ovphysx_FIND_QUIETLY)
    message(STATUS "Found ovphysx: ${ovphysx_VERSION}")
    message(STATUS "  Include directories: ${ovphysx_INCLUDE_DIRS}")
    message(STATUS "  Libraries: ${ovphysx_LIBRARIES}")
    message(STATUS "  Plugins directory: ${ovphysx_PLUGINS_DIR}")
    message(STATUS "  PhysX include directory: ${ovphysx_PHYSX_INCLUDE_DIR}")
endif()

# Handle required/optional components
check_required_components(ovphysx)
