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
cmake_minimum_required(VERSION 3.16)

set(AVM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(AVM_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}")
if("${AVM_ROOT}" STREQUAL "${AVM_CONFIG_DIR}")
  message(
    FATAL_ERROR
      "Building from within the avm source tree is not supported.\n"
      "Hint: Run these commands\n"
      "$ rm -rf CMakeCache.txt CMakeFiles\n"
      "$ mkdir -p ../avm_build\n"
      "$ cd ../avm_build\n"
      "And re-run CMake from the avm_build directory.")
endif()

project(AVM C CXX ASM)

# GENERATED source property global visibility.
if(POLICY CMP0118)
  cmake_policy(SET CMP0118 NEW)
endif()

if(NOT EMSCRIPTEN)
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE
        "Release"
        CACHE STRING "Build type: Debug, Release, RelWithDebInfo or MinSizeRel"
              FORCE)
  endif()
endif()

# Updating version info.
# https://www.gnu.org/software/libtool/manual/libtool.html#Updating-version-info
set(SO_VERSION 2)
set(SO_FILE_VERSION 2.0.0)

# Enable generators like Xcode and Visual Studio to place projects in folders.
set_property(GLOBAL PROPERTY USE_FOLDERS TRUE)

include("${AVM_ROOT}/build/cmake/avm_configure.cmake")
include("${AVM_ROOT}/avm_dsp/avm_dsp.cmake")
include("${AVM_ROOT}/avm_mem/avm_mem.cmake")
include("${AVM_ROOT}/avm_ports/avm_ports.cmake")
include("${AVM_ROOT}/avm_scale/avm_scale.cmake")
include("${AVM_ROOT}/avm_util/avm_util.cmake")
include("${AVM_ROOT}/av2/av2.cmake")
include("${AVM_ROOT}/build/cmake/avm_install.cmake")
include("${AVM_ROOT}/build/cmake/sanitizers.cmake")
include("${AVM_ROOT}/build/cmake/util.cmake")
include("${AVM_ROOT}/test/test.cmake")

list(
  APPEND
  AVM_RTCD_SOURCES
  "${AVM_CONFIG_DIR}/config/avm_dsp_rtcd.h"
  "${AVM_CONFIG_DIR}/config/avm_scale_rtcd.h"
  "${AVM_CONFIG_DIR}/config/av2_rtcd.h"
  "${AVM_ROOT}/avm_dsp/avm_dsp_rtcd_defs.pl"
  "${AVM_ROOT}/avm_dsp/avm_dsp_rtcd.c"
  "${AVM_ROOT}/avm_scale/avm_scale_rtcd.pl"
  "${AVM_ROOT}/avm_scale/avm_scale_rtcd.c"
  "${AVM_ROOT}/av2/common/av2_rtcd_defs.pl"
  "${AVM_ROOT}/av2/common/av2_rtcd.c"
  "${AVM_ROOT}/build/cmake/rtcd.pl")

list(
  APPEND
  AVM_LIBWEBM_SOURCES
  "${AVM_ROOT}/third_party/libwebm/common/hdr_util.cc"
  "${AVM_ROOT}/third_party/libwebm/common/hdr_util.h"
  "${AVM_ROOT}/third_party/libwebm/common/webmids.h"
  "${AVM_ROOT}/third_party/libwebm/mkvmuxer/mkvmuxer.cc"
  "${AVM_ROOT}/third_party/libwebm/mkvmuxer/mkvmuxer.h"
  "${AVM_ROOT}/third_party/libwebm/mkvmuxer/mkvmuxertypes.h"
  "${AVM_ROOT}/third_party/libwebm/mkvmuxer/mkvmuxerutil.cc"
  "${AVM_ROOT}/third_party/libwebm/mkvmuxer/mkvmuxerutil.h"
  "${AVM_ROOT}/third_party/libwebm/mkvmuxer/mkvwriter.cc"
  "${AVM_ROOT}/third_party/libwebm/mkvmuxer/mkvwriter.h"
  "${AVM_ROOT}/third_party/libwebm/mkvparser/mkvparser.cc"
  "${AVM_ROOT}/third_party/libwebm/mkvparser/mkvparser.h"
  "${AVM_ROOT}/third_party/libwebm/mkvparser/mkvreader.cc"
  "${AVM_ROOT}/third_party/libwebm/mkvparser/mkvreader.h")

list(
  APPEND
  AVM_LIBYUV_SOURCES
  "${AVM_ROOT}/third_party/libyuv/include/libyuv/basic_types.h"
  "${AVM_ROOT}/third_party/libyuv/include/libyuv/convert.h"
  "${AVM_ROOT}/third_party/libyuv/include/libyuv/convert_argb.h"
  "${AVM_ROOT}/third_party/libyuv/include/libyuv/convert_from.h"
  "${AVM_ROOT}/third_party/libyuv/include/libyuv/cpu_id.h"
  "${AVM_ROOT}/third_party/libyuv/include/libyuv/planar_functions.h"
  "${AVM_ROOT}/third_party/libyuv/include/libyuv/rotate.h"
  "${AVM_ROOT}/third_party/libyuv/include/libyuv/row.h"
  "${AVM_ROOT}/third_party/libyuv/include/libyuv/scale.h"
  "${AVM_ROOT}/third_party/libyuv/include/libyuv/scale_row.h"
  "${AVM_ROOT}/third_party/libyuv/source/cpu_id.cc"
  "${AVM_ROOT}/third_party/libyuv/source/planar_functions.cc"
  "${AVM_ROOT}/third_party/libyuv/source/row_any.cc"
  "${AVM_ROOT}/third_party/libyuv/source/row_common.cc"
  "${AVM_ROOT}/third_party/libyuv/source/row_gcc.cc"
  "${AVM_ROOT}/third_party/libyuv/source/row_mips.cc"
  "${AVM_ROOT}/third_party/libyuv/source/row_neon.cc"
  "${AVM_ROOT}/third_party/libyuv/source/row_neon64.cc"
  "${AVM_ROOT}/third_party/libyuv/source/row_win.cc"
  "${AVM_ROOT}/third_party/libyuv/source/scale.cc"
  "${AVM_ROOT}/third_party/libyuv/source/scale_any.cc"
  "${AVM_ROOT}/third_party/libyuv/source/scale_common.cc"
  "${AVM_ROOT}/third_party/libyuv/source/scale_gcc.cc"
  "${AVM_ROOT}/third_party/libyuv/source/scale_mips.cc"
  "${AVM_ROOT}/third_party/libyuv/source/scale_neon.cc"
  "${AVM_ROOT}/third_party/libyuv/source/scale_neon64.cc"
  "${AVM_ROOT}/third_party/libyuv/source/scale_win.cc")

list(
  APPEND
  AVM_SOURCES
  "${AVM_CONFIG_DIR}/config/avm_config.c"
  "${AVM_CONFIG_DIR}/config/avm_config.h"
  "${AVM_ROOT}/avm/avm.h"
  "${AVM_ROOT}/avm/avm_codec.h"
  "${AVM_ROOT}/avm/avm_decoder.h"
  "${AVM_ROOT}/avm/avm_encoder.h"
  "${AVM_ROOT}/avm/avm_frame_buffer.h"
  "${AVM_ROOT}/avm/avm_image.h"
  "${AVM_ROOT}/avm/avm_integer.h"
  "${AVM_ROOT}/avm/avmcx.h"
  "${AVM_ROOT}/avm/avmdx.h"
  "${AVM_ROOT}/avm/internal/avm_codec_internal.h"
  "${AVM_ROOT}/avm/internal/avm_image_internal.h"
  "${AVM_ROOT}/avm/src/avm_codec.c"
  "${AVM_ROOT}/avm/src/avm_decoder.c"
  "${AVM_ROOT}/avm/src/avm_encoder.c"
  "${AVM_ROOT}/avm/src/avm_image.c"
  "${AVM_ROOT}/avm/src/avm_integer.c")

