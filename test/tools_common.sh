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
##  This file contains shell code shared by test scripts for libavm tools.

# Use $AVM_TEST_TOOLS_COMMON_SH as a pseudo include guard.
if [ -z "${AVM_TEST_TOOLS_COMMON_SH}" ]; then
AVM_TEST_TOOLS_COMMON_SH=included

set -e
devnull='> /dev/null 2>&1'
AVM_TEST_PREFIX=""

elog() {
  echo "$@" 1>&2
}

vlog() {
  if [ "${AVM_TEST_VERBOSE_OUTPUT}" = "yes" ]; then
    echo "$@"
  fi
}

# Sets $AVM_TOOL_TEST to the name specified by positional parameter one.
test_begin() {
  AVM_TOOL_TEST="${1}"
}

# Clears the AVM_TOOL_TEST variable after confirming that $AVM_TOOL_TEST matches
# positional parameter one.
test_end() {
  if [ "$1" != "${AVM_TOOL_TEST}" ]; then
    echo "FAIL completed test mismatch!."
    echo "  completed test: ${1}"
    echo "  active test: ${AVM_TOOL_TEST}."
    return 1
  fi
  AVM_TOOL_TEST='<unset>'
}

# Echoes the target configuration being tested.
test_configuration_target() {
  avm_config_c="${LIBAVM_CONFIG_PATH}/config/avm_config.c"
  # Clean up the cfg pointer line from avm_config.c for easier re-use by
  # someone examining a failure in the example tests.
  # 1. Run grep on avm_config.c for cfg and limit the results to 1.
  # 2. Split the line using ' = ' as separator.
  # 3. Abuse sed to consume the leading " and trailing "; from the assignment
  #    to the cfg pointer.
  cmake_config=$(awk -F ' = ' '/cfg/ { print $NF; exit }' "${avm_config_c}" \
    | sed -e s/\"// -e s/\"\;//)
  echo cmake generated via command: cmake path/to/avm ${cmake_config}
}

# Trap function used for failure reports and tool output directory removal.
# When the contents of $AVM_TOOL_TEST do not match the string '<unset>', reports
# failure of test stored in $AVM_TOOL_TEST.
cleanup() {
  if [ -n "${AVM_TOOL_TEST}" ] && [ "${AVM_TOOL_TEST}" != '<unset>' ]; then
    echo "FAIL: $AVM_TOOL_TEST"
  fi
  if [ "${AVM_TEST_PRESERVE_OUTPUT}" = "yes" ]; then
    return
  fi
  if [ -n "${AVM_TEST_OUTPUT_DIR}" ] && [ -d "${AVM_TEST_OUTPUT_DIR}" ]; then
    rm -rf "${AVM_TEST_OUTPUT_DIR}"
  fi
}

# Echoes the version string assigned to the VERSION_STRING_NOSP variable defined
# in $LIBAVM_CONFIG_PATH/config/avm_version.h to stdout.
cmake_version() {
  avm_version_h="${LIBAVM_CONFIG_PATH}/config/avm_version.h"

  # Find VERSION_STRING_NOSP line, split it with '"' and print the next to last
  # field to output the version string to stdout.
  avm_version=$(awk -F \" '/VERSION_STRING_NOSP/ {print $(NF-1)}' \
    "${avm_version_h}")
  echo "v${avm_version}"
}

# Echoes current git version as reported by running 'git describe', or the
# version used by the cmake build when git is unavailable.
# Note: version.cmake strips optional prefix before the version. So,
# source_version() function needs to do the same, so that the strings can be
# compared in check_version_strings() later.
source_version() {
  if git --version > /dev/null 2>&1; then
    (cd "$(dirname "${0}")"
    git describe | sed 's/.*-v/v/')
  else
    cmake_version
  fi
}

# Echoes warnings to stdout when source version and CMake build generated
# version are out of sync.
check_version_strings() {
  cmake_version=$(cmake_version)
  source_version=$(source_version)

  if [ "${cmake_version}" != "${source_version}" ]; then
    echo "Warning: version has changed since last cmake run."
    vlog "  cmake version: ${cmake_version} version now: ${source_version}"
  fi
}

# $1 is the name of an environment variable containing a directory name to
# test.
test_env_var_dir() {
  local dir=$(eval echo "\${$1}")
  if [ ! -d "${dir}" ]; then
    elog "'${dir}': No such directory"
    elog "The $1 environment variable must be set to a valid directory."
    return 1
  fi
}

# This script requires that the LIBAVM_BIN_PATH, LIBAVM_CONFIG_PATH, and
# LIBAVM_TEST_DATA_PATH variables are in the environment: Confirm that
# the variables are set and that they all evaluate to directory paths.
verify_avm_test_environment() {
  test_env_var_dir "LIBAVM_BIN_PATH" \
    && test_env_var_dir "LIBAVM_CONFIG_PATH" \
    && test_env_var_dir "LIBAVM_TEST_DATA_PATH"
}

# Greps avm_config.h in LIBAVM_CONFIG_PATH for positional parameter one, which
# should be a LIBAVM preprocessor flag. Echoes yes to stdout when the feature
# is available.
avm_config_option_enabled() {
  avm_config_option="${1}"
  avm_config_file="${LIBAVM_CONFIG_PATH}/config/avm_config.h"
  config_line=$(grep "${avm_config_option}" "${avm_config_file}")
  if echo "${config_line}" | egrep -q '1$'; then
    echo yes
  fi
}

# Echoes yes when output of test_configuration_target() contains win32 or win64.
is_windows_target() {
  if test_configuration_target \
     | grep -q -e win32 -e win64 > /dev/null 2>&1; then
    echo yes
  fi
}

# Echoes path to $1 when it's executable and exists in one of the directories
# included in $tool_paths, or an empty string. Caller is responsible for testing
# the string once the function returns.
avm_tool_path() {
  local tool_name="$1"
  local root_path="${LIBAVM_BIN_PATH}"
  local suffix="${AVM_TEST_EXE_SUFFIX}"
  local tool_paths="\
    ${root_path}/${tool_name}${suffix} \
    ${root_path}/../${tool_name}${suffix} \
    ${root_path}/tools/${tool_name}${suffix} \
    ${root_path}/../tools/${tool_name}${suffix}"

  local toolpath=""

  for tool_path in ${tool_paths}; do
    if [ -x "${tool_path}" ] && [ -f "${tool_path}" ]; then
      echo "${tool_path}"
      return 0
    fi
  done

  return 1
}

# Echoes yes to stdout when the file named by positional parameter one exists
# in LIBAVM_BIN_PATH, and is executable.
avm_tool_available() {
  local tool_name="$1"
  local tool="${LIBAVM_BIN_PATH}/${tool_name}${AVM_TEST_EXE_SUFFIX}"
  [ -x "${tool}" ] && echo yes
}

# Echoes yes to stdout when avm_config_option_enabled() reports yes for
# CONFIG_AV2_DECODER.
av2_decode_available() {
  [ "$(avm_config_option_enabled CONFIG_AV2_DECODER)" = "yes" ] && echo yes
}

# Echoes yes to stdout when avm_config_option_enabled() reports yes for
# CONFIG_AV2_ENCODER.
av2_encode_available() {
  [ "$(avm_config_option_enabled CONFIG_AV2_ENCODER)" = "yes" ] && echo yes
}

# Echoes "fast" encode params for use with avmenc.
avmenc_encode_test_fast_params() {
  echo "--cpu-used=2
        --limit=${AV2_ENCODE_TEST_FRAME_LIMIT}
        --lag-in-frames=0
        --test-decode=fatal"
}

# Echoes yes to stdout when avm_config_option_enabled() reports yes for
# CONFIG_WEBM_IO.
webm_io_available() {
  [ "$(avm_config_option_enabled CONFIG_WEBM_IO)" = "yes" ] && echo yes
}

# Filters strings from $1 using the filter specified by $2. Filter behavior
# depends on the presence of $3. When $3 is present, strings that match the
# filter are excluded. When $3 is omitted, strings matching the filter are
# included.
# The filtered result is echoed to stdout.
filter_strings() {
  strings=${1}
  filter=${2}
  exclude=${3}

  if [ -n "${exclude}" ]; then
    # When positional parameter three exists the caller wants to remove strings.
    # Tell grep to invert matches using the -v argument.
    exclude='-v'
  else
    unset exclude
  fi

  if [ -n "${filter}" ]; then
    for s in ${strings}; do
      if echo "${s}" | egrep -q ${exclude} "${filter}" > /dev/null 2>&1; then
        filtered_strings="${filtered_strings} ${s}"
      fi
    done
  else
    filtered_strings="${strings}"
  fi
  echo "${filtered_strings}"
}

# Runs user test functions passed via positional parameters one and two.
# Functions in positional parameter one are treated as environment verification
# functions and are run unconditionally. Functions in positional parameter two
# are run according to the rules specified in avm_test_usage().
run_tests() {
  local env_tests="verify_avm_test_environment $1"
  local tests_to_filter="$2"
  local test_name="${AVM_TEST_NAME}"

  if [ -z "${test_name}" ]; then
    test_name="$(basename "${0%.*}")"
  fi

  if [ "${AVM_TEST_RUN_DISABLED_TESTS}" != "yes" ]; then
    # Filter out DISABLED tests.
    tests_to_filter=$(filter_strings "${tests_to_filter}" ^DISABLED exclude)
  fi

  if [ -n "${AVM_TEST_FILTER}" ]; then
    # Remove tests not matching the user's filter.
    tests_to_filter=$(filter_strings "${tests_to_filter}" ${AVM_TEST_FILTER})
  fi

  # User requested test listing: Dump test names and return.
  if [ "${AVM_TEST_LIST_TESTS}" = "yes" ]; then
    for test_name in $tests_to_filter; do
      echo ${test_name}
    done
    return
  fi

  # Don't bother with the environment tests if everything else was disabled.
  [ -z "${tests_to_filter}" ] && return

  # Combine environment and actual tests.
  local tests_to_run="${env_tests} ${tests_to_filter}"

  check_version_strings

  # Run tests.
  for test in ${tests_to_run}; do
    test_begin "${test}"
    vlog "  RUN  ${test}"
    "${test}"
    vlog "  PASS ${test}"
    test_end "${test}"
  done

  local tested_config="$(test_configuration_target) @ $(source_version)"
  echo "${test_name}: Done, all tests pass for ${tested_config}."
}

avm_test_usage() {
cat << EOF
  Usage: ${0##*/} [arguments]
    --bin-path <path to libavm binaries directory>
    --config-path <path to libavm config directory>
    --filter <filter>: User test filter. Only tests matching filter are run.
    --run-disabled-tests: Run disabled tests.
    --help: Display this message and exit.
    --test-data-path <path to libavm test data directory>
    --show-program-output: Shows output from all programs being tested.
    --prefix: Allows for a user specified prefix to be inserted before all test
              programs. Grants the ability, for example, to run test programs
              within valgrind.
    --list-tests: List all test names and exit without actually running tests.
    --verbose: Verbose output.

    When the --bin-path option is not specified the script attempts to use
    \$LIBAVM_BIN_PATH and then the current directory.

    When the --config-path option is not specified the script attempts to use
    \$LIBAVM_CONFIG_PATH and then the current directory.

    When the -test-data-path option is not specified the script attempts to use
    \$LIBAVM_TEST_DATA_PATH and then the current directory.
EOF
}

# Returns non-zero (failure) when required environment variables are empty
# strings.
avm_test_check_environment() {
  if [ -z "${LIBAVM_BIN_PATH}" ] || \
     [ -z "${LIBAVM_CONFIG_PATH}" ] || \
     [ -z "${LIBAVM_TEST_DATA_PATH}" ]; then
    return 1
  fi
}

# Echo avmenc command line parameters allowing use of a raw yuv file as
# input to avmenc.
yuv_raw_input() {
  echo ""${YUV_RAW_INPUT}"
       --width="${YUV_RAW_INPUT_WIDTH}"
       --height="${YUV_RAW_INPUT_HEIGHT}""
}

# Do a small encode for testing decoders.
encode_yuv_raw_input_av2() {
  if [ "$(av2_encode_available)" = "yes" ]; then
    local output="$1"
    local encoder="$(avm_tool_path avmenc)"
    shift
    eval "${encoder}" $(yuv_raw_input) \
      $(avmenc_encode_test_fast_params) \
      --output="${output}" \
      $@ \
      ${devnull}

    if [ ! -e "${output}" ]; then
      elog "Output file does not exist."
      return 1
    fi
  fi
}

# Parse the command line.
while [ -n "$1" ]; do
  case "$1" in
    --bin-path)
      LIBAVM_BIN_PATH="$2"
      shift
      ;;
    --config-path)
      LIBAVM_CONFIG_PATH="$2"
      shift
      ;;
    --filter)
      AVM_TEST_FILTER="$2"
      shift
      ;;
    --run-disabled-tests)
      AVM_TEST_RUN_DISABLED_TESTS=yes
      ;;
    --help)
      avm_test_usage
      exit
      ;;
    --test-data-path)
      LIBAVM_TEST_DATA_PATH="$2"
      shift
      ;;
    --prefix)
      AVM_TEST_PREFIX="$2"
      shift
      ;;
    --verbose)
      AVM_TEST_VERBOSE_OUTPUT=yes
      ;;
    --show-program-output)
      devnull=
      ;;
    --list-tests)
      AVM_TEST_LIST_TESTS=yes
      ;;
    *)
      avm_test_usage
      exit 1
      ;;
  esac
  shift
