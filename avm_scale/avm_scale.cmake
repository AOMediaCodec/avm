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
if(AVM_AVM_SCALE_AVM_SCALE_CMAKE_)
  return()
endif() # AVM_AVM_SCALE_AVM_SCALE_CMAKE_
set(AVM_AVM_SCALE_AVM_SCALE_CMAKE_ 1)

list(
  APPEND
  AVM_SCALE_SOURCES
  "${AVM_ROOT}/avm_scale/avm_scale.h"
  "${AVM_ROOT}/avm_scale/generic/avm_scale.c"
  "${AVM_ROOT}/avm_scale/generic/gen_scalers.c"
  "${AVM_ROOT}/avm_scale/generic/yv12config.c"
  "${AVM_ROOT}/avm_scale/generic/yv12extend.c"
  "${AVM_ROOT}/avm_scale/yv12config.h")

list(APPEND AVM_SCALE_INTRIN_DSPR2
     "${AVM_ROOT}/avm_scale/mips/dspr2/yv12extend_dspr2.c")

# Creates the avm_scale build target and makes libavm depend on it. The libavm
# target must exist before this function is called.
function(setup_avm_scale_targets)
  add_library(avm_scale OBJECT ${AVM_SCALE_SOURCES})
  target_sources(avm PRIVATE $<TARGET_OBJECTS:avm_scale>)

  if(HAVE_DSPR2)
    add_intrinsics_object_library("" "dspr2" "avm_scale"
                                  "AVM_SCALE_INTRIN_DSPR2")
  endif()

  target_sources(avm PRIVATE $<TARGET_OBJECTS:avm_scale>)
  if(BUILD_SHARED_LIBS)
    target_sources(avm_static PRIVATE $<TARGET_OBJECTS:avm_scale>)
  endif()

  # Pass the new lib targets up to the parent scope instance of
  # $AVM_LIB_TARGETS.
  set(AVM_LIB_TARGETS
      ${AVM_LIB_TARGETS} avm_scale
      PARENT_SCOPE)
endfunction()