list(
  APPEND
  AVM_COMMON_APP_UTIL_SOURCES
  "${AVM_ROOT}/av2/arg_defs.h"
  "${AVM_ROOT}/av2/arg_defs.c"
  "${AVM_ROOT}/common/args_helper.c"
  "${AVM_ROOT}/common/args_helper.h"
  "${AVM_ROOT}/common/args.c"
  "${AVM_ROOT}/common/args.h"
  "${AVM_ROOT}/common/av2_config.c"
  "${AVM_ROOT}/common/av2_config.h"
  "${AVM_ROOT}/common/md5_utils.c"
  "${AVM_ROOT}/common/md5_utils.h"
  "${AVM_ROOT}/common/tools_common.c"
  "${AVM_ROOT}/common/tools_common.h"
  "${AVM_ROOT}/common/video_common.h"
  "${AVM_ROOT}/common/rawenc.c"
  "${AVM_ROOT}/common/rawenc.h"
  "${AVM_ROOT}/common/y4menc.c"
  "${AVM_ROOT}/common/y4menc.h")

list(APPEND AVM_LANCZOS_RESAMPLE_SOURCES
     "${AVM_ROOT}/common/lanczos_resample.c"
     "${AVM_ROOT}/common/lanczos_resample.h")

list(
  APPEND
  AVM_DECODER_APP_UTIL_SOURCES
  "${AVM_ROOT}/common/ivfdec.c"
  "${AVM_ROOT}/common/ivfdec.h"
  "${AVM_ROOT}/common/obudec.c"
  "${AVM_ROOT}/common/obudec.h"
  "${AVM_ROOT}/common/video_reader.c"
  "${AVM_ROOT}/common/video_reader.h")

list(
  APPEND
  AVM_ENCODER_APP_UTIL_SOURCES
  "${AVM_ROOT}/common/ivfenc.c"
  "${AVM_ROOT}/common/ivfenc.h"
  "${AVM_ROOT}/common/video_writer.c"
  "${AVM_ROOT}/common/video_writer.h"
  "${AVM_ROOT}/common/warnings.c"
  "${AVM_ROOT}/common/warnings.h"
  "${AVM_ROOT}/common/y4minput.c"
  "${AVM_ROOT}/common/y4minput.h"
  "${AVM_ROOT}/common/stream_iter.c"
  "${AVM_ROOT}/examples/encoder_util.h"
  "${AVM_ROOT}/examples/encoder_util.c")

list(APPEND AVM_ENCODER_STATS_SOURCES "${AVM_ROOT}/stats/rate_hist.c"
     "${AVM_ROOT}/stats/rate_hist.h")

list(APPEND AVM_VERSION_SOURCES "${AVM_CONFIG_DIR}/config/avm_version.h")

list(APPEND AVM_WEBM_DECODER_SOURCES "${AVM_ROOT}/common/webmdec.cc"
     "${AVM_ROOT}/common/webmdec.h")

list(APPEND AVM_WEBM_ENCODER_SOURCES "${AVM_ROOT}/common/webmenc.cc"
     "${AVM_ROOT}/common/webmenc.h")

include_directories(
  ${AVM_ROOT}
  ${AVM_CONFIG_DIR}
  ${AVM_ROOT}/apps
  ${AVM_ROOT}/common
  ${AVM_ROOT}/examples
  ${AVM_ROOT}/stats
  ${AVM_ROOT}/third_party/abseil-cpp)

# Targets
add_library(avm_version ${AVM_VERSION_SOURCES})
add_dummy_source_file_to_target(avm_version c)
add_custom_command(
  OUTPUT "${AVM_CONFIG_DIR}/config/avm_version.h"
  COMMAND
    ${CMAKE_COMMAND} ARGS -DAVM_CONFIG_DIR=${AVM_CONFIG_DIR}
    -DAVM_ROOT=${AVM_ROOT} -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
    -DPERL_EXECUTABLE=${PERL_EXECUTABLE} -P
    "${AVM_ROOT}/build/cmake/version.cmake"
  COMMENT "Writing avm_version.h"
  VERBATIM)

add_custom_target(
  avm_version_check
  COMMAND
    ${CMAKE_COMMAND} -DAVM_CONFIG_DIR=${AVM_CONFIG_DIR} -DAVM_ROOT=${AVM_ROOT}
    -DGIT_EXECUTABLE=${GIT_EXECUTABLE} -DPERL_EXECUTABLE=${PERL_EXECUTABLE} -P
    "${AVM_ROOT}/build/cmake/version.cmake"
  COMMENT "Updating version info if necessary."
  VERBATIM)

if(BUILD_SHARED_LIBS AND NOT MSVC)
  # Generate version file immediately for non-MSVC shared builds: The version
  # string is needed for the avm target.
  execute_process(
    COMMAND
      ${CMAKE_COMMAND} -DAVM_CONFIG_DIR=${AVM_CONFIG_DIR}
      -DAVM_ROOT=${AVM_ROOT} -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
      -DPERL_EXECUTABLE=${PERL_EXECUTABLE} -P
      "${AVM_ROOT}/build/cmake/version.cmake")
endif()

add_dependencies(avm_version avm_version_check)

# TODO(tomfinegan): Move rtcd target setup where it belongs for each rtcd
# source.
add_rtcd_build_step(
  "${AVM_ROOT}/avm_dsp/avm_dsp_rtcd_defs.pl"
  "${AVM_CONFIG_DIR}/config/avm_dsp_rtcd.h"
  "${AVM_ROOT}/avm_dsp/avm_dsp_rtcd.c" "avm_dsp_rtcd")
add_rtcd_build_step(
  "${AVM_ROOT}/avm_scale/avm_scale_rtcd.pl"
  "${AVM_CONFIG_DIR}/config/avm_scale_rtcd.h"
  "${AVM_ROOT}/avm_scale/avm_scale_rtcd.c" "avm_scale_rtcd")
add_rtcd_build_step(
  "${AVM_ROOT}/av2/common/av2_rtcd_defs.pl"
  "${AVM_CONFIG_DIR}/config/av2_rtcd.h" "${AVM_ROOT}/av2/common/av2_rtcd.c"
  "av2_rtcd")

add_library(avm_rtcd OBJECT ${AVM_RTCD_SOURCES})
add_dependencies(avm_rtcd avm_version)

if(ENABLE_EXAMPLES)
  add_library(avm_encoder_stats OBJECT ${AVM_ENCODER_STATS_SOURCES})
  set(AVM_LIB_TARGETS ${AVM_LIB_TARGETS} avm_encoder_stats)
endif()

add_library(avm ${AVM_SOURCES} $<TARGET_OBJECTS:avm_rtcd>)
if(BUILD_SHARED_LIBS)
  add_library(avm_static STATIC ${AVM_SOURCES} $<TARGET_OBJECTS:avm_rtcd>)
  set_target_properties(avm_static PROPERTIES OUTPUT_NAME avm)

  if(NOT MSVC)
    # Extract version string and set VERSION/SOVERSION for the avm target.
    extract_version_string("${AVM_CONFIG_DIR}/config/avm_version.h"
                           avm_version_triple)

    # Strip any trailing version information, if present.
    string(FIND "${avm_version_triple}" "-" dash_pos)
    if(NOT dash_pos EQUAL -1)
      string(SUBSTRING "${avm_version_triple}" 0 ${dash_pos} avm_version_triple)
    endif()

    # cmake-format: off
    # VERSION is embedded in the .so file name.
    # libavm.so -> libavm.so.SOVERSION
    # libavm.so.SOVERSION -> libavm.so.VERSION
    # libavm.so.VERSION
    # cmake-format: on
    set_target_properties(avm PROPERTIES SOVERSION ${SO_VERSION})
    set_target_properties(avm PROPERTIES VERSION ${SO_FILE_VERSION})
  endif()
