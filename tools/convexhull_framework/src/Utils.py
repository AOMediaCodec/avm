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
import re
import sys
from csv import DictReader
import subprocess
import time
import logging
import hashlib
import math
import numpy as np
import scipy.interpolate
import matplotlib.pyplot as plt
from operator import itemgetter
from Config import LogLevels, ContentPath, Platform, Path_RDResults, QPs, PSNR_Y_WEIGHT, PSNR_U_WEIGHT, PSNR_V_WEIGHT, \
APSNR_Y_WEIGHT, APSNR_U_WEIGHT, APSNR_V_WEIGHT, InterpolatePieces, UsePCHIPInterpolation, FFMPEG
from AV2CTCVideo import Y4M_CLIPs, CTC_TEST_SET
from CalcBDRate import BD_RATE

class Clip:
    file_name = ""
    file_path = ""
    file_class = ""
    width = 0
    height = 0
    fmt = ""
    fps_num = 0
    fps_denom = 0
    fps = 0
    bit_depth = 0

    def __init__(self, Name="", Path = "", Class="", Width=0, Height=0, Fmt="", FPS_num=0, FPS_denom=0, Bit_depth=0):
        self.file_name = Name
        self.file_path = Path
        self.file_class = Class
        self.width = Width
        self.height = Height
        self.fmt = Fmt
        self.fps_num = FPS_num
        self.fps_denom = FPS_denom
        if (self.fps_num == 0):
            self.fps = 0
        else:
            self.fps = round(self.fps_num / self.fps_denom)
        self.bit_depth = Bit_depth

class Record:
    test_cfg = ""
    encode_mode = ""
    codec_name = ""
    encode_preset = ""
    file_class = ""
    file_name = ""
    orig_res = ""
    fps = ""
    bit_depth = 0
    coded_res = ""
    qp = 0
    bitrate = 0.0
    psnr_y = 0.0
    psnr_u = 0.0
    psnr_v = 0.0
    overall_psnr = 0.0
    ssim_y = 0.0
    ms_ssim_y = 0.0
    vmaf_y = 0.0
    vmaf_y_neg = 0.0
    psnr_hvs = 0.0
    ciede2k = 0.0
    apsnr_y = 0.0
    apsnr_u = 0.0
    apsnr_v = 0.0
    overall_apsnr = 0.0
    cambi = 0.0
    enc_time = 0.0
    dec_time = 0.0
    enc_instr = 0.0
    dec_instr = 0.0
    enc_cycle = 0.0
    dec_cycle = 0.0

    def __init__(self, test_cfg, encode_mode , codec_name, encode_preset, file_class, file_name,
                 orig_res, fps, bit_depth, coded_res, qp, bitrate, psnr_y, psnr_u, psnr_v,
                 ssim_y, ms_ssim_y, vmaf_y, vmaf_y_neg, psnr_hvs, ciede2k, apsnr_y, apsnr_u,
                 apsnr_v, cambi, enc_time, dec_time, enc_instr, dec_instr, enc_cycle, dec_cycle):

        self.test_cfg = test_cfg
        self.encode_mode = encode_mode
        self.codec_name = codec_name
        self.encode_preset = encode_preset
        self.file_class = file_class
        self.file_name = file_name
        self.orig_res = orig_res
        self.fps = fps
        self.bit_depth = bit_depth
        self.coded_res = coded_res
        self.qp = qp
        self.bitrate = float(bitrate)
        self.psnr_y = float(psnr_y)
        self.psnr_u = float(psnr_u)
        self.psnr_v = float(psnr_v)
        self.overall_psnr = (PSNR_Y_WEIGHT * self.psnr_y +
                             PSNR_U_WEIGHT * self.psnr_u +
                             PSNR_V_WEIGHT * self.psnr_v) / (PSNR_Y_WEIGHT + PSNR_U_WEIGHT + PSNR_V_WEIGHT)
        self.ssim_y = float(ssim_y)
        self.ms_ssim_y = float(ms_ssim_y)
        self.vmaf_y = float(vmaf_y)
        self.vmaf_y_neg = float(vmaf_y_neg)
        self.psnr_hvs = float(psnr_hvs)
        self.ciede2k = float(ciede2k)
        self.apsnr_y = float(apsnr_y)
        self.apsnr_u = float(apsnr_u)
        self.apsnr_v = float(apsnr_v)
        self.overall_apsnr = 10 * math.log10(1 / ((APSNR_Y_WEIGHT/pow(10, (self.apsnr_y / 10)) +
                                                   APSNR_U_WEIGHT/pow(10, (self.apsnr_u / 10)) +
                                                   APSNR_V_WEIGHT/pow(10, (self.apsnr_v / 10))) /
                                              (APSNR_Y_WEIGHT + APSNR_U_WEIGHT + APSNR_V_WEIGHT)))
        self.cambi = float(cambi)
        self.enc_time = float(enc_time)
        self.dec_time = float(dec_time)
        self.enc_instr = float(enc_instr)
        self.dec_instr = float(dec_instr)
        self.enc_cycle = float(enc_cycle)
        self.dec_cycle = float(dec_cycle)

