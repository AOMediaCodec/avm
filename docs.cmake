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
if(AVM_DOCS_CMAKE_)
  return()
endif() # AVM_DOCS_CMAKE_
set(AVM_DOCS_CMAKE_ 1)

cmake_minimum_required(VERSION 3.16)

set(AVM_DOXYFILE "${AVM_CONFIG_DIR}/doxyfile")
set(AVM_DOXYGEN_CONFIG_TEMPLATE "libs.doxy_template")
set(AVM_DOXYGEN_OUTPUT_DIR "${AVM_CONFIG_DIR}/dox")
set(AVM_DOXYGEN_SECTIONS "av2")

set(AVM_DOXYGEN_SOURCES
    "${AVM_ROOT}/avm/avm.h"
    "${AVM_ROOT}/avm/avm_codec.h"
    "${AVM_ROOT}/avm/avm_decoder.h"
    "${AVM_ROOT}/avm/avm_encoder.h"
    "${AVM_ROOT}/avm/avm_frame_buffer.h"
    "${AVM_ROOT}/avm/avm_image.h"
    "${AVM_ROOT}/avm/avm_integer.h"
    "${AVM_ROOT}/av2/common/av2_common_int.h"
    "${AVM_ROOT}/av2/common/av2_loopfilter.h"
    "${AVM_ROOT}/av2/common/blockd.h"
    "${AVM_ROOT}/av2/common/cdef.h"
    "${AVM_ROOT}/av2/common/enums.h"
    "${AVM_ROOT}/av2/common/restoration.h"
    "${AVM_ROOT}/keywords.dox"
    "${AVM_ROOT}/mainpage.dox"
    "${AVM_ROOT}/usage.dox")

if(CONFIG_AV2_DECODER)
  set(AVM_DOXYGEN_EXAMPLE_SOURCES
      ${AVM_DOXYGEN_EXAMPLE_SOURCES}
      "${AVM_ROOT}/apps/avmdec.c"
      "${AVM_ROOT}/examples/decode_to_md5.c"
      "${AVM_ROOT}/examples/decode_with_drops.c"
      "${AVM_ROOT}/examples/simple_decoder.c")

  set(AVM_DOXYGEN_EXAMPLE_DESCRIPTIONS
      ${AVM_DOXYGEN_EXAMPLE_DESCRIPTIONS} "Full featured decoder."
      "Frame by frame MD5 checksum." "Drops frames while decoding."
      "Simplified decoder loop.")

  set(AVM_DOXYGEN_SECTIONS ${AVM_DOXYGEN_SECTIONS} "av2_decoder decoder")

  set(AVM_DOXYGEN_SOURCES
      ${AVM_DOXYGEN_SOURCES} "${AVM_ROOT}/avm/avmdx.h"
      "${AVM_ROOT}/usage_dx.dox" "${AVM_ROOT}/av2/decoder/decoder.h")

  if(CONFIG_ANALYZER)
    set(AVM_DOXYGEN_EXAMPLE_SOURCES ${AVM_DOXYGEN_EXAMPLE_SOURCES}
                                    "${AVM_ROOT}/examples/analyzer.cc")

    set(AVM_DOXYGEN_EXAMPLE_DESCRIPTIONS ${AVM_DOXYGEN_EXAMPLE_DESCRIPTIONS}
                                         "Bitstream analyzer.")
  endif()

  if(CONFIG_INSPECTION)
    set(AVM_DOXYGEN_EXAMPLE_SOURCES ${AVM_DOXYGEN_EXAMPLE_SOURCES}
                                    "${AVM_ROOT}/examples/inspect.c")

    set(AVM_DOXYGEN_EXAMPLE_DESCRIPTIONS ${AVM_DOXYGEN_EXAMPLE_DESCRIPTIONS}
                                         "Bitstream inspector.")
  endif()

  set(AVM_DOXYGEN_SOURCES ${AVM_DOXYGEN_SOURCES}
                          "${AVM_ROOT}/doc/dev_guide/av2_decoder.dox")
endif()