endif()

if(NOT WIN32 AND NOT APPLE)
  target_link_libraries(avm ${AVM_LIB_LINK_TYPE} m)
  if(BUILD_SHARED_LIBS)
    target_link_libraries(avm_static ${AVM_LIB_LINK_TYPE} m)
  endif()
endif()

# List of object and static library targets.
set(AVM_LIB_TARGETS ${AVM_LIB_TARGETS} avm_rtcd avm_mem avm_scale avm)
if(BUILD_SHARED_LIBS)
  set(AVM_LIB_TARGETS ${AVM_LIB_TARGETS} avm_static)
endif()

add_library(lanczos_resample OBJECT ${AVM_LANCZOS_RESAMPLE_SOURCES})
list(APPEND AVM_LIB_TARGETS lanczos_resample)
target_sources(avm PRIVATE $<TARGET_OBJECTS:lanczos_resample>)
if(BUILD_SHARED_LIBS)
  target_sources(avm_static PRIVATE $<TARGET_OBJECTS:lanczos_resample>)
endif()

# Setup dependencies.
setup_avm_dsp_targets()
setup_avm_mem_targets()
setup_avm_ports_targets()
setup_avm_util_targets()
setup_avm_scale_targets()
setup_av2_targets()

# Make all library targets depend on avm_rtcd to make sure it builds first.
foreach(avm_lib ${AVM_LIB_TARGETS})
  if(NOT "${avm_lib}" STREQUAL "avm_rtcd")
    add_dependencies(${avm_lib} avm_rtcd)
  endif()
endforeach()

# Generate C/C++ stub files containing the function usage_exit(). Users of the
# avm_common_app_util library must define this function. This is a convenience
# to allow omission of the function from applications that might want to use
# other pieces of the util support without defining usage_exit().
file(WRITE "${AVM_GEN_SRC_DIR}/usage_exit.c" "void usage_exit(void) {}")
file(WRITE "${AVM_GEN_SRC_DIR}/usage_exit.cc"
     "extern \"C\" void usage_exit(void) {}")

#
# Application and application support targets.
#
if(ENABLE_EXAMPLES
   OR ENABLE_TESTS
   OR ENABLE_TOOLS)
  add_library(avm_common_app_util OBJECT ${AVM_COMMON_APP_UTIL_SOURCES})
  set_property(TARGET ${example} PROPERTY FOLDER examples)
  # common/av2_config.c depends on internal headers that require *rtcd.h
  add_dependencies(avm_common_app_util avm_rtcd)
  if(CONFIG_AV2_DECODER)
    add_library(avm_decoder_app_util OBJECT ${AVM_DECODER_APP_UTIL_SOURCES})
    set_property(TARGET ${example} PROPERTY FOLDER examples)
    # obudec depends on internal headers that require *rtcd.h
    add_dependencies(avm_decoder_app_util avm_rtcd)
  endif()
  if(CONFIG_AV2_ENCODER)
    add_library(avm_encoder_app_util OBJECT ${AVM_ENCODER_APP_UTIL_SOURCES})
    set_property(TARGET ${example} PROPERTY FOLDER examples)
  endif()
endif()

if((CONFIG_AV2_DECODER OR CONFIG_AV2_ENCODER) AND ENABLE_EXAMPLES)
  add_executable(resize_util "${AVM_ROOT}/examples/resize_util.c"
                             $<TARGET_OBJECTS:avm_common_app_util>)
  set_property(TARGET ${example} PROPERTY FOLDER examples)
  list(APPEND AVM_APP_TARGETS resize_util)
  add_executable(
    lanczos_resample_y4m
    "${AVM_ROOT}/tools/lanczos/lanczos_resample.c"
    "${AVM_ROOT}/tools/lanczos/lanczos_resample.h"
    "${AVM_ROOT}/tools/lanczos/lanczos_resample_y4m.c")
  add_executable(
    lanczos_resample_yuv
    "${AVM_ROOT}/tools/lanczos/lanczos_resample.c"
    "${AVM_ROOT}/tools/lanczos/lanczos_resample.h"
    "${AVM_ROOT}/tools/lanczos/lanczos_resample_yuv.c")
  list(APPEND AVM_APP_TARGETS lanczos_resample_y4m lanczos_resample_yuv)
  add_executable(
    lanczos_resample_filter
    "${AVM_ROOT}/tools/lanczos/lanczos_resample.c"
    "${AVM_ROOT}/tools/lanczos/lanczos_resample.h"
    "${AVM_ROOT}/tools/lanczos/lanczos_resample_filter.c")
  list(APPEND AVM_APP_TARGETS lanczos_resample_filter)
endif()

