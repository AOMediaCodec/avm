#
# Copyright (c) 2021, Alliance for Open Media. All rights reserved
#
# This source code is subject to the terms of the BSD 3-Clause Clear License and
# the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
# License was not distributed with this source code in the LICENSE file, you can
# obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance
# for Open Media Patent License 1.0 was not distributed with this source code in
# the PATENTS file, you can obtain it at aomedia.org/license/patent-license/.
#
if(AOM_BUILD_CMAKE_TENSORFLOW_LITE_CMAKE_)
  return()
endif() # AOM_BUILD_CMAKE_TENSORFLOW_LITE_CMAKE_
set(AOM_BUILD_CMAKE_TENSORFLOW_LITE_CMAKE_ 1)

include(ExternalProject)
include(FindGit)

# Checks if Tensorflow has been checked out -- if not, uses the git submodule
# command to fetch it.
function(checkout_submodule_)
  # As a quick sanity check, see if at least 1 expected file or directory is
  # present in each submodule. If so, assume they are all checked out (if they
  # are not, then the base directory will be empty).
  if(EXISTS "${AOM_ROOT}/third_party/tensorflow/tensorflow")
    return()
  endif()
  if(NOT GIT_FOUND)
    message(
      FATAL_ERROR
        "Tensorflow-Lite not present; " "git could not be found; "
        "please check out submodules with 'git submodule update --init'")
  endif()
  # Note that "git submodule update --init" must be run from inside the git
  # repository; the --git-dir flag does not work.
  message("Checking out Tensorflow-Lite submodule")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" submodule update --init
    WORKING_DIRECTORY "${AOM_ROOT}"
    OUTPUT_VARIABLE submodule_out
    ERROR_VARIABLE submodule_err
    RESULT_VARIABLE submodule_result)
  if(NOT ${submodule_result} EQUAL 0)
    message(
      FATAL_ERROR
        "Unable to run 'git submodule update --init': "
        "Return code: "
        ${submodule_result}
        ", STDOUT: "
        ${submodule_out}
        ", STDERR: "
        ${submodule_err})
  endif()
endfunction()

# Add the TF-lite link-related library to the named target (e.g., an executable
# or library). This function handles the different naming conventions of
# operating systems.
function(target_link_tf_lite_dep_ named_target subdir libname)
  if(NOT (("${subdir}" STREQUAL "") OR ("${subdir}" MATCHES "/$")))
    message(
      FATAL_ERROR "sub-directory must be empty or end with a slash: ${subdir}")
  endif()

  set(STATIC_LIBRARY_DIR "")
  if(MSVC)
    set(STATIC_LIBRARY_DIR "$<CONFIG>/")
  endif()
  target_link_libraries(
    ${named_target}
    PRIVATE
      "${CMAKE_BINARY_DIR}/tensorflow_lite/${subdir}${STATIC_LIBRARY_DIR}${CMAKE_STATIC_LIBRARY_PREFIX}${libname}${CMAKE_STATIC_LIBRARY_SUFFIX}"
  )
endfunction()

# Add TF-lite libraries onto the named target at link time (e.g., an executable
# or library). For enabling TF-lite for experiments, prefer the
# "experiment_requires_tf_lite" function.
function(target_link_tf_lite_libraries named_target)
  target_link_libraries(${named_target} ${AOM_LIB_LINK_TYPE} Threads::Threads)
  target_link_libraries(${named_target} PRIVATE fake_dl)
  target_link_tf_lite_dep_(${named_target} "" tensorflow-lite)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/flags/
                           absl_flags)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/flags/
                           absl_flags_internal)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/flags/
                           absl_flags_config)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/flags/
                           absl_flags_program_name)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/flags/
                           absl_flags_marshalling)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/hash/
                           absl_hash)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/hash/
                           absl_city)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/status/
                           absl_status)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/types/
                           absl_bad_optional_access)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/strings/
                           absl_cord)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/strings/
                           absl_str_format_internal)
  target_link_tf_lite_dep_(
    ${named_target} _deps/abseil-cpp-build/absl/synchronization/
    absl_synchronization)
  target_link_tf_lite_dep_(
    ${named_target} _deps/abseil-cpp-build/absl/debugging/ absl_stacktrace)
  target_link_tf_lite_dep_(
    ${named_target} _deps/abseil-cpp-build/absl/debugging/ absl_symbolize)
  target_link_tf_lite_dep_(
    ${named_target} _deps/abseil-cpp-build/absl/debugging/
    absl_debugging_internal)
  target_link_tf_lite_dep_(
    ${named_target} _deps/abseil-cpp-build/absl/debugging/
    absl_demangle_internal)
  target_link_tf_lite_dep_(
    ${named_target} _deps/abseil-cpp-build/absl/synchronization/
    absl_graphcycles_internal)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/base/
                           absl_malloc_internal)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/time/
                           absl_time)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/strings/
                           absl_strings)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/strings/
                           absl_strings_internal)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/base/
                           absl_throw_delegate)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/base/
                           absl_base)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/base/
                           absl_spinlock_wait)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/numeric/
                           absl_int128)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/time/
                           absl_civil_time)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/time/
                           absl_time_zone)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/types/
                           absl_bad_variant_access)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/base/
                           absl_raw_logging_internal)
  target_link_tf_lite_dep_(${named_target} _deps/abseil-cpp-build/absl/base/
                           absl_log_severity)
  target_link_tf_lite_dep_(${named_target} _deps/farmhash-build/ farmhash)
  target_link_tf_lite_dep_(${named_target} _deps/fft2d-build/ fft2d_fftsg2d)
  target_link_tf_lite_dep_(${named_target} _deps/fft2d-build/ fft2d_fftsg)
  target_link_tf_lite_dep_(${named_target} _deps/flatbuffers-build/ flatbuffers)
  target_link_tf_lite_dep_(${named_target} _deps/xnnpack-build/ XNNPACK)
  target_link_tf_lite_dep_(${named_target} _deps/ruy-build/ ruy)
  target_link_tf_lite_dep_(${named_target} cpuinfo/ cpuinfo)
  target_link_tf_lite_dep_(${named_target} clog/ clog)
  target_link_tf_lite_dep_(${named_target} pthreadpool/ pthreadpool)