def ParseCSVFile(csv_file):
    records = {}
    with open(csv_file, 'r') as f:
        list_of_data = list(DictReader(f))
        for data in list_of_data:
            key = data['CodedRes'] + "_" + data['QP']
            name = data['Name']
            record = Record(data['TestCfg'], data['EncodeMethod'], data['CodecName'], data['EncodePreset'],
                            data['Class'], data['Name'], data['OrigRes'], data['FPS'], data['BitDepth'],
                            data['CodedRes'], data['QP'], data['Bitrate(kbps)'], data['PSNR_Y'], data['PSNR_U'],
                            data['PSNR_V'], data['SSIM_Y(dB)'], data['MS-SSIM_Y(dB)'], data['VMAF_Y'],
                            data['VMAF_Y-NEG'], data['PSNR-HVS'], data['CIEDE2000'], data['APSNR_Y'],
                            data['APSNR_U'], data['APSNR_V'], data['CAMBI'], data['EncT[s]'], data['DecT[s]'],
                            data['EncInstr'], data['DecInstr'], data['EncCycles'], data['DecCycles'])

            if name not in records.keys():
                records[name] = {}
            records[name][key] = record
    return records

def Cleanfolder(folder):
    if os.path.isdir(folder):
        for f in os.listdir(folder):
            file = os.path.join(folder, f)
            if os.path.isfile(file):
                os.remove(file)

def CreateNewSubfolder(parent, name):
    if name == '' or name is None:
        return None
    folder = os.path.join(parent, name)
    if not os.path.exists(folder):
        os.makedirs(folder)
    return folder


def DeleteFile(file, LogCmdOnly):
    CmdLogger.write("::Delete\n")
    if Platform == "Windows":
        cmd = "del " + file
    else:
        cmd = "rm " + file
    ExecuteCmd(cmd, LogCmdOnly)

def GetShortContentName(content, isshort=True):
    basename = os.path.splitext(os.path.basename(content))[0]
    if isshort:
        item = re.findall(r"([a-zA-Z0-9]+)_", basename)
        if len(item) == 0:
            name = basename
        else:
            name = item[0]
    else:
        name = basename
    return name

def GetEncLogFile(bsfile, logpath):
    filename = GetShortContentName(bsfile, False) + '_EncLog.txt'
    return os.path.join(logpath, filename)

def parseY4MHeader(y4m):
    """
    Parse y4m information from its header.
    """
    w = 0; h = 0; fps_num = 0; fps_denom = 0; fr = 0; fmt = "420"; bit_depth = 8;
    #print("parsing " + y4m)
    with open(y4m, 'rb') as f:
        line = f.readline().decode('utf-8')
        #YUV4MPEG2 W4096 H2160 F30000:1001 Ip A0:0 C420p10 XYSCSS=420P10
        m = re.search(r"W([0-9]+) H([0-9]+) F([0-9]+)\:([0-9]+)", line)
        if m:
            w = int(m.group(1))
            h = int(m.group(2))
            fps_num = float(m.group(3))
            fps_denom = float(m.group(4))
            fps = round(fps_num / fps_denom)
        m = re.search(r"C([0-9]+)p([0-9]+)", line)
        if m:
            fmt = m.group(1)
            bit_depth = int(m.group(2))
    if w == 0 or h == 0 or fps == 0:
        print("Failed to parse the input y4m file!\n")
        sys.exit()
    return (w, h, fps_num, fps_denom, fps, fmt, bit_depth)

def CreateClipList(test_cfg):
    clip_list = []; test_set = []
    #[filename, class, width, height, fps_num, fps_denom, bitdepth, fmt]
    test_set = CTC_TEST_SET[test_cfg]

    for cls in test_set:
        for file in Y4M_CLIPs[cls]:
            y4m = os.path.join(ContentPath, cls, file)
            w, h, fps_num, fps_denom, fps, fmt, bit_depth = parseY4MHeader(y4m)
            clip = Clip(file, y4m, cls, w, h, fmt, fps_num, fps_denom, bit_depth)
            clip_list.append(clip)
    return clip_list

def GetContentDict(clip_list):
    dict = {}
    for clip in clip_list:
        cls = clip.file_class
        file = clip.file_path
        if os.path.isfile(file):
            if cls in dict:
                if clip not in dict[cls]:
                    dict[cls].append(clip)
            else:
                dict[cls] = [clip]
    return dict

def CalcRowsClassAndContentDict(rowstart, clip_list, times=1):
    contentsdict = GetContentDict(clip_list)
    ofstc = rowstart
    rows_class = []
    for cls, clips in contentsdict.items():
        rows_class.append(ofstc)
        ofstc = ofstc + len(clips) * times
    return contentsdict, rows_class


