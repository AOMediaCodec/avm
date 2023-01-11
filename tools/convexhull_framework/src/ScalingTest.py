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
from Config import DnScaleRatio, FrameNum, QualityList, LoggerName,\
     Path_ScalingResults, ScalQty_WtCols, ScalQty_startRow, LineColors, \
     ScalSumQty_WtCols, AS_DOWNSCALE_ON_THE_FLY
from Utils import Cleanfolder, GetShortContentName, CreateChart_Scatter, \
     AddSeriesToChart_Scatter, UpdateChart, InsertChartsToSheet, \
     CalcRowsClassAndContentDict, Clip, CreateChart_Bar, AddSeriesToChart_Bar, \
     DeleteFile
import Utils

subloggername = "ScalingTest"
loggername = LoggerName + '.' + '%s' % subloggername
logger = logging.getLogger(loggername)

StatsMetrics = ['Max', 'Std+', 'Average', 'Std-', 'Min']

def GetScalingResultExcelFile(method, rationum, scalAlgoNum):
    filename = "ScalingResults_RatioNum_%d_%s_AlgoNum_%d.xlsx"\
               % (rationum, method, scalAlgoNum)
    file = os.path.join(Path_ScalingResults, filename)
    return file

def GetScalingResultExcelFile_PerContent(content, method):
    filename = GetShortContentName(content, False)
    filename = "ScalingResults_%s_%s.xlsx" % (method, filename)
    file = os.path.join(Path_ScalingResults, filename)
    return file

