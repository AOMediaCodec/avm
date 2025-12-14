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
if(AVM_BUILD_CMAKE_AVM_CONFIGURE_CMAKE_)
  return()
endif() # AVM_BUILD_CMAKE_AVM_CONFIGURE_CMAKE_
set(AVM_BUILD_CMAKE_AVM_CONFIGURE_CMAKE_ 1)

include(FindGit)
include(FindPerl)
include(FindThreads)

include("${AVM_ROOT}/build/cmake/avm_config_defaults.cmake")
include("${AVM_ROOT}/build/cmake/avm_experiment_deps.cmake")
include("${AVM_ROOT}/build/cmake/avm_optimization.cmake")
include("${AVM_ROOT}/build/cmake/compiler_flags.cmake")
include("${AVM_ROOT}/build/cmake/compiler_tests.cmake")
include("${AVM_ROOT}/build/cmake/util.cmake")

if(DEFINED CONFIG_LOWBITDEPTH)
  message(WARNING "CONFIG_LOWBITDEPTH has been removed. \
    high bit depth internal pipeline is always used.")
endif()

# Generate the user config settings.
list(APPEND avm_build_vars ${AVM_CONFIG_VARS} ${AVM_OPTION_VARS})
foreach(cache_var ${avm_build_vars})
  get_property(
    cache_var_helpstring
    CACHE ${cache_var}
    PROPERTY HELPSTRING)
  if("${cache_var_helpstring}" STREQUAL "${cmake_cmdline_helpstring}")
    set(AVM_CMAKE_CONFIG "${AVM_CMAKE_CONFIG} -D${cache_var}=${${cache_var}}")
  endif()
endforeach()
string(STRIP "${AVM_CMAKE_CONFIG}" AVM_CMAKE_CONFIG)

# Detect target CPU.
if(NOT AVM_TARGET_CPU)
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" cpu_lowercase)
  if("${cpu_lowercase}" STREQUAL "amd64" OR "${cpu_lowercase}" STREQUAL
                                            "x86_64")
    if(${CMAKE_SIZEOF_VOID_P} EQUAL 4)
      set(AVM_TARGET_CPU "x86")
    elseif(${CMAKE_SIZEOF_VOID_P} EQUAL 8)
      set(AVM_TARGET_CPU "x86_64")
    else()
      message(
        FATAL_ERROR
          "--- Unexpected pointer size (${CMAKE_SIZEOF_VOID_P}) for\n"
          "      CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}\n"
          "      CMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}\n"
          "      CMAKE_GENERATOR=${CMAKE_GENERATOR}\n")
    endif()
  elseif("${cpu_lowercase}" STREQUAL "i386" OR "${cpu_lowercase}" STREQUAL
                                               "x86")
    set(AVM_TARGET_CPU "x86")
  elseif("${cpu_lowercase}" MATCHES "^arm" OR "${cpu_lowercase}" MATCHES
                                              "^mips")
    set(AVM_TARGET_CPU "${cpu_lowercase}")
  elseif("${cpu_lowercase}" MATCHES "aarch64")
    set(AVM_TARGET_CPU "arm64")
  elseif("${cpu_lowercase}" MATCHES "^ppc")
    set(AVM_TARGET_CPU "ppc")
  else()
    message(WARNING "The architecture ${CMAKE_SYSTEM_PROCESSOR} is not "
                    "supported, falling back to the generic target")
    set(AVM_TARGET_CPU "generic")
  endif()
endif()

if(CMAKE_TOOLCHAIN_FILE) # Add toolchain file to config string.
  if(IS_ABSOLUTE "${CMAKE_TOOLCHAIN_FILE}")
    file(RELATIVE_PATH toolchain_path "${AVM_CONFIG_DIR}"
         "${CMAKE_TOOLCHAIN_FILE}")
  else()
    set(toolchain_path "${CMAKE_TOOLCHAIN_FILE}")
  endif()
  set(toolchain_string "-DCMAKE_TOOLCHAIN_FILE=\\\"${toolchain_path}\\\"")
  set(AVM_CMAKE_CONFIG "${toolchain_string} ${AVM_CMAKE_CONFIG}")