def CreateChart_Bar(wb, title, xaxis_name, yaxis_name):
    chart = wb.add_chart({'type': 'column'})
    chart.set_title({'name': title, 'name_font': {'color': 'white'}})
    chart.set_x_axis({'name': xaxis_name,
                      'major_gridlines': {'visible': True, 'line': {'width': 0.25}},
                      'name_font': {'color': 'white'},
                      'num_font': {'color': 'white', 'transparency': 80},
                      'label_position' : 'low'
                      })
    chart.set_y_axis({'name': yaxis_name, 'name_font': {'color': 'white'},
                      'num_font': {'color': 'white'}})
    chart.set_style(11)
    chart.set_size({'x_scale': 1.5, 'y_scale': 2.0})
    chart.set_chartarea({"fill": {'color': '#505050'}})
    chart.set_plotarea({"fill": {'color': '#505050'}})
    chart.set_legend({'position': 'bottom', 'font': {'color': 'white'}})
    return chart

def AddSeriesToChart_Bar(shtname, rows, coly, colx, chart, seriname):
    yvalues = [shtname, rows[0], coly, rows[-1], coly]
    xvalues = [shtname, rows[0], colx, rows[-1], colx]

    chart.add_series({
        'name': seriname,
        'categories': xvalues,
        'values': yvalues
    })


def CreateChart_Scatter(wb, title, xaxis_name, yaxis_name):
    chart = wb.add_chart({'type': 'scatter', 'subtype': 'straight_with_markers'})
    chart.set_title({'name': title, 'name_font': {'color': 'white'}})
    chart.set_x_axis({'name': xaxis_name,
                      'major_gridlines': {'visible': True, 'line': {'width': 0.25}},
                      'name_font': {'color': 'white'},
                      'num_font': {'color': 'white', 'transparency': 80},
                      'label_position' : 'low'
                      })
    chart.set_y_axis({'name': yaxis_name, 'name_font': {'color': 'white'},
                      'num_font': {'color': 'white'}})
    chart.set_style(12)
    chart.set_size({'x_scale': 1.5, 'y_scale': 2.0})
    chart.set_chartarea({"fill": {'color': '#505050'}})
    chart.set_plotarea({"fill": {'color': '#505050'}})
    chart.set_legend({'position': 'bottom', 'font': {'color': 'white'}})
    return chart

def CreateChart_Line(wb, titlename, yaxis_name):
    chart = wb.add_chart({'type': 'line', 'name_font': {'size': 10.5}})
    chart.set_title({'name': titlename})
    chart.set_x_axis({'text_axis': True})
    chart.set_y_axis({'name': yaxis_name, 'name_font': {'size': 11}})
    chart.set_size({'x_scale': 1.5, 'y_scale': 2.0})
    chart.set_legend({'position': 'right', 'font': {'size': 10.5}})
    chart.set_high_low_lines(
        {'line': {'color': 'black', 'size': 2}}
    )
    return chart

def AddSeriesToChart_Scatter(shtname, rows, coly, colx, chart, seriname,
                             linecolor):
    yvalues = [shtname, rows[0], coly, rows[-1], coly]
    xvalues = [shtname, rows[0], colx, rows[-1], colx]

    chart.add_series({
        'name': seriname,
        'categories': xvalues,
        'values': yvalues,
        'line': {'color': linecolor, 'width': 1.5},
        'marker': {'type': 'circle', 'size': 5,
                   'border': {'color': linecolor, 'size': 0.75},
                   'fill': {'color': linecolor}},
    })

def AddSeriesToChart_Scatter_Rows(shtname, cols, rowy, rowx, chart, seriname,
                                  linecolor):
    yvalues = [shtname, rowy, cols[0], rowy, cols[-1]]
    xvalues = [shtname, rowx, cols[0], rowx, cols[-1]]

    chart.add_series({
        'name': seriname,
        'categories': xvalues,
        'values': yvalues,
        'line': {'color': linecolor, 'width': 1.0, 'dash_type': 'dash_dot'},
        'marker': {'type': 'square', 'size': 5,
                   'border': {'color': 'white', 'size': 0.75}}
    })

def AddSeriesToChart_Line(shtname, rows, coly, colx, chart, seriname, shape,
                          ssize, linecolor):
    yvalues = [shtname, rows[0], coly, rows[-1], coly]
    xvalues = [shtname, rows[0], colx, rows[-1], colx]
    chart.add_series({
        'name': seriname,
        'categories': xvalues,
        'values': yvalues,
        'line': {'none': True},
        'marker': {'type': shape,
                   'size': ssize,
                   'border': {'color': linecolor, 'size': 2},
                   'fill': {'color': linecolor}},
    })

def UpdateChart(chart, ymin, ymax, margin, yaxis_name, precsn):
    interval = ymax - ymin
    finalmax = ymax + interval * margin
    finalmin = ymin - interval * margin
    floatprecn = "{:.%df}" % precsn
    finalmin = float(floatprecn.format(finalmin))
    finalmax = float(floatprecn.format(finalmax))
    chart.set_y_axis({'name': yaxis_name,
                      'name_font': {'color': 'white'},
                      'num_font': {'color': 'white'},
                      'min': finalmin, 'max': finalmax})