if(CONFIG_AV2_ENCODER)
  set(AVM_DOXYGEN_EXAMPLE_SOURCES
      ${AVM_DOXYGEN_EXAMPLE_SOURCES}
      "${AVM_ROOT}/apps/avmenc.c"
      "${AVM_ROOT}/examples/lossless_encoder.c"
      "${AVM_ROOT}/examples/set_maps.c"
      "${AVM_ROOT}/examples/simple_encoder.c"
      "${AVM_ROOT}/examples/twopass_encoder.c")

  set(AVM_DOXYGEN_EXAMPLE_DESCRIPTIONS
      ${AVM_DOXYGEN_EXAMPLE_DESCRIPTIONS} "Full featured encoder."
      "Simplified lossless encoder." "Set active and ROI maps."
      "Simplified encoder loop." "Two-pass encoder loop.")

  set(AVM_DOXYGEN_EXAMPLE_SOURCES ${AVM_DOXYGEN_EXAMPLE_SOURCES}
                                  "${AVM_ROOT}/examples/scalable_encoder.c")

  set(AVM_DOXYGEN_EXAMPLE_DESCRIPTIONS ${AVM_DOXYGEN_EXAMPLE_DESCRIPTIONS}
                                       "Scalable encoder loop.")

  set(AVM_DOXYGEN_SECTIONS ${AVM_DOXYGEN_SECTIONS} "av2_encoder encoder")

  set(AVM_DOXYGEN_SOURCES ${AVM_DOXYGEN_SOURCES} "${AVM_ROOT}/avm/avmcx.h"
                          "${AVM_ROOT}/usage_cx.dox")
  set(AVM_DOXYGEN_SOURCES ${AVM_DOXYGEN_SOURCES}
                          "${AVM_ROOT}/doc/dev_guide/av2_encoder.dox")
  set(AVM_DOXYGEN_SOURCES
      ${AVM_DOXYGEN_SOURCES}
      "${AVM_ROOT}/avm_scale/yv12config.h"
      "${AVM_ROOT}/av2/encoder/bitstream.h"
      "${AVM_ROOT}/av2/encoder/block.h"
      "${AVM_ROOT}/av2/encoder/aq_cyclicrefresh.h"
      "${AVM_ROOT}/av2/encoder/encode_strategy.c"
      "${AVM_ROOT}/av2/encoder/encode_strategy.h"
      "${AVM_ROOT}/av2/encoder/encodeframe.c"
      "${AVM_ROOT}/av2/encoder/encoder.c"
      "${AVM_ROOT}/av2/encoder/encoder.h"
      "${AVM_ROOT}/av2/encoder/encodetxb.h"
      "${AVM_ROOT}/av2/encoder/firstpass.h"
      "${AVM_ROOT}/av2/encoder/gop_structure.h"
      "${AVM_ROOT}/av2/encoder/interp_search.c"
      "${AVM_ROOT}/av2/encoder/intra_mode_search.h"
      "${AVM_ROOT}/av2/encoder/intra_mode_search.c"
      "${AVM_ROOT}/av2/encoder/intra_mode_search_utils.h"
      "${AVM_ROOT}/av2/encoder/lookahead.h"
      "${AVM_ROOT}/av2/encoder/palette.h"
      "${AVM_ROOT}/av2/encoder/palette.c"
      "${AVM_ROOT}/av2/encoder/partition_search.h"
      "${AVM_ROOT}/av2/encoder/partition_search.c"
      "${AVM_ROOT}/av2/encoder/pass2_strategy.h"
      "${AVM_ROOT}/av2/encoder/pass2_strategy.c"
      "${AVM_ROOT}/av2/encoder/pickcdef.h"
      "${AVM_ROOT}/av2/encoder/picklpf.h"
      "${AVM_ROOT}/av2/encoder/pickrst.h"
      "${AVM_ROOT}/av2/encoder/ratectrl.c"
      "${AVM_ROOT}/av2/encoder/ratectrl.h"
      "${AVM_ROOT}/av2/encoder/rc_utils.h"
      "${AVM_ROOT}/av2/encoder/rdopt.h"
      "${AVM_ROOT}/av2/encoder/rdopt.c"
      "${AVM_ROOT}/av2/encoder/speed_features.h"
      "${AVM_ROOT}/av2/encoder/temporal_filter.h"
      "${AVM_ROOT}/av2/encoder/temporal_filter.c"
      "${AVM_ROOT}/av2/encoder/tpl_model.h"
      "${AVM_ROOT}/av2/encoder/tx_search.h")
endif()

