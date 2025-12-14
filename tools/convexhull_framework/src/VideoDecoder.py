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
import Utils
from Config import AVMDEC, AV2DEC, EnableTimingInfo, Platform, UsePerfUtil, FFMPEG
from Utils import ExecuteCmd, GetShortContentName, ConvertYUVToY4M, DeleteFile

def DecodeWithAVM(test_cfg, infile, outfile, dec_perf, decode_to_yuv, dec_log, LogCmdOnly=False):
    if decode_to_yuv:
        args = " --codec=av2 --summary --rawvideo -o %s %s" % (outfile, infile)
    else:
        args = " --codec=av2 --summary -o %s %s" % (outfile, infile)
    cmd = AVMDEC + args + "> %s 2>&1"%dec_log
    if EnableTimingInfo:
        if Platform == "Windows":
            cmd = "ptime " + cmd + " >%s"%dec_perf
        elif Platform == "Darwin":
            cmd = "gtime --verbose --output=%s "%dec_perf + cmd
        else:
            if UsePerfUtil:
                cmd = "3>%s perf stat --log-fd 3 "%dec_perf +cmd
            else:
                cmd = "/usr/bin/time --verbose --output=%s "%dec_perf + cmd

    ExecuteCmd(cmd, LogCmdOnly)

def DecodeWithAV2(test_cfg, infile, outfile, dec_perf, decode_to_yuv, dec_log, LogCmdOnly=False):
    if decode_to_yuv:
        args = " --codec=av2 --summary --rawvideo -o %s %s" % (outfile, infile)
    else:
        args = " --codec=av2 --summary -o %s %s" % (outfile, infile)
    cmd = AV2DEC + args + "> %s 2>&1"%dec_log
    if EnableTimingInfo:
        if Platform == "Windows":
            cmd = "ptime " + cmd + " >%s" % dec_perf
        elif Platform == "Darwin":
            cmd = "gtime --verbose --output=%s " % dec_perf + cmd
        else:
            if UsePerfUtil:
                cmd = "3>%s perf stat --log-fd 3 " % dec_perf + cmd
            else:
                cmd = "/usr/bin/time --verbose --output=%s " % dec_perf + cmd

    ExecuteCmd(cmd, LogCmdOnly)

def DecodeWithHEVC(test_cfg, clip, infile, outfile, dec_perf, decode_to_yuv, dec_log, LogCmdOnly=False):
    args = " -i %s " % infile
    if clip.fmt == '420' and clip.bit_depth == 8:
        pix_fmt = "yuv420p"
    elif clip.fmt == '420' and clip.bit_depth == 10:
        pix_fmt = "yuv420p10le -strict -1"
    else:
        print("Unsupported color format")

    args += " -pix_fmt %s %s" % (pix_fmt, outfile)
    cmd = FFMPEG + args + "> %s 2>&1"%dec_log
    if EnableTimingInfo:
        if Platform == "Windows":
            cmd = "ptime " + cmd + " >%s" % dec_perf
        elif Platform == "Darwin":
            cmd = "gtime --verbose --output=%s " % dec_perf + cmd
        else:
            if UsePerfUtil:
                cmd = "3>%s perf stat --log-fd 3 " % dec_perf + cmd
            else:
                cmd = "/usr/bin/time --verbose --output=%s " % dec_perf + cmd

    ExecuteCmd(cmd, LogCmdOnly)

def VideoDecode(clip, method, test_cfg, codec, infile, outfile, dec_perf, decode_to_yuv, dec_log, LogCmdOnly=False):
    Utils.CmdLogger.write("::Decode\n")
    if codec == 'av2' and method == 'avm':
        DecodeWithAVM(test_cfg, infile, outfile, dec_perf, decode_to_yuv, dec_log, LogCmdOnly)
    elif codec == 'av2':
        DecodeWithAV2(test_cfg, infile, outfile, dec_perf, decode_to_yuv, dec_log, LogCmdOnly)
    elif codec == 'hevc':
        DecodeWithHEVC(test_cfg, clip, infile, outfile, dec_perf, decode_to_yuv, dec_log, LogCmdOnly)
    else:
        raise ValueError("invalid parameter for decode.")