def InsertChartsToSheet(sht, startrow, startcol, charts):
    height = 30
    width = 12
    num = len(charts)
    row = startrow
    for i in range(1, num, 2):
        sht.insert_chart(row, startcol, charts[i - 1])
        sht.insert_chart(row, startcol + width, charts[i])
        row = row + height

def ExecuteCmd(cmd, LogCmdOnly):
    CmdLogger.write(cmd + "\n")
    ret = 0
    if not LogCmdOnly:
        ret = subprocess.call(cmd, shell=True)
    return ret

def SetupLogging(level, logcmdonly, name, cmd_log_path, test_log_path):
    global Logger
    Logger = logging.getLogger(name)

    if logcmdonly or level != 0:
        global CmdLogger
        logfilename = os.path.join(cmd_log_path, '%s_TestCmd.log'%(name))
        CmdLogger = open(logfilename, 'w')

    if level != 0:
        logfilename = os.path.join(test_log_path, '%s_Test.log' %(name))
        hdlr = logging.FileHandler(logfilename)
        formatter = logging.Formatter("%(asctime)s - %(levelname)s - %(message)s")
        hdlr.setFormatter(formatter)
        Logger.addHandler(hdlr)
        if level in range(len(LogLevels)):
            # valid level input parameter
            lvl = LogLevels[level]
            levelname = logging.getLevelName(lvl)
        else:
            # if not valid, default set to 'INFO'
            levelname = logging.getLevelName('INFO')
        Logger.setLevel(levelname)

def md5(fname):
    if os.path.exists(fname):
        hash_md5 = hashlib.md5()
        with open(fname, "rb") as f:
            for chunk in iter(lambda: f.read(4096), b""):
                hash_md5.update(chunk)
        return hash_md5.hexdigest()
    else:
        return ""

def GatherInstrCycleInfo(bsfile, Path_TimingLog):
    assert(Platform != "Windows" and Platform != "Darwin")
    enc_perf = GetEncPerfFile(bsfile, Path_TimingLog)
    dec_perf = GetDecPerfFile(bsfile, Path_TimingLog)
    enc_time = 0; dec_time = 0; enc_instr = 0; enc_cycles = 0
    dec_instr = 0; dec_cycles = 0
    flog = open(enc_perf, 'r')
    for line in flog:
        m = re.search(r"(\S+)\s+instructions", line)
        if m:
            enc_instr = int(m.group(1).replace(',', ''))
        m = re.search(r"(\S+)\s+cycles", line)
        if m:
            enc_cycles = int(m.group(1).replace(',', ''))
        m = re.search(r"(\S+)\s+seconds\s+user", line)
        if m:
            enc_time = float(m.group(1))
    flog.close()

    flog = open(dec_perf, 'r')
    for line in flog:
        m = re.search(r"(\S+)\s+instructions", line)
        if m:
            dec_instr = int(m.group(1).replace(',', ''))
        m = re.search(r"(\S+)\s+cycles", line)
        if m:
            dec_cycles = int(m.group(1).replace(',', ''))
        m = re.search(r"(\S+)\s+seconds\s+user", line)
        if m:
            dec_time = float(m.group(1))
    flog.close()
    return enc_time, dec_time, enc_instr, dec_instr, enc_cycles, dec_cycles

def GatherPerfInfo(bsfile, Path_TimingLog):
    enc_perf = GetEncPerfFile(bsfile, Path_TimingLog)
    dec_perf = GetDecPerfFile(bsfile, Path_TimingLog)
    enc_time = 0.0; dec_time = 0.0
    flog = open(enc_perf, 'r')
    for line in flog:
        if Platform == "Windows":
            m = re.search(r"Execution time:\s+(\d+\.?\d*)", line)
        else:
            m = re.search(r"User time \(seconds\):\s+(\d+\.?\d*)", line)
        if m:
            enc_time = float(m.group(1))
    flog.close()

    flog = open(dec_perf, 'r')
    for line in flog:
        if Platform == "Windows":
            m = re.search(r"Execution time:\s+(\d+\.?\d*)", line)
        else:
            m = re.search(r"User time \(seconds\):\s+(\d+\.?\d*)", line)
        if m:
            dec_time = float(m.group(1))
    flog.close()
    return enc_time, dec_time

def GetEncPerfFile(bsfile, perfpath):
    filename = GetShortContentName(bsfile, False) + '_EncTime.txt'
    return os.path.join(perfpath, filename)

def GetDecPerfFile(bsfile, perfpath):
    filename = GetShortContentName(bsfile, False) + '_DecTime.txt'
    return os.path.join(perfpath, filename)