done

# Handle running the tests from a build directory without arguments when running
# the tests on *nix/macosx.
LIBAVM_BIN_PATH="${LIBAVM_BIN_PATH:-.}"
LIBAVM_CONFIG_PATH="${LIBAVM_CONFIG_PATH:-.}"
LIBAVM_TEST_DATA_PATH="${LIBAVM_TEST_DATA_PATH:-.}"

# Create a temporary directory for output files, and a trap to clean it up.
if [ -n "${TMPDIR}" ]; then
  AVM_TEST_TEMP_ROOT="${TMPDIR}"
elif [ -n "${TEMPDIR}" ]; then
  AVM_TEST_TEMP_ROOT="${TEMPDIR}"
else
  AVM_TEST_TEMP_ROOT=/tmp
fi

AVM_TEST_OUTPUT_DIR="${AVM_TEST_OUTPUT_DIR:-${AVM_TEST_TEMP_ROOT}/avm_test_$$}"

if ! mkdir -p "${AVM_TEST_OUTPUT_DIR}" || \
   [ ! -d "${AVM_TEST_OUTPUT_DIR}" ]; then
  echo "${0##*/}: Cannot create output directory, giving up."
  echo "${0##*/}:   AVM_TEST_OUTPUT_DIR=${AVM_TEST_OUTPUT_DIR}"
  exit 1
