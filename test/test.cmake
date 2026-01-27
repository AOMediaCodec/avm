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
if(AVM_TEST_TEST_CMAKE_)
  return()
endif() # AVM_TEST_TEST_CMAKE_
set(AVM_TEST_TEST_CMAKE_ 1)

include(FindPython)
include(ProcessorCount)

include("${AVM_ROOT}/test/test_data_util.cmake")

set(AVM_UNIT_TEST_DATA_LIST_FILE "${AVM_ROOT}/test/test-data.sha1")
set(AVM_IDE_TEST_FOLDER "test")
set(AVM_IDE_TESTDATA_FOLDER "testdata")

list(APPEND AVM_UNIT_TEST_WRAPPER_SOURCES "${AVM_GEN_SRC_DIR}/usage_exit.c"
     "${AVM_ROOT}/test/test_libavm.cc")

list(
  APPEND
  AVM_UNIT_TEST_COMMON_SOURCES
  "${AVM_ROOT}/test/acm_random.h"
  "${AVM_ROOT}/test/avm_integer_test.cc"
  "${AVM_ROOT}/test/av2_config_test.cc"
  "${AVM_ROOT}/test/av2_key_value_api_test.cc"
  "${AVM_ROOT}/test/clear_system_state.h"
  "${AVM_ROOT}/test/codec_factory.h"
  "${AVM_ROOT}/test/decode_test_driver.cc"
  "${AVM_ROOT}/test/decode_test_driver.h"
  "${AVM_ROOT}/test/function_equivalence_test.h"
  "${AVM_ROOT}/test/log2_test.cc"
  "${AVM_ROOT}/test/md5_helper.h"
  "${AVM_ROOT}/test/metadata_test.cc"
  "${AVM_ROOT}/test/register_state_check.h"
  "${AVM_ROOT}/test/test_vectors.cc"
  "${AVM_ROOT}/test/test_vectors.h"
  "${AVM_ROOT}/test/transform_test_base.h"
  "${AVM_ROOT}/test/util.h"
  "${AVM_ROOT}/test/video_source.h")

list(
  APPEND
  AVM_UNIT_TEST_DECODER_SOURCES
  "${AVM_ROOT}/test/decode_api_test.cc"
  "${AVM_ROOT}/test/external_frame_buffer_test.cc"
  "${AVM_ROOT}/test/invalid_file_test.cc"
  "${AVM_ROOT}/test/test_vector_test.cc"
  "${AVM_ROOT}/test/ivf_video_source.h")

list(
  APPEND
  AVM_UNIT_TEST_ENCODER_SOURCES
  "${AVM_ROOT}/test/aq_segment_test.cc"
  "${AVM_ROOT}/test/borders_test.cc"
  "${AVM_ROOT}/test/cpu_speed_test.cc"
  "${AVM_ROOT}/test/encode_api_test.cc"
  "${AVM_ROOT}/test/encode_small_width_height_test.cc"
  "${AVM_ROOT}/test/encode_test_driver.cc"
  "${AVM_ROOT}/test/encode_test_driver.h"
  "${AVM_ROOT}/test/end_to_end_test.cc"
  "${AVM_ROOT}/test/gf_pyr_height_test.cc"
  "${AVM_ROOT}/test/i420_video_source.h"
  "${AVM_ROOT}/test/level_test.cc"
  "${AVM_ROOT}/test/monochrome_test.cc"
  "${AVM_ROOT}/test/resize_test.cc"
  "${AVM_ROOT}/test/y4m_test.cc"
  "${AVM_ROOT}/test/y4m_video_source.h"
  "${AVM_ROOT}/test/yuv_video_source.h"
  "${AVM_ROOT}/test/time_stamp_test.cc")

list(APPEND AVM_DECODE_PERF_TEST_SOURCES "${AVM_ROOT}/test/decode_perf_test.cc")
list(APPEND AVM_UNIT_TEST_WEBM_SOURCES "${AVM_ROOT}/test/webm_video_source.h")
list(APPEND AVM_TEST_INTRA_PRED_SPEED_SOURCES "${AVM_GEN_SRC_DIR}/usage_exit.c"
     "${AVM_ROOT}/test/test_intra_pred_speed.cc")