if(CONFIG_AV2_DECODER AND CONFIG_AV2_ENCODER)
  set(AVM_DOXYGEN_EXAMPLE_SOURCES ${AVM_DOXYGEN_EXAMPLE_SOURCES}
                                  "${AVM_ROOT}/examples/avm_cx_set_ref.c")

  set(AVM_DOXYGEN_EXAMPLE_DESCRIPTIONS ${AVM_DOXYGEN_EXAMPLE_DESCRIPTIONS}
                                       "Set encoder reference frame.")
endif()

# Iterates over list named by $list_name and appends each item to $AVM_DOXYFILE
# as values assigned to $var_name with no line breaks between list items.
# Appends a new line after the entire config variable is expanded.
function(write_cmake_list_to_doxygen_config_var var_name list_name)
  unset(output_string)
  foreach(list_item ${${list_name}})
    set(output_string "${output_string} ${list_item} ")
  endforeach()
  string(STRIP "${output_string}" output_string)
  file(APPEND "${AVM_DOXYFILE}" "${var_name} += ${output_string}\n")
endfunction()

function(get_name file_path name_var)
  get_filename_component(file_basename ${file_path} NAME)
  get_filename_component(${name_var} ${file_basename} NAME_WE)
  set(${name_var}
      ${${name_var}}
      PARENT_SCOPE)
endfunction()

