# SPDX-License-Identifier: GPL-3.0-or-later

####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was copalConfig.cmake.in                            ########

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

include(CMakeFindDependencyMacro)

# The static library records its native backend link deps in its export set, so
# a consumer must be able to resolve them. Only pull them in when copal was
# actually built with those backends.
set(COPAL_WITH_SDL OFF)
set(COPAL_WITH_OPENGL OFF)
if(COPAL_WITH_OPENGL)
    find_dependency(OpenGL)
endif()
if(COPAL_WITH_SDL)
    find_dependency(SDL2)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/copalTargets.cmake")

check_required_components(copal)
