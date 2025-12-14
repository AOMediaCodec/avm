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

from parakit.entropy.codec_default_cdf import (
    AV2_PROB_COST,
    AV2_PROB_COST_SHIFT,
    CDF_INIT_TOP,
    CDF_PROB_BITS,
    CDF_PROB_TOP,
)


def clog2(x):
    """Ceiling of log2"""
    if x <= 0:
        raise ValueError("clog2 input error")
    return (x - 1).bit_length()


def flog2(x):
    """Floor of log2"""
    if x <= 0:
        raise ValueError("flog2 input error")
    return x.bit_length() - 1


def update_cdfinv_av2(cdf, val, counter, nsymb, roffset=0):
    """Python implementation of the following C code from AVM codec:
    --------------------------------------------------------------
    static INLINE void update_cdf(avm_cdf_prob *cdf, int8_t val, int nsymbs) {
    int rate;
    int i, tmp;
    static const int nsymbs2speed[17] = { 0, 0, 1, 1, 2, 2, 2, 2, 2,
                                            2, 2, 2, 2, 2, 2, 2, 2 };
    assert(nsymbs < 17);
    rate = 3 + (cdf[nsymbs] > 15) + (cdf[nsymbs] > 31) +
            nsymbs2speed[nsymbs];  // + get_msb(nsymbs);
    tmp = AVM_ICDF(0);
    // Single loop (faster)
    for (i = 0; i < nsymbs - 1; ++i) {
        tmp = (i == val) ? 0 : tmp;
        if (tmp < cdf[i]) {
        cdf[i] -= ((cdf[i] - tmp) >> rate);
        } else {
        cdf[i] += ((tmp - cdf[i]) >> rate);
        }
    }
    cdf[nsymbs] += (cdf[nsymbs] < 32);
    }
    --------------------------------------------------------------
    """
    nsymbs2speed = [0, 0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2]
    rate = 3 + nsymbs2speed[nsymb]
    if counter > 15:
        rate = rate + 1
    if counter > 31:
        rate = rate + 1
    rate = rate + roffset
    tmp = CDF_INIT_TOP
    for i in range(nsymb - 1):
        if i == val:
            tmp = 0
        if tmp < cdf[i]:
            cdf[i] -= (cdf[i] - tmp) // (2**rate)
        else:
            cdf[i] += (tmp - cdf[i]) // (2**rate)
    return cdf


def get_prob(num, den):
    """Python implementation of the following C code from AVM codec:
    --------------------------------------------------------------
    static INLINE uint8_t get_prob(unsigned int num, unsigned int den) {
      assert(den != 0);
      {
        const int p = (int)(((uint64_t)num * 256 + (den >> 1)) / den);
        // (p > 255) ? 255 : (p < 1) ? 1 : p;
        const int clipped_prob = p | ((255 - p) >> 23) | (p == 0);
        return (uint8_t)clipped_prob;
      }
    }
    --------------------------------------------------------------
    """
    p = int(((num * 256) + (den // 2)) / den)
    if p > 255:
        p = 255
    if p < 1:
        p = 1
    return p


def cost_literal_av2(n):
    """Python implementation of the following C code from AVM codec:
    --------------------------------------------------------------
    define av2_cost_literal(n) ((n) * (1 << AV2_PROB_COST_SHIFT))
    --------------------------------------------------------------
    """
    return n * (2**AV2_PROB_COST_SHIFT)


def cost_symbol_av2(p15):
    """Python implementation of the following C code from AVM codec:
    --------------------------------------------------------------
    static INLINE int av2_cost_symbol(avm_cdf_prob p15) {
      // p15 can be out of range [1, CDF_PROB_TOP - 1]. Clamping it, so that the
      // following cost calculation works correctly. Otherwise, if p15 =
      // CDF_PROB_TOP, shift would be -1, and "p15 << shift" would be wrong.
      p15 = (avm_cdf_prob)clamp(p15, 1, CDF_PROB_TOP - 1);
      assert(0 < p15 && p15 < CDF_PROB_TOP);
      const int shift = CDF_PROB_BITS - 1 - get_msb(p15);
      const int prob = get_prob(p15 << shift, CDF_PROB_TOP);
      assert(prob >= 128);
      return av2_prob_cost[prob - 128] + av2_cost_literal(shift);
    }
    --------------------------------------------------------------
    """
    if p15 > (CDF_PROB_TOP - 1):
        p15 = CDF_PROB_TOP - 1
    if p15 < 1:
        p15 = 1
    msb = flog2(int(p15))  # or int(math.floor(math.log2(p15))) using math
    shift = CDF_PROB_BITS - 1 - msb
    prob_scaled = p15 * (2**shift)
    prob = get_prob(prob_scaled, CDF_PROB_TOP)
    if prob < 128:
        raise ValueError(
            f"Normalized probability value is less than 128 (prob={prob},msb={msb},prob_scaled={prob_scaled})"
        )
    return AV2_PROB_COST[prob - 128] + cost_literal_av2(shift)


def pmf2cdfinv_av2(pmf):
    """converts pmf to cdf-inverse"""
    cdf = CDF_INIT_TOP - np.cumsum(pmf)
    return cdf


def cdfinv2pmf_av2(cdf_inv):
    """converts cdf-inverse to pmf"""
    cdf = np.insert(cdf_inv, 0, CDF_INIT_TOP)
    pmf = np.diff(CDF_INIT_TOP - cdf)
    return pmf


def pmf2cdf_av2(pmf):
    """converts pmf to cdf"""
    return CDF_INIT_TOP - pmf2cdfinv_av2(pmf)


def cdf2pmf_av2(cdf):
    """converts cdf to pmf"""
    cdf_ext = np.insert(cdf, 0, 0)
    pmf = np.diff(cdf_ext)
    return pmf


def count2cdf_av2(value_count):
    """Python implementation of the following C code from AVM codec:
    --------------------------------------------------------------
    static void counts_to_cdf(const avm_count_type *counts, avm_cdf_prob *cdf, int modes) {
        int64_t csum[CDF_MAX_SIZE];
        assert(modes <= CDF_MAX_SIZE);

        csum[0] = counts[0] + 1;
        for (int i = 1; i < modes; ++i) csum[i] = counts[i] + 1 + csum[i - 1];

        for (int i = 0; i < modes; ++i) fprintf(logfile, "%d ", counts[i]);
        fprintf(logfile, "\n");

        int64_t sum = csum[modes - 1];
        const int64_t round_shift = sum >> 1;
        for (int i = 0; i < modes; ++i) {
            cdf[i] = (csum[i] * CDF_PROB_TOP + round_shift) / sum;
            cdf[i] = AVMMIN(cdf[i], CDF_PROB_TOP - (modes - 1 + i) * 4);
            cdf[i] = (i == 0) ? AVMMAX(cdf[i], 4) : AVMMAX(cdf[i], cdf[i - 1] + 4);
        }
    }
    --------------------------------------------------------------
    """
    value_count = value_count + 1
    cdf_count = np.cumsum(value_count)
    total_count = value_count.sum()
    round_shift = total_count // 2
    nsymb = len(cdf_count)
    cdf = np.zeros(nsymb).astype(int)
    for i in range(nsymb):
        cdf[i] = int((cdf_count[i] * CDF_INIT_TOP + round_shift) / total_count)
        cdf[i] = min(cdf[i], CDF_INIT_TOP - (nsymb - 1 + i) * 4)
        if i == 0:
            cdf[i] = max(cdf[i], 4)
        else:
            cdf[i] = max(cdf[i], cdf[i - 1] + 4)
    return cdf
