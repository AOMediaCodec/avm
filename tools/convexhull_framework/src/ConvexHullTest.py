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
import subprocess
from EncDecUpscale import Run_EncDec_Upscale, GetBsReconFileName, Encode, GetBitstreamFile, Decode
from VideoScaler import GetDownScaledOutFile, DownScaling, UpScaling
from CalculateQualityMetrics import CalculateQualityMetric, GatherQualityMetrics
from Utils import GetShortContentName, CreateNewSubfolder, SetupLogging, \
     Cleanfolder, CreateClipList, Clip, GatherPerfInfo, GetEncLogFile, GetDecLogFile, \
     GetRDResultCsvFile, GatherPerframeStat, GatherInstrCycleInfo, ParseDecLogFile, \
     Interpolate_Bilinear, Interpolate_PCHIP, convex_hull, DeleteFile, md5, get_total_frame
from ScalingTest import Run_Scaling_Test
from Config import LogLevels, QPs, QualityList, WorkPath, \
     Path_RDResults, DnScalingAlgos, UpScalingAlgos, \
     EncodeMethods, CodecNames, LoggerName, DnScaleRatio, \
     EnablePreInterpolation, AS_DOWNSCALE_ON_THE_FLY,\
     UsePerfUtil, ScaleMethods, EnableTimingInfo, InterpolatePieces, EnableMD5, \
     UsePCHIPInterpolation, EnableParallelGopEncoding, GOP_SIZE
