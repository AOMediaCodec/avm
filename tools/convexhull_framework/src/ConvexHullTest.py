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
import argparse

from EncDecUpscale import Run_EncDec_Upscale, GetBsReconFileName
from VideoScaler import GetDownScaledOutFile, DownScaling
from CalculateQualityMetrics import CalculateQualityMetric, GatherQualityMetrics
from Utils import GetShortContentName, CreateNewSubfolder, SetupLogging, \
     Cleanfolder, CreateClipList, Clip, GatherPerfInfo, GetEncLogFile, \
     GetRDResultCsvFile, GatherPerframeStat, GatherInstrCycleInfo, \
     Interpolate_Bilinear, Interpolate_PCHIP, convex_hull, DeleteFile, md5
from ScalingTest import Run_Scaling_Test
import Utils
from Config import LogLevels, FrameNum, QPs, QualityList, WorkPath, \
     Path_RDResults, DnScalingAlgos, UpScalingAlgos, \
     EncodeMethods, CodecNames, LoggerName, DnScaleRatio, \
     EnablePreInterpolation, AS_DOWNSCALE_ON_THE_FLY,\
     UsePerfUtil, ScaleMethods, EnableTimingInfo, InterpolatePieces, EnableMD5, \
     UsePCHIPInterpolation, HEVC_QPs

###############################################################################
##### Helper Functions ########################################################
def setupWorkFolderStructure():
    global Path_Bitstreams, Path_DecodedYuv, Path_UpScaleYuv, Path_DnScaleYuv, \
    Path_QualityLog, Path_TestLog, Path_CfgFiles, Path_DecUpScaleYuv, Path_PerfLog, \
    Path_EncLog, Path_DecLog, Path_VmafLog, Path_CmdLog
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
    Path_DecLog = CreateNewSubfolder(WorkPath, "decLogs")
    Path_CmdLog = CreateNewSubfolder(WorkPath, "cmdLogs")
    Path_VmafLog = CreateNewSubfolder(WorkPath, "vmafLogs")

###############################################################################
######### Major Functions #####################################################
def CleanUp_workfolders():
    folders = [Path_DnScaleYuv, Path_Bitstreams, Path_DecodedYuv, Path_QualityLog,
               Path_TestLog, Path_CfgFiles, Path_PerfLog, Path_EncLog, Path_DecLog,
               Path_VmafLog]
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
                                          Path_CfgFiles, Path_PerfLog, Path_EncLog, Path_DecLog,
                                          upScalAlgo, ScaleMethod, SaveMemory, LogCmdOnly)
            #calcualte quality distortion
            Utils.Logger.info("start quality metric calculation")
            CalculateQualityMetric(clip.file_path, FrameNum['AS'], reconyuv,
                                   clip.fmt, clip.width, clip.height,
                                   clip.bit_depth, Path_QualityLog, Path_VmafLog, LogCmdOnly)
            if SaveMemory:
                DeleteFile(reconyuv, LogCmdOnly)
            if LogCmdOnly:
                Utils.CmdLogger.write("============== %s Job End =================\n" % JobName)
        if SaveMemory:
            if AS_DOWNSCALE_ON_THE_FLY and dnscalyuv != clip.file_path:
                DeleteFile(dnscalyuv)
        Utils.Logger.info("finish running encode test.")
    Utils.Logger.info("finish running encode test.")

def SaveConvexHullResults(content, ScaleMethod, dnScAlgos, upScAlgos, csv, perframe_csv,
                                 EnablePreInterpolation=False):
    Utils.Logger.info("start saving RD results to excel file.......")
    missing = open("AS_Missing.log", 'wt')
    if not os.path.exists(Path_RDResults):
        os.makedirs(Path_RDResults)

    QPSet = QPs['AS']
    total_frames = FrameNum['AS']

    DnScaledRes = [(int(clip.width / ratio), int(clip.height / ratio))
                   for ratio in DnScaleRatio]
    contentname = GetShortContentName(clip.file_name)
    for indx in list(range(len(dnScAlgos))):
        # write RD data
        for i in range(len(DnScaledRes)):
            DnScaledW = DnScaledRes[i][0]
            DnScaledH = DnScaledRes[i][1]

            bitratesKbps = []; qualities = []

            for qp in QPSet:
                bs, reconyuv = GetBsReconFileName(EncodeMethod, CodecName, 'AS',
                                                  EncodePreset, clip, DnScaledW,
                                                  DnScaledH, ScaleMethod, dnScAlgos[indx],
                                                  upScAlgos[indx], qp, 0, total_frames,
                                                  Path_Bitstreams, False, i)

                if not os.path.exists(bs):
                    print("%s is missing" % bs)
                    missing.write("\n%s is missing" % bs)
                    continue

                bitrate = round((os.path.getsize(bs) * 8 * (clip.fps_num / clip.fps_denom)
                           / FrameNum['AS']) / 1000.0, 6)
                bitratesKbps.append(bitrate)

                quality, perframe_vmaf_log = GatherQualityMetrics(reconyuv, Path_QualityLog)

                if not quality:
                    print("%s quality metrics is missing" % bs)
                    missing.write("\n%s quality metrics is missing" % bs)
                    continue

                qualities.append(quality)

                #"TestCfg,EncodeMethod,CodecName,EncodePreset,Class,OrigRes,Name,FPS,BitDepth,CodedRes,QP,Bitrate(kbps)")
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

    missing.close()
    Utils.Logger.info("finish export convex hull results to excel file.")


def ParseArguments(raw_args):
    parser = argparse.ArgumentParser(prog='ConvexHullTest.py',
                                     usage='%(prog)s [options]',
                                     description='')
    parser.add_argument('-f', '--function', dest='Function', type=str,
                        required=True, metavar='',
                        choices=["clean", "scaling","encode",
                                 "convexhull"],
                        help="function to run: clean, scaling, encode,"
                             " convexhull")
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
        SetupLogging(LogLevel, LogCmdOnly, LoggerName, WorkPath, Path_TestLog)
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
                  "BitDepth,CodedRes,QP,Bitrate(kbps)")
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
                           "BitDepth,QP,POC,FrameType,Level,qindex,FrameSize")
        for qty in QualityList:
            if not qty.startswith("APSNR"):
                perframe_csv.write(',' + qty)
        perframe_csv.write('\n')

        for clip in clip_list:
            SaveConvexHullResults(clip, ScaleMethod, DnScalingAlgos, UpScalingAlgos, csv, perframe_csv,
                                         EnablePreInterpolation)
        csv.close()
        perframe_csv.close()
    else:
        Utils.Logger.error("invalid parameter value of Function")
