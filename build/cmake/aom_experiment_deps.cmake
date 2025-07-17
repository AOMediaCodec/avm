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
if(AOM_BUILD_CMAKE_AOM_EXPERIMENT_DEPS_CMAKE_)
  return()
endif() # AOM_BUILD_CMAKE_AOM_EXPERIMENT_DEPS_CMAKE_
set(AOM_BUILD_CMAKE_AOM_EXPERIMENT_DEPS_CMAKE_ 1)

# Adjusts CONFIG_* CMake variables to address conflicts between active AV1
# experiments.
macro(fix_experiment_configs)

  if(CONFIG_ANALYZER)
    change_config_and_warn(CONFIG_INSPECTION 1 CONFIG_ANALYZER)
  endif()

  if(CONFIG_EXTRACT_PROTO)
    change_config_and_warn(CONFIG_ACCOUNTING 1 CONFIG_EXTRACT_PROTO)
    change_config_and_warn(CONFIG_INSPECTION 1 CONFIG_EXTRACT_PROTO)
  endif()

  if(CONFIG_DIST_8X8 AND CONFIG_MULTITHREAD)
    change_config_and_warn(CONFIG_DIST_8X8 0 CONFIG_MULTITHREAD)
  endif()

  if(CONFIG_ERP_TFLITE)
    change_config_and_warn(CONFIG_TENSORFLOW_LITE 1 CONFIG_ERP_TFLITE)
  endif()

  # CONFIG_MULTITHREAD is dependent on CONFIG_PARAKIT_COLLECT_DATA.
  if(CONFIG_PARAKIT_COLLECT_DATA AND CONFIG_MULTITHREAD)
    change_config_and_warn(CONFIG_MULTITHREAD 0 CONFIG_PARAKIT_COLLECT_DATA)
  endif()

  # CONFIG_THROUGHPUT_ANALYSIS requires CONFIG_ACCOUNTING. If CONFIG_ACCOUNTING
  # is off, we also turn off CONFIG_THROUGHPUT_ANALYSIS.
  if(NOT CONFIG_ACCOUNTING AND CONFIG_THROUGHPUT_ANALYSIS)
    change_config_and_warn(CONFIG_THROUGHPUT_ANALYSIS 0 !CONFIG_ACCOUNTING)
  endif()

  # CONFIG_IST_ANY_SET is dependent on CONFIG_IST_SET_FLAG. If
  # CONFIG_IST_SET_FLAG is off, CONFIG_IST_ANY_SET needs to be turned off.
  if(NOT CONFIG_IST_SET_FLAG AND CONFIG_IST_ANY_SET)
    change_config_and_warn(CONFIG_IST_ANY_SET 0 !CONFIG_IST_SET_FLAG)
  endif()

  # CONFIG_UV_CFL depends on CONFIG_AIMC
  if(NOT CONFIG_AIMC AND CONFIG_UV_CFL)
    change_config_and_warn(CONFIG_UV_CFL 0 !CONFIG_AIMC)
  endif()

  if(CONFIG_TEMP_LR)
    change_config_and_warn(CONFIG_COMBINE_PC_NS_WIENER 1 CONFIG_TEMP_LR)
  endif()

  if(CONFIG_COMBINE_PC_NS_WIENER_ADD)
    change_config_and_warn(CONFIG_COMBINE_PC_NS_WIENER 1
                           CONFIG_COMBINE_PC_NS_WIENER_ADD)
    change_config_and_warn(CONFIG_TEMP_LR 1 CONFIG_COMBINE_PC_NS_WIENER_ADD)
  endif()

  # CONFIG_D072_SKIP_MODE_IMPROVE is dependent on CONFIG_SKIP_MODE_ENHANCEMENT
  # If CONFIG_SKIP_MODE_ENHANCEMENT is off, CONFIG_D072_SKIP_MODE_IMPROVE needs
  # to be turned off.
  if(NOT CONFIG_SKIP_MODE_ENHANCEMENT AND CONFIG_D072_SKIP_MODE_IMPROVE)
    change_config_and_warn(CONFIG_D072_SKIP_MODE_IMPROVE 0
                           !CONFIG_SKIP_MODE_ENHANCEMENT)
  endif()

  # CONFIG_AFFINE_REFINEMENT_SB depends on CONFIG_AFFINE_REFINEMENT
  if(NOT CONFIG_AFFINE_REFINEMENT AND CONFIG_AFFINE_REFINEMENT_SB)
    change_config_and_warn(CONFIG_AFFINE_REFINEMENT_SB 0
                           !CONFIG_AFFINE_REFINEMENT)
  endif()

  # CONFIG_MVP_SIMPLIFY depends on CONFIG_MVP_IMPROVEMENT
  if(NOT CONFIG_MVP_IMPROVEMENT AND CONFIG_MVP_SIMPLIFY)
    change_config_and_warn(CONFIG_MVP_SIMPLIFY 0 !CONFIG_MVP_IMPROVEMENT)
  endif()

  # CONFIG_TMVP_IMPROVE depends on CONFIG_MVP_IMPROVEMENT
  if(NOT CONFIG_MVP_IMPROVEMENT AND CONFIG_TMVP_IMPROVE)
    change_config_and_warn(CONFIG_TMVP_IMPROVE 0 !CONFIG_MVP_IMPROVEMENT)
  endif()

  # CONFIG_TX_PARTITION_CTX depends on CONFIG_NEW_TX_PARTITION
  if(NOT CONFIG_NEW_TX_PARTITION AND CONFIG_TX_PARTITION_CTX)
    change_config_and_warn(CONFIG_TX_PARTITION_CTX 0 !CONFIG_NEW_TX_PARTITION)
  endif()

  if(CONFIG_ML_PART_SPLIT)
    change_config_and_warn(CONFIG_TENSORFLOW_LITE 1 CONFIG_ML_PART_SPLIT)
  endif()

  if(CONFIG_DIP_EXT_PRUNING)
    change_config_and_warn(CONFIG_TENSORFLOW_LITE 1 CONFIG_DIP_EXT_PRUNING)
  endif()

  # CONFIG_DRL_WRL_SIMPLIFY depends on CONFIG_MVP_IMPROVEMENT
  if(NOT CONFIG_MVP_IMPROVEMENT AND CONFIG_DRL_WRL_SIMPLIFY)
    change_config_and_warn(CONFIG_DRL_WRL_SIMPLIFY 0 !CONFIG_MVP_IMPROVEMENT)
  endif()

  # CONFIG_MV_TRAJECTORY depends on CONFIG_TMVP_MEM_OPT
  if(NOT CONFIG_TMVP_MEM_OPT AND CONFIG_MV_TRAJECTORY)
    change_config_and_warn(CONFIG_MV_TRAJECTORY 0 !CONFIG_TMVP_MEM_OPT)
  endif()

  if(CONFIG_TCQ)
    change_config_and_warn(CONFIG_CONTEXT_DERIVATION 1 CONFIG_TCQ)
  endif()

  # CONFIG_DRL_LINE_BUFFER_REDUCTION depends on CONFIG_MVP_SIMPLIFY
  if(NOT CONFIG_MVP_SIMPLIFY AND CONFIG_DRL_LINE_BUFFER_REDUCTION)
    change_config_and_warn(CONFIG_DRL_LINE_BUFFER_REDUCTION 0
                           !CONFIG_MVP_SIMPLIFY)
  endif()

  # CONFIG_DRL_LINE_BUFFER_REDUCTION depends on CONFIG_CWG_E099_DRL_WRL_SIMPLIFY
  if(NOT CONFIG_CWG_E099_DRL_WRL_SIMPLIFY AND CONFIG_DRL_LINE_BUFFER_REDUCTION)
    change_config_and_warn(CONFIG_DRL_LINE_BUFFER_REDUCTION 0
                           !CONFIG_CWG_E099_DRL_WRL_SIMPLIFY)
  endif()

  # CONFIG_IMPROVED_SECONDARY_REFERENCE depends on
  # CONFIG_ENHANCED_FRAME_CONTEXT_INIT
  if(NOT CONFIG_ENHANCED_FRAME_CONTEXT_INIT
     AND CONFIG_IMPROVED_SECONDARY_REFERENCE)
    change_config_and_warn(CONFIG_IMPROVED_SECONDARY_REFERENCE 0
                           !CONFIG_ENHANCED_FRAME_CONTEXT_INIT)
  endif()
endmacro()