fi

AVM_TEST_PRESERVE_OUTPUT=${AVM_TEST_PRESERVE_OUTPUT:-no}

if [ "$(is_windows_target)" = "yes" ]; then
  AVM_TEST_EXE_SUFFIX=".exe"
fi

# Variables shared by tests.
AV2_ENCODE_CPU_USED=${AV2_ENCODE_CPU_USED:-5}
AV2_ENCODE_TEST_FRAME_LIMIT=${AV2_ENCODE_TEST_FRAME_LIMIT:-5}
AV2_IVF_FILE="${AV2_IVF_FILE:-${AVM_TEST_OUTPUT_DIR}/av2.ivf}"
AV2_OBU_FILE="${AV2_OBU_FILE:-${AVM_TEST_OUTPUT_DIR}/av2.obu}"
AV2_OBU_LCR_OPS_ATLAS_FILE="${AV2_OBU_LCR_OPS_ATLAS_FILE:-${AVM_TEST_OUTPUT_DIR}/av2.lcr_ops_atlas.obu}"
AV2_WEBM_FILE="${AV2_WEBM_FILE:-${AVM_TEST_OUTPUT_DIR}/av2.webm}"

YUV_RAW_INPUT="${LIBAVM_TEST_DATA_PATH}/hantro_collage_w352h288.yuv"
YUV_RAW_INPUT_WIDTH=352
YUV_RAW_INPUT_HEIGHT=288

