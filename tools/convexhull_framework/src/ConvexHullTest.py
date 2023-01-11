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
import sys
import xlsxwriter
import argparse

from EncDecUpscale import Run_EncDec_Upscale, GetBsReconFileName
from VideoScaler import GetDownScaledOutFile, DownScaling
from CalculateQualityMetrics import CalculateQualityMetric, GatherQualityMetrics
from Utils import GetShortContentName, CreateChart_Scatter,\
     AddSeriesToChart_Scatter, InsertChartsToSheet, CreateNewSubfolder,\
     SetupLogging, UpdateChart, AddSeriesToChart_Scatter_Rows,\
     Cleanfolder, CreateClipList, Clip, GatherPerfInfo, GetEncLogFile, \
     GetRDResultCsvFile, GatherPerframeStat, GatherInstrCycleInfo, \
     Interpolate_Bilinear, Interpolate_PCHIP, convex_hull, DeleteFile, md5
from PostAnalysis_Summary import GenerateSumRDExcelFile,\
     GenerateSumCvxHullExcelFile
from ScalingTest import Run_Scaling_Test, SaveScalingResultsToExcel
import Utils
from Config import LogLevels, FrameNum, QPs, CvxH_WtCols,\
     CvxH_WtRows, QualityList, LineColors, SummaryOutPath, WorkPath, \
     Path_RDResults, DnScalingAlgos, UpScalingAlgos, ConvexHullColor, \
     EncodeMethods, CodecNames, LoggerName, DnScaleRatio, TargetQtyMetrics, \
     CvxHDataRows, CvxHDataStartRow, CvxHDataStartCol, CvxHDataNum, \
     Int_ConvexHullColor, EnablePreInterpolation, AS_DOWNSCALE_ON_THE_FLY,\
     UsePerfUtil, ScaleMethods, EnableTimingInfo, InterpolatePieces, EnableMD5, \
     UsePCHIPInterpolation, HEVC_QPs

###############################################################################
##### Helper Functions ########################################################
def CleanIntermediateFiles():
    folders = [Path_DecodedYuv, Path_CfgFiles]
    if not KeepUpscaledOutput:
        folders += [Path_DecUpScaleYuv, Path_UpScaleYuv]

    for folder in folders:
        Cleanfolder(folder)

def GetRDResultExcelFile(clip):
    contentBaseName = GetShortContentName(clip.file_name, False)
    filename = "RDResults_%s_%s_%s_%s.xlsx" % (contentBaseName, EncodeMethod,
                                               CodecName, EncodePreset)
    file = os.path.join(Path_RDResults, filename)
    return file

def setupWorkFolderStructure():
    global Path_Bitstreams, Path_DecodedYuv, Path_UpScaleYuv, Path_DnScaleYuv, \
    Path_QualityLog, Path_TestLog, Path_CfgFiles, Path_DecUpScaleYuv, Path_PerfLog, \
    Path_EncLog, Path_CmdLog
    Path_Bitstreams = CreateNewSubfolder(WorkPath, "bitstreams")
    Path_DecodedYuv = CreateNewSubfolder(WorkPath, "decodedYUVs")
    Path_UpScaleYuv = CreateNewSubfolder(WorkPath, "upscaledYUVs")
    Path_DecUpScaleYuv = CreateNewSubfolder(WorkPath, "decUpscaledYUVs")
    Path_DnScaleYuv = CreateNewSubfolder(WorkPath, "downscaledYUVs")
    Path_QualityLog = CreateNewSubfolder(WorkPath, "qualityLogs")
    Path_TestLog = CreateNewSubfolder(WorkPath, "testLogs")
    Path_CfgFiles = CreateNewSubfolder(WorkPath, "configFiles")
    Path_PerfLog = CreateNewSubfolder(WorkPath, "perfLogs")
    Path_EncLog = CreateNewSubfolder(WorkPath, "encLogs")
    Path_CmdLog = CreateNewSubfolder(WorkPath, "cmdLogs")