if(CONFIG_AV2_DECODER AND ENABLE_EXAMPLES)
  add_executable(
    avmdec "${AVM_ROOT}/apps/avmdec.c" $<TARGET_OBJECTS:avm_common_app_util>
           $<TARGET_OBJECTS:avm_decoder_app_util>)
  target_sources(avmdec PRIVATE $<TARGET_OBJECTS:lanczos_resample>)
  add_executable(
    decode_to_md5
    "${AVM_ROOT}/examples/decode_to_md5.c"
    $<TARGET_OBJECTS:avm_common_app_util>
    $<TARGET_OBJECTS:avm_decoder_app_util>)
  add_executable(
    decode_with_drops
    "${AVM_ROOT}/examples/decode_with_drops.c"
    $<TARGET_OBJECTS:avm_common_app_util>
    $<TARGET_OBJECTS:avm_decoder_app_util>)
  add_executable(
    simple_decoder
    "${AVM_ROOT}/examples/simple_decoder.c"
    $<TARGET_OBJECTS:avm_common_app_util>
    $<TARGET_OBJECTS:avm_decoder_app_util>)
  add_executable(
    scalable_decoder
    "${AVM_ROOT}/examples/scalable_decoder.c"
    $<TARGET_OBJECTS:avm_common_app_util>
    $<TARGET_OBJECTS:avm_decoder_app_util>)

  if(CONFIG_ANALYZER)
    add_executable(
      analyzer
      "${AVM_ROOT}/examples/analyzer.cc" $<TARGET_OBJECTS:avm_common_app_util>
      $<TARGET_OBJECTS:avm_decoder_app_util>)
    target_link_libraries(analyzer ${AVM_LIB_LINK_TYPE} ${wxWidgets_LIBRARIES})
    list(APPEND AVM_APP_TARGETS analyzer)
    list(APPEND AVM_DECODER_EXAMPLE_TARGETS analyzer)
  endif()

  if(CONFIG_INSPECTION)
    add_executable(
      inspect
      "${AVM_ROOT}/examples/inspect.c" $<TARGET_OBJECTS:avm_common_app_util>
      $<TARGET_OBJECTS:avm_decoder_app_util>)
    list(APPEND AVM_DECODER_EXAMPLE_TARGETS inspect)

    if(EMSCRIPTEN)
      add_preproc_definition(_POSIX_SOURCE)
      append_link_flag_to_target("inspect" "--emrun")
      append_link_flag_to_target("inspect" "-s USE_PTHREADS=0")
      append_link_flag_to_target("inspect" "-s WASM=1")
      append_link_flag_to_target("inspect" "-s MODULARIZE=1")
      append_link_flag_to_target("inspect" "-s ALLOW_MEMORY_GROWTH=1")
      append_link_flag_to_target(
        "inspect" "-s \'EXTRA_EXPORTED_RUNTIME_METHODS=[\"UTF8ToString\"]\'")
      append_link_flag_to_target("inspect"
                                 "-s EXPORT_NAME=\"\'DecoderModule\'\"")
      append_link_flag_to_target("inspect" "--memory-init-file 0")

      if("${CMAKE_BUILD_TYPE}" STREQUAL "")

        # Default to -O3 when no build type is specified.
        append_compiler_flag("-O3")
      endif()

      em_link_post_js(inspect "${AVM_ROOT}/tools/inspect-post.js")
    endif()
  endif()

  if(CONFIG_EXTRACT_PROTO)
    list(APPEND AVM_EXTRACT_PROTO_SOURCES
         "${AVM_ROOT}/tools/extract_proto/extract_proto.cc"
         "${AVM_ROOT}/tools/extract_proto/enum_mappings.h")
    set(AVM_FRAME_PROTO_PATH "${AVM_ROOT}/tools/extract_proto/avm_frame.proto")
    find_package(Protobuf REQUIRED)
    set(ABSL_PROPAGATE_CXX_STD ON)
    include_directories(${Protobuf_INCLUDE_DIRS})
    protobuf_generate_cpp(AVM_FRAME_PROTO_SRCS AVM_FRAME_PROTO_HDRS
                          ${AVM_FRAME_PROTO_PATH})
    add_subdirectory(third_party/abseil-cpp)
    # Find Python and generate the enums mapping for extract_proto.
    find_package(Python3)
    if(NOT PYTHON3_FOUND)
      message(FATAL_ERROR "python3 is required to build extract_proto.")
    endif()

    set(EXTRACT_PROTO_ENUMS_MAPPING_HEADER
        "${AVM_ROOT}/tools/extract_proto/enum_mappings.h")
    add_custom_command(
      OUTPUT ${EXTRACT_PROTO_ENUMS_MAPPING_HEADER}
      COMMAND
        ${Python_EXECUTABLE}
        "${AVM_ROOT}/tools/extract_proto/generate_enum_mappings.py"
        --enums_header "${AVM_ROOT}/av2/common/av2_common_int.h" --enums_header
        "${AVM_ROOT}/av2/common/blockd.h" --enums_header
        "${AVM_ROOT}/av2/common/enums.h" --enums_header
        "${AVM_ROOT}/av2/common/filter.h" --enums_header
        "${AVM_ROOT}/av2/common/mv.h" --enums_header
        "${AVM_ROOT}/av2/decoder/accounting.h" --output
        "${AVM_ROOT}/tools/extract_proto/enum_mappings.h"
      DEPENDS "${AVM_ROOT}/tools/extract_proto/generate_enum_mappings.py"
              "${AVM_ROOT}/av2/common/av2_common_int.h"
              "${AVM_ROOT}/av2/common/blockd.h"
              "${AVM_ROOT}/av2/common/enums.h"
              "${AVM_ROOT}/av2/common/filter.h"
              "${AVM_ROOT}/av2/common/mv.h"
              "${AVM_ROOT}/av2/decoder/accounting.h"
      VERBATIM)
    add_executable(
      extract_proto
      ${AVM_EXTRACT_PROTO_SOURCES} ${AVM_FRAME_PROTO_SRCS}
      ${AVM_FRAME_PROTO_HDRS} $<TARGET_OBJECTS:avm_common_app_util>
      $<TARGET_OBJECTS:avm_decoder_app_util>)
    target_link_libraries(
      extract_proto
      PRIVATE absl::base
              absl::flags
              absl::flags_parse
              absl::log
              absl::log_flags
              absl::log_globals
              absl::log_initialize
              absl::status
              absl::statusor
              absl::strings
              ${Protobuf_LIBRARIES})

    list(APPEND AVM_APP_TARGETS extract_proto)
  endif()

  # Maintain a list of decoder example targets.
  list(
    APPEND
    AVM_DECODER_EXAMPLE_TARGETS
    avmdec
    decode_to_md5
    decode_with_drops
    scalable_decoder
    simple_decoder)

  # Add decoder examples to the app targets list.
  list(APPEND AVM_APP_TARGETS ${AVM_DECODER_EXAMPLE_TARGETS})
endif()