def Run_Scaling_Test(clip, dnScalAlgo, upScalAlgo, path_dnscl, path_upscl,
                     path_log, path_cfg, savememory, keepupscaledyuv, ScaleMethod,
                     LogCmdOnly=False):
    logger.info("start running scaling tests with content %s"
                % os.path.basename(clip.file_name))
    DnScaledRes = [(int(clip.width / ratio), int(clip.height / ratio))
                   for ratio in DnScaleRatio]
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
            dnscalyuv = DownScaling(ScaleMethod, clip, FrameNum['AS'], DnScaledW, DnScaledH,
                                    path_dnscl, path_cfg, dnScalAlgo, LogCmdOnly)
        dnscaled_clip = Clip(GetShortContentName(dnscalyuv, False)+'.y4m',
                             dnscalyuv, "", DnScaledW, DnScaledH,
                             clip.fmt, clip.fps_num, clip.fps_denom,
                             clip.bit_depth)
        upscaleyuv = UpScaling(ScaleMethod, dnscaled_clip, FrameNum['AS'], clip.width, clip.height,
                               path_upscl, path_cfg, upScalAlgo, LogCmdOnly)
        CalculateQualityMetric(clip.file_path, FrameNum['AS'], upscaleyuv, clip.fmt,
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

def GeneratePerClipExcelFile(ScaleMethod, dnScalAlgos, upScalAlgos, clip, path_log):
    logger.info("start generate excel file for content :%s" % clip.file_name)
    excFile = GetScalingResultExcelFile_PerContent(clip.file_name, ScaleMethod)
    wb = xlsxwriter.Workbook(excFile)
    shtname = GetShortContentName(clip.file_name)
    sht = wb.add_worksheet(shtname)

    scaleRatios = DnScaleRatio
    if (1.0 in scaleRatios):
        scaleRatios.remove(1.0)

    sht.write(1, 0, 'Content Name')
    sht.write(2, 0, clip.file_name)
    sht.write(1, 1, 'Scaling Ratio')
    sht.write_column(2, 1, scaleRatios)
    pre_title = ['Width', 'Height', 'DnScaledWidth', 'DnScaledHeight']
    sht.write_row(1, 2, pre_title)
    for dn_algo, up_algo, col in zip(dnScalAlgos, upScalAlgos, ScalQty_WtCols):
        algos = dn_algo + '--' + up_algo
        sht.write(0, col, algos)
        sht.write_row(1, col, QualityList)

    rows = [ScalQty_startRow + i for i in range(len(scaleRatios))]
    continfos = []
    for ratio, row in zip(scaleRatios, rows):
        dw = int(clip.width / ratio)
        dh = int(clip.height / ratio)
        info = [clip.width, clip.height, dw, dh]
        sht.write_row(row, 2, info)
        continfos.append(info)

    charts = []; y_mins = {}; y_maxs = {}
    for qty, x in zip(QualityList, range(len(QualityList))):
        chart_title = 'Scaling Quality - %s' % qty
        xaxis_name = 'scaling ratio'
        chart = CreateChart_Scatter(wb, chart_title, xaxis_name, qty)
        charts.append(chart)
        y_mins[x] = []; y_maxs[x] = []

    for dn_algo, up_algo, col, i in zip(dnScalAlgos, upScalAlgos, ScalQty_WtCols,
                                        range(len(dnScalAlgos))):
        qualities = []
        seriname = dn_algo + '--' + up_algo
        for ratio, row, idx in zip(scaleRatios, rows, range(len(scaleRatios))):
            w = continfos[idx][0]
            h = continfos[idx][1]
            dw = continfos[idx][2]
            dh = continfos[idx][3]
            dnScalOut = GetDownScaledOutFile(clip, dw, dh, path_log, ScaleMethod, dn_algo)
            ds_clip = Clip(GetShortContentName(dnScalOut, False) + '.y4m',
                 dnScalOut, "", dw, dh, clip.fmt, clip.fps_num, clip.fps_denom,
                 clip.bit_depth)
            upScalOut = GetUpScaledOutFile(ds_clip, clip.width, clip.height,
                                           ScaleMethod, up_algo, path_log)
            qtys, perframe_vmaf_log = GatherQualityMetrics(upScalOut, path_log)
            sht.write_row(row, col, qtys)
            qualities.append(qtys)
        for x in range(len(QualityList)):
            AddSeriesToChart_Scatter(shtname, rows, col + x, 1, charts[x],
                                     seriname, LineColors[i])

        # get min and max of y-axis for a certain dn up scaling algo
        for qty, x in zip(QualityList, range(len(QualityList))):
            qs = [row[x] for row in qualities]
            y_mins[x].append(min(qs))
            y_maxs[x].append(max(qs))

    for qty, x in zip(QualityList, range(len(QualityList))):
        ymin = min(y_mins[x])
        ymax = max(y_maxs[x])
        margin = 0.1  # add 10% on min and max value for y_axis range
        num_precsn = 5 if 'MS-SSIM' in qty else 3
        UpdateChart(charts[x], ymin, ymax, margin, qty, num_precsn)

    startrow = rows[-1] + 2; startcol = 1
    InsertChartsToSheet(sht, startrow, startcol, charts)

    wb.close()
    logger.info("finish export scaling quality results to excel file.")

def GenerateSummarySheet(wb, ScaleMethod, dnScalAlgos, upScalAlgos, ratio, path_log):
    logger.info("start generate summary sheet for ratio %2.2f" % ratio)

    shts = []
    shtname = "Ratio=%1.2f" % ratio
    sht = wb.add_worksheet(shtname)
    shts.append(sht)
    sht.write(1, 0, 'Content Class')
    sht.write(1, 1, 'Content NO.')
    sht.write(1, 2, 'Content Name')
    pre_title = ['Width', 'Height', 'DnScaledWidth', 'DnScaledHeight']
    sht.write_row(1, 3, pre_title)

    for dn_algo, up_algo, col in zip(dnScalAlgos, upScalAlgos, ScalSumQty_WtCols):
        algos = dn_algo + '--' + up_algo
        sht.write(0, col, algos)
        sht.write_row(1, col, QualityList)

    content_infos = {}; totalnum_contents = 0
    for (clss, clip_list), row_clss in zip(ClipDict.items(), Rows_Class):
        sht.write(row_clss, 0, clss)
        totalnum_contents = totalnum_contents + len(clip_list)
        for clip, row_cont in zip(clip_list, range(len(clip_list))):
            dw = int(clip.width / ratio)
            dh = int(clip.height / ratio)
            shortcntname = GetShortContentName(clip.file_name, False)
            sht.write(row_clss + row_cont, 2, shortcntname)
            infos = [clip.width, clip.height, dw, dh]
            sht.write_row(row_clss + row_cont, 3, infos)
            content_infos[shortcntname] = infos

    charts = []; y_mins = {}; y_maxs = {}
    for qty, x in zip(QualityList, range(len(QualityList))):
        chart_title = '%s of %s' % (qty, shtname)
        chart = CreateChart_Bar(wb, chart_title, 'Contents', qty)
        charts.append(chart)
        y_mins[x] = []; y_maxs[x] = []

    rows_all = [ScalQty_startRow + i for i in range(totalnum_contents)]
    sht.write_column(ScalQty_startRow, 1, range(totalnum_contents))
    for dn_algo, up_algo, col, i in zip(dnScalAlgos, upScalAlgos,
                                        ScalSumQty_WtCols,
                                        range(len(dnScalAlgos))):
        qualities = []
        seriname = dn_algo + '--' + up_algo
        for (clss, clip_list), row_clss in zip(ClipDict.items(), Rows_Class):
            for clip, row_cont in zip(clip_list, range(len(clip_list))):
                key = GetShortContentName(clip.file_name, False)
                w = content_infos[key][0]
                h = content_infos[key][1]
                dw = content_infos[key][2]
                dh = content_infos[key][3]
                dnScalOut = GetDownScaledOutFile(clip, dw, dh, path_log, ScaleMethod, dn_algo)
                ds_clip = Clip(GetShortContentName(dnScalOut, False) + '.y4m',
                               dnScalOut, clss, dw, dh, clip.fmt, clip.fps_num,
                               clip.fps_denom, clip.bit_depth)
                upScalOut = GetUpScaledOutFile(ds_clip, w, h, ScaleMethod, up_algo, path_log)
                qtys, perframe_vmaf_log = GatherQualityMetrics(upScalOut, path_log)
                sht.write_row(row_clss + row_cont, col, qtys)
                qualities.append(qtys)

        for x in range(len(QualityList)):
            AddSeriesToChart_Bar(shtname, rows_all, col + x, 1, charts[x], seriname)

        # get min and max of y-axis for a certain dn up scaling algo
        for qty, x in zip(QualityList, range(len(QualityList))):
            qs = [row[x] for row in qualities]
            y_mins[x].append(min(qs))
            y_maxs[x].append(max(qs))

    for qty, x in zip(QualityList, range(len(QualityList))):
        ymin = min(y_mins[x])
        ymax = max(y_maxs[x])
        margin = 0.1  # add 10% on min and max value for y_axis range
        num_precsn = 5 if 'MS-SSIM' in qty else 3
        UpdateChart(charts[x], ymin, ymax, margin, qty, num_precsn)

    startrow = rows_all[-1] + 2; startcol = 1
    InsertChartsToSheet(sht, startrow, startcol, charts)
    logger.info("finish average sheet for ratio:%2.2f." % ratio)

    return sht

def GenerateAverageSheet(wb, sumsht, dnScalAlgos, upScalAlgos, ratio):
    logger.info("start generate average sheet for ratio %2.2f" % ratio)

    rdsht = sumsht
    rdshtname = rdsht.get_name()
    shtname = "AverageForRatio=%1.2f" % ratio
    sht = wb.add_worksheet(shtname)
    sht.write(1, 0, 'QualityMetrics')
    sht.write(1, 1, 'Content Class')
    sht.write(1, 2, 'Content Number')

    interval = 1
    step = len(StatsMetrics) + interval
    startcol = 3
    cols_avg = [startcol + step * i for i in range(len(dnScalAlgos))]
    for col, dn_algo, up_algo in zip(cols_avg, dnScalAlgos, upScalAlgos):
        algos = dn_algo + '--' + up_algo
        sht.write(0, col, algos)
        sht.write_row(1, col, StatsMetrics)

    step = len(ClipDict) + 1  # 1 extra row for total of each class
    startrow = 2
    rows_qtymtr = [startrow + step * i for i in range(len(QualityList))]
    for qty, row_qm, y in zip(QualityList, rows_qtymtr, range(len(QualityList))):
        sht.write(row_qm, 0, qty)

        #charts = []
        #titlename = 'Quality Statistics %s' % qty
        #chart = CreateChart_Line(wb, titlename, qty)
        #charts.append(chart)

        totalnum_contents = 0
        for (cls, clip_list), idx, rdrow_cls in zip(ClipDict.items(),
                                                   range(len(ClipDict)),
                                                   Rows_Class):
            sht.write(row_qm + idx, 1, cls)
            num_content = len(clip_list)
            totalnum_contents = totalnum_contents + num_content
            sht.write(row_qm + idx, 2, num_content)
            for rdcol, wtcol in zip(ScalSumQty_WtCols, cols_avg):
                startcell = xlrd.cellname(rdrow_cls, rdcol + y)
                endcell = xlrd.cellname(rdrow_cls + num_content - 1, rdcol + y)
                formula = '=MAX(\'%s\'!%s:%s)' % (rdshtname, startcell, endcell)
                sht.write(row_qm + idx, wtcol, formula)

                formula = '=SUM(\'%s\'!%s:%s)/%d' % (rdshtname, startcell,
                                                     endcell, num_content)
                sht.write(row_qm + idx, wtcol + 2, formula)

                formula = '=MIN(\'%s\'!%s:%s)' % (rdshtname, startcell, endcell)
                sht.write(row_qm + idx, wtcol + 4, formula)

                avgcell = xlrd.cellname(row_qm + idx, wtcol + 2)
                formula = '= %s + _xlfn.STDEV.P(\'%s\'!%s:%s)'\
                          % (avgcell, rdshtname, startcell, endcell)
                sht.write(row_qm + idx, wtcol + 1, formula)

                formula = '= %s - _xlfn.STDEV.P(\'%s\'!%s:%s)'\
                          % (avgcell, rdshtname, startcell, endcell)
                sht.write(row_qm + idx, wtcol + 3, formula)

        #write total contents statistics
        wtrow = row_qm + len(ClipDict)
        sht.write(wtrow, 1, 'Total')
        sht.write(wtrow, 2, totalnum_contents)
        for rdcol, wtcol in zip(ScalSumQty_WtCols, cols_avg):
            startcell = xlrd.cellname(ScalQty_startRow, rdcol + y)
            endcell = xlrd.cellname(ScalQty_startRow + totalnum_contents - 1,
                                    rdcol + y)
            formula = '=MAX(\'%s\'!%s:%s)' % (rdshtname, startcell, endcell)
            sht.write(wtrow, wtcol, formula)

            formula = '=SUM(\'%s\'!%s:%s)/%d'\
                      % (rdshtname, startcell, endcell, totalnum_contents)
            sht.write(wtrow, wtcol + 2, formula)

            formula = '=MIN(\'%s\'!%s:%s)' % (rdshtname, startcell, endcell)
            sht.write(wtrow, wtcol + 4, formula)

            avgcell = xlrd.cellname(wtrow, wtcol + 2)
            formula = '= %s + _xlfn.STDEV.P(\'%s\'!%s:%s)'\
                      % (avgcell, rdshtname, startcell, endcell)
            sht.write(wtrow, wtcol + 1, formula)

            formula = '= %s - _xlfn.STDEV.P(\'%s\'!%s:%s)'\
                      % (avgcell, rdshtname, startcell, endcell)
            sht.write(wtrow, wtcol + 3, formula)

    logger.info("finish average sheet for ratio:%2.2f." % ratio)

def SaveScalingResultsToExcel(ScaleMethod, dnScalAlgos, upScalAlgos, clip_list, path_log):
    logger.info("start saving scaling quality results to excel files.......")
    if not os.path.exists(Path_ScalingResults):
        os.makedirs(Path_ScalingResults)

    logger.info("start generating per content scaling quality excel file.......")
    for clip in clip_list:
        GeneratePerClipExcelFile(ScaleMethod, dnScalAlgos, upScalAlgos, clip, path_log)

    scaleRatios = DnScaleRatio
    if (1.0 in scaleRatios):
        scaleRatios.remove(1.0)
    logger.info("start generating scaling quality summary excel file.......")
    sumexcFile = GetScalingResultExcelFile(ScaleMethod, len(scaleRatios), len(dnScalAlgos))
    wb = xlsxwriter.Workbook(sumexcFile)
    # to generate rows number of starting of each class: Rows_Class
    global ClipDict, Rows_Class
    ClipDict, Rows_Class = CalcRowsClassAndContentDict(ScalQty_startRow,  clip_list)
    sumShts = []
    for ratio in scaleRatios:
        sht = GenerateSummarySheet(wb, ScaleMethod, dnScalAlgos, upScalAlgos, ratio, path_log)
        sumShts.append(sht)
    for ratio, sumsht in zip(scaleRatios, sumShts):
        GenerateAverageSheet(wb, sumsht, dnScalAlgos, upScalAlgos, ratio)

    wb.close()
    logger.info("finish saving scaling quality results to excel files.......")
