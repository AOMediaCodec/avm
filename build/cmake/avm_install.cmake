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
list(
  APPEND
  AVM_INSTALL_INCS
  "${AVM_ROOT}/avm/avm.h"
  "${AVM_ROOT}/avm/avm_codec.h"
  "${AVM_ROOT}/avm/avm_frame_buffer.h"
  "${AVM_ROOT}/avm/avm_image.h"
  "${AVM_ROOT}/avm/avm_integer.h"
  "${AVM_ROOT}/avm/avm.h")

if(CONFIG_AV2_DECODER)
  list(APPEND AVM_INSTALL_INCS "${AVM_ROOT}/avm/avm_decoder.h"
       "${AVM_ROOT}/avm/avmdx.h")
endif()

if(CONFIG_AV2_ENCODER)
  list(APPEND AVM_INSTALL_INCS "${AVM_ROOT}/avm/avmcx.h"
       "${AVM_ROOT}/avm/avm_encoder.h")
endif()

# Generate avm.pc and setup dependencies to ensure it is created when necessary.
# Note: avm.pc generation uses GNUInstallDirs:
# https://cmake.org/cmake/help/latest/module/GNUInstallDirs.html
macro(setup_avm_install_targets)
  if(NOT (MSVC OR XCODE))
    include("GNUInstallDirs")
    set(AVM_PKG_CONFIG_FILE "${AVM_CONFIG_DIR}/avm.pc")

    # Create a dummy library target for creating avm.pc.
    create_dummy_source_file(avm_pc c AVM_PKG_CONFIG_SOURCES)
    add_library(avm_pc ${AVM_PKG_CONFIG_SOURCES})

    # Setup a rule to generate avm.pc.
    add_custom_command(
      OUTPUT "${AVM_PKG_CONFIG_FILE}"
      COMMAND
        ${CMAKE_COMMAND} ARGS -DAVM_CONFIG_DIR=${AVM_CONFIG_DIR}
        -DAVM_ROOT=${AVM_ROOT} -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
        -DCMAKE_INSTALL_BINDIR=${CMAKE_INSTALL_BINDIR}
        -DCMAKE_INSTALL_INCLUDEDIR=${CMAKE_INSTALL_INCLUDEDIR}
        -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
        -DCMAKE_PROJECT_NAME=${CMAKE_PROJECT_NAME}
        -DCONFIG_MULTITHREAD=${CONFIG_MULTITHREAD}
        -DHAVE_PTHREAD_H=${HAVE_PTHREAD_H} -P
        "${AVM_ROOT}/build/cmake/pkg_config.cmake"
      COMMENT "Writing avm.pc"
      VERBATIM)

    # Explicitly add a dependency on the pkg-config file to ensure it's built.
    get_property(
      avm_pc_sources
      TARGET avm_pc
      PROPERTY SOURCES)
    set_source_files_properties(${avm_pc_sources} OBJECT_DEPENDS
                                "${AVM_PKG_CONFIG_FILE}")

    # Our pkg-config file carries version information: add a dependency on the
    # version rule.
    add_dependencies(avm_pc avm_version)

    if(CONFIG_AV2_DECODER)
      if(ENABLE_EXAMPLES)
        list(APPEND AVM_INSTALL_BINS avmdec)
      endif()
    endif()

    if(CONFIG_AV2_ENCODER)
      if(ENABLE_EXAMPLES)
        list(APPEND AVM_INSTALL_BINS avmenc)
      endif()
    endif()

    if(BUILD_SHARED_LIBS)
      set(AVM_INSTALL_LIBS avm avm_static)
    else()
      set(AVM_INSTALL_LIBS avm)
    endif()

    # Setup the install rules.
    install(
      FILES ${AVM_INSTALL_INCS}
      DESTINATION "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}/avm")
    install(
      FILES "${AVM_PKG_CONFIG_FILE}"
      DESTINATION "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/pkgconfig")
    install(TARGETS ${AVM_INSTALL_LIBS}
            DESTINATION "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

    if(ENABLE_EXAMPLES)
      install(TARGETS ${AVM_INSTALL_BINS}
              DESTINATION "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
    endif()
  endif()
endmacro()