def GetRDResultCsvFile(EncodeMethod, CodecName, EncodePreset, test_cfg):
    filename = "RDResults_%s_%s_%s_Preset_%s.csv" % \
               (EncodeMethod, CodecName, test_cfg, EncodePreset)
    avg_file = os.path.join(Path_RDResults, filename)
    filename = "Perframe_RDResults_%s_%s_%s_Preset_%s.csv" % \
               (EncodeMethod, CodecName, test_cfg, EncodePreset)
    perframe_data = os.path.join(Path_RDResults, filename)
    return avg_file, perframe_data


def GatherPerframeStat(test_cfg,EncodeMethod,CodecName,EncodePreset,clip, name, width, height,
                       qp,enc_log,perframe_csv,perframe_vmaf_log):
    enc_list = [''] * len(perframe_vmaf_log)
    flog = open(enc_log, 'r')

    for line in flog:
        if line.startswith("POC"):
            #POC:     0 [ KEY ][Q:143]:      40272 Bytes, 1282.9ms, 36.5632 dB(Y), 45.1323 dB(U), 46.6284 dB(V), 38.0736 dB(Avg)    [  0,  0,  0,  0,  0,  0,  0,]
            m = re.search(r"POC:\s+(\d+)\s+\[( KEY |INTER)\]\[Level:(\d+)\]\[Q:\s*(\d+)\]:\s+(\d+)\s+Bytes,",line)
            if m:
                POC = m.group(1)
                frame_type = m.group(2)
                pyd_level = m.group(3)
                qindex = m.group(4)
                frame_size = m.group(5)
                if enc_list[int(POC)] == '':
                    enc_list[int(POC)] = "%s,%s,%s,%s,%s"%(POC,frame_type,pyd_level,qindex,frame_size)

    for i in range(len(enc_list)):
        #"TestCfg,EncodeMethod,CodecName,EncodePreset,Class,Name,Res,FPS,BitDepth,QP,POC,FrameType,Level,qindex,FrameSize")
        perframe_csv.write("%s,%s,%s,%s,%s,%s,%s,%s,%d,%d,%s,%s\n"
                           %(test_cfg,EncodeMethod,CodecName,EncodePreset,clip.file_class,name,str(clip.width)+"x"+str(clip.height),
                             clip.fps,clip.bit_depth,qp,enc_list[i],perframe_vmaf_log[i]))


def plot_rd_curve(br, qty, qty_str, name, x_label, line_color=None,
                  line_style=None, marker_format=None):
    # generate samples between max and min of quality metrics
    brqtypairs = []
    for i in range(min(len(qty), len(br))):
        brqtypairs.append((br[i], qty[i]))
    brqtypairs.sort(key = itemgetter(0, 1))
    new_br = [brqtypairs[i][0] for i in range(len(brqtypairs))]
    new_qty = [brqtypairs[i][1] for i in range(len(brqtypairs))]
    '''
    min_br = min(new_br)
    max_br = max(new_br)
    lin = np.linspace(min_br, max_br, num=100, retstep=True)
    samples = lin[0]
    v = scipy.interpolate.pchip_interpolate(new_br, new_qty, samples)
    plt.plot(samples, v, linestyle=line_style, color=line_color)
    plt.scatter(new_br, new_qty, color=line_color, marker=marker_format)
    '''
    plt.plot(new_br, new_qty, linestyle=line_style, color=line_color)
    plt.scatter(new_br, new_qty, color=line_color, marker=marker_format, label=name)
    plt.xlabel(x_label)
    plt.ylabel(qty_str)

def Interpolate_Bilinear(RDPoints, QPs, InterpolatePieces, logBr=True):
    '''
    generate interpolated points on a RD curve.
    input is list of existing RD points as (bitrate, quality) tuple
    total number of interpolated points depends on the min and max QP
    '''
    # sort the pair based on bitrate in decreasing order
    # if bitrate is the same, then sort based on quality in increasing order
    RDPoints.sort(key=itemgetter(0, 1), reverse=True)
    # sort QPs in decreasing order
    #QPs.sort(reverse=True)
    int_points = []

    for i in range(1, len(QPs)):
        # generate samples for each segement
        br = [RDPoints[i - 1][0], RDPoints[i][0]]
        qty = [RDPoints[i - 1][1], RDPoints[i][1]]
        if logBr:
            br = [math.log10(br[i]) for i in range(len(br))]

        # slope is negative
        qty_slope = (qty[1] - qty[0]) / InterpolatePieces
        br_slope = (br[1] - br[0]) / InterpolatePieces
        for j in range(0, InterpolatePieces):
            int_br = br[0] + j * br_slope
            int_br = pow(10, int_br)
            int_qty = qty[0] + j * qty_slope
            int_points += [(int_br, int_qty)]

    # add the last rd points from the input
    int_points += [(RDPoints[-1][0], RDPoints[-1][1])]
    int_points = [(round(int_points[i][0], 6), round(int_points[i][1], 6)) for i in range(len(int_points))]

    '''
    print("before interpolation:")
    for i in range(len(br)):
        print("%f, %f"%(br[i], qty[i]))
    print("after interpolation:")
    for i in range(len(int_points)):
        print("%f, %f"%(int_points[i][0], int_points[i][1]))

    result = all(elem in int_points for elem in RDPoints)
    if result:
        print("Yes, Interpolation contains all elements in the input")
    else:
        print("No, Interpolation does not contain all elements in the input")
    '''
    return int_points