if(NOT BUILD_SHARED_LIBS)
  list(
    APPEND
    AVM_UNIT_TEST_COMMON_SOURCES
    "${AVM_ROOT}/test/av2_common_int_test.cc"
    "${AVM_ROOT}/test/bawp_test.cc"
    "${AVM_ROOT}/test/bitwriter_buffer_test.cc"
    "${AVM_ROOT}/test/cdef_test.cc"
    "${AVM_ROOT}/test/cfl_test.cc"
    "${AVM_ROOT}/test/convolve_test.cc"
    "${AVM_ROOT}/test/hiprec_convolve_test.cc"
    "${AVM_ROOT}/test/hiprec_convolve_test_util.cc"
    "${AVM_ROOT}/test/hiprec_convolve_test_util.h"
    "${AVM_ROOT}/test/intrabc_test.cc"
    "${AVM_ROOT}/test/intrapred_test.cc"
    "${AVM_ROOT}/test/intra_matrix_test.cc"
    "${AVM_ROOT}/test/loopfilter_test.cc"
    "${AVM_ROOT}/test/opt_flow_test.cc"
    "${AVM_ROOT}/test/scan_test.cc"
    "${AVM_ROOT}/test/simd_cmp_impl.h"
    "${AVM_ROOT}/test/simd_impl.h"
    "${AVM_ROOT}/test/gdf_test.cc")

  if(CONFIG_ACCOUNTING)
    list(APPEND AVM_UNIT_TEST_COMMON_SOURCES
         "${AVM_ROOT}/test/accounting_test.cc")
  endif()

  if(CONFIG_AV2_DECODER AND CONFIG_AV2_ENCODER)
    list(
      APPEND
      AVM_UNIT_TEST_COMMON_SOURCES
      "${AVM_ROOT}/test/altref_test.cc"
      "${AVM_ROOT}/test/av2_encoder_parms_get_to_decoder.cc"
      "${AVM_ROOT}/test/binary_codes_test.cc"
      "${AVM_ROOT}/test/boolcoder_test.cc"
      "${AVM_ROOT}/test/cnn_test.cc"
      "${AVM_ROOT}/test/decode_multithreaded_test.cc"
      "${AVM_ROOT}/test/divu_small_test.cc"
      "${AVM_ROOT}/test/dr_prediction_test.cc"
      "${AVM_ROOT}/test/ec_test.cc"
      "${AVM_ROOT}/test/ethread_test.cc"
      "${AVM_ROOT}/test/film_grain_table_test.cc"
      "${AVM_ROOT}/test/frame_multi_qmatrix_test.cc"
      "${AVM_ROOT}/test/fwd_kf_test.cc"
      "${AVM_ROOT}/test/kf_test.cc"
      "${AVM_ROOT}/test/lossless_test.cc"
      "${AVM_ROOT}/test/mfh_test.cc"
      "${AVM_ROOT}/test/quant_test.cc"
      "${AVM_ROOT}/test/sb_multipass_test.cc"
      "${AVM_ROOT}/test/screen_content_test.cc"
      "${AVM_ROOT}/test/segment_binarization_sync.cc"
      "${AVM_ROOT}/test/still_picture_test.cc"
      "${AVM_ROOT}/test/subgop_test.cc"
      "${AVM_ROOT}/test/superframe_test.cc"
      "${AVM_ROOT}/test/temporal_layers_test.cc"
      "${AVM_ROOT}/test/tile_config_test.cc"
      "${AVM_ROOT}/test/tile_independence_test.cc"
      "${AVM_ROOT}/test/temporal_filter_test.cc")
  endif()

  list(APPEND AVM_UNIT_TEST_COMMON_INTRIN_NEON
       "${AVM_ROOT}/test/simd_cmp_neon.cc")
  if(HAVE_NEON)
    list(APPEND AVM_UNIT_TEST_COMMON_SOURCES
         "${AVM_ROOT}/test/simd_neon_test.cc")
  endif()

  list(APPEND AVM_UNIT_TEST_COMMON_INTRIN_SSE2
       "${AVM_ROOT}/test/simd_cmp_sse2.cc")
  if(HAVE_SSE2)
    list(APPEND AVM_UNIT_TEST_COMMON_SOURCES
         "${AVM_ROOT}/test/simd_sse2_test.cc")
  endif()

  list(APPEND AVM_UNIT_TEST_COMMON_INTRIN_SSSE3
       "${AVM_ROOT}/test/simd_cmp_ssse3.cc")
  if(HAVE_SSSE3)
    list(APPEND AVM_UNIT_TEST_COMMON_SOURCES
         "${AVM_ROOT}/test/simd_ssse3_test.cc")
  endif()

  if(HAVE_SSE4)
    list(APPEND AVM_UNIT_TEST_COMMON_SOURCES
         "${AVM_ROOT}/test/simd_sse4_test.cc")
  endif()

  list(APPEND AVM_UNIT_TEST_COMMON_INTRIN_AVX2
       "${AVM_ROOT}/test/simd_cmp_avx2.cc")
  if(HAVE_AVX2)
    list(APPEND AVM_UNIT_TEST_COMMON_SOURCES
         "${AVM_ROOT}/test/simd_avx2_test.cc")
  endif()

  list(
    APPEND
    AVM_UNIT_TEST_ENCODER_SOURCES
    "${AVM_ROOT}/test/arf_freq_test.cc"
    "${AVM_ROOT}/test/av2_convolve_test.cc"
    "${AVM_ROOT}/test/av2_nn_predict_test.cc"
    "${AVM_ROOT}/test/av2_wedge_utils_test.cc"
    "${AVM_ROOT}/test/av2_ccso_simd_cmp.cc"
    "${AVM_ROOT}/test/blend_a64_mask_1d_test.cc"
    "${AVM_ROOT}/test/blend_a64_mask_test.cc"
    "${AVM_ROOT}/test/comp_avg_pred_test.cc"
    "${AVM_ROOT}/test/comp_avg_pred_test.h"
    "${AVM_ROOT}/test/comp_mask_variance_test.cc"
    "${AVM_ROOT}/test/edge_detect_test.cc"
    "${AVM_ROOT}/test/encodetxb_test.cc"
    "${AVM_ROOT}/test/error_block_test.cc"
    "${AVM_ROOT}/test/fft_test.cc"
    "${AVM_ROOT}/test/fwht4x4_test.cc"
    "${AVM_ROOT}/test/horver_correlation_test.cc"
    "${AVM_ROOT}/test/masked_sad_test.cc"
    "${AVM_ROOT}/test/masked_variance_test.cc"
    "${AVM_ROOT}/test/motion_vector_test.cc"
    "${AVM_ROOT}/test/noise_model_test.cc"
    "${AVM_ROOT}/test/quantize_func_test.cc"
    "${AVM_ROOT}/test/sad_test.cc"
    "${AVM_ROOT}/test/stream_iter_test.cc"
    "${AVM_ROOT}/test/subtract_test.cc"
    "${AVM_ROOT}/test/reconinter_test.cc"
    "${AVM_ROOT}/test/sum_squares_test.cc"
    "${AVM_ROOT}/test/sse_sum_test.cc"
    "${AVM_ROOT}/test/variance_test.cc"
    "${AVM_ROOT}/test/warp_filter_test.cc"
    "${AVM_ROOT}/test/warp_filter_test_util.cc"
    "${AVM_ROOT}/test/warp_filter_test_util.h"
    "${AVM_ROOT}/test/webmenc_test.cc"
    "${AVM_ROOT}/test/palette_test.cc")

  list(APPEND AVM_UNIT_TEST_ENCODER_SOURCES
       "${AVM_ROOT}/test/lossless_idtx_test.cc")

  list(
    APPEND AVM_UNIT_TEST_ENCODER_INTRIN_SSE4_1
    "${AVM_ROOT}/test/av2_quantize_test.cc"
    "${AVM_ROOT}/test/corner_match_test.cc" "${AVM_ROOT}/test/simd_cmp_sse4.cc")

  if(NOT HAVE_SSE2)
    list(REMOVE_ITEM AVM_UNIT_TEST_ENCODER_SOURCES
         "${AVM_ROOT}/test/quantize_func_test.cc")
  endif()

  if(HAVE_SSE4_1)
    list(APPEND AVM_UNIT_TEST_ENCODER_SOURCES
         "${AVM_ROOT}/test/av2_convolve_scale_test.cc"
         "${AVM_ROOT}/test/intra_edge_test.cc")
  endif()

  if(HAVE_SSE4_2)
    list(APPEND AVM_UNIT_TEST_ENCODER_SOURCES "${AVM_ROOT}/test/hash_test.cc")
  endif()

  list(APPEND AVM_UNIT_TEST_ENCODER_SOURCES "${AVM_ROOT}/test/trellis_test.cc")