endfunction()

# Can Tensorflow-Lite be enabled with the current build system? Sets the
# variable with either 0 or 1 as the value. If 0, also prints an explanatory
# message.
function(is_tflite_supported result)
  # Cross-compile is not currently implemented.
  if(CMAKE_TOOLCHAIN_FILE)
    message("TOOLCHAIN: ${CMAKE_TOOLCHAIN_FILE}")
    message(WARNING "No cross-compile support for TensorFlow Lite; disabling")
    set(${result}
        0
        PARENT_SCOPE)
    return()
  endif()
  # TF-Lite specifies a minimum CMake version of 3.16, but Jenkins uses 3.7.2.
  # Until Jenkins is upgraded, disable TF-Lite if a lower version of CMake is
  # detected.
  if(${CMAKE_VERSION} VERSION_LESS "3.16")
    message(
      WARNING "Tensorflow Lite requres CMake version 3.16 or higher; version "
              "${CMAKE_VERSION} detected; disabling")
    set(${result}
        0
        PARENT_SCOPE)
    return()
  endif()
  set(${result}
      1
      PARENT_SCOPE)
endfunction()

# Adds appropriate build rules / targets. Only invoke this function if
# is_tf_lite_supported returns true.
function(setup_tensorflow_lite)
  if("${AOM_ROOT}" STREQUAL "")
    message(FATAL_ERROR "AOM_ROOT variable must not be empty.")
  endif()

  if(MSVC)
    add_compile_definitions(NOMINMAX=1)
  endif()
  checkout_submodule_()

  # Allow code to reference TF.
  include_directories("${AOM_ROOT}/third_party/tensorflow")

  externalproject_add(
    tensorflow_lite
    SOURCE_DIR "${AOM_ROOT}/third_party/tensorflow/tensorflow/lite"
    PREFIX "${CMAKE_BINARY_DIR}/tensorflow_lite"
    BINARY_DIR "${CMAKE_BINARY_DIR}/tensorflow_lite"
    DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/tensorflow_lite"
    CMAKE_ARGS "-DCMAKE_BUILD_TYPE=Release"
    LOG_BUILD 1)

  # TF-Lite uses dlsym and dlopen for delegation, but linking with -ldl is not
  # supported in static builds. Use a dummy implementation (callers must not use
  # delegation).
  add_library(fake_dl OBJECT "${AOM_ROOT}/common/fake_dl.h"
                             "${AOM_ROOT}/common/fake_dl.cc")

  # TF-Lite depends on this, and downloads it during compilation.
  include_directories(
    "${CMAKE_CURRENT_BINARY_DIR}/tensorflow_lite/flatbuffers/include/")

  add_dependencies(aom_av1_common tensorflow_lite fake_dl)
  foreach(aom_app ${AOM_APP_TARGETS})
    add_dependencies(${aom_app} tensorflow_lite fake_dl)
    target_link_tf_lite_libraries(${aom_app})
  endforeach()
endfunction()

# Signal that the experiment needs TF-lite enabled.
function(experiment_requires_tf_lite experiment_name)
  # Experiment is not enabled, no need to include TF-Lite in the build.
  if(NOT ${${experiment_name}})
    return()
  endif()
  set(supported 0)
  is_tflite_supported(supported)
  if(NOT ${supported})
    # Disable the experiment so Gerrit will not test this case.
    message(WARNING "Disabling ${experiment_name}.")
    set(${experiment_name}
        0
        PARENT_SCOPE)
    set(CONFIG_TENSORFLOW_LITE
        0
        PARENT_SCOPE)
    return()
  endif()
  # Otherwise, enable TF-lite.
  set(CONFIG_TENSORFLOW_LITE
      1
      PARENT_SCOPE)
endfunction()