if(CONFIG_AV2_ENCODER)
  if(ENABLE_EXAMPLES)
    add_executable(
      avmenc
      "${AVM_ROOT}/apps/avmenc.c" $<TARGET_OBJECTS:avm_common_app_util>
      $<TARGET_OBJECTS:avm_encoder_app_util>
      $<TARGET_OBJECTS:avm_encoder_stats>)
    add_executable(
      lossless_encoder
      "${AVM_ROOT}/examples/lossless_encoder.c"
      $<TARGET_OBJECTS:avm_common_app_util>
      $<TARGET_OBJECTS:avm_encoder_app_util>)
    add_executable(
      set_maps
      "${AVM_ROOT}/examples/set_maps.c" $<TARGET_OBJECTS:avm_common_app_util>
      $<TARGET_OBJECTS:avm_encoder_app_util>)
    add_executable(
      simple_encoder
      "${AVM_ROOT}/examples/simple_encoder.c"
      $<TARGET_OBJECTS:avm_common_app_util>
      $<TARGET_OBJECTS:avm_encoder_app_util>)
    add_executable(
      twopass_encoder
      "${AVM_ROOT}/examples/twopass_encoder.c"
      $<TARGET_OBJECTS:avm_common_app_util>
      $<TARGET_OBJECTS:avm_encoder_app_util>)
    add_executable(
      noise_model
      "${AVM_ROOT}/examples/noise_model.c"
      $<TARGET_OBJECTS:avm_common_app_util>
      $<TARGET_OBJECTS:avm_encoder_app_util>)
    add_executable(
      scalable_encoder
      "${AVM_ROOT}/examples/scalable_encoder.c"
      $<TARGET_OBJECTS:avm_common_app_util>
      $<TARGET_OBJECTS:avm_encoder_app_util>)

    # Maintain a list of encoder example targets.
    list(
      APPEND
      AVM_ENCODER_EXAMPLE_TARGETS
      avmenc
      lossless_encoder
      noise_model
      set_maps
      simple_encoder
      scalable_encoder
      twopass_encoder)
  endif()

  if(ENABLE_TOOLS)
    if(CONFIG_ENTROPY_STATS AND NOT BUILD_SHARED_LIBS)

      # TODO(tomfinegan): Sort out why a simple link command with
      # avm_entropy_optimizer.c won't work on macos, but dragging in all the
      # helper machinery allows the link to succeed.
      add_executable(
        avm_entropy_optimizer
        "${AVM_GEN_SRC_DIR}/usage_exit.c"
        "${AVM_ROOT}/tools/avm_entropy_optimizer.c"
        $<TARGET_OBJECTS:avm_common_app_util>
        $<TARGET_OBJECTS:avm_encoder_app_util>)

      # Maintain a list of encoder tool targets.
      list(APPEND AVM_ENCODER_TOOL_TARGETS avm_entropy_optimizer)
    endif()
  endif()

  # Add encoder examples and tools to the targets list.
  list(APPEND AVM_APP_TARGETS ${AVM_ENCODER_EXAMPLE_TARGETS}
       ${AVM_ENCODER_TOOL_TARGETS})

  if(CONFIG_TENSORFLOW_LITE)
    # Building TFLite from third_party sources. Previously TFLite was fetched
    # from github as part of the build, using the following syntax:
    # cmake-format: off
    # include(FetchContent)
    # set(TFLITE_TAG "v2.17.0")
    # fetchcontent_declare(
    #   tflite
    #   GIT_REPOSITORY https://github.com/tensorflow/tensorflow
    #   GIT_TAG ${TFLITE_TAG}
    #   GIT_SHALLOW TRUE)
    # fetchcontent_getproperties(tflite)
    # if(NOT tflite_POPULATED)
    #   fetchcontent_populate(tflite)
    #   add_subdirectory(${tflite_SOURCE_DIR}/tensorflow/lite/c
    #                    ${tflite_BINARY_DIR})
    # endif()
    # cmake-format: on
    #
    # However some enviroments don't allow internet access during cmake build,
    # so the new approach is to store all the necessary TFLite dependencies as
    # archives inside of the third_party directory. These archives are extracted
    # below and built as sub-projects. The fetchcontent based snippet above can
    # be used in the future to update the stored archives for the dependencies
    # should TFlite version be updated,
    message(STATUS "Building TFLite from local sources...")
    # These are the paths of the stored TFLite dependencies, use the the
    # fetchcontent snippet above, then look for messages like: "Performing
    # download step (download, verify and extract) for 'xyz'" go to
    # "xyz-source", Then archive the source into "xyz.tar.gz" and and add the
    # variable "XYZ_SOURCE_DIR" pointing to "third_party/xyz" like below, also
    # update the command line to extrac the archive below.

    string(
      CONCAT
        EXTRACT_TF_UPDATE_DEPS_CMAKE_CONFIG
        "cd ${CMAKE_CURRENT_SOURCE_DIR}/third_party;"
        "tar -xzf tensorflow.tar.gz; "
        "cd tensorflow; "
        "patch -p1 < ../tensorflow-designated-initializers.patch; "
        "cd ..; "
        "tar -xzf deps_cmake.tar.gz; "
        "cp deps_cmake/abseil-cpp.cmake ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tensorflow/tensorflow/lite/tools/cmake/modules; "
        "cp deps_cmake/cpuinfo.cmake ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tensorflow/tensorflow/lite/tools/cmake/modules; "
        "cp deps_cmake/eigen.cmake ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tensorflow/tensorflow/lite/tools/cmake/modules; "
        "cp deps_cmake/farmhash.cmake ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tensorflow/tensorflow/lite/tools/cmake/modules; "
        "cp deps_cmake/fft2d.cmake ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tensorflow/tensorflow/lite/tools/cmake/modules; "
        "cp deps_cmake/flatbuffers.cmake ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tensorflow/tensorflow/lite/tools/cmake/modules; "
        "cp deps_cmake/gemmlowp.cmake ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tensorflow/tensorflow/lite/tools/cmake/modules; "
        "cp deps_cmake/ml_dtypes.cmake ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tensorflow/tensorflow/lite/tools/cmake/modules; "
        "cp deps_cmake/neon2sse.cmake ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tensorflow/tensorflow/lite/tools/cmake/modules; "
        "cp deps_cmake/protobuf.cmake ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tensorflow/tensorflow/lite/tools/cmake/modules; "
        "cp deps_cmake/ruy.cmake ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tensorflow/tensorflow/lite/tools/cmake/modules; "
        "cp deps_cmake/xnnpack.cmake ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tensorflow/tensorflow/lite/tools/cmake/modules"
    )

    execute_process(COMMAND bash -c "${EXTRACT_TF_UPDATE_DEPS_CMAKE_CONFIG}")
    string(
      CONCAT EXTRACT_DEPS_TO_BUILD
             "cd ${CMAKE_CURRENT_SOURCE_DIR}/third_party; "
             "tar -xzf abseil-cpp.tar.gz -C ${CMAKE_BINARY_DIR}; "
             "tar -xzf eigen.tar.gz -C ${CMAKE_BINARY_DIR}; "
             "tar -xzf neon2sse.tar.gz -C ${CMAKE_BINARY_DIR}; "
             "tar -xzf xnnpack.tar.gz -C ${CMAKE_BINARY_DIR}; "
             "tar -xzf ruy.tar.gz -C ${CMAKE_BINARY_DIR}; "
             "tar -xzf farmhash.tar.gz -C ${CMAKE_BINARY_DIR}; "
             "tar -xzf fft2d.tar.gz -C ${CMAKE_BINARY_DIR}; "
             "tar -xzf gemmlowp.tar.gz -C ${CMAKE_BINARY_DIR}; "
             "tar -xzf ml_dtypes.tar.gz -C ${CMAKE_BINARY_DIR}; "
             "tar -xzf cpuinfo.tar.gz -C ${CMAKE_BINARY_DIR}; "
             "tar -xzf protobuf.tar.gz -C ${CMAKE_BINARY_DIR}; "
             "tar -xzf flatbuffers.tar.gz -C ${CMAKE_BINARY_DIR}; "
             "tar -xzf fp16.tar.gz -C ${CMAKE_BINARY_DIR}; "
             "tar -xzf pthreadpool.tar.gz -C ${CMAKE_BINARY_DIR}; "
             "tar -xzf psimd.tar.gz -C ${CMAKE_BINARY_DIR}; "
             "tar -xzf FXdiv.tar.gz -C ${CMAKE_BINARY_DIR}; "
             "tar -xzf benchmark.tar.gz -C ${CMAKE_BINARY_DIR}; "
             "tar -xzf googletest.tar.gz -C ${CMAKE_BINARY_DIR}")

    execute_process(COMMAND bash -c "${EXTRACT_DEPS_TO_BUILD}")

    set(FP16_SOURCE_DIR ${CMAKE_BINARY_DIR}/fp16)
    set(PTHREADPOOL_SOURCE_DIR ${CMAKE_BINARY_DIR}/pthreadpool)
    set(PSIMD_SOURCE_DIR ${CMAKE_BINARY_DIR}/psimd)
    set(FXDIV_SOURCE_DIR ${CMAKE_BINARY_DIR}/FXdiv)
    set(GOOGLEBENCHMARK_SOURCE_DIR ${CMAKE_BINARY_DIR}/benchmark)
    set(GOOGLETEST_SOURCE_DIR ${CMAKE_BINARY_DIR}/googletest)
    set(abseil-cpp_SOURCE_DIR ${CMAKE_BINARY_DIR}/abseil-cpp)
    set(CPUINFO_SOURCE_DIR ${CMAKE_BINARY_DIR}/cpuinfo)
    set(EIGEN_SOURCE_DIR ${CMAKE_BINARY_DIR}/eigen)
    set(FARMHASH_SOURCE_DIR ${CMAKE_BINARY_DIR}/farmhash)
    set(FFT2D_SOURCE_DIR ${CMAKE_BINARY_DIR}/fft2d)
    set(FLATBUFFERS_SOURCE_DIR ${CMAKE_BINARY_DIR}/flatbuffers)
    set(GEMMLOWP_SOURCE_DIR ${CMAKE_BINARY_DIR}/gemmlowp)
    set(ML_DTYPES_SOURCE_DIR ${CMAKE_BINARY_DIR}/ml_dtypes)
    set(PROTOBUF_SOURCE_DIR ${CMAKE_BINARY_DIR}/protobuf)
    set(RUY_SOURCE_DIR ${CMAKE_BINARY_DIR}/ruy)
    set(NEON2SSE_SOURCE_DIR ${CMAKE_BINARY_DIR}/neon2sse)
    set(XNNPACK_SOURCE_DIR ${CMAKE_BINARY_DIR}/XNNPACK)

    # static linking makes life with TFLite much easier
    set(TFLITE_C_BUILD_SHARED_LIBS OFF)

    # Keep RUY on. Ruy is a general GEMM library.
    set(TFLITE_ENABLE_RUY ON)
    # Turn XNNPack on.
    set(TFLITE_ENABLE_XNNPACK ON)

    # Some of the subprojects (e.g. Eigen) are very noisy and emit status
    # messages all the time. Temporary ignore status messages while adding this
    # to silence it. Ugly but effective.
    set(OLD_CMAKE_MESSAGE_LOG_LEVEL ${CMAKE_MESSAGE_LOG_LEVEL})
    set(CMAKE_MESSAGE_LOG_LEVEL WARNING)
    add_subdirectory(third_party/tensorflow/tensorflow/lite/c)
    set(CMAKE_MESSAGE_LOG_LEVEL ${OLD_CMAKE_MESSAGE_LOG_LEVEL})

    # Disable some noisy warnings in tflite
    target_compile_options(tensorflow-lite PRIVATE -w)
    include_directories(${AVM_ROOT}/third_party/tensorflow)
    include_directories("${CMAKE_CURRENT_BINARY_DIR}/flatbuffers/include/")
    add_dependencies(avm_av2_common tensorflow-lite)

    # Figure out all the directories that were add_subdirectory-ed.
    set(ALL_DEP_DIRS ${AVM_ROOT}/third_party/tensorflow/tensorflow/lite/c)
    while(TRUE)
      list(LENGTH ALL_DEP_DIRS ALL_DEP_DIRS_LEN)
      set(DIR_LIST ${ALL_DEP_DIRS})
      foreach(DIR ${DIR_LIST})
        get_directory_property(SUBDIRS DIRECTORY ${DIR} SUBDIRECTORIES)
        list(APPEND ALL_DEP_DIRS ${SUBDIRS})
      endforeach()
      list(REMOVE_DUPLICATES ALL_DEP_DIRS)
      list(LENGTH ALL_DEP_DIRS NEW_ALL_DEP_DIRS_LEN)
      # Break when no new folder is found.
      if(${ALL_DEP_DIRS_LEN} EQUAL ${NEW_ALL_DEP_DIRS_LEN})
        break()
      endif()
    endwhile()
    # Suppress the warnings for each target defined in the dependecy
    # directories.
    foreach(DEP_DIR ${ALL_DEP_DIRS})
      get_directory_property(DEP_TARGETS DIRECTORY ${DEP_DIR}
                                                   BUILDSYSTEM_TARGETS)
      foreach(DEP_TARGET ${DEP_TARGETS})
        get_target_property(DEP_TYPE ${DEP_TARGET} TYPE)
        if(${DEP_TYPE} STREQUAL "INTERFACE_LIBRARY")
          target_compile_options(${DEP_TARGET} INTERFACE -w)
        elseif(NOT (${DEP_TYPE} STREQUAL "UTILITY"))
          # There is a custom UTILITY type, so do not deal with it.
          target_compile_options(${DEP_TARGET} PUBLIC -w)
        endif()
      endforeach()
    endforeach()
  endif()

  if(CONFIG_USE_VMAF_RC AND NOT CONFIG_TUNE_VMAF)
    message(FATAL_ERROR "Turn on CONFIG_TUNE_VMAF to use CONFIG_USE_VMAF_RC.")
  endif()

  if(CONFIG_TUNE_VMAF)
    find_package(PkgConfig)
    if(CONFIG_USE_VMAF_RC)
      if(PKG_CONFIG_FOUND)
        pkg_check_modules(VMAF_RC REQUIRED libvmaf_rc)
        target_link_libraries(avm PRIVATE ${VMAF_RC_LDFLAGS}
                                          ${VMAF_RC_LIBRARIES})
        target_include_directories(avm PRIVATE ${VMAF_RC_INCLUDE_DIRS})
        if(VMAF_RC_CFLAGS)
          append_compiler_flag("${VMAF_RC_CFLAGS}")
        endif()
      else()
        message(FATAL_ERROR "CONFIG_USE_VMAF_RC error: pkg-config not found.")
      endif()
    else()
      if(PKG_CONFIG_FOUND)
        pkg_check_modules(VMAF REQUIRED libvmaf)
      else()
        find_library(VMAF_LIBRARIES vmaf)
        find_path(VMAF_INCLUDE_DIRS libvmaf.h PATH_SUFFIXES libvmaf)
        if(VMAF_LIBRARIES AND VMAF_INCLUDE_DIRS)
          message(STATUS "Found VMAF library: ${VMAF_LIBRARIES}")
          message(STATUS "Found VMAF include: ${VMAF_INCLUDE_DIRS}")
        else()
          message(FATAL_ERROR "VMAF library not found.")
        endif()
      endif()
      target_link_libraries(avm PRIVATE ${VMAF_LDFLAGS} ${VMAF_LIBRARIES})
      target_include_directories(avm PRIVATE ${VMAF_INCLUDE_DIRS})
      if(VMAF_CFLAGS)
        append_compiler_flag("${VMAF_CFLAGS}")
      endif()
    endif()
    set_target_properties(avm PROPERTIES LINKER_LANGUAGE CXX)
    if(BUILD_SHARED_LIBS)
      set_target_properties(avm_static PROPERTIES LINKER_LANGUAGE CXX)
    endif()
  endif()