function(setup_documentation_targets)

  # Sanity check: the lengths of these lists must match.
  list(LENGTH AVM_DOXYGEN_EXAMPLE_SOURCES num_sources)
  list(LENGTH AVM_DOXYGEN_EXAMPLE_DESCRIPTIONS num_descs)
  if(NOT ${num_sources} EQUAL ${num_descs})
    message(FATAL_ERROR "Unqeual example and description totals.")
  endif()

  # Take the list of examples and produce example_basename.dox for each file in
  # the list.
  file(MAKE_DIRECTORY "${AVM_DOXYGEN_OUTPUT_DIR}")
  foreach(example_file ${AVM_DOXYGEN_EXAMPLE_SOURCES})
    unset(example_basename)
    get_name("${example_file}" "example_name")
    set(example_dox "${AVM_DOXYGEN_OUTPUT_DIR}/${example_name}.dox")
    set(dox_string "/*!\\page example_${example_name} ${example_name}\n")
    set(dox_string "${dox_string} \\includelineno ${example_file}\n*/\n")
    file(WRITE "${example_dox}" ${dox_string})
    set(AVM_DOXYGEN_SOURCES ${AVM_DOXYGEN_SOURCES} "${example_dox}")
  endforeach()

  # Generate samples.dox, an index page that refers to the example_basename.dox
  # files that were just created.
  set(samples_header
      "
/*!\\page samples Sample Code
This SDK includes a number of sample applications. Each sample documents a
feature of the SDK in both prose and the associated C code. The following
samples are included:
")

  set(utils_desc
      "
In addition, the SDK contains a number of utilities. Since these utilities are
built upon the concepts described in the sample code listed above, they are not
documented in pieces like the samples are. Their source is included here for
reference. The following utilities are included:
")

  # Write the description for the samples section.
  set(samples_dox "${AVM_CONFIG_DIR}/samples.dox")
  file(WRITE "${samples_dox}" "${samples_header}\n")

  # Iterate over $AVM_DOXYGEN_EXAMPLE_SOURCES and
  # $AVM_DOXYGEN_EXAMPLE_DESCRIPTIONS and massage example names as required by
  # AV2's doxygen setup.
  math(EXPR max_example_index "${num_sources} - 1")
  foreach(NUM RANGE ${max_example_index})
    list(GET AVM_DOXYGEN_EXAMPLE_SOURCES ${NUM} ex_name)
    get_name("${ex_name}" "ex_name")

    # AV2's doxygen lists avmdec and avmenc as utils apart from the examples.
    # Save the indexes for another pass.
    if("${ex_name}" MATCHES "avmdec\|avmenc")
      set(util_indexes "${util_indexes}" "${NUM}")
      continue()
    endif()
    list(GET AVM_DOXYGEN_EXAMPLE_DESCRIPTIONS ${NUM} ex_desc)
    file(APPEND "${samples_dox}" " - \\subpage example_${ex_name} ${ex_desc}\n")
  endforeach()

  # Write the description and index for the utils.
  file(APPEND "${samples_dox}" "${utils_desc}\n")
  foreach(util_index ${util_indexes})
    list(GET AVM_DOXYGEN_EXAMPLE_SOURCES ${util_index} ex_name)
    get_name("${ex_name}" "ex_name")
    list(GET AVM_DOXYGEN_EXAMPLE_DESCRIPTIONS ${util_index} ex_desc)
    file(APPEND "${samples_dox}" " - \\subpage example_${ex_name} ${ex_desc}\n")
  endforeach()
  file(APPEND "${samples_dox}" "*/")

  # Add $samples_dox to the doxygen inputs.
  get_filename_component(samples_dox ${samples_dox} NAME)
  set(AVM_DOXYGEN_SOURCES ${AVM_DOXYGEN_SOURCES} ${samples_dox})

  # There are issues to show Markdown file for old Doxygen version. Here, only
  # enable Markdown support for 1.8.16 or newer.
  if(${DOXYGEN_VERSION_VALUE} GREATER_EQUAL 1008016)
    set(AVM_DOXYGEN_SECTIONS ${AVM_DOXYGEN_SECTIONS} "av2_md_support")
    set(AVM_DOXYGEN_SOURCES ${AVM_DOXYGEN_SOURCES} "${AVM_ROOT}/README.md")
    # Uncomment and add AlgorithmDescription.md in result page when it is done.
    # set(AVM_DOXYGEN_SOURCES ${AVM_DOXYGEN_SOURCES}
    # "${AVM_ROOT}/doc/AlgorithmDescription.md")
  endif()

  # Generate libavm's doxyfile.
  file(WRITE "${AVM_DOXYFILE}" "##\n## GENERATED FILE. DO NOT EDIT\n##\n")
  file(READ "${AVM_ROOT}/${AVM_DOXYGEN_CONFIG_TEMPLATE}" doxygen_template_data)
  file(APPEND "${AVM_DOXYFILE}" ${doxygen_template_data})
  file(APPEND "${AVM_DOXYFILE}"
       "EXAMPLE_PATH += ${AVM_ROOT} ${AVM_ROOT}/examples\n")
  file(APPEND "${AVM_DOXYFILE}"
       "INCLUDE_PATH += ${AVM_CONFIG_DIR} ${AVM_ROOT}\n")
  file(APPEND "${AVM_DOXYFILE}"
       "STRIP_FROM_PATH += ${AVM_ROOT} ${AVM_CONFIG_DIR}\n")
  write_cmake_list_to_doxygen_config_var("INPUT" "AVM_DOXYGEN_SOURCES")
  write_cmake_list_to_doxygen_config_var("ENABLED_SECTIONS"
                                         "AVM_DOXYGEN_SECTIONS")

  # Add AOMedia logo.
  set(avm_logo "aomedia_logo_200.png")
  configure_file(${AVM_ROOT}/${avm_logo} ${AVM_CONFIG_DIR}/${avm_logo} COPYONLY)
  file(APPEND "${AVM_DOXYFILE}"
       "PROJECT_LOGO = ${AVM_CONFIG_DIR}/${avm_logo}\n")

  # Only set HAVE_DOT to YES if dot tool is found.
  if(DOXYGEN_DOT_FOUND)
    file(APPEND "${AVM_DOXYFILE}" "HAVE_DOT = YES\n")
    file(APPEND "${AVM_DOXYFILE}" "DOT_GRAPH_MAX_NODES = 10000\n")
  endif()

  # Add image path.
  file(APPEND "${AVM_DOXYFILE}" "IMAGE_PATH += ${AVM_ROOT}/doc/dev_guide\n")

  # Allow banner style comments
  file(APPEND "${AVM_DOXYFILE}" "JAVADOC_BANNER = YES")

  # Add the doxygen generation rule.
  add_custom_target(
    docs ALL
    COMMAND "${DOXYGEN_EXECUTABLE}" "${AVM_DOXYFILE}"
    DEPENDS "${AVM_DOXYFILE}" ${AVM_DOXYGEN_SOURCES}
            ${AVM_DOXYGEN_EXAMPLE_SOURCES} "${AVM_DOXYGEN_CONFIG_TEMPLATE}"
    SOURCES "${AVM_DOXYFILE}" ${AVM_DOXYGEN_SOURCES}
            ${AVM_DOXYGEN_EXAMPLE_SOURCES} "${AVM_DOXYGEN_CONFIG_TEMPLATE}")
endfunction()