else()

  # Add detected CPU to the config string.
  set(AVM_CMAKE_CONFIG "-DAVM_TARGET_CPU=${AVM_TARGET_CPU} ${AVM_CMAKE_CONFIG}")
endif()
set(AVM_CMAKE_CONFIG "-G \\\"${CMAKE_GENERATOR}\\\" ${AVM_CMAKE_CONFIG}")
file(RELATIVE_PATH source_path "${AVM_CONFIG_DIR}" "${AVM_ROOT}")
set(AVM_CMAKE_CONFIG "cmake ${source_path} ${AVM_CMAKE_CONFIG}")
string(STRIP "${AVM_CMAKE_CONFIG}" AVM_CMAKE_CONFIG)

message("--- avm_configure: Detected CPU: ${AVM_TARGET_CPU}")
set(AVM_TARGET_SYSTEM ${CMAKE_SYSTEM_NAME})

string(TOLOWER "${CMAKE_BUILD_TYPE}" build_type_lowercase)
if("${build_type_lowercase}" STREQUAL "debug")
  set(CONFIG_DEBUG 1)
endif()

if(BUILD_SHARED_LIBS)
  set(CONFIG_PIC 1)
  set(CONFIG_SHARED 1)
endif()

if(NOT MSVC)
  if(CONFIG_PIC)

    # TODO(tomfinegan): clang needs -pie in CMAKE_EXE_LINKER_FLAGS for this to
    # work.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    if("${AVM_TARGET_SYSTEM}" STREQUAL "Linux" AND "${AVM_TARGET_CPU}" MATCHES
                                                   "^armv[78]")
      set(AVM_AS_FLAGS ${AVM_AS_FLAGS} --defsym PIC=1)
    else()
      set(AVM_AS_FLAGS ${AVM_AS_FLAGS} -DPIC)
    endif()
  endif()
endif()

if("${AVM_TARGET_CPU}" STREQUAL "x86" OR "${AVM_TARGET_CPU}" STREQUAL "x86_64")
  find_program(CMAKE_ASM_NASM_COMPILER yasm $ENV{YASM_PATH})
  if(NOT CMAKE_ASM_NASM_COMPILER OR ENABLE_NASM)
    unset(CMAKE_ASM_NASM_COMPILER CACHE)
    find_program(CMAKE_ASM_NASM_COMPILER nasm $ENV{NASM_PATH})
  endif()

  include(CheckLanguage)
  check_language(ASM_NASM)
  if(CMAKE_ASM_NASM_COMPILER)
    get_asm_obj_format("objformat")
    unset(CMAKE_ASM_NASM_OBJECT_FORMAT)
    set(CMAKE_ASM_NASM_OBJECT_FORMAT ${objformat})
    enable_language(ASM_NASM)
    if(CMAKE_ASM_NASM_COMPILER_ID STREQUAL "NASM")
      test_nasm()
    endif()
    # Xcode requires building the objects manually, so pass the object format
    # flag.
    if(XCODE)
      set(AVM_AS_FLAGS -f ${objformat} ${AVM_AS_FLAGS})
    endif()
  else()
    message(
      FATAL_ERROR
        "Unable to find assembler. Install 'yasm' or 'nasm.' "
        "To build without optimizations, add -DAVM_TARGET_CPU=generic to "
        "your cmake command line.")
  endif()
  string(STRIP "${AVM_AS_FLAGS}" AVM_AS_FLAGS)