endif()

if(ENABLE_EXAMPLES)

  # Maintain a separate variable listing only the examples to facilitate
  # installation of example programs into an examples sub directory of
  # $AVM_DIST_DIR/bin when building the dist target.
  list(APPEND AVM_EXAMPLE_TARGETS ${AVM_DECODER_EXAMPLE_TARGETS}
       ${AVM_ENCODER_EXAMPLE_TARGETS})
endif()

if(ENABLE_TOOLS)
  if(CONFIG_AV2_DECODER)
    add_executable(
      dump_obu
      "${AVM_GEN_SRC_DIR}/usage_exit.cc"
      "${AVM_ROOT}/tools/dump_obu.cc"
      "${AVM_ROOT}/tools/obu_parser.cc"
      "${AVM_ROOT}/tools/obu_parser.h"
      $<TARGET_OBJECTS:avm_common_app_util>
      $<TARGET_OBJECTS:avm_decoder_app_util>)

    list(APPEND AVM_TOOL_TARGETS dump_obu)
    list(APPEND AVM_APP_TARGETS dump_obu)

    add_executable(
      stream_multiplexer
      "${AVM_GEN_SRC_DIR}/usage_exit.cc"
      "${AVM_ROOT}/tools/stream_mux.h"
      "${AVM_ROOT}/tools/stream_multiplexer.cc"
      $<TARGET_OBJECTS:avm_common_app_util>
      $<TARGET_OBJECTS:avm_decoder_app_util>)

    list(APPEND AVM_TOOL_TARGETS stream_multiplexer)
    list(APPEND AVM_APP_TARGETS stream_multiplexer)

    add_executable(
      stream_demuxer
      "${AVM_GEN_SRC_DIR}/usage_exit.cc"
      "${AVM_ROOT}/tools/stream_mux.h"
      "${AVM_ROOT}/tools/stream_demuxer.cc"
      $<TARGET_OBJECTS:avm_common_app_util>
      $<TARGET_OBJECTS:avm_decoder_app_util>)

    list(APPEND AVM_TOOL_TARGETS stream_demuxer)
    list(APPEND AVM_APP_TARGETS stream_demuxer)

    # Maintain a separate variable listing only the examples to facilitate
    # installation of example programs into an tools sub directory of
    # $AVM_DIST_DIR/bin when building the dist target.
    list(APPEND AVM_TOOL_TARGETS ${AVM_DECODER_TOOL_TARGETS}
         ${AVM_ENCODER_TOOL_TARGETS})
  endif()
endif()

if(ENABLE_EXAMPLES
   AND CONFIG_AV2_DECODER
   AND CONFIG_AV2_ENCODER)
  add_executable(
    avm_cx_set_ref
    "${AVM_ROOT}/examples/avm_cx_set_ref.c"
    $<TARGET_OBJECTS:avm_common_app_util>
    $<TARGET_OBJECTS:avm_encoder_app_util>)
  list(APPEND AVM_EXAMPLE_TARGETS avm_cx_set_ref)
  list(APPEND AVM_APP_TARGETS avm_cx_set_ref)
endif()

if((ENABLE_EXAMPLES OR ENABLE_TESTS)
   AND CONFIG_AV2_ENCODER
   AND CONFIG_AV2_DECODER)
  add_executable(
    bru_encoder_decoder
    "${AVM_ROOT}/examples/bru_encoder_decoder.c"
    $<TARGET_OBJECTS:avm_common_app_util>
    $<TARGET_OBJECTS:avm_encoder_app_util>
    $<TARGET_OBJECTS:avm_decoder_app_util>)
  list(APPEND AVM_EXAMPLE_TARGETS bru_encoder_decoder)
  list(APPEND AVM_APP_TARGETS bru_encoder_decoder)