endif()

if(ENABLE_TESTS)
  find_package(Python COMPONENTS Interpreter)
  if(NOT Python_Interpreter_FOUND)
    message(
      FATAL_ERROR
        "--- Unit tests require Python, rerun cmake with "
        "-DENABLE_TESTS=0 to avoid this error, or install Python and "
        "make sure it's in your PATH.")
  endif()

  if(BUILD_SHARED_LIBS AND APPLE) # Silence an RPATH warning.
    set(CMAKE_MACOSX_RPATH 1)
  endif()

  include_directories(
    "${AVM_ROOT}/third_party/googletest/src/googletest/include")

  include_directories("${AVM_ROOT}/third_party/googletest/src/googletest")
  add_library(
    avm_gtest STATIC
    "${AVM_ROOT}/third_party/googletest/src/googletest/src/gtest-all.cc")
  set_property(TARGET avm_gtest PROPERTY FOLDER ${AVM_IDE_TEST_FOLDER})
  if(MSVC OR WIN32)
    target_compile_definitions(avm_gtest PRIVATE GTEST_OS_WINDOWS=1)
  elseif(CONFIG_MULTITHREAD AND CMAKE_USE_PTHREADS_INIT)
    target_compile_definitions(avm_gtest PRIVATE GTEST_HAS_PTHREAD=1)
  else()
    target_compile_definitions(avm_gtest PRIVATE GTEST_HAS_PTHREAD=0)
  endif()