elseif("${AVM_TARGET_CPU}" MATCHES "arm")
  if("${AVM_TARGET_SYSTEM}" STREQUAL "Darwin")
    if(NOT CMAKE_ASM_COMPILER)
      set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})
    endif()
    set(AVM_AS_FLAGS -arch ${AVM_TARGET_CPU} -isysroot ${CMAKE_OSX_SYSROOT})
  elseif("${AVM_TARGET_SYSTEM}" STREQUAL "Windows")
    if(NOT CMAKE_ASM_COMPILER)
      set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER} "-c -mimplicit-it=always")
    endif()
  else()
    if(NOT CMAKE_ASM_COMPILER)
      set(CMAKE_ASM_COMPILER as)
    endif()
  endif()
  include(CheckLanguage)
  check_language(ASM)
  if(NOT CMAKE_ASM_COMPILER)
    message(
      FATAL_ERROR
        "Unable to find assembler and optimizations are enabled."
        "Searched for ${CMAKE_ASM_COMPILER}. Install it, add it to your path,"
        "or set the assembler directly by adding "
        "-DCMAKE_ASM_COMPILER=<assembler path> to your CMake command line."
        "To build without optimizations, add -DAVM_TARGET_CPU=generic to your "
        "cmake command line.")
  endif()
  enable_language(ASM)
  string(STRIP "${AVM_AS_FLAGS}" AVM_AS_FLAGS)
endif()

if(CONFIG_ANALYZER)
  include(FindwxWidgets)
  find_package(wxWidgets REQUIRED adv base core)
  include(${wxWidgets_USE_FILE})
endif()

if(NOT MSVC AND CMAKE_C_COMPILER_ID MATCHES "GNU\|Clang")
  set(CONFIG_GCC 1)
endif()

if(CONFIG_GCOV)
  message("--- Testing for CONFIG_GCOV support.")
  require_linker_flag("-fprofile-arcs -ftest-coverage")
  require_compiler_flag("-fprofile-arcs -ftest-coverage" YES)
endif()

if(CONFIG_GPROF)
  message("--- Testing for CONFIG_GPROF support.")
  require_compiler_flag("-pg" YES)
endif()

if("${AVM_TARGET_SYSTEM}" MATCHES "Darwin\|Linux\|Windows\|Android")
  set(CONFIG_OS_SUPPORT 1)
endif()

# Define macros that affect Windows headers.
if("${AVM_TARGET_SYSTEM}" STREQUAL "Windows")
  # The default _WIN32_WINNT value in MinGW is 0x0502 (Windows XP with SP2). Set
  # it to 0x0601 (Windows 7).
  add_compiler_flag_if_supported("-D_WIN32_WINNT=0x0601")
  # Tell windows.h to not define the min() and max() macros. This allows us to
  # use std::min(), std::numeric_limits<T>::max(), etc. in C++ code.
  add_compiler_flag_if_supported("-DNOMINMAX")
endif()

# We increase stack size for MSVC to 8 MB (default stack size on Linux) to avoid
# stack overflow. See issue https://gitlab.com/AOMediaCodec/avm/-/issues/786 for
# repro case and an analysis of large stack variables.
if(MSVC)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /STACK:8388608")
endif()

#
# Fix CONFIG_* dependencies. This must be done before including cpu.cmake to
# ensure RTCD_CONFIG_* are properly set.
fix_experiment_configs()

# Test compiler support.
avm_get_inline("INLINE")

# Don't just check for pthread.h, but use the result of the full pthreads
# including a linking check in FindThreads above.
set(HAVE_PTHREAD_H ${CMAKE_USE_PTHREADS_INIT})
avm_check_source_compiles("unistd_check" "#include <unistd.h>" HAVE_UNISTD_H)

if(NOT WIN32)
  avm_push_var(CMAKE_REQUIRED_LIBRARIES "m")
  avm_check_c_compiles(
    "fenv_check"
    "#define _GNU_SOURCE
                        #include <fenv.h>
                        void unused(void) {
                          (void)unused;
                          (void)feenableexcept(FE_DIVBYZERO | FE_INVALID);
                        }"
    HAVE_FEXCEPT)
  avm_pop_var(CMAKE_REQUIRED_LIBRARIES)
endif()

include("${AVM_ROOT}/build/cmake/cpu.cmake")

if(ENABLE_CCACHE)
  set_compiler_launcher(ENABLE_CCACHE ccache)
endif()

if(ENABLE_DISTCC)
  set_compiler_launcher(ENABLE_DISTCC distcc)
endif()

if(ENABLE_GOMA)
  set_compiler_launcher(ENABLE_GOMA gomacc)
endif()

if(NOT CONFIG_AV2_DECODER AND NOT CONFIG_AV2_ENCODER)
  message(FATAL_ERROR "Decoder and encoder disabled, nothing to build.")
