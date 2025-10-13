#!/bin/sh
## Copyright (c) 2021, Alliance for Open Media. All rights reserved
##
## This source code is subject to the terms of the BSD 3-Clause Clear License and the
## Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear License was
## not distributed with this source code in the LICENSE file, you can obtain it
## at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance for Open Media Patent
## License 1.0 was not distributed with this source code in the PATENTS file, you
## can obtain it at aomedia.org/license/patent-license/.
##
## This file runs all of the tests for the libaom examples.
##
. $(dirname $0)/tools_common.sh

example_tests=$(ls -r $(dirname $0)/*.sh)

# List of script names to exclude.
exclude_list="best_encode examples run_encodes tools_common"
exclude_list="$exclude_list twopass_encoder"
exclude_list="$exclude_list extract_proto_test"
exclude_list="$exclude_list aomcx_set_ref"

# Filter out the scripts in $exclude_list.
for word in ${exclude_list}; do
  example_tests=$(filter_strings "${example_tests}" "${word}" exclude)
done

for test in ${example_tests}; do
  # Source each test script so that exporting variables can be avoided.
  AOM_TEST_NAME="$(basename ${test%.*})"
  . "${test}"
done