endif()

foreach(avm_app ${AVM_APP_TARGETS})
  target_link_libraries(${avm_app} ${AVM_LIB_LINK_TYPE} avm)
endforeach()

if(ENABLE_EXAMPLES
   OR ENABLE_TESTS
   OR ENABLE_TOOLS)
  if(CONFIG_LIBYUV)
    add_library(yuv OBJECT ${AVM_LIBYUV_SOURCES})
    if(NOT MSVC)
      target_compile_options(yuv PRIVATE -Wno-unused-parameter)
    endif()
    include_directories("${AVM_ROOT}/third_party/libyuv/include")

    # Add to existing targets.
    foreach(avm_app ${AVM_APP_TARGETS})
      target_sources(${avm_app} PRIVATE $<TARGET_OBJECTS:yuv>)
      set_property(TARGET ${avm_app} PROPERTY LINKER_LANGUAGE CXX)
    endforeach()
  endif()

  if(CONFIG_WEBM_IO)
    add_library(webm OBJECT ${AVM_LIBWEBM_SOURCES})
    include_directories("${AVM_ROOT}/third_party/libwebm")
    target_compile_definitions(webm PRIVATE __STDC_CONSTANT_MACROS)
    target_compile_definitions(webm PRIVATE __STDC_LIMIT_MACROS)

    if(NOT MSVC)
      target_compile_options(webm PRIVATE -Wno-shadow)
    endif()

    # Add to existing targets.
    if(CONFIG_AV2_DECODER)
      target_sources(avm_decoder_app_util PRIVATE ${AVM_WEBM_DECODER_SOURCES})
    endif()

    if(CONFIG_AV2_ENCODER)
      target_sources(avm_encoder_app_util PRIVATE ${AVM_WEBM_ENCODER_SOURCES})
    endif()

    foreach(avm_app ${AVM_APP_TARGETS})
      target_sources(${avm_app} PRIVATE $<TARGET_OBJECTS:webm>)
      set_property(TARGET ${avm_app} PROPERTY LINKER_LANGUAGE CXX)
    endforeach()
  endif()
endif()

if(ENABLE_TESTS)

  # Create test_libavm target and the targets it depends on.
  setup_avm_test_targets()
endif()

if(HAVE_PTHREAD_H AND CONFIG_MULTITHREAD)
  find_package(Threads)
  target_link_libraries(avm ${AVM_LIB_LINK_TYPE} Threads::Threads)
  if(BUILD_SHARED_LIBS)
    target_link_libraries(avm_static ${AVM_LIB_LINK_TYPE} Threads::Threads)
  endif()
endif()

if(XCODE)

  # TODO(tomfinegan): Make sure target has no C++ files before doing this as
  # it's not necessary in that case.
  if(CONFIG_LIBYUV OR CONFIG_WEBM_IO)

    # The Xcode generator does not obey LINKER_LANGUAGE. Because of the issue
    # what looks like a C++ file needs to be in any target that Xcode will link
    # when the target contains a C++ dependency. Without this Xcode will try to
    # link with the C linker, which always ends badly when a dependency actually
    # includes C++.

    # Note: LINKER_LANGUAGE is explicitly set to C++ for all targets touched
    # here, it really is the Xcode generator's fault, or just a deficiency in
    # Xcode itself.
    foreach(avm_app ${AVM_APP_TARGETS})
      add_dummy_source_file_to_target("${avm_app}" "cc")
    endforeach()
  endif()
endif()

if(CONFIG_TENSORFLOW_LITE AND CONFIG_AV2_ENCODER)
  target_link_libraries(avm ${AVM_LIB_LINK_TYPE} tensorflow-lite)
  if(BUILD_SHARED_LIBS)
    target_link_libraries(avm_static ${AVM_LIB_LINK_TYPE} tensorflow-lite)
  endif()

  if(ENABLE_EXAMPLES)
    add_executable(tf_lite_model "${AVM_ROOT}/examples/tf_lite_model.cc")
    list(APPEND AVM_EXAMPLE_TARGETS tf_lite_model)
    list(APPEND AVM_APP_TARGETS tf_lite_model)
    add_dependencies(tf_lite_model tensorflow-lite)
    target_link_libraries(tf_lite_model PRIVATE tensorflow-lite)
    add_executable(cnn_restore_y4m
                   "${AVM_ROOT}/examples/cnn_restore/cnn_restore_y4m.cc")
    list(APPEND AVM_APP_TARGETS cnn_restore_y4m)
    add_dependencies(cnn_restore_y4m tensorflow-lite)
    target_link_libraries(cnn_restore_y4m PRIVATE tensorflow-lite)
  endif()
endif()

if(ENABLE_EXAMPLES AND "${CMAKE_GENERATOR}" MATCHES "Makefiles$")

  # For historical purposes place the example binaries in the example directory.
  file(MAKE_DIRECTORY "${AVM_CONFIG_DIR}/examples")

  foreach(target ${AVM_EXAMPLE_TARGETS})
    if(NOT "${target}" MATCHES "avmdec\|avmenc")
      set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                                 "${AVM_CONFIG_DIR}/examples")
    endif()
  endforeach()

  if(ENABLE_TOOLS AND AVM_TOOL_TARGETS)

    # The same expectation is true for tool targets.
    file(MAKE_DIRECTORY "${AVM_CONFIG_DIR}/tools")
    set_target_properties(
      ${AVM_TOOL_TARGETS} PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                     "${AVM_CONFIG_DIR}/tools")
  endif()
endif()

if(BUILD_SHARED_LIBS)
  # Don't use -Wl,-z,defs with Clang's sanitizers.
  #
  # Clang's AddressSanitizer documentation says "When linking shared libraries,
  # the AddressSanitizer run-time is not linked, so -Wl,-z,defs may cause link
  # errors (don't use it with AddressSanitizer)." See
  # https://clang.llvm.org/docs/AddressSanitizer.html#usage. Similarly, see
  # https://clang.llvm.org/docs/MemorySanitizer.html#usage.
  if(NOT
     (APPLE
      OR CYGWIN
      OR WIN32)
     AND NOT (CMAKE_C_COMPILER_ID MATCHES "Clang" AND SANITIZE))
    # The -z defs linker option reports unresolved symbol references from object
    # files when building a shared library.
    if("${CMAKE_VERSION}" VERSION_LESS "3.13")
      # target_link_options() is not available before CMake 3.13.
      target_link_libraries(avm PRIVATE -Wl,-z,defs)
    else()
      target_link_options(avm PRIVATE LINKER:-z,defs)
    endif()
  endif()

  include("${AVM_ROOT}/build/cmake/exports.cmake")
  setup_exports_target()
endif()

# Handle user supplied compile and link flags last to ensure they're obeyed.
set_user_flags()

