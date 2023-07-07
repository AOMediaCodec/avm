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

  if(CONFIG_DIST_8X8 AND CONFIG_MULTITHREAD)
    change_config_and_warn(CONFIG_DIST_8X8 0 CONFIG_MULTITHREAD)
  endif()

  # CONFIG_THROUGHPUT_ANALYSIS requires CONFIG_ACCOUNTING. If CONFIG_ACCOUNTING
  # is off, we also turn off CONFIG_THROUGHPUT_ANALYSIS.
  if(NOT CONFIG_ACCOUNTING AND CONFIG_THROUGHPUT_ANALYSIS)
    change_config_and_warn(CONFIG_THROUGHPUT_ANALYSIS 0 !CONFIG_ACCOUNTING)
  endif()

  # CONFIG_CCSO_EXT is dependent on CONFIG_CCSO. If CONFIG_CCSO is off,
  # CONFIG_CCSO_EXT needs to be turned off.
  if(NOT CONFIG_CCSO AND CONFIG_CCSO_EXT)
    change_config_and_warn(CONFIG_CCSO_EXT 0 !CONFIG_CCSO)
  endif()

  # CONFIG_ATC_REDUCED_TXSET depends on CONFIG_ATC_NEWTXSETS. If
  # CONFIG_ATC_NEWTXSETS is off, then CONFIG_ATC_REDUCED_TXSET needs to be
  # disabled.
  if(NOT CONFIG_ATC_NEWTXSETS AND CONFIG_ATC_REDUCED_TXSET)
    change_config_and_warn(CONFIG_ATC_REDUCED_TXSET 0 !CONFIG_ATC_NEWTXSETS)
  endif()

  # CONFIG_OPTFLOW_ON_TIP is dependent on CONFIG_OPTFLOW_REFINEMENT and
  # CONFIG_TIP. If any of them is off, CONFIG_OPTFLOW_ON_TIP needs to be turned
  # off.
  if(NOT CONFIG_OPTFLOW_REFINEMENT AND CONFIG_OPTFLOW_ON_TIP)
    change_config_and_warn(CONFIG_OPTFLOW_ON_TIP 0 !CONFIG_OPTFLOW_REFINEMENT)
  endif()
  if(NOT CONFIG_TIP AND CONFIG_OPTFLOW_ON_TIP)
    change_config_and_warn(CONFIG_OPTFLOW_ON_TIP 0 !CONFIG_TIP)
  endif()

  # CONFIG_IMPROVED_JMVD is dependent on CONFIG_JOINT_MVD. If CONFIG_JOINT_MVD
  # is off, CONFIG_IMPROVED_JMVD needs to be turned off.
  if(NOT CONFIG_JOINT_MVD AND CONFIG_IMPROVED_JMVD)
    change_config_and_warn(CONFIG_IMPROVED_JMVD 0 !CONFIG_JOINT_MVD)
  endif()

  # CONFIG_WARP_REF_LIST depends on CONFIG_EXTENDED_WARP_PREDICTION
  if(NOT CONFIG_EXTENDED_WARP_PREDICTION AND CONFIG_WARP_REF_LIST)
    change_config_and_warn(CONFIG_WARP_REF_LIST 0
                           !CONFIG_EXTENDED_WARP_PREDICTION)
  endif()

  # CONFIG_WARPMV depends on CONFIG_WARP_REF_LIST
  if(NOT CONFIG_WARP_REF_LIST AND CONFIG_WARPMV)
    change_config_and_warn(CONFIG_WARPMV 0 !CONFIG_WARP_REF_LIST)
  endif()

  # CONFIG_CWG_D067_IMPROVED_WARP depends on CONFIG_WARP_REF_LIST
  if(NOT CONFIG_WARP_REF_LIST AND CONFIG_CWG_D067_IMPROVED_WARP)
    change_config_and_warn(CONFIG_CWG_D067_IMPROVED_WARP 0
                           !CONFIG_WARP_REF_LIST)
  endif()

  # CONFIG_CWG_D067_IMPROVED_WARP depends on CONFIG_WARPMV
  if(NOT CONFIG_WARPMV AND CONFIG_CWG_D067_IMPROVED_WARP)
    change_config_and_warn(CONFIG_CWG_D067_IMPROVED_WARP 0 !CONFIG_WARPMV)
  endif()

  # Begin: CWG-C016.
  if(CONFIG_WIENER_NONSEP_CROSS_FILT)
    change_config_and_warn(CONFIG_WIENER_NONSEP 1
                           CONFIG_WIENER_NONSEP_CROSS_FILT)
  endif()
  # End: CWG-C016.

  # CONFIG_H_PARTITION is dependent on CONFIG_EXT_RECUR_PARTITIONS. If
  # CONFIG_EXT_RECUR_PARTITIONS is off, CONFIG_H_PARTITION needs to be turned
  # off.
  if(NOT CONFIG_EXT_RECUR_PARTITIONS AND CONFIG_H_PARTITION)
    change_config_and_warn(CONFIG_H_PARTITION 0 !CONFIG_EXT_RECUR_PARTITIONS)
  endif()

  # CONFIG_UNEVEN_4WAY is dependent on CONFIG_EXT_RECUR_PARTITIONS. If
  # CONFIG_EXT_RECUR_PARTITIONS is off, CONFIG_UNEVEN_4WAY needs to be turned
  # off.
  if(NOT CONFIG_EXT_RECUR_PARTITIONS AND CONFIG_UNEVEN_4WAY)
    change_config_and_warn(CONFIG_UNEVEN_4WAY 0 !CONFIG_EXT_RECUR_PARTITIONS)
  endif()

  # CONFIG_H_PARTITION is dependent on CONFIG_EXT_RECUR_PARTITIONS. If
  # CONFIG_EXT_RECUR_PARTITIONS is off, CONFIG_H_PARTITION needs to be turned
  # off.
  if(NOT CONFIG_EXT_RECUR_PARTITIONS AND CONFIG_H_PARTITION)
    change_config_and_warn(CONFIG_H_PARTITION 0 !CONFIG_EXT_RECUR_PARTITIONS)
  endif()

endmacro()
