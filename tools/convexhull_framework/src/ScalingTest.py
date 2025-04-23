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

import os
import xlsxwriter
import xlrd
import logging
from CalculateQualityMetrics import CalculateQualityMetric, GatherQualityMetrics
from VideoScaler import GetDownScaledOutFile, DownScaling, UpScaling,\
     GetUpScaledOutFile
from Config import DnScaleRatio, QualityList, LoggerName,\
     Path_ScalingResults, AS_DOWNSCALE_ON_THE_FLY
from Utils import Cleanfolder, GetShortContentName, Clip, DeleteFile, get_total_frame
import Utils

subloggername = "ScalingTest"
loggername = LoggerName + '.' + '%s' % subloggername
logger = logging.getLogger(loggername)

def Run_Scaling_Test(clip, dnScalAlgo, upScalAlgo, path_dnscl, path_upscl,
                     path_log, path_cfg, savememory, keepupscaledyuv, ScaleMethod,
                     LogCmdOnly=False):
    logger.info("start running scaling tests with content %s"
                % os.path.basename(clip.file_name))
    DnScaledRes = [(int(clip.width / ratio), int(clip.height / ratio))
                   for ratio in DnScaleRatio]
    total_frame = get_total_frames('AS', clip)
    for i in range(len(DnScaledRes)):
        DnScaledW = DnScaledRes[i][0]
        DnScaledH = DnScaledRes[i][1]
        if (DnScaledW == clip.width and DnScaledH == clip.height):
            continue
        logger.info("start downscaling content to %dx%d" % (DnScaledW, DnScaledH))
        JobName = '%s_Downscaling_%dx%d' % \
                  (GetShortContentName(clip.file_name, False), DnScaledW, DnScaledH)
        if LogCmdOnly:
            Utils.CmdLogger.write("============== %s Job Start =================\n"%JobName)
        # downscaling
        dnscalyuv = GetDownScaledOutFile(clip, DnScaledW, DnScaledH,
                                         path_dnscl, ScaleMethod, dnScalAlgo)
        if not os.path.isfile(dnscalyuv):
            dnscalyuv = DownScaling(ScaleMethod, clip, total_frame, DnScaledW, DnScaledH,
                                    path_dnscl, path_cfg, dnScalAlgo, LogCmdOnly)
        dnscaled_clip = Clip(GetShortContentName(dnscalyuv, False)+'.y4m',
                             dnscalyuv, "", DnScaledW, DnScaledH,
                             clip.fmt, clip.fps_num, clip.fps_denom,
                             clip.bit_depth)
        upscaleyuv = UpScaling(ScaleMethod, dnscaled_clip, total_frame, clip.width, clip.height,
                               path_upscl, path_cfg, upScalAlgo, LogCmdOnly)
        CalculateQualityMetric(clip.file_path, total_frame, upscaleyuv, clip.fmt,
                               clip.width, clip.height, clip.bit_depth,
                               path_log, LogCmdOnly)
        if savememory:
            if dnscalyuv != clip.file_path:
                DeleteFile(dnscalyuv, LogCmdOnly)
            if not keepupscaledyuv:
                DeleteFile(upscaleyuv, LogCmdOnly)

        if LogCmdOnly:
            Utils.CmdLogger.write("============== %s Job End =================\n" % JobName)
    logger.info("finish running scaling test.")