endif()

if(DECODE_HEIGHT_LIMIT OR DECODE_WIDTH_LIMIT)
  change_config_and_warn(CONFIG_SIZE_LIMIT 1
                         "DECODE_HEIGHT_LIMIT and DECODE_WIDTH_LIMIT")
endif()

if(CONFIG_SIZE_LIMIT)
  if(NOT DECODE_HEIGHT_LIMIT OR NOT DECODE_WIDTH_LIMIT)
    message(FATAL_ERROR "When setting CONFIG_SIZE_LIMIT, DECODE_HEIGHT_LIMIT "
                        "and DECODE_WIDTH_LIMIT must be set.")
  endif()
endif()

# Test compiler flags.
if(MSVC)
  # It isn't possible to specify C99 conformance for MSVC.
  add_cxx_flag_if_supported("/std:c++17")
  add_compiler_flag_if_supported("/W3")

  # Disable MSVC warnings that suggest making code non-portable.
  add_compiler_flag_if_supported("/wd4996")
  if(ENABLE_WERROR)
    add_compiler_flag_if_supported("/WX")
  endif()
else()
  require_c_flag("-std=c99" YES)
  require_cxx_flag_nomsvc("-std=c++17" YES)
  add_compiler_flag_if_supported("-Wall")
  add_compiler_flag_if_supported("-Wdisabled-optimization")
  add_compiler_flag_if_supported("-Wextra")
  add_compiler_flag_if_supported("-Wfloat-conversion")
  add_compiler_flag_if_supported("-Wlogical-op")
  add_compiler_flag_if_supported("-Wno-comment")
  add_compiler_flag_if_supported("-Wpointer-arith")
  add_compiler_flag_if_supported("-Wshorten-64-to-32")
  add_compiler_flag_if_supported("-Wsign-compare")
  add_compiler_flag_if_supported("-Wstring-conversion")
  add_compiler_flag_if_supported("-Wtype-limits")
  add_compiler_flag_if_supported("-Wuninitialized")
  add_compiler_flag_if_supported("-Wunused")
  add_compiler_flag_if_supported("-Wvla")

  if(CMAKE_C_COMPILER_ID MATCHES "GNU")
    add_c_flag_if_supported("-Wstack-usage=960000")
    add_cxx_flag_if_supported("-Wstack-usage=960000")
  endif()

  if(CMAKE_C_COMPILER_ID MATCHES "GNU" AND SANITIZE MATCHES "address")
    # Disable no optimization warning when compiling with sanitizers
    add_compiler_flag_if_supported("-Wno-disabled-optimization")
  endif()

  # Flags valid for C, but not for C++.
  add_c_flag_if_supported("-Wimplicit-function-declaration")

  # Add -Wshadow only for C files to avoid massive gtest warning spam.
  add_c_flag_if_supported("-Wshadow")

  # Add -Wundef only for C files to avoid massive gtest warning spam.
  add_c_flag_if_supported("-Wundef")

  # Quiet gcc 6 vs 7 abi warnings:
  # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=77728
  if("${AVM_TARGET_CPU}" MATCHES "arm")
    add_cxx_flag_if_supported("-Wno-psabi")
  endif()

  if(ENABLE_WERROR)
    add_compiler_flag_if_supported("-Werror")
  endif()

  if(ENABLE_ASSERTS)
    add_compiler_flag_if_supported("-UNDEBUG")
  endif()

  if("${build_type_lowercase}" MATCHES "rel")
    add_compiler_flag_if_supported("-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0")
  endif()
  add_compiler_flag_if_supported("-D_LARGEFILE_SOURCE")
  add_compiler_flag_if_supported("-D_FILE_OFFSET_BITS=64")
endif()

set(AVM_LIB_LINK_TYPE PUBLIC)
if(EMSCRIPTEN)

  # Avoid CMake generation time errors resulting from collisions with the form
  # of target_link_libraries() used by Emscripten.cmake.
  unset(AVM_LIB_LINK_TYPE)
endif()

