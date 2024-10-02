"""
Copyright (c) 2024, Alliance for Open Media. All rights reserved

This source code is subject to the terms of the BSD 3-Clause Clear License
and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
License was not distributed with this source code in the LICENSE file, you
can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
Alliance for Open Media Patent License 1.0 was not distributed with this
source code in the PATENTS file, you can obtain it at
aomedia.org/license/patent-license/.
"""
DEFAULT_PROB_INITIALIZER = False
DEFAULT_RATE_PARAMETER = False
ZERO_RATE_PARAMETER = False
MIN_NUM_DATA_SAMPLES_NEEDED = 10
DESIRED_RATE_IDX = "best_rate_idx"

REGULAR_CTX_GROUP_MAPPING = {
    "eob_flag_cdf16": "av1_default_eob_multi16_cdfs",
    "eob_flag_cdf32": "av1_default_eob_multi32_cdfs",
}