def Interpolate_PCHIP(RDPoints, QPs, InterpolatePieces, logBr=True):
    '''
    generate interpolated points on a RD curve.
    input is list of existing RD points as (bitrate, quality) tuple
    total number of interpolated points depends on the min and max QP
    this version interpolate over the bitrate and quality range piece by
    piece, so all input RD data points are guaranteed in the output
    '''
    # sort the pair based on bitrate in increasing order
    # if bitrate is the same, then sort based on quality in increasing order
    RDPoints.sort(key = itemgetter(0, 1))
    br = [RDPoints[i][0] for i in range(len(RDPoints))]
    if logBr:
        br = [math.log10(br[i]) for i in range(len(br))]
    qty = [RDPoints[i][1] for i in range(len(RDPoints))]
    # sort QPs in decreasing order
    QPs.sort(reverse=True)
    int_points = []

    for i in range(1, len(QPs)):
        # generate samples between max and min of quality metrics
        lin = np.linspace(br[i-1], br[i], num = InterpolatePieces, retstep = True)
        int_br = lin[0]

        # interpolation using pchip
        int_qty = scipy.interpolate.pchip_interpolate(br, qty, int_br)
        int_points += [(pow(10, int_br[i]), int_qty[i]) for i in range(len(int_br) - 1)]
    # add the last rd points from the input
    int_points += [(pow(10, br[-1]), qty[-1])]

    int_points = [(round(int_points[i][0], 6), round(int_points[i][1], 6)) for i in range(len(int_points))]

    '''
    print("before interpolation:")
    for i in range(len(br)):
        print("%f, %f"%(br[i], qty[i]))
    print("after interpolation:")
    for i in range(len(int_points)):
        print("%f, %f"%(int_points[i][0], int_points[i][1]))

    result = all(elem in int_points for elem in RDPoints)
    if result:
        print("Yes, Interpolation contains all elements in the input")
    else:
        print("No, Interpolation does not contain all elements in the input")
    '''
    return int_points

def Interpolate_PCHIP1(RDPoints, QPs, InterpolatePieces, logBr=True):
    '''
    generate interpolated points on a RD curve.
    input is list of existing RD points as (bitrate, quality) tuple
    total number of interpolated points depends on the min and max QP
    '''
    # sort the pair based on bitrate in increasing order
    # if bitrate is the same, then sort based on quality in increasing order
    RDPoints.sort(key = itemgetter(0, 1))
    br = [RDPoints[i][0] for i in range(len(RDPoints))]
    if logBr:
        br = [math.log10(br[i]) for i in range(len(br))]
    qty = [RDPoints[i][1] for i in range(len(RDPoints))]

    # generate samples between max and min of quality metrics
    min_br = min(br); max_br = max(br)
    num_points = (len(QPs) - 1) * InterpolatePieces + 1
    lin = np.linspace(min_br, max_br, num = num_points, retstep = True)
    int_br = lin[0]

    # interpolation using pchip
    int_qty = scipy.interpolate.pchip_interpolate(br, qty, int_br)

    int_points = [(pow(10, int_br[i]), int_qty[i]) for i in range(len(int_br))]
    int_points = [(round(int_points[i][0], 6), round(int_points[i][1], 6)) for i in range(len(int_points))]

    '''
    print("before interpolation:")
    for i in range(len(br)):
        print("%f, %f"%(br[i], qty[i]))
    print("after interpolation:")
    for i in range(len(int_points)):
        print("%f, %f"%(int_points[i][0], int_points[i][1]))

    result = all(elem in int_points for elem in RDPoints)
    if result:
        print("Yes, Interpolation contains all elements in the input")
    else:
        print("No, Interpolation does not contain all elements in the input")
    '''
    return int_points

'''
The convex_hull function is adapted based on the original python implementation
from https://en.wikibooks.org/wiki/Algorithm_Implementation/Geometry/Convex_hull/Monotone_chain
It is changed to return the lower and upper portions of the convex hull separately
to get the convex hull based on traditional rd curve, only the upper portion is
needed.
'''

def convex_hull(points):
    """Computes the convex hull of a set of 2D points.
    Input: an iterable sequence of (x, y) pairs representing the points.
    Output: a list of vertices of the convex hull in counter-clockwise order,
      starting from the vertex with the lexicographically smallest coordinates.
    Implements Andrew's monotone chain algorithm. O(n log n) complexity.
    """

    # Sort the points lexicographically (tuples are compared lexicographically).
    # Remove duplicates to detect the case we have just one unique point.
    points = sorted(set(points))

    # Boring case: no points or a single point, possibly repeated multiple times.
    if len(points) <= 1:
        return points

    # 2D cross product of OA and OB vectors, i.e. z-component of their 3D cross
    # product. Returns a positive value, if OAB makes a counter-clockwise turn,
    # negative for clockwise turn, and zero if the points are collinear.
    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    # Build lower hull
    lower = []
    for p in points:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)

    # Build upper hull
    upper = []
    for p in reversed(points):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)

    return lower, upper