def LookUpQPAndResInCvxHull(qtyvals, qtyhull, qtycvhQPs, qtycvhRes):
    cvhqtys = [h[1] for h in qtyhull]
    qtyQPs = []; qtyRes = []
    for val in qtyvals:
        closest_idx = min(range(len(cvhqtys)), key=lambda i: abs(cvhqtys[i] - val))
        if (closest_idx == 0 and val > cvhqtys[0]) or \
           (closest_idx == (len(qtyvals) - 1) and val < cvhqtys[-1]):
            Utils.Logger.info("the give value of quality metric is out of range"\
                              "of convex hull test quality values.")

        qtyQPs.append(qtycvhQPs[closest_idx])
        qtyRes.append(qtycvhRes[closest_idx])

    return qtyQPs, qtyRes


def AddConvexHullCurveToCharts(sht, charts, rdPoints, dnScaledRes, tgtqmetrics,
                               EnablePreInterpolation = False, int_rdPoints = None):
    if EnablePreInterpolation:
        assert int_rdPoints is not None

    shtname = sht.get_name()
    sht.write(CvxHDataStartRow, CvxHDataStartCol, "ConvexHull Data")

    hull = {}; cvh_QPs = {}; cvh_Res_txt = {}; int_hull = {}
    max_len = 0

    for qty, idx, row in zip(QualityList, range(len(QualityList)), CvxHDataRows):
        lower, upper = convex_hull(rdPoints[idx])
        hull[qty] = upper
        max_len = max(max_len, len(upper))
        sht.write(row, CvxHDataStartCol, qty)
        sht.write(row + 1, CvxHDataStartCol, "Bitrate(kbps)")
        sht.write(row + 2, CvxHDataStartCol, "QP")
        sht.write(row + 3, CvxHDataStartCol, 'Resolution')
        if EnablePreInterpolation:
            lower, upper = convex_hull(int_rdPoints[idx])
            int_hull[qty] = upper
            sht.write(row + 4, CvxHDataStartCol, "Int_" + qty)
            sht.write(row + 5, CvxHDataStartCol, "Int_Bitrate(kbps)")

        brts = [h[0] for h in hull[qty]]
        qtys = [h[1] for h in hull[qty]]
        sht.write_row(row, CvxHDataStartCol + 1, qtys)
        sht.write_row(row + 1, CvxHDataStartCol + 1, brts)

        cvh_idxs = [rdPoints[idx].index((brt, qty)) for brt, qty in zip(brts, qtys)]
        cvh_QPs[qty] = [QPs['AS'][i % len(QPs['AS'])] for i in cvh_idxs]
        cvh_Res = [dnScaledRes[i // len(QPs['AS'])] for i in cvh_idxs]
        cvh_Res_txt[qty] = ["%sx%s" % (x, y) for (x, y) in cvh_Res]
        sht.write_row(row + 2, CvxHDataStartCol + 1, cvh_QPs[qty])
        sht.write_row(row + 3, CvxHDataStartCol + 1, cvh_Res_txt[qty])
        if EnablePreInterpolation:
            int_brts = [h[0] for h in int_hull[qty]]
            int_qtys = [h[1] for h in int_hull[qty]]
            sht.write_row(row + 4, CvxHDataStartCol + 1, int_qtys)
            sht.write_row(row + 5, CvxHDataStartCol + 1, int_brts)

        cols = [CvxHDataStartCol + 1 + i for i in range(len(hull[qty]))]
        AddSeriesToChart_Scatter_Rows(shtname, cols, row, row + 1, charts[idx],
                                      'ConvexHull', ConvexHullColor)
        if EnablePreInterpolation:
            int_cols = [CvxHDataStartCol + 1 + i for i in range(len(int_hull[qty]))]
            AddSeriesToChart_Scatter_Rows(shtname, int_cols, row + 4, row + 5,
                                          charts[idx], 'Int_ConvexHull',
                                          Int_ConvexHullColor)
    endrow = CvxHDataRows[-1] + CvxHDataNum

    # find out QP/resolution for given qty metric and qty value
    startrow_fdout = endrow + 1
    sht.write(startrow_fdout, CvxHDataStartCol,
              "  Find out QP/resolution for given quality metrics:")
    numitem_fdout = 4  # qtymetric values, QP, resolution, one empty row
    startrows_fdout = [startrow_fdout + 1 + i * numitem_fdout
                       for i in range(len(tgtqmetrics))]

    for metric, idx in zip(tgtqmetrics, range(len(tgtqmetrics))):
        if metric not in QualityList:
            Utils.Logger.error("wrong qty metric name. should be one of the" \
                               " name in QualityList.")
            return endrow

        qtyvals = tgtqmetrics[metric]
        qtyQPs, qtyRes = LookUpQPAndResInCvxHull(qtyvals, hull[metric],
                                                 cvh_QPs[metric],
                                                 cvh_Res_txt[metric])
        # write the look up result into excel file
        startrow = startrows_fdout[idx]
        sht.write(startrow, CvxHDataStartCol, metric)
        sht.write_row(startrow, 1, qtyvals)
        sht.write(startrow + 1, CvxHDataStartCol, 'QP')
        sht.write_row(startrow + 1, CvxHDataStartCol + 1, qtyQPs)
        sht.write(startrow + 2, CvxHDataStartCol, 'Resolution')
        sht.write_row(startrow + 2, CvxHDataStartCol + 1, qtyRes)
        endrow = startrow + 3

    return endrow

###############################################################################
######### Major Functions #####################################################
def CleanUp_workfolders():
    folders = [Path_DnScaleYuv, Path_Bitstreams, Path_DecodedYuv, Path_QualityLog,
               Path_TestLog, Path_CfgFiles, Path_PerfLog, Path_EncLog]
    if not KeepUpscaledOutput:
        folders += [Path_UpScaleYuv, Path_DecUpScaleYuv]

    for folder in folders:
        Cleanfolder(folder)

def Run_ConvexHull_Test(clip, dnScalAlgo, upScalAlgo, ScaleMethod, LogCmdOnly = False):
    Utils.Logger.info("start encode %s" % clip.file_name)
    DnScaledRes = [(int(clip.width / ratio), int(clip.height / ratio)) for ratio in
                   DnScaleRatio]
    for i in range(len(DnScaledRes)):
        DnScaledW = DnScaledRes[i][0]
        DnScaledH = DnScaledRes[i][1]
        # downscaling if the downscaled file does not exist
        dnscalyuv = GetDownScaledOutFile(clip, DnScaledW, DnScaledH, Path_DnScaleYuv,
                                         ScaleMethod, dnScalAlgo, AS_DOWNSCALE_ON_THE_FLY, i)
        if not os.path.isfile(dnscalyuv):
            dnscalyuv = DownScaling(ScaleMethod, clip, FrameNum['AS'], DnScaledW, DnScaledH,
                                    Path_DnScaleYuv, Path_CfgFiles, dnScalAlgo, LogCmdOnly)
        ds_clip = Clip(GetShortContentName(dnscalyuv, False)+'.y4m', dnscalyuv,
                       clip.file_class, DnScaledW, DnScaledH, clip.fmt, clip.fps_num,
                       clip.fps_denom, clip.bit_depth)
        QPSet = QPs['AS']
        if CodecName == "hevc":
            QPSet = HEVC_QPs['AS']
        for QP in QPSet:
            Utils.Logger.info("start encode and upscale for QP %d" % QP)
            JobName = '%s_%s_%s_%s_%dx%d_Preset_%s_QP_%d' % \
                      (GetShortContentName(clip.file_name, False),
                       EncodeMethod, CodecName, "AS", DnScaledW, DnScaledH, EncodePreset, QP)
            if LogCmdOnly:
                Utils.CmdLogger.write("============== %s Job Start =================\n" % JobName)

            #encode and upscaling
            reconyuv = Run_EncDec_Upscale(EncodeMethod, CodecName, EncodePreset,
                                          ds_clip, 'AS', QP, FrameNum['AS'],
                                          clip.width, clip.height, Path_Bitstreams,
                                          Path_DecodedYuv, Path_DecUpScaleYuv,
                                          Path_CfgFiles, Path_PerfLog, Path_EncLog, upScalAlgo,
                                          ScaleMethod, SaveMemory, LogCmdOnly)
            #calcualte quality distortion
            Utils.Logger.info("start quality metric calculation")
            CalculateQualityMetric(clip.file_path, FrameNum['AS'], reconyuv,
                                   clip.fmt, clip.width, clip.height,
                                   clip.bit_depth, Path_QualityLog, LogCmdOnly)
            if SaveMemory:
                DeleteFile(reconyuv, LogCmdOnly)
            if LogCmdOnly:
                Utils.CmdLogger.write("============== %s Job End =================\n" % JobName)
        if SaveMemory:
            if AS_DOWNSCALE_ON_THE_FLY and dnscalyuv != clip.file_path:
                DeleteFile(dnscalyuv)
        Utils.Logger.info("finish running encode test.")
    Utils.Logger.info("finish running encode test.")

def SaveConvexHullResultsToExcel(content, ScaleMethod, dnScAlgos, upScAlgos, csv, perframe_csv,
                                 EnablePreInterpolation=False):
    Utils.Logger.info("start saving RD results to excel file.......")
    if not os.path.exists(Path_RDResults):
        os.makedirs(Path_RDResults)
    excFile = GetRDResultExcelFile(clip)
    wb = xlsxwriter.Workbook(excFile)
    shts = []
    for i in range(len(dnScAlgos)):
        shtname = dnScAlgos[i] + '--' + upScAlgos[i]
        shts.append(wb.add_worksheet(shtname))

    QPSet = QPs['AS']
    if CodecName == "hevc":
        QPSet = HEVC_QPs['AS']

    DnScaledRes = [(int(clip.width / ratio), int(clip.height / ratio))
                   for ratio in DnScaleRatio]
    contentname = GetShortContentName(clip.file_name)
    for sht, indx in zip(shts, list(range(len(dnScAlgos)))):
        # write QP
        sht.write(1, 0, "QP")
        sht.write_column(CvxH_WtRows[0], 0, QPSet)
        shtname = sht.get_name()

        charts = [];  y_mins = {}; y_maxs = {}; RDPoints = {}; Int_RDPoints = {}
        for qty, x in zip(QualityList, range(len(QualityList))):
            chart_title = 'RD Curves - %s with %s' % (contentname, shtname)
            xaxis_name = 'Bitrate - Kbps'
            chart = CreateChart_Scatter(wb, chart_title, xaxis_name, qty)
            charts.append(chart)
            y_mins[x] = []; y_maxs[x] = []; RDPoints[x] = []; Int_RDPoints[x] = []

        # write RD data
        for col, i in zip(CvxH_WtCols, range(len(DnScaledRes))):
            DnScaledW = DnScaledRes[i][0]
            DnScaledH = DnScaledRes[i][1]
            sht.write(0, col, "resolution=%dx%d" % (DnScaledW, DnScaledH))
            sht.write(1, col, "Bitrate(kbps)")
            sht.write_row(1, col + 1, QualityList)

            bitratesKbps = []; qualities = []

            for qp in QPSet:
                bs, reconyuv = GetBsReconFileName(EncodeMethod, CodecName, 'AS',
                                                  EncodePreset, clip, DnScaledW,
                                                  DnScaledH, ScaleMethod, dnScAlgos[indx],
                                                  upScAlgos[indx], qp,
                                                  Path_Bitstreams, False, i)

                bitrate = round((os.path.getsize(bs) * 8 * (clip.fps_num / clip.fps_denom)
                           / FrameNum['AS']) / 1000.0, 6)
                bitratesKbps.append(bitrate)
                quality, perframe_vmaf_log = GatherQualityMetrics(reconyuv, Path_QualityLog)
                qualities.append(quality)

                #"TestCfg,EncodeMethod,CodecName,EncodePreset,Class,OrigRes,Name,FPS,Bit Depth,CodedRes,QP,Bitrate(kbps)")
                csv.write("%s,%s,%s,%s,%s,%s,%s,%.4f,%d,%s,%d,%f"%
                          ("AS", EncodeMethod, CodecName, EncodePreset, clip.file_class,contentname,
                           str(clip.width)+"x"+str(clip.height), clip.fps,clip.bit_depth,
                           str(DnScaledW)+"x"+str(DnScaledH),qp,bitrate))
                for qty in quality:
                    csv.write(",%f"%qty)

                if EnableTimingInfo:
                    if UsePerfUtil:
                        enc_time, dec_time, enc_instr, dec_instr, enc_cycles, dec_cycles = GatherInstrCycleInfo(bs, Path_PerfLog)
                        csv.write(",%.2f,%.2f,%s,%s,%s,%s,"%(enc_time,dec_time,enc_instr,dec_instr,enc_cycles,dec_cycles))
                    else:
                        enc_time, dec_time = GatherPerfInfo(bs, Path_PerfLog)
                        csv.write(",%.2f,%.2f," % (enc_time, dec_time))
                else:
                    csv.write(",,,")

                if EnableMD5:
                    enc_md5 = md5(bs)
                    dec_md5 = md5(reconyuv)
                    csv.write("%s,%s" % (enc_md5, dec_md5))
                csv.write("\n")

                if (EncodeMethod == 'aom'):
                    enc_log = GetEncLogFile(bs, Path_EncLog)
                    GatherPerframeStat("AS", EncodeMethod, CodecName, EncodePreset, clip, GetShortContentName(bs),
                                       DnScaledW, DnScaledH, qp, enc_log, perframe_csv,
                                       perframe_vmaf_log)
            sht.write_column(CvxH_WtRows[0], col, bitratesKbps)
            for qs, row in zip(qualities, CvxH_WtRows):
                sht.write_row(row, col + 1, qs)

            seriname = "resolution %dx%d" % (DnScaledW, DnScaledH)
            for x in range(len(QualityList)):
                # add RD curves of current resolution to each quality chart
                AddSeriesToChart_Scatter(shtname, CvxH_WtRows, col + 1 + x, col,
                                         charts[x], seriname, LineColors[i])
                # get min and max of y-axis
                qs = [row[x] for row in qualities]
                y_mins[x].append(min(qs))
                y_maxs[x].append(max(qs))
                # get RD points - (bitrate, quality) for each quality metrics
                rdpnts = [(brt, qty) for brt, qty in zip(bitratesKbps, qs)]
                RDPoints[x] = RDPoints[x] + rdpnts
                if EnablePreInterpolation:
                    if UsePCHIPInterpolation:
                        int_rdpnts = Interpolate_PCHIP(rdpnts, QPSet[:], InterpolatePieces, True)
                    else:
                        int_rdpnts = Interpolate_Bilinear(rdpnts, QPSet[:], InterpolatePieces, True)
                    Int_RDPoints[x] = Int_RDPoints[x] + int_rdpnts

        # add convexhull curve to charts
        endrow = AddConvexHullCurveToCharts(sht, charts, RDPoints, DnScaledRes,
                                            TargetQtyMetrics, EnablePreInterpolation,
                                            Int_RDPoints)

        #update RD chart with approprate y axis range
        for qty, x in zip(QualityList, range(len(QualityList))):
            ymin = min(y_mins[x])
            ymax = max(y_maxs[x])
            margin = 0.1  # add 10% on min and max value for y_axis range
            num_precsn = 5 if 'MS-SSIM' in qty else 3
            UpdateChart(charts[x], ymin, ymax, margin, qty, num_precsn)

        startrow = endrow + 2; startcol = 1
        InsertChartsToSheet(sht, startrow, startcol, charts)

    wb.close()
    Utils.Logger.info("finish export convex hull results to excel file.")


def ParseArguments(raw_args):
    parser = argparse.ArgumentParser(prog='ConvexHullTest.py',
                                     usage='%(prog)s [options]',
                                     description='')
    parser.add_argument('-f', '--function', dest='Function', type=str,
                        required=True, metavar='',
                        choices=["clean", "scaling", "sumscaling", "encode",
                                 "convexhull", "summary"],
                        help="function to run: clean, scaling, sumscaling, encode,"
                             " convexhull, summary")
    parser.add_argument('-k', "--KeepUpscaleOutput", dest='KeepUpscaledOutput',
                        type=bool, default=False, metavar='',
                        help="in function clean, if keep upscaled yuv files. It"
                             " is false by default")
    parser.add_argument('-s', "--SaveMemory", dest='SaveMemory', type=bool,
                        default=True, metavar='',
                        help="save memory mode will delete most files in"
                             " intermediate steps and keeps only necessary "
                             "ones for RD calculation. It is false by default")
    parser.add_argument('-CmdOnly', "--LogCmdOnly", dest='LogCmdOnly', type=bool,
                        default=False, metavar='',
                        help="LogCmdOnly mode will only capture the command sequences"
                             "It is false by default")
    parser.add_argument('-l', "--LoggingLevel", dest='LogLevel', type=int,
                        default=3, choices=range(len(LogLevels)), metavar='',
                        help="logging level: 0:No Logging, 1: Critical, 2: Error,"
                             " 3: Warning, 4: Info, 5: Debug")
    parser.add_argument('-c', "--CodecName", dest='CodecName', type=str,
                        choices=CodecNames, metavar='',
                        help="CodecName: av1, av2")
    parser.add_argument('-m', "--EncodeMethod", dest='EncodeMethod', type=str,
                        choices=EncodeMethods, metavar='',
                        help="EncodeMethod: aom, svt")
    parser.add_argument('-p', "--EncodePreset", dest='EncodePreset', type=str,
                        metavar='', help="EncodePreset: 0,1,2... for aom and svt")
    parser.add_argument('-t', '--ScaleMethod', dest='ScaleMethod', type=str,
                        choices=ScaleMethods, metavar='',
                        help="ScaleMethod: ffmpeg, hdrtool, aom")

    if len(raw_args) == 1:
        parser.print_help()
        sys.exit(1)
    args = parser.parse_args(raw_args[1:])

    global Function, KeepUpscaledOutput, SaveMemory, LogLevel, CodecName,\
        EncodeMethod, ScaleMethod, EncodePreset, LogCmdOnly
    Function = args.Function
    KeepUpscaledOutput = args.KeepUpscaledOutput
    SaveMemory = args.SaveMemory
    LogLevel = args.LogLevel
    CodecName = args.CodecName
    EncodeMethod = args.EncodeMethod
    ScaleMethod = args.ScaleMethod
    EncodePreset = args.EncodePreset
    LogCmdOnly = args.LogCmdOnly


######################################
# main
######################################
if __name__ == "__main__":
    #sys.argv = ["","-f","clean"]
    #sys.argv = ["","-f","scaling", "-t", "hdrtool"]
    #sys.argv = ["", "-f", "sumscaling", "-t", "hdrtool"]
    #sys.argv = ["", "-f", "encode","-c","av2","-m","aom","-p","6", "-t", "hdrtool"]
    #sys.argv = ["", "-f", "convexhull","-c","av2","-m","aom","-p","0", "-t", "hdrtool"]
    #sys.argv = ["", "-f", "summary", "-c", "av2", "-m", "aom", "-p", "0", "-t", "hdrtool"]
    ParseArguments(sys.argv)

    # preparation for executing functions
    setupWorkFolderStructure()
    if Function != 'clean':
        SetupLogging(LogLevel, LogCmdOnly, LoggerName, Path_CmdLog, Path_TestLog)
        clip_list = CreateClipList('AS')

    # execute functions
    if Function == 'clean':
        CleanUp_workfolders()
    elif Function == 'scaling':
        for clip in clip_list:
            for dnScaleAlgo, upScaleAlgo in zip(DnScalingAlgos, UpScalingAlgos):
                Run_Scaling_Test(clip, dnScaleAlgo, upScaleAlgo,
                                 Path_DnScaleYuv, Path_UpScaleYuv, Path_QualityLog,
                                 Path_CfgFiles, SaveMemory, KeepUpscaledOutput, ScaleMethod,
                                 LogCmdOnly)
        if SaveMemory:
            Cleanfolder(Path_DnScaleYuv)
            if not KeepUpscaledOutput:
                Cleanfolder(Path_UpScaleYuv)
    elif Function == 'sumscaling':
        SaveScalingResultsToExcel(ScaleMethod, DnScalingAlgos, UpScalingAlgos, clip_list,
                                  Path_QualityLog)
    elif Function == 'encode':
        for clip in clip_list:
            for dnScalAlgo, upScalAlgo in zip(DnScalingAlgos, UpScalingAlgos):
                Run_ConvexHull_Test(clip, dnScalAlgo, upScalAlgo, ScaleMethod, LogCmdOnly)
        if SaveMemory:
            Cleanfolder(Path_DnScaleYuv)
            if not KeepUpscaledOutput:
                Cleanfolder(Path_UpScaleYuv)
    elif Function == 'convexhull':
        csv_file, perframe_csvfile = GetRDResultCsvFile(EncodeMethod, CodecName, EncodePreset, "AS")
        csv = open(csv_file, "wt")
        csv.write("TestCfg,EncodeMethod,CodecName,EncodePreset,Class,Name,OrigRes,FPS," \
                  "Bit Depth,CodedRes,QP,Bitrate(kbps)")
        for qty in QualityList:
            csv.write(',' + qty)
        csv.write(",EncT[s],DecT[s]")
        if UsePerfUtil:
            csv.write(",EncInstr,DecInstr,EncCycles,DecCycles")
        if EnableMD5:
            csv.write(",EncMD5,DecMD5")

        csv.write("\n")

        perframe_csv = open(perframe_csvfile, 'wt')
        perframe_csv.write("TestCfg,EncodeMethod,CodecName,EncodePreset,Class,Name,Res,FPS," \
                           "Bit Depth,QP,POC,FrameType,Level,qindex,FrameSize")
        for qty in QualityList:
            if not qty.startswith("APSNR"):
                perframe_csv.write(',' + qty)
        perframe_csv.write('\n')

        for clip in clip_list:
            SaveConvexHullResultsToExcel(clip, ScaleMethod, DnScalingAlgos, UpScalingAlgos, csv, perframe_csv,
                                         EnablePreInterpolation)
        csv.close()
        perframe_csv.close()
    elif Function == 'summary':
        RDResultFilesGenerated = []
        for clip in clip_list:
            RDResultFilesGenerated.append(GetRDResultExcelFile(clip))

        RDsmfile = GenerateSumRDExcelFile(EncodeMethod, CodecName, EncodePreset,
                                          SummaryOutPath, RDResultFilesGenerated,
                                          clip_list)
        Utils.Logger.info("RD data summary file generated: %s" % RDsmfile)

        CvxHsmfile = GenerateSumCvxHullExcelFile(EncodeMethod, CodecName,
                                                 EncodePreset, SummaryOutPath,
                                                 RDResultFilesGenerated,
                                                 EnablePreInterpolation)
        Utils.Logger.info("Convex hull summary file generated: %s" % CvxHsmfile)
    else:
        Utils.Logger.error("invalid parameter value of Function")
