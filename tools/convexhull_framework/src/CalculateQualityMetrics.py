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

import logging
from Config import QualityList, PerceptualQualityList, EnablePerceptualMetrics, LoggerName
import Utils
from CalcQtyWithVmafTool import VMAF_CalQualityMetrics, VMAF_GatherQualityMetrics,\
     VMAFMetricsFullList

subloggername = "CalcQtyMetrics"
loggername = LoggerName + '.' + '%s' % subloggername
logger = logging.getLogger(loggername)

def CalculateQualityMetric(src_file, framenum, reconYUV, fmt, width, height,
                           bit_depth, QualityLogPath, VmafLogPath, LogCmdOnly=False,
                           clip=None, PerceptualLogPath=None):
    Utils.CmdLogger.write("::Quality Metrics\n")
    VMAF_CalQualityMetrics(src_file, reconYUV, QualityLogPath, VmafLogPath, LogCmdOnly)

    # Optional perceptual metrics. Imported lazily so that a run with the
    # feature disabled never pulls in the optional dependency chain.
    if EnablePerceptualMetrics and clip is not None:
        from CalcPerceptualMetrics import Perceptual_CalQualityMetrics
        Perceptual_CalQualityMetrics(src_file, reconYUV, clip, QualityLogPath,
                                     PerceptualLogPath or VmafLogPath, LogCmdOnly)

def GatherQualityMetrics(reconYUV, logfilePath):
    qresult, per_frame_log = VMAF_GatherQualityMetrics(reconYUV, logfilePath)
    if (len(qresult) == 0):
        return [], per_frame_log, 0
    results = []
    for metric in QualityList:
        if metric in VMAFMetricsFullList:
            indx = VMAFMetricsFullList.index(metric)
            results.append(qresult[indx])
        else:
            logger.error("invalid quality metrics in QualityList")
            results.append(0.0)

    return results, per_frame_log, len(per_frame_log)

def GatherPerceptualMetrics(reconYUV, logfilePath, frame_num):
    """Aggregates and per-frame rows for the perceptual metrics.

    Returns ([], []) when the feature is off, so callers can append
    unconditionally. Aggregates are in PerceptualQualityList order; a metric
    that was skipped (e.g. LPIPS on HDR content) comes back as NaN, which the
    CSV writers render as an empty cell rather than a misleading 0.0.
    """
    if not EnablePerceptualMetrics or not PerceptualQualityList:
        return [], []

    from CalcPerceptualMetrics import (Perceptual_GatherQualityMetrics,
                                       PerceptualMetricsFullList)

    # A blank row has one empty field per ENABLED metric, matching the header.
    blank_row = ",".join([''] * len(PerceptualQualityList))

    presult, per_frame_values = Perceptual_GatherQualityMetrics(reconYUV, logfilePath)
    if len(presult) == 0:
        logger.error("perceptual metrics log missing or unreadable for %s" % reconYUV)
        return [float('nan')] * len(PerceptualQualityList), [blank_row] * frame_num

    # Select the enabled subset, in PerceptualQualityList order, from the
    # full-list-ordered values the parser returns.
    indices = [PerceptualMetricsFullList.index(m) for m in PerceptualQualityList]
    results = [presult[i] for i in indices]
    per_frame_log = [",".join([row[i] for i in indices]) for row in per_frame_values]

    # Keep the per-frame table the same length as the VMAF one; the two are
    # written side by side and a mismatch would misalign every row.
    if frame_num and len(per_frame_log) < frame_num:
        per_frame_log += [blank_row] * (frame_num - len(per_frame_log))

    return results, per_frame_log[:frame_num] if frame_num else per_frame_log
