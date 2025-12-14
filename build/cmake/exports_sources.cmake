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
if(AVM_BUILD_CMAKE_EXPORTS_SOURCES_CMAKE_)
  return()
endif() # AVM_BUILD_CMAKE_EXPORTS_SOURCES_CMAKE_
set(AVM_BUILD_CMAKE_EXPORTS_SOURCES_CMAKE_ 1)

list(APPEND AVM_EXPORTS_SOURCES "${AVM_ROOT}/avm/exports_com"
     "${AVM_ROOT}/av2/exports_com")

if(CONFIG_AV2_DECODER)
  list(APPEND AVM_EXPORTS_SOURCES "${AVM_ROOT}/avm/exports_dec"
       "${AVM_ROOT}/av2/exports_dec")
  if(CONFIG_INSPECTION)
    list(APPEND AVM_EXPORTS_SOURCES "${AVM_ROOT}/av2/exports_ident")
  endif()
endif()

if(CONFIG_AV2_ENCODER)
  list(APPEND AVM_EXPORTS_SOURCES "${AVM_ROOT}/avm/exports_enc"
       "${AVM_ROOT}/av2/exports_enc")
endif()

if(ENABLE_TESTS)
  list(APPEND AVM_EXPORTS_SOURCES "${AVM_ROOT}/avm/exports_test"
       "${AVM_ROOT}/av2/exports_test")
endif()