def ConvertY4MToYUV(clip, yuv_file, LogCmdOnly=False):
    cmd = FFMPEG + " -i %s %s"%(clip.file_path, yuv_file)
    ExecuteCmd(cmd, LogCmdOnly)

def ConvertYUVToY4M(clip, yuv_file, y4m_file, LogCmdOnly=False):
    if clip.fmt == '420' and clip.bit_depth == 8:
        pix_fmt = "yuv420p"
    elif clip.fmt == '420' and clip.bit_depth == 10:
        pix_fmt = "yuv420p10le"
    else:
        print("Unsupported color format")

    cmd = FFMPEG + " -s %dx%d -r %d -pix_fmt %s -i %s " \
          % (clip.width, clip.height, clip.fps, pix_fmt, yuv_file)

    if clip.fmt == '420' and clip.bit_depth == 10:
        cmd += " -strict -1 "

    cmd += y4m_file

    ExecuteCmd(cmd, LogCmdOnly)

'''
######################################
# main
######################################
if __name__ == "__main__":

    reslutions = ["2160p","1440p","1080p","720p","540p","360p"]

    v100 = {
        "2160p" :[(28124.3888,42.551087), (13410.6054,41.963043),(6624.8041,41.253859),
                  (3708.8954,40.269257), (2267.0327,38.851295), (1202.0951,36.555146)],
        "1440p" :[(13059.8067,41.676595),(7204.4079,41.138107), (3991.3772,40.326393),
                  (2421.4641,39.111245), (1480.2871,37.463730), (776.6609,34.972284)],
        "1080p" :[(8636.0310,40.758537), (4963.3505,40.201735),(2942.9205,39.319614),
                  (1794.6165,38.008143), (1100.3667,36.289067), (572.5143,33.780085)],
        "720p"  :[(5014.1575,38.466380), (3109.7014,37.982144), (1908.0157,37.170517),
                  (1169.5947,35.960981), (709.4616,34.320416), (367.4529,31.955818)],
        "540p"  :[(3529.1047,36.319288), (2226.4025,35.926225), (1377.1422,35.258939),
                  (847.5488,34.235784),  (514.2378,32.783286), (264.8687,30.603150)],
        "360p"  :[(2074.6269,33.283164), (1337.9322,33.014666), (837.4124,32.539225),
                  (518.8043,31.7650240), (317.5901,30.611798), (163.9739,28.715523)],
    }

    ext_quant = {
        "2160p" :[(21904.5767,42.352874), (8260.0464,41.485785), (3484.7011,40.150674),
                  (1721.3162,38.029638), (840.7506,35.148980), (403.3394,31.916100)],
        "1440p" :[(11079.3354,41.531791), (4732.6200,40.600643), (2264.0707,38.974245),
                  (1123.4501,36.561528), (535.7830,33.499672), (247.1633,30.225781)],
        "1080p" :[(7414.1059,40.608075), (3422.7434,39.617804), (1681.2615,37.863167),
                  (827.9659,35.367416), (392.5133,32.287467), (176.3639,29.046296)],
        "720p"  :[(4369.3869,38.339367), (2196.3292,37.445383), (1095.0071,35.819310),
                  (534.0604,33.443757), (249.4170,30.538937), (111.7762,27.447369)],
        "540p"  :[(3146.1818,36.233705), (1621.3030,35.523001), (805.4026,34.153004),
                  (395.6523,32.064279), (185.0912,29.359337), (84.0083,26.433119)],
        "360p"  :[(1852.7085,33.227620), (979.2103,32.730902), (493.6036,31.698794),
                  (244.7067,29.973807), (116.4755,27.604518), (54.5436,24.896702)],
    }

    Int_RDPoints = {}
    Int_RDPoints["v100"] = []; Int_RDPoints["ext_quant"] = []
    for res in reslutions:
        rdpnts = [(v100[res][i][0], v100[res][i][1]) for i in range(len(v100[res]))]
        if UsePCHIPInterpolation:
            int_rdpnts = Interpolate_PCHIP(rdpnts, QPs['AS'][:], InterpolatePieces, True)
        else:
            int_rdpnts = Interpolate_Bilinear(rdpnts, QPs['AS'][:], InterpolatePieces, True)
        Int_RDPoints["v100"] += int_rdpnts
        rdpnts = [(ext_quant[res][i][0], ext_quant[res][i][1]) for i in range(len(ext_quant[res]))]
        if UsePCHIPInterpolation:
            int_rdpnts = Interpolate_PCHIP(rdpnts, QPs['AS'][:], InterpolatePieces, True)
        else:
            int_rdpnts = Interpolate_Bilinear(rdpnts, QPs['AS'][:], InterpolatePieces, True)
        Int_RDPoints["ext_quant"] += int_rdpnts

    br = {}; psnr = {}
    lower, upper = convex_hull(Int_RDPoints["v100"])
    br["v100"] = [upper[i][0] for i in range(len(upper))]
    psnr["v100"] = [upper[i][1] for i in range(len(upper))]

    lower, upper = convex_hull(Int_RDPoints["ext_quant"])
    br["ext_quant"] = [upper[i][0] for i in range(len(upper))]
    psnr["ext_quant"] = [upper[i][1] for i in range(len(upper))]


    plt.figure(figsize=(15, 10))
    plot_rd_curve(br["v100"], psnr["v100"], "psnr_y", 'v1.0.0', "bitrate(Kbps)", 'b', '-', '*')
    plot_rd_curve(br["ext_quant"], psnr["ext_quant"], "psnr_y", 'ext-quant', "bitrate(Kbps)", 'r', '-', '+')

    plt.legend()
    plt.grid(True)
    plt.show()

    bdrate = BD_RATE("psnr_y", br["v100"], psnr["v100"], br["ext_quant"], psnr["ext_quant"])

    rdpoints = {
        "2160p" :[(37547.9659,43.9085),(19152.0922,42.5703),(9291.0302,41.048),
                  (4623.8611,39.3547),(2317.0762,37.4839),(1010.1394,35.2487)],
        "1440p" :[(19569.5627,42.2546),(10333.8803,41.05),(5206.9764,39.5806),
                  (2615.9834,37.8888),(1298.0177,36.0098),(562.8501,33.8222)],
        "1080p" :[(12487.7129,40.6077),(6690.0226,39.5905),(3427.771,38.2816),
                  (1724.92,36.701),(847.6557,34.9042),(369.607,32.8162)],
        "720p"  :[(6202.9626,37.2784),(3414.0641,36.6894),(1812.6317,35.8205),
                  (934.1797,34.6135),(457.374,33.0808),(203.929,31.2627)],
        "540p"  :[(3648.3578,34.7304),(2053.9891,34.375),(1121.7496,33.8025),
                  (590.8836,32.9133),(291.6739,31.6711),(135.4018,30.1146)],
        "360p"  :[(1677.5655,32.0908),(984.1863,31.8834),(554.9822,31.5193),
                  (299.4827,30.8819),(152.3105,29.9195),(76.5757,28.6167)],
    }
    formats = {
        "2160p": ['r', '-', 'o'],
        "1440p": ['b', '-', '+'],
        "1080p": ['g', '-', '*'],
        "720p" : ['c', '-', '.'],
        "540p" : ['r', '-', '^'],
        "360p" : ['b', '-', '<'],
    }
    plt.figure(figsize=(15, 10))

    print("Before Interpolation:")
    for res in reslutions:
        br   = [rdpoints[res][i][0] for i in range(len(rdpoints[res]))]
        psnr = [rdpoints[res][i][1] for i in range(len(rdpoints[res]))]
        plot_rd_curve(br, psnr, "psnr_y", res, "bitrate(Kbps)", formats[res][0],formats[res][1],formats[res][2])

    plt.legend()
    plt.grid(True)
    plt.show()

    print("Bilinear:")
    int_rdpoints = {}
    Int_RDPoints = []
    NumPoints = 0
    plt.figure(figsize=(15, 10))

    for res in reslutions:
        rdpnts = [(rdpoints[res][i][0], rdpoints[res][i][1]) for i in range(len(rdpoints[res]))]
        int_rdpnts = Interpolate_Bilinear(rdpoints[res], QPs['AS'][:])
        NumPoints += len(int_rdpnts)
        # print(rdpnts)
        # print(int_rdpnts)
        result = all(elem in int_rdpnts for elem in rdpnts)
        if result:
            print("Yes, Interpolation contains all elements in the input")
        else:
            print("No, Interpolation does not contain all elements in the input")
        int_rdpoints[res] = int_rdpnts
        Int_RDPoints += int_rdpnts
        br = [int_rdpoints[res][i][0] for i in range(len(int_rdpoints[res]))]
        psnr = [int_rdpoints[res][i][1] for i in range(len(int_rdpoints[res]))]
        plot_rd_curve(br, psnr, "psnr_y", res, "bitrate(Kbps)", formats[res][0], formats[res][1], formats[res][2])

    print("Number of Interpolated points = %d" % NumPoints)

    plt.legend()
    plt.grid(True)
    plt.show()

    print("Convex Hull:")
    lower, upper = convex_hull(Int_RDPoints)
    br    = [upper[i][0] for i in range(len(upper))]
    psnr  = [upper[i][1] for i in range(len(upper))]
    print("Number of Convex Hull points = %d"%len(upper))
    print(upper)

    plt.figure(figsize=(15, 10))
    plot_rd_curve(br, psnr, "psnr_y", 'convex-hull', "bitrate(Kbps)", 'b', '-', '*')
    plt.legend()
    plt.grid(True)
    plt.show()
'''