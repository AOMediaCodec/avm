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

  # CONFIG_MULTITHREAD is dependent on CONFIG_PARAKIT_COLLECT_DATA.
  if(CONFIG_PARAKIT_COLLECT_DATA AND CONFIG_MULTITHREAD)
    change_config_and_warn(CONFIG_MULTITHREAD 0 CONFIG_PARAKIT_COLLECT_DATA)
  endif()

  # CONFIG_THROUGHPUT_ANALYSIS requires CONFIG_ACCOUNTING. If CONFIG_ACCOUNTING
  # is off, we also turn off CONFIG_THROUGHPUT_ANALYSIS.
  if(NOT CONFIG_ACCOUNTING AND CONFIG_THROUGHPUT_ANALYSIS)
    change_config_and_warn(CONFIG_THROUGHPUT_ANALYSIS 0 !CONFIG_ACCOUNTING)
  endif()

  # CONFIG_UV_CFL depends on CONFIG_AIMC
  if(NOT CONFIG_AIMC AND CONFIG_UV_CFL)
    change_config_and_warn(CONFIG_UV_CFL 0 !CONFIG_AIMC)
  endif()

  if(CONFIG_ML_PART_SPLIT)
    change_config_and_warn(CONFIG_TENSORFLOW_LITE 1 CONFIG_ML_PART_SPLIT)
  endif()

  if(CONFIG_DIP_EXT_PRUNING)
    change_config_and_warn(CONFIG_TENSORFLOW_LITE 1 CONFIG_DIP_EXT_PRUNING)
  endif()

  if(CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES)
    change_config_and_warn(CONFIG_LR_FRAMEFILTERS_IN_HEADER 1
                           CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES)
  endif()
  if(CONFIG_CWG_F349_SIGNAL_TILE_INFO)
    change_config_and_warn(CONFIG_CWG_E242_SIGNAL_TILE_INFO 1
                           CONFIG_CWG_F349_SIGNAL_TILE_INFO)
  endif()

  if(CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES)
    change_config_and_warn(CONFIG_MINIMUM_LR_UNIT_SIZE_64x64 1
                           CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES)
  endif()

  if(CONFIG_CWG_E242_SEQ_HDR_ID)
    change_config_and_warn(CONFIG_MULTI_FRAME_HEADER 1
                           CONFIG_CWG_E242_SEQ_HDR_ID)
  endif()

  if(CONFIG_SCAN_TYPE_METADATA)
    change_config_and_warn(CONFIG_METADATA 1 CONFIG_SCAN_TYPE_METADATA)
  endif()

  if(CONFIG_F024_KEYOBU)
    change_config_and_warn(CONFIG_F106_OBU_TILEGROUP 1 CONFIG_F024_KEYOBU)
    change_config_and_warn(CONFIG_F106_OBU_SWITCH 1 CONFIG_F024_KEYOBU)
    change_config_and_warn(CONFIG_F106_OBU_SEF 1 CONFIG_F024_KEYOBU)
    change_config_and_warn(CONFIG_F106_OBU_TIP 1 CONFIG_F024_KEYOBU)
    change_config_and_warn(CONFIG_CWG_F317 1 CONFIG_F024_KEYOBU)
    change_config_and_warn(CONFIG_REMOVAL_REDUNDANT_FRAME_HEADER 1
                           CONFIG_F024_KEYOBU)
    change_config_and_warn(CONFIG_RANDOM_ACCESS_SWITCH_FRAME 1
                           CONFIG_F024_KEYOBU)
  endif()

endmacro()