# Generate avm_config templates.
set(avm_config_asm_template "${AVM_CONFIG_DIR}/config/avm_config.asm.cmake")
set(avm_config_h_template "${AVM_CONFIG_DIR}/config/avm_config.h.cmake")
execute_process(
  COMMAND
    ${CMAKE_COMMAND} -DAVM_CONFIG_DIR=${AVM_CONFIG_DIR} -DAVM_ROOT=${AVM_ROOT}
    -P "${AVM_ROOT}/build/cmake/generate_avm_config_templates.cmake")

# Generate avm_config.{asm,h}.
configure_file("${avm_config_asm_template}"
               "${AVM_CONFIG_DIR}/config/avm_config.asm")
configure_file("${avm_config_h_template}"
               "${AVM_CONFIG_DIR}/config/avm_config.h")

# Read the current git hash.
find_package(Git)
if(NOT GIT_FOUND)
  message("--- Git missing, version will be read from CHANGELOG.")
endif()

configure_file("${AVM_ROOT}/build/cmake/avm_config.c.template"
               "${AVM_CONFIG_DIR}/config/avm_config.c")

# Find Perl and generate the RTCD sources.
find_package(Perl)
if(NOT PERL_FOUND)
  message(FATAL_ERROR "Perl is required to build libavm.")
endif()

set(AVM_RTCD_CONFIG_FILE_LIST
    "${AVM_ROOT}/avm_dsp/avm_dsp_rtcd_defs.pl"
    "${AVM_ROOT}/avm_scale/avm_scale_rtcd.pl"
    "${AVM_ROOT}/av2/common/av2_rtcd_defs.pl")
set(AVM_RTCD_HEADER_FILE_LIST
    "${AVM_CONFIG_DIR}/config/avm_dsp_rtcd.h"
    "${AVM_CONFIG_DIR}/config/avm_scale_rtcd.h"
    "${AVM_CONFIG_DIR}/config/av2_rtcd.h")
set(AVM_RTCD_SOURCE_FILE_LIST
    "${AVM_ROOT}/avm_dsp/avm_dsp_rtcd.c"
    "${AVM_ROOT}/avm_scale/avm_scale_rtcd.c"
    "${AVM_ROOT}/av2/common/av2_rtcd.c")
set(AVM_RTCD_SYMBOL_LIST avm_dsp_rtcd avm_scale_rtcd av2_rtcd)
list(LENGTH AVM_RTCD_SYMBOL_LIST AVM_RTCD_CUSTOM_COMMAND_COUNT)
math(EXPR AVM_RTCD_CUSTOM_COMMAND_COUNT "${AVM_RTCD_CUSTOM_COMMAND_COUNT} - 1")

foreach(NUM RANGE ${AVM_RTCD_CUSTOM_COMMAND_COUNT})
  list(GET AVM_RTCD_CONFIG_FILE_LIST ${NUM} AVM_RTCD_CONFIG_FILE)
  list(GET AVM_RTCD_HEADER_FILE_LIST ${NUM} AVM_RTCD_HEADER_FILE)
  list(GET AVM_RTCD_SOURCE_FILE_LIST ${NUM} AVM_RTCD_SOURCE_FILE)
  list(GET AVM_RTCD_SYMBOL_LIST ${NUM} AVM_RTCD_SYMBOL)
  execute_process(
    COMMAND
      ${PERL_EXECUTABLE} "${AVM_ROOT}/build/cmake/rtcd.pl"
      --arch=${AVM_TARGET_CPU} --sym=${AVM_RTCD_SYMBOL} ${AVM_RTCD_FLAGS}
      --config=${AVM_CONFIG_DIR}/config/avm_config.h ${AVM_RTCD_CONFIG_FILE}
    OUTPUT_FILE ${AVM_RTCD_HEADER_FILE})
endforeach()

# Generate avm_version.h.
execute_process(
  COMMAND
    ${CMAKE_COMMAND} -DAVM_CONFIG_DIR=${AVM_CONFIG_DIR} -DAVM_ROOT=${AVM_ROOT}
    -DGIT_EXECUTABLE=${GIT_EXECUTABLE} -DPERL_EXECUTABLE=${PERL_EXECUTABLE} -P
    "${AVM_ROOT}/build/cmake/version.cmake")