Y4M_NOSQ_PAR_INPUT="${LIBAVM_TEST_DATA_PATH}/park_joy_90p_8_420_a10-1.y4m"
Y4M_720P_INPUT="${LIBAVM_TEST_DATA_PATH}/niklas_1280_720_30.y4m"

# Setup a trap function to clean up after tests complete.
trap cleanup EXIT

vlog "$(basename "${0%.*}") test configuration:
  LIBAVM_BIN_PATH=${LIBAVM_BIN_PATH}
  LIBAVM_CONFIG_PATH=${LIBAVM_CONFIG_PATH}
  LIBAVM_TEST_DATA_PATH=${LIBAVM_TEST_DATA_PATH}
  AVM_TEST_EXE_SUFFIX=${AVM_TEST_EXE_SUFFIX}
  AVM_TEST_FILTER=${AVM_TEST_FILTER}
  AVM_TEST_LIST_TESTS=${AVM_TEST_LIST_TESTS}
  AVM_TEST_OUTPUT_DIR=${AVM_TEST_OUTPUT_DIR}
  AVM_TEST_PREFIX=${AVM_TEST_PREFIX}
  AVM_TEST_PRESERVE_OUTPUT=${AVM_TEST_PRESERVE_OUTPUT}
  AVM_TEST_RUN_DISABLED_TESTS=${AVM_TEST_RUN_DISABLED_TESTS}
  AVM_TEST_SHOW_PROGRAM_OUTPUT=${AVM_TEST_SHOW_PROGRAM_OUTPUT}
  AVM_TEST_TEMP_ROOT=${AVM_TEST_TEMP_ROOT}
  AVM_TEST_VERBOSE_OUTPUT=${AVM_TEST_VERBOSE_OUTPUT}
  AV2_ENCODE_CPU_USED=${AV2_ENCODE_CPU_USED}
  AV2_ENCODE_TEST_FRAME_LIMIT=${AV2_ENCODE_TEST_FRAME_LIMIT}
  AV2_IVF_FILE=${AV2_IVF_FILE}
  AV2_OBU_FILE=${AV2_OBU_FILE}
  AV2_OBU_LCR_OPS_ATLAS_FILE=${AV2_OBU_LCR_OPS_ATLAS_FILE}
  AV2_WEBM_FILE=${AV2_WEBM_FILE}
  YUV_RAW_INPUT=${YUV_RAW_INPUT}
  YUV_RAW_INPUT_WIDTH=${YUV_RAW_INPUT_WIDTH}
  YUV_RAW_INPUT_HEIGHT=${YUV_RAW_INPUT_HEIGHT}
  Y4M_NOSQ_PAR_INPUT=${Y4M_NOSQ_PAR_INPUT}"

fi  # End $AVM_TEST_TOOLS_COMMON_SH pseudo include guard.