# Aomedia documentation rule.
set(DOXYGEN_VERSION_VALUE 0)
if(ENABLE_DOCS)
  include(FindDoxygen)
  if(DOXYGEN_FOUND)
    # Check if Doxygen version is >= minimum required version(i.e. 1.8.10).
    set(MINIMUM_DOXYGEN_VERSION 1008010)

    if(DOXYGEN_VERSION)
      # Strip SHA1 from version string if present.
      string(REGEX REPLACE "^([0-9]+\\.[0-9]+\\.[0-9]+).*" "\\1"
                           DOXYGEN_VERSION ${DOXYGEN_VERSION})
      # Replace dots with semicolons to create a list.
      string(REGEX REPLACE "\\." ";" DOXYGEN_VERSION_LIST ${DOXYGEN_VERSION})
      # Parse version components from the list.
      list(GET DOXYGEN_VERSION_LIST 0 DOXYGEN_MAJOR)
      list(GET DOXYGEN_VERSION_LIST 1 DOXYGEN_MINOR)
      list(GET DOXYGEN_VERSION_LIST 2 DOXYGEN_PATCH)
    endif()

    # Construct a version value for comparison.
    math(EXPR DOXYGEN_MAJOR "${DOXYGEN_MAJOR}*1000000")
    math(EXPR DOXYGEN_MINOR "${DOXYGEN_MINOR}*1000")
    math(EXPR DOXYGEN_VERSION_VALUE
         "${DOXYGEN_MAJOR} + ${DOXYGEN_MINOR} + ${DOXYGEN_PATCH}")

    if(${DOXYGEN_VERSION_VALUE} LESS ${MINIMUM_DOXYGEN_VERSION})
      set(DOXYGEN_FOUND NO)
    endif()
  endif()

  if(DOXYGEN_FOUND)
    include("${AVM_ROOT}/docs.cmake")
    setup_documentation_targets()
  else()
    message(
      "--- Cannot find doxygen(version 1.8.10 or newer), ENABLE_DOCS turned off."
    )
    set(ENABLE_DOCS OFF)
  endif()
endif()

# Aomedia dist rule.
if(CONFIG_AV2_DECODER AND ENABLE_EXAMPLES)
  list(APPEND AVM_DIST_APPS $<TARGET_FILE:avmdec>)
endif()
if(CONFIG_AV2_ENCODER AND ENABLE_EXAMPLES)
  list(APPEND AVM_DIST_APPS $<TARGET_FILE:avmenc>)
endif()

if(ENABLE_EXAMPLES)
  foreach(example ${AVM_EXAMPLE_TARGETS})
    list(APPEND AVM_DIST_EXAMPLES $<TARGET_FILE:${example}>)
    set_property(TARGET ${example} PROPERTY FOLDER examples)
  endforeach()
endif()

if(ENABLE_TOOLS)
  foreach(tool ${AVM_TOOL_TARGETS})
    list(APPEND AVM_DIST_TOOLS $<TARGET_FILE:${tool}>)
    set_property(TARGET ${tool} PROPERTY FOLDER tools)
  endforeach()
endif()

if(NOT AVM_DIST_DIR)
  set(AVM_DIST_DIR "${AVM_CONFIG_DIR}/dist")
endif()

add_custom_target(
  dist
  COMMAND
    ${CMAKE_COMMAND} -DAVM_ROOT=${AVM_ROOT} -DAVM_CONFIG_DIR=${AVM_CONFIG_DIR}
    -DAVM_DIST_DIR=${AVM_DIST_DIR} -DAVM_DIST_APPS="${AVM_DIST_APPS}"
    -DAVM_DIST_EXAMPLES="${AVM_DIST_EXAMPLES}"
    -DAVM_DIST_TOOLS="${AVM_DIST_TOOLS}"
    -DAVM_DIST_INCLUDES="${AVM_INSTALL_INCS}" -DAVM_DIST_LIBS=$<TARGET_FILE:avm>
    -DENABLE_DOCS=${ENABLE_DOCS} -P "${AVM_ROOT}/build/cmake/dist.cmake"
  DEPENDS ${AVM_INSTALL_BINS} ${AVM_INSTALL_LIBS} ${AVM_INSTALL_INCS}
          ${AVM_EXAMPLE_TARGETS} ${AVM_TOOL_TARGETS})

if(ENABLE_DOCS)
  add_dependencies(dist docs)
endif()

# Collect all variables containing libavm source files.
get_cmake_property(all_cmake_vars VARIABLES)
foreach(var ${all_cmake_vars})
  if("${var}" MATCHES "SOURCES$\|_INTRIN_\|_ASM_"
     AND NOT "${var}" MATCHES "_APP_\|DOXYGEN\|LIBWEBM\|LIBYUV\|_PKG_\|TEST")
    list(APPEND avm_source_vars ${var})
  endif()
endforeach()

if(NOT CONFIG_AV2_DECODER)
  list(FILTER avm_source_vars EXCLUDE REGEX "_DECODER_")
endif()

# Libavm_srcs.txt generation.
set(libavm_srcs_txt_file "${AVM_CONFIG_DIR}/libavm_srcs.txt")
file(WRITE "${libavm_srcs_txt_file}" "# This file is generated. DO NOT EDIT.\n")

# Static source file list first.
foreach(avm_source_var ${avm_source_vars})
  foreach(file ${${avm_source_var}})
    if(NOT "${file}" MATCHES "${AVM_CONFIG_DIR}")
      string(REPLACE "${AVM_ROOT}/" "" file "${file}")
      if(NOT CONFIG_AV2_DECODER AND "${file}" MATCHES "avm_decoder")
        continue()
      endif()
      file(APPEND "${libavm_srcs_txt_file}" "${file}\n")
    endif()
  endforeach()
endforeach()

file(APPEND "${libavm_srcs_txt_file}"
     "# Files below this line are generated by the libavm build system.\n")
foreach(avm_source_var ${avm_source_vars})
  foreach(file ${${avm_source_var}})
    if("${file}" MATCHES "${AVM_CONFIG_DIR}")
      string(REPLACE "${AVM_CONFIG_DIR}/" "" file "${file}")
      file(APPEND "${libavm_srcs_txt_file}" "${file}\n")
    endif()
  endforeach()
endforeach()

# Libavm_srcs.gni generation.
set(libavm_srcs_gni_file "${AVM_CONFIG_DIR}/libavm_srcs.gni")
file(WRITE "${libavm_srcs_gni_file}" "# This file is generated. DO NOT EDIT.\n")

foreach(avm_source_var ${avm_source_vars})
  if("${${avm_source_var}}" MATCHES "${AVM_ROOT}")
    string(TOLOWER ${avm_source_var} avm_source_var_lowercase)
    file(APPEND "${libavm_srcs_gni_file}" "\n${avm_source_var_lowercase} = [\n")
  endif()

  foreach(file ${${avm_source_var}})
    if(NOT "${file}" MATCHES "${AVM_CONFIG_DIR}")
      string(REPLACE "${AVM_ROOT}" "//third_party/libavm/source/libavm" file
                     "${file}")
      if(NOT CONFIG_AV2_DECODER AND "${file}" MATCHES "avm_decoder")
        continue()
      endif()
      file(APPEND "${libavm_srcs_gni_file}" "  \"${file}\",\n")
    endif()
  endforeach()

  if("${${avm_source_var}}" MATCHES "${AVM_ROOT}")
    file(APPEND "${libavm_srcs_gni_file}" "]\n")
  endif()
endforeach()

file(APPEND "${libavm_srcs_gni_file}"
     "\n# Files below this line are generated by the libavm build system.\n")

foreach(avm_source_var ${avm_source_vars})
  if("${${avm_source_var}}" MATCHES "${AVM_CONFIG_DIR}")
    string(TOLOWER ${avm_source_var} avm_source_var_lowercase)
    file(APPEND "${libavm_srcs_gni_file}"
         "\n${avm_source_var_lowercase}_gen = [\n")
  endif()
  foreach(file ${${avm_source_var}})
    if(NOT "${file}" MATCHES "${AVM_ROOT}")
      string(REPLACE "${AVM_CONFIG_DIR}" "//third_party/libavm/source/libavm"
                     file "${file}")
      file(APPEND "${libavm_srcs_gni_file}" "  \"${file}\",\n")
    endif()
  endforeach()

  if("${${avm_source_var}}" MATCHES "${AVM_CONFIG_DIR}")
    file(APPEND "${libavm_srcs_gni_file}" "]\n")
  endif()
endforeach()

# Generate avm.pc and setup install rule.
setup_avm_install_targets()
