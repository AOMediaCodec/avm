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
import numpy as np

CDF_PROB_BITS = 15
CDF_INIT_TOP = 32768
CDF_PROB_TOP = 2**CDF_PROB_BITS
AV2_PROB_COST_SHIFT = 9

MAX_CTX_DIM = 4  # maximum dimension in context tables

AV2_PROB_COST = (
    512,
    506,
    501,
    495,
    489,
    484,
    478,
    473,
    467,
    462,
    456,
    451,
    446,
    441,
    435,
    430,
    425,
    420,
    415,
    410,
    405,
    400,
    395,
    390,
    385,
    380,
    375,
    371,
    366,
    361,
    356,
    352,
    347,
    343,
    338,
    333,
    329,
    324,
    320,
    316,
    311,
    307,
    302,
    298,
    294,
    289,
    285,
    281,
    277,
    273,
    268,
    264,
    260,
    256,
    252,
    248,
    244,
    240,
    236,
    232,
    228,
    224,
    220,
    216,
    212,
    209,
    205,
    201,
    197,
    194,
    190,
    186,
    182,
    179,
    175,
    171,
    168,
    164,
    161,
    157,
    153,
    150,
    146,
    143,
    139,
    136,
    132,
    129,
    125,
    122,
    119,
    115,
    112,
    109,
    105,
    102,
    99,
    95,
    92,
    89,
    86,
    82,
    79,
    76,
    73,
    70,
    66,
    63,
    60,
    57,
    54,
    51,
    48,
    45,
    42,
    38,
    35,
    32,
    29,
    26,
    23,
    20,
    18,
    15,
    12,
    9,
    6,
    3,
)


def av2_default_cdf_parameters(n_taps):
    arr = np.arange(1, n_taps)
    cdf = (2**15) / n_taps * arr
    cdf = cdf.round().astype(int)
    return cdf


def av2_default_pmf(n_taps):
    cdf = av2_default_cdf_parameters(n_taps)
    cdf = np.append(cdf, CDF_INIT_TOP)
    pmf = np.diff(cdf)
    return pmf


def print_default_cdf_parameters(n_taps):
    print(get_default_avm_cdf_string(n_taps))


def get_avm_cdf_entry(n_taps, cdf):
    str_cdf = f"AVM_CDF{n_taps}("
    for i, p in enumerate(cdf):
        if i < n_taps - 2:
            str_cdf += str(p).rjust(5) + ", "
        else:
            str_cdf += str(p).rjust(5) + ")"
    return str_cdf


def get_default_avm_cdf_string(n_taps):
    cdf = av2_default_cdf_parameters(n_taps)
    return get_avm_cdf_entry(n_taps, cdf)


def get_avm_cdf_string(cdf):
    n_taps = len(cdf) + 1
    return get_avm_cdf_entry(n_taps, cdf)


if __name__ == "__main__":
    print_default_cdf_parameters(n_taps=2)
    print_default_cdf_parameters(n_taps=3)
    print_default_cdf_parameters(n_taps=4)
    print_default_cdf_parameters(n_taps=5)
    print_default_cdf_parameters(n_taps=6)
    print_default_cdf_parameters(n_taps=7)
    print_default_cdf_parameters(n_taps=8)
    print_default_cdf_parameters(n_taps=9)
    print_default_cdf_parameters(n_taps=10)
    print_default_cdf_parameters(n_taps=11)
    print_default_cdf_parameters(n_taps=12)
    print_default_cdf_parameters(n_taps=15)
    print_default_cdf_parameters(n_taps=16)
