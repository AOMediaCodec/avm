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
import yaml


def read_config_data(cfg_file):
    with open(cfg_file) as f:
        data = yaml.load(f, Loader=yaml.SafeLoader)
        return (data["TEST_OUTPUT_TAG"], data["DESIRED_CTX_LIST"])


def read_config_decode(cfg_file):
    with open(cfg_file) as f:
        data = yaml.load(f, Loader=yaml.SafeLoader)
        return data["BITSTREAM_EXTENSION"].replace(".", "")


def read_config_context(cfg_file, context_name):
    with open(cfg_file) as f:
        data = yaml.load(f, Loader=yaml.SafeLoader)
        return data[context_name]
