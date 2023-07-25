#!/usr/bin/env python
## Copyright (c) 2021, Alliance for Open Media. All rights reserved
##
## This source code is subject to the terms of the BSD 3-Clause Clear License and the
## Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear License was
## not distributed with this source code in the LICENSE file, you can obtain it
## at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance for Open Media Patent
## License 1.0 was not distributed with this source code in the PATENTS file, you
## can obtain it at aomedia.org/license/patent-license/.
##
__author__ = "maggie.sun@intel.com, ryanlei@meta.com"

import numpy as np
import math
import scipy.interpolate
import logging
from Config import LoggerName
from operator import itemgetter

subloggername = "CalcBDRate"
loggername = LoggerName + '.' + '%s' % subloggername
logger = logging.getLogger(loggername)

def non_decreasing(L):
    return all(x<=y for x, y in zip(L, L[1:]))

def check_monotonicity(RDPoints):
    '''
    check if the input list of RD points are monotonic, assuming the input
    has been sorted in the quality value non-decreasing order. expect the bit
    rate should also be in the non-decreasing order
    '''
    br = [RDPoints[i][0] for i in range(len(RDPoints))]
    qty = [RDPoints[i][1] for i in range(len(RDPoints))]
    return non_decreasing(br) and non_decreasing(qty)

def filter_vmaf_non_monotonic(br_qty_pairs):
    '''
    To solve the problem with VMAF non-monotonicity in a flat (saturated)
    region of the curve, if VMAF non-monotonicity happens at VMAF value
    99.5 or above, the non-monotonic value and the values corresponding
    to bitrates higher than the non-monotonic value are excluded from the
    BD-rate calculation. The VMAF BD-rate number is still reported and
    used in the VMAF metric average.
    '''
    #first sort input RD pairs by bit rate
    out_br_qty_pairs = []
    br_qty_pairs.sort(key = itemgetter(0, 1))
    for i in range(len(br_qty_pairs)):
        if (i != 0 and
            br_qty_pairs[i][0] >= out_br_qty_pairs[-1][0] and
            br_qty_pairs[i][1] < out_br_qty_pairs[-1][1] and
            out_br_qty_pairs[-1][1] >= 99.5):
            break
        else:
            out_br_qty_pairs.append(br_qty_pairs[i])
    return out_br_qty_pairs

# BJONTEGAARD    Bjontegaard metric
# Calculation is adapted from Google implementation
# PCHIP method - Piecewise Cubic Hermite Interpolating Polynomial interpolation
def BD_RATE(qty_type, br1, qtyMtrc1, br2, qtyMtrc2):
    brqtypairs1 = []; brqtypairs2 = []
    for i in range(min(len(qtyMtrc1), len(br1))):
        if (br1[i] != '' and qtyMtrc1[i] != ''):
            brqtypairs1.append((br1[i], qtyMtrc1[i]))
    for i in range(min(len(qtyMtrc2), len(br2))):
        if (br2[i] != '' and qtyMtrc2[i] != ''):
            brqtypairs2.append((br2[i], qtyMtrc2[i]))

    if (qty_type == 'VMAF_Y' or qty_type == 'VMAF_Y-NEG'):
        brqtypairs1 = filter_vmaf_non_monotonic(brqtypairs1)
        brqtypairs2 = filter_vmaf_non_monotonic(brqtypairs2)

    # sort the pair based on quality metric values in increasing order
    # if quality metric values are the same, then sort the bit rate in increasing order
    brqtypairs1.sort(key = itemgetter(1, 0))
    brqtypairs2.sort(key = itemgetter(1, 0))

    rd1_monotonic = check_monotonicity(brqtypairs1)
    rd2_monotonic = check_monotonicity(brqtypairs2)
    if (rd1_monotonic == False or rd2_monotonic == False):
        return (-1, "Error: Non-monotonic")

    try:
        logbr1 = [math.log(x[0]) for x in brqtypairs1]
        qmetrics1 = [100.0 if x[1] == float('inf') else x[1] for x in brqtypairs1]
        logbr2 = [math.log(x[0]) for x in brqtypairs2]
        qmetrics2 = [100.0 if x[1] == float('inf') else x[1] for x in brqtypairs2]
    except ValueError:
        return (-1, "Error: Invalid Input Data")

    if not brqtypairs1 or not brqtypairs2:
        logger.info("Error: one of input lists is empty!")
        return (-1, "Error: one of input lists is empty!")

    # remove duplicated quality metric value, the RD point with higher bit rate is removed
    dup_idx = [i for i in range(1, len(qmetrics1)) if qmetrics1[i - 1] == qmetrics1[i]]
    for idx in sorted(dup_idx, reverse=True):
        del qmetrics1[idx]
        del logbr1[idx]
    dup_idx = [i for i in range(1, len(qmetrics2)) if qmetrics2[i - 1] == qmetrics2[i]]
    for idx in sorted(dup_idx, reverse=True):
        del qmetrics2[idx]
        del logbr2[idx]

    # find max and min of quality metrics
    min_int = max(min(qmetrics1), min(qmetrics2))
    max_int = min(max(qmetrics1), max(qmetrics2))
    if min_int >= max_int:
        logger.info("Error: no overlap from input 2 lists of quality metrics!")
        return (-1, "Error: no overlap from input 2 lists of quality metrics!")

    # generate samples between max and min of quality metrics
    lin = np.linspace(min_int, max_int, num=100, retstep=True)
    interval = lin[1]
    samples = lin[0]

    # interpolation
    v1 = scipy.interpolate.pchip_interpolate(qmetrics1, logbr1, samples)
    v2 = scipy.interpolate.pchip_interpolate(qmetrics2, logbr2, samples)

    # Calculate the integral using the trapezoid method on the samples.
    int1 = np.trapz(v1, dx=interval)
    int2 = np.trapz(v2, dx=interval)

    # find avg diff
    avg_exp_diff = (int2 - int1) / (max_int - min_int)
    avg_diff = (math.exp(avg_exp_diff) - 1) * 100

    return (0, round(avg_diff, 4))

'''
if __name__ == "__main__":
    br1 = [9563.04, 6923.28, 4894.8, 3304.32, 2108.4, 1299.84]
    #qty1 = [50.0198, 46.9709, 43.4791, 39.6659, 35.8063, 32.3055]
    #qty1 = [50.0198, 46.9709, 43.4791, 48.0000, 35.8063, 32.3055]
    qty1 = [99.8198, 99.7709, 98.4791, 99.5000, 98.8063, 98.3055]
    br2 = [9758.88, 7111.68, 5073.36, 3446.4, 2178, 1306.56]
    #qty2 = [49.6767, 46.7027, 43.2038, 39.297, 35.2944, 31.5938]
    qty2 = [99.8767, 99.7027, 99.2038, 99.200, 98.2944, 97.5938]
    qty_type = 'VMAF-Y'

    plot_rd_curve(br1, qty1, qty_type, 'r', '-', 'o')
    plot_rd_curve(br2, qty2, qty_type, 'b', '-', '*')
    plt.show()

    bdrate = BD_RATE('VMAF_Y', br1, qty1, br2, qty2)
    if bdrate != 'Non-monotonic Error':
        print("bdrate calculated is %3.3f%%" % bdrate)
    else:
        print("there is Non-monotonic Error in bdrate calculation")
'''