endif()

# Setup testdata download targets, test build targets, and test run targets. The
# libavm and app util targets must exist before this function is called.
function(setup_avm_test_targets)

  # TODO(tomfinegan): Build speed optimization. $AVM_UNIT_TEST_COMMON_SOURCES
  # and $AVM_UNIT_TEST_ENCODER_SOURCES are very large. The build of test targets
  # could be sped up (on multicore build machines) by compiling sources in each
  # list into separate object library targets, and then linking them into
  # test_libavm.
  add_library(test_avm_common OBJECT ${AVM_UNIT_TEST_COMMON_SOURCES})
  set_property(TARGET test_avm_common PROPERTY FOLDER ${AVM_IDE_TEST_FOLDER})
  add_dependencies(test_avm_common avm)

  if(CONFIG_AV2_DECODER)
    add_library(test_avm_decoder OBJECT ${AVM_UNIT_TEST_DECODER_SOURCES})
    set_property(TARGET test_avm_decoder PROPERTY FOLDER ${AVM_IDE_TEST_FOLDER})
    add_dependencies(test_avm_decoder avm)
  endif()

  if(CONFIG_AV2_ENCODER)
    add_library(test_avm_encoder OBJECT ${AVM_UNIT_TEST_ENCODER_SOURCES})
    set_property(TARGET test_avm_encoder PROPERTY FOLDER ${AVM_IDE_TEST_FOLDER})
    add_dependencies(test_avm_encoder avm)
  endif()

  if(CONFIG_AV2_ENCODER)
    add_executable(encoder_link_test "${AVM_ROOT}/test/encoder_link_test.c")
    # encoder_link_test should link with the avm library and nothing else.
    target_link_libraries(encoder_link_test ${AVM_LIB_LINK_TYPE} avm)
  endif()

  add_executable(
    test_libavm
    ${AVM_UNIT_TEST_WRAPPER_SOURCES} $<TARGET_OBJECTS:avm_common_app_util>
    $<TARGET_OBJECTS:test_avm_common>)
  set_property(TARGET test_libavm PROPERTY FOLDER ${AVM_IDE_TEST_FOLDER})
  list(APPEND AVM_APP_TARGETS test_libavm)

  if(CONFIG_AV2_DECODER)
    target_sources(test_libavm PRIVATE $<TARGET_OBJECTS:avm_decoder_app_util>
                                       $<TARGET_OBJECTS:test_avm_decoder>)

    if(ENABLE_DECODE_PERF_TESTS AND CONFIG_WEBM_IO)
      target_sources(test_libavm PRIVATE ${AVM_DECODE_PERF_TEST_SOURCES})
    endif()
  endif()

  if(CONFIG_AV2_ENCODER)
    target_sources(test_libavm PRIVATE $<TARGET_OBJECTS:test_avm_encoder>
                                       $<TARGET_OBJECTS:avm_encoder_app_util>)

    if(ENABLE_ENCODE_PERF_TESTS)
      target_sources(test_libavm PRIVATE ${AVM_ENCODE_PERF_TEST_SOURCES})
    endif()

    if(NOT BUILD_SHARED_LIBS)
      add_executable(
        test_intra_pred_speed ${AVM_TEST_INTRA_PRED_SPEED_SOURCES}
                              $<TARGET_OBJECTS:avm_common_app_util>)
      set_property(TARGET test_intra_pred_speed PROPERTY FOLDER
                                                         ${AVM_IDE_TEST_FOLDER})
      target_link_libraries(test_intra_pred_speed ${AVM_LIB_LINK_TYPE} avm
                            avm_gtest)
      list(APPEND AVM_APP_TARGETS test_intra_pred_speed)
    endif()
  endif()

  target_link_libraries(test_libavm ${AVM_LIB_LINK_TYPE} avm avm_gtest)

  if(CONFIG_LIBYUV)
    target_sources(test_libavm PRIVATE $<TARGET_OBJECTS:yuv>)
  endif()
  if(CONFIG_WEBM_IO)
    target_sources(test_libavm PRIVATE $<TARGET_OBJECTS:webm>)
  endif()
  if(HAVE_SSE2)
    add_intrinsics_source_to_target("-msse2" "test_libavm"
                                    "AVM_UNIT_TEST_COMMON_INTRIN_SSE2")
  endif()
  if(HAVE_SSSE3)
    add_intrinsics_source_to_target("-mssse3" "test_libavm"
                                    "AVM_UNIT_TEST_COMMON_INTRIN_SSSE3")
  endif()
  if(HAVE_SSE4_1)
    add_intrinsics_source_to_target("-msse4.1" "test_libavm"
                                    "AVM_UNIT_TEST_COMMON_INTRIN_SSE4_1")
    if(CONFIG_AV2_ENCODER)
      if(AVM_UNIT_TEST_ENCODER_INTRIN_SSE4_1)
        add_intrinsics_source_to_target("-msse4.1" "test_libavm"
                                        "AVM_UNIT_TEST_ENCODER_INTRIN_SSE4_1")
      endif()
    endif()
  endif()
  if(HAVE_AVX2)
    add_intrinsics_source_to_target("-mavx2" "test_libavm"
                                    "AVM_UNIT_TEST_COMMON_INTRIN_AVX2")
  endif()
  if(HAVE_NEON)
    add_intrinsics_source_to_target("${AVM_NEON_INTRIN_FLAG}" "test_libavm"
                                    "AVM_UNIT_TEST_COMMON_INTRIN_NEON")
  endif()

  if(ENABLE_TESTDATA)
    make_test_data_lists("${AVM_UNIT_TEST_DATA_LIST_FILE}" test_files
                         test_file_checksums)
    list(LENGTH test_files num_test_files)
    list(LENGTH test_file_checksums num_test_file_checksums)

    math(EXPR max_file_index "${num_test_files} - 1")
    foreach(test_index RANGE ${max_file_index})
      list(GET test_files ${test_index} test_file)
      list(GET test_file_checksums ${test_index} test_file_checksum)
      add_custom_target(
        testdata_${test_index}
        COMMAND
          ${CMAKE_COMMAND} -DAVM_CONFIG_DIR="${AVM_CONFIG_DIR}"
          -DAVM_ROOT="${AVM_ROOT}" -DAVM_TEST_FILE="${test_file}"
          -DAVM_TEST_CHECKSUM=${test_file_checksum} -P
          "${AVM_ROOT}/test/test_data_download_worker.cmake")
      set_property(TARGET testdata_${test_index}
                   PROPERTY FOLDER ${AVM_IDE_TESTDATA_FOLDER})
      list(APPEND testdata_targets testdata_${test_index})
    endforeach()

    # Create a custom build target for running each test data download target.
    add_custom_target(testdata)
    add_dependencies(testdata ${testdata_targets})
    set_property(TARGET testdata PROPERTY FOLDER ${AVM_IDE_TESTDATA_FOLDER})

    # Skip creation of test run targets when generating for Visual Studio and
    # Xcode unless the user explicitly requests IDE test hosting. This is done
    # to make build cycles in the IDE tolerable when the IDE command for build
    # project is used to build AVM. Default behavior in IDEs is to build all
    # targets, and the test run takes hours.
    if(((NOT MSVC) AND (NOT XCODE)) OR ENABLE_IDE_TEST_HOSTING)

      # Pick a reasonable number of targets (this controls parallelization).
      processorcount(num_test_targets)
      if(num_test_targets EQUAL 0) # Just default to 10 targets when there's no
                                   # processor count available.
        set(num_test_targets 10)
      endif()

      math(EXPR max_shard_index "${num_test_targets} - 1")
      foreach(shard_index RANGE ${max_shard_index})
        set(test_name "test_${shard_index}")
        add_custom_target(
          ${test_name}
          COMMAND
            ${CMAKE_COMMAND} -DGTEST_SHARD_INDEX=${shard_index}
            -DGTEST_TOTAL_SHARDS=${num_test_targets}
            -DTEST_LIBAVM=$<TARGET_FILE:test_libavm> -P
            "${AVM_ROOT}/test/test_runner.cmake"
          DEPENDS testdata test_libavm)
        set_property(TARGET ${test_name} PROPERTY FOLDER ${AVM_IDE_TEST_FOLDER})
        list(APPEND test_targets ${test_name})
      endforeach()
      add_custom_target(runtests)
      set_property(TARGET runtests PROPERTY FOLDER ${AVM_IDE_TEST_FOLDER})
      add_dependencies(runtests ${test_targets})
    endif()
  endif()

  # Collect all variables containing libavm test source files.
  get_cmake_property(all_cmake_vars VARIABLES)
  foreach(var ${all_cmake_vars})

    # https://github.com/cheshirekow/cmake_format/issues/34
    # cmake-format: off
    if (("${var}" MATCHES "_TEST_" AND NOT
         "${var}" MATCHES
         "_DATA_\|_CMAKE_\|INTRA_PRED\|_COMPILED\|_HOSTING\|_PERF_\|CODER_")
        OR (CONFIG_AV2_ENCODER AND ENABLE_ENCODE_PERF_TESTS AND
            "${var}" MATCHES "_ENCODE_PERF_TEST_")
        OR (CONFIG_AV2_DECODER AND ENABLE_DECODE_PERF_TESTS AND
            "${var}" MATCHES "_DECODE_PERF_TEST_")
        OR (CONFIG_AV2_ENCODER AND "${var}" MATCHES "_TEST_ENCODER_")
        OR (CONFIG_AV2_DECODER AND  "${var}" MATCHES "_TEST_DECODER_"))
      list(APPEND avm_test_source_vars ${var})
    endif()
    # cmake-format: on
  endforeach()

  # Libavm_test_srcs.txt generation.
  set(libavm_test_srcs_txt_file "${AVM_CONFIG_DIR}/libavm_test_srcs.txt")
  file(WRITE "${libavm_test_srcs_txt_file}"
       "# This file is generated. DO NOT EDIT.\n")

  # Static source file list first.
  foreach(avm_test_source_var ${avm_test_source_vars})
    foreach(file ${${avm_test_source_var}})
      if(NOT "${file}" MATCHES "${AVM_CONFIG_DIR}")
        string(REPLACE "${AVM_ROOT}/" "" file "${file}")
        file(APPEND "${libavm_test_srcs_txt_file}" "${file}\n")
      endif()
    endforeach()
  endforeach()

  set(AVM_APP_TARGETS
      ${AVM_APP_TARGETS}
      PARENT_SCOPE)
endfunction()
