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
from Config import QualityList, LoggerName
import Utils
from CalcQtyWithVmafTool import VMAF_CalQualityMetrics, VMAF_GatherQualityMetrics,\
     VMAFMetricsFullList

subloggername = "CalcQtyMetrics"
loggername = LoggerName + '.' + '%s' % subloggername
logger = logging.getLogger(loggername)

def CalculateQualityMetric(src_file, framenum, reconYUV, fmt, width, height,
                           bit_depth, QualityLogPath, VmafLogPath, LogCmdOnly=False):
    Utils.CmdLogger.write("::Quality Metrics\n")
    VMAF_CalQualityMetrics(src_file, reconYUV, QualityLogPath, VmafLogPath, LogCmdOnly)

def GatherQualityMetrics(reconYUV, logfilePath):
    qresult, per_frame_log = VMAF_GatherQualityMetrics(reconYUV, logfilePath)
    if (len(qresult) == 0):
        return [], per_frame_log
    results = []
    for metric in QualityList:
        if metric in VMAFMetricsFullList:
            indx = VMAFMetricsFullList.index(metric)
            results.append(qresult[indx])
        else:
            logger.error("invalid quality metrics in QualityList")
            results.append(0.0)

    return results, per_frame_log