from CalcQtyWithVmafTool import GetVMAFLogFile
import Utils

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
    total_frame = get_total_frame('AS', clip)
    for i in range(len(DnScaledRes)):
        DnScaledW = DnScaledRes[i][0]
        DnScaledH = DnScaledRes[i][1]
        # downscaling if the downscaled file does not exist
        dnscalyuv = GetDownScaledOutFile(clip, DnScaledW, DnScaledH, Path_DnScaleYuv,
                                         ScaleMethod, dnScalAlgo, AS_DOWNSCALE_ON_THE_FLY, i)
        if not os.path.isfile(dnscalyuv):
            dnscalyuv = DownScaling(ScaleMethod, clip, total_frame, DnScaledW, DnScaledH,
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
                                          ds_clip, 'AS', QP, total_frame,
                                          clip.width, clip.height, Path_Bitstreams,
                                          Path_DecodedYuv, Path_DecUpScaleYuv,
                                          Path_CfgFiles, Path_PerfLog, Path_EncLog, Path_DecLog,
                                          upScalAlgo, ScaleMethod, SaveMemory, LogCmdOnly)
            #calcualte quality distortion
            Utils.Logger.info("start quality metric calculation")
            CalculateQualityMetric(clip.file_path, total_frame, reconyuv,
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

def Run_Parallel_ConvexHull_Test(clip, dnScalAlgo, upScalAlgo, ScaleMethod, LogCmdOnly = False):
    if EnableParallelGopEncoding:
        Utils.Logger.info("start encode %s" % clip.file_name)
        DnScaledRes = [(int(clip.width / ratio), int(clip.height / ratio)) for ratio in
                       DnScaleRatio]
        total_frame = get_total_frame('AS', clip)
        for i in range(len(DnScaledRes)):
            DnScaledW = DnScaledRes[i][0]
            DnScaledH = DnScaledRes[i][1]
            # downscaling if the downscaled file does not exist
            dnscalyuv = GetDownScaledOutFile(clip, DnScaledW, DnScaledH, Path_DnScaleYuv,
                                             ScaleMethod, dnScalAlgo, AS_DOWNSCALE_ON_THE_FLY, i)
            if not os.path.isfile(dnscalyuv):
                dnscalyuv = DownScaling(ScaleMethod, clip, total_frame, DnScaledW, DnScaledH,
                                        Path_DnScaleYuv, Path_CfgFiles, dnScalAlgo, LogCmdOnly)
            ds_clip = Clip(GetShortContentName(dnscalyuv, False)+'.y4m', dnscalyuv,
                           clip.file_class, DnScaledW, DnScaledH, clip.fmt, clip.fps_num,
                           clip.fps_denom, clip.bit_depth)
            QPSet = QPs['AS']
            for QP in QPSet:
                Utils.Logger.info("start encode for QP %d" % QP)
                total_frame = get_total_frame('AS', clip)

                # Encode
                start_frame = 0
                while start_frame < total_frame:
                    #encode
                    num_frames = GOP_SIZE
                    if (start_frame + num_frames) > total_frame:
                        num_frames = total_frame - start_frame

                    JobName = '%s_%s_%s_%s_%dx%d_Preset_%s_QP_%d_start_%d_frames_%d' % \
                          (GetShortContentName(clip.file_name, False),
                           EncodeMethod, CodecName, "AS", DnScaledW, DnScaledH,
                           EncodePreset, QP, start_frame, num_frames)

                    if LogCmdOnly:
                        Utils.CmdLogger.write("============== %s Job Start =================\n"%JobName)

                    #encode
                    bsFile = Encode(EncodeMethod, CodecName, EncodePreset, ds_clip, 'AS', QP,
                                num_frames, Path_Bitstreams, Path_PerfLog,
                                Path_EncLog, start_frame, LogCmdOnly)

                    #decode
                    #bsFile = os.path.join(Path_Bitstreams, "%s.obu" % (JobName))
                    decodedYUV = Decode(ds_clip, EncodeMethod, 'AS', CodecName, bsFile, Path_DecodedYuv, Path_PerfLog,
                                False, Path_DecLog, LogCmdOnly)
                    if SaveMemory:
                        DeleteFile(decodedYUV, LogCmdOnly)

                    start_frame += num_frames
                    if LogCmdOnly:
                        Utils.CmdLogger.write("============== %s Job End =================\n" % JobName)

            if SaveMemory:
                if AS_DOWNSCALE_ON_THE_FLY and dnscalyuv != clip.file_path:
                    DeleteFile(dnscalyuv)
            Utils.Logger.info("finish running encode test.")
        Utils.Logger.info("finish running encode test.")
    else:
        Utils.Logger.error("parallel encode function can only be used with Parallel Gop Encoding mode")

def SaveConvexHullResults(content, ScaleMethod, dnScAlgos, upScAlgos, csv, perframe_csv,
                                 EnablePreInterpolation=False):
    Utils.Logger.info("start saving RD results to excel file.......")
    missing = open("AS_Missing.log", 'wt')
    if not os.path.exists(Path_RDResults):
        os.makedirs(Path_RDResults)

    QPSet = QPs['AS']
    total_frames = get_total_frame('AS', clip)

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
                    print("bitstream %s is missing" % bs)
                    missing.write("\nbitstream %s is missing" % bs)
                    continue

                dec_log = GetDecLogFile(bs, Path_DecLog)
                if not os.path.exists(dec_log):
                    print("dec_log %s is missing" % dec_log)
                    missing.write("\ndec_log %s is missing" % dec_log)
                    continue

                num_dec_frames = ParseDecLogFile(dec_log)
                if num_dec_frames == 0:
                    print("decoding error in %s" % dec_log)
                    missing.write("\ndecoding error in %s" % dec_log)
                    continue

                vmaf_log = GetVMAFLogFile(reconyuv, Path_QualityLog)
                if not os.path.exists(vmaf_log):
                    print("vmaf_log %s is missing" % vmaf_log)
                    missing.write("\nvmaf_log %s is missing" % vmaf_log)
                    continue

                quality, perframe_vmaf_log, frame_num = GatherQualityMetrics(reconyuv, Path_QualityLog)

                if not quality:
                    print("%s quality metrics is missing" % bs)
                    missing.write("\n%s quality metrics is missing" % bs)
                    continue

                bitrate = round((os.path.getsize(bs) * 8 * (clip.fps_num / clip.fps_denom)
                           / frame_num) / 1000.0, 6)
                bitratesKbps.append(bitrate)

                qualities.append(quality)

                #"TestCfg,EncodeMethod,CodecName,EncodePreset,Class,OrigRes,Name,FPS,BitDepth,CodedRes,QP,Bitrate(kbps)")
                csv.write("%s,%s,%s,%s,%s,%s,%s,%.4f,%d,%s,%d,%f"%
                          ("AS", EncodeMethod, CodecName, EncodePreset, clip.file_class,contentname,
                           str(clip.width)+"x"+str(clip.height), clip.fps,clip.bit_depth,
                           str(DnScaledW)+"x"+str(DnScaledH),qp,bitrate))
                for qty in quality:
                    csv.write(",%f"%qty)

                if EnableTimingInfo and not EnableParallelGopEncoding:
                    if UsePerfUtil:
                        enc_time, dec_time, enc_instr, dec_instr, enc_cycles, dec_cycles = GatherInstrCycleInfo(bs, Path_PerfLog)
                        csv.write(",%.2f,%.2f,%s,%s,%s,%s,"%(enc_time,dec_time,enc_instr,dec_instr,enc_cycles,dec_cycles))
                    else:
                        enc_time, dec_time = GatherPerfInfo(bs, Path_PerfLog)
                        csv.write(",%.2f,%.2f," % (enc_time, dec_time))
                else:
                    csv.write(",,,,,,,")

                if EnableMD5:
                    enc_md5 = md5(bs)
                    dec_md5 = md5(reconyuv)
                    csv.write("%s,%s" % (enc_md5, dec_md5))
                csv.write("\n")

                if (EncodeMethod == 'avm') and not EnableParallelGopEncoding:
                    enc_log = GetEncLogFile(bs, Path_EncLog)
                    GatherPerframeStat("AS", EncodeMethod, CodecName, EncodePreset, clip, GetShortContentName(bs),
                                       DnScaledW, DnScaledH, qp, enc_log, perframe_csv,
                                       perframe_vmaf_log)

    missing.close()
    Utils.Logger.info("finish export convex hull results to excel file.")

def Run_AS_Concatenate_Test(test_cfg, clip, dnScalAlgo, codec, method, preset, LogCmdOnly = False):
    if EnableParallelGopEncoding:
        Utils.Logger.info("start running %s concatenate tests with %s"
                      % (test_cfg, clip.file_name))
        DnScaledRes = [(int(clip.width / ratio), int(clip.height / ratio)) for ratio in
                       DnScaleRatio]
        for i in range(len(DnScaledRes)):
            DnScaledW = DnScaledRes[i][0]
            DnScaledH = DnScaledRes[i][1]

            # downscaling if the downscaled file does not exist
            dnscalyuv = GetDownScaledOutFile(clip, DnScaledW, DnScaledH, Path_DnScaleYuv,
                                             ScaleMethod, dnScalAlgo, AS_DOWNSCALE_ON_THE_FLY, i)
            ds_clip = Clip(GetShortContentName(dnscalyuv, False)+'.y4m', dnscalyuv,
                           clip.file_class, DnScaledW, DnScaledH, clip.fmt, clip.fps_num,
                           clip.fps_denom, clip.bit_depth)
            QPSet = QPs['AS']
            for QP in QPSet:
                Utils.Logger.info("start encode for QP %d" % QP)
                total_frame = get_total_frame('AS', clip)

                cmd = "cat "
                start_frame = 0
                while start_frame < total_frame:
                    num_frames = GOP_SIZE
                    if (start_frame + num_frames) > total_frame:
                        num_frames = total_frame - start_frame

                    bsfile = GetBitstreamFile(method, codec, test_cfg, preset, ds_clip.file_path,
                              QP, start_frame, num_frames, Path_Bitstreams)
                    cmd += " %s " % bsfile
                    start_frame += num_frames

                bsfile = '%s_%s_%s_%s_Preset_%s_QP_%d_start_%d_frames_%d.obu' % \
                            (GetShortContentName(ds_clip.file_name, False),
                            method, codec, test_cfg, preset, QP, 0, total_frame)

                cmd += " > %s/%s" % (Path_Bitstreams, bsfile)

                if LogCmdOnly:
                    Utils.CmdLogger.write("%s\n" % cmd)
                else:
                    subprocess.call(cmd, shell=True)

def Run_AS_Decode_Test(test_cfg, clip, dnScalAlgo, upScalAlgo, scale_method, codec, method, preset, LogCmdOnly = False):
    if EnableParallelGopEncoding:
        Utils.Logger.info("start running %s decode tests with %s"
                      % (test_cfg, clip.file_name))
        DnScaledRes = [(int(clip.width / ratio), int(clip.height / ratio)) for ratio in
                       DnScaleRatio]
        for i in range(len(DnScaledRes)):
            DnScaledW = DnScaledRes[i][0]
            DnScaledH = DnScaledRes[i][1]

            # downscaling if the downscaled file does not exist
            dnscalyuv = GetDownScaledOutFile(clip, DnScaledW, DnScaledH, Path_DnScaleYuv,
                                             ScaleMethod, dnScalAlgo, AS_DOWNSCALE_ON_THE_FLY, i)
            ds_clip = Clip(GetShortContentName(dnscalyuv, False)+'.y4m', dnscalyuv,
                           clip.file_class, DnScaledW, DnScaledH, clip.fmt, clip.fps_num,
                           clip.fps_denom, clip.bit_depth)

            QPSet = QPs[test_cfg]

            for QP in QPSet:
                Utils.Logger.info("start decode  with QP %d" % (QP))
                total_frame = get_total_frame(test_cfg, clip.file_name)

                # decode
                JobName = '%s_%s_%s_%s_Preset_%s_QP_%d' % \
                              (GetShortContentName(ds_clip.file_name, False),
                              method, codec, test_cfg, preset, QP)
                if LogCmdOnly:
                    Utils.CmdLogger.write("============== %s Job Start =================\n"%JobName)

                bsfile = '%s/%s_%s_%s_%s_Preset_%s_QP_%d_start_%d_frames_%d.obu' % \
                        (Path_Bitstreams, GetShortContentName(ds_clip.file_name, False),
                        method, codec, test_cfg, preset, QP, 0, total_frame)

                decodedYUV = Decode(ds_clip, method, test_cfg, codec, bsfile, Path_DecodedYuv, Path_PerfLog, False,
                                Path_DecLog, LogCmdOnly)

                dec_clip = Clip(GetShortContentName(decodedYUV, False) + ".y4m",
                            decodedYUV, ds_clip.file_class, ds_clip.width, ds_clip.height,
                            ds_clip.fmt, 0, 0, ds_clip.bit_depth)
                upscaledYUV = UpScaling(scale_method, dec_clip, total_frame, clip.width, clip.height,
                            Path_UpScaleYuv, Path_CfgFiles, upScalAlgo, LogCmdOnly)
                if SaveMemory and decodedYUV != upscaledYUV:
                    DeleteFile(decodedYUV, LogCmdOnly)

                #calcualte quality distortion
                Utils.Logger.info("start quality metric calculation")
                CalculateQualityMetric(clip.file_path, total_frame, upscaledYUV,
                                       clip.fmt, clip.width, clip.height,
                                       clip.bit_depth, Path_QualityLog, Path_VmafLog, LogCmdOnly)
                if SaveMemory:
                    DeleteFile(upscaledYUV, LogCmdOnly)
                if LogCmdOnly:
                    Utils.CmdLogger.write("============== %s Job End =================\n" % JobName)
    else:
        Utils.Logger.error("decode function can only be used with Parallel Gop Encoding mode")

def ParseArguments(raw_args):
    parser = argparse.ArgumentParser(prog='ConvexHullTest.py',
                                     usage='%(prog)s [options]',
                                     description='')
    parser.add_argument('-f', '--function', dest='Function', type=str,
                        required=True, metavar='',
                        choices=["clean", "scaling","encode",
                                 "convexhull", "concatnate", "decode"],
                        help="function to run: clean, scaling, encode,"
                             " convexhull, concatnate, decode")
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
                        help="CodecName: av2, av2")
    parser.add_argument('-m', "--EncodeMethod", dest='EncodeMethod', type=str,
                        choices=EncodeMethods, metavar='',
                        help="EncodeMethod: avm, svt")
    parser.add_argument('-p', "--EncodePreset", dest='EncodePreset', type=str,
                        metavar='', help="EncodePreset: 0,1,2... for avm and svt")
    parser.add_argument('-t', '--ScaleMethod', dest='ScaleMethod', type=str,
                        choices=ScaleMethods, metavar='',
                        help="ScaleMethod: ffmpeg, hdrtool, avm")

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
    #sys.argv = ["", "-f", "encode","-c","av2","-m","avm","-p","6", "-t", "hdrtool"]
    #sys.argv = ["", "-f", "convexhull","-c","av2","-m","avm","-p","0", "-t", "hdrtool"]
    #sys.argv = ["", "-f", "summary", "-c", "av2", "-m", "avm", "-p", "0", "-t", "hdrtool"]
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
                if EnableParallelGopEncoding:
                    Run_Parallel_ConvexHull_Test(clip, dnScalAlgo, upScalAlgo, ScaleMethod, LogCmdOnly)
                else:
                    Run_ConvexHull_Test(clip, dnScalAlgo, upScalAlgo, ScaleMethod, LogCmdOnly)
        if SaveMemory:
            Cleanfolder(Path_DnScaleYuv)
            if not KeepUpscaledOutput:
                Cleanfolder(Path_UpScaleYuv)
    elif Function == 'decode':
        test_cfg = "AS"
        if EnableParallelGopEncoding:
            clip_list = CreateClipList(test_cfg)
            for clip in clip_list:
                for dnScalAlgo, upScalAlgo in zip(DnScalingAlgos, UpScalingAlgos):
                    if EnableParallelGopEncoding:
                        Run_AS_Decode_Test(test_cfg, clip, dnScalAlgo, upScalAlgo, ScaleMethod, CodecName, EncodeMethod, EncodePreset, LogCmdOnly)
                    else:
                        Utils.Logger.error("concatenate function can only be used with Parallel Gop Encoding mode and RA configuration")
        else:
            print("decode command can only be used when parallel gop encoding is enabled")
    elif Function == 'concatnate':
        test_cfg = "AS"
        if EnableParallelGopEncoding:
            clip_list = CreateClipList(test_cfg)
            for clip in clip_list:
                for dnScalAlgo, upScalAlgo in zip(DnScalingAlgos, UpScalingAlgos):
                    if EnableParallelGopEncoding:
                        Run_AS_Concatenate_Test(test_cfg, clip, dnScalAlgo, CodecName, EncodeMethod, EncodePreset, LogCmdOnly)
                    else:
                        Utils.Logger.error("concatenate function can only be used with Parallel Gop Encoding mode and RA configuration")
        else:
            print("concatnate command can only be used when parallel gop encoding is enabled")
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
