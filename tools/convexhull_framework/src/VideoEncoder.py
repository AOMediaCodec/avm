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
import math
import Utils
from Config import AOMENC, AV1ENC, SVTAV1, EnableTimingInfo, Platform, UsePerfUtil, CTC_VERSION, HEVCCfgFile, \
     HMENC, EnableOpenGOP, GOP_SIZE, SUB_GOP_SIZE, EnableTemporalFilter
from Utils import ExecuteCmd, ConvertY4MToYUV, DeleteFile, GetShortContentName

def get_qindex_from_QP(QP):
    quantizer_to_qindex = [
    0,   4,   8,   12,  16,  20,  24,  28,  32,  36,  40,  44,  48,
    52,  56,  60,  64,  68,  72,  76,  80,  84,  88,  92,  96,  100,
    104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 148, 152,
    156, 160, 164, 168, 172, 176, 180, 184, 188, 192, 196, 200, 204,
    208, 212, 216, 220, 224, 228, 232, 236, 240, 244, 249, 255]
    if (QP > 63):
        print(" QP %d is out of range (0 to 63), clamp to 63", QP)
        return quantizer_to_qindex[63]
    return quantizer_to_qindex[QP]

def EncodeWithAOM_AV2(clip, test_cfg, QP, framenum, outfile, preset, enc_perf,
                      enc_log, LogCmdOnly=False):
    args = " --verbose --codec=av1 -v --psnr --obu --frame-parallel=0" \
           " --cpu-used=%s --limit=%d --passes=1 --end-usage=q --i%s " \
           " --use-fixed-qp-offsets=1 --deltaq-mode=0 " \
           " --enable-tpl-model=0 --fps=%d/%d -w %d -h %d" \
           % (preset, framenum, clip.fmt, clip.fps_num, clip.fps_denom, clip.width, clip.height)

    # config enoding bitdepth
    if ((CTC_VERSION in ['6.0']) and (clip.file_class in ['A2', 'A4', 'B1'])):
        # CWG-D088
        args += " --input-bit-depth=%d --bit-depth=10" % (clip.bit_depth)
    else:
        args += " --input-bit-depth=%d --bit-depth=%d" % (clip.bit_depth, clip.bit_depth)

    if CTC_VERSION in ['2.0', '3.0', '4.0', '5.0', '6.0']:
        args += " --qp=%d" % QP
    else:
        args += " --use-16bit-internal --cq-level=%d" % QP

    # For 4K clip, encode with 2 tile columns using two threads.
    # --tile-columns value is in log2.
    if (clip.width >= 3840 and clip.height >= 2160):
        args += " --tile-columns=1 --threads=2 --row-mt=0 "
    elif ((CTC_VERSION in ['4.0']) and (clip.file_class in ['A2', 'B1']) and (test_cfg == "LD")):
        args += " --tile-rows=1 --threads=2 --row-mt=0 "
    elif ((CTC_VERSION in ['5.0', '6.0']) and (clip.file_class in ['A2', 'B1']) and (test_cfg == "LD")):
        args += " --tile-rows=1 --tile-columns=1 --threads=4 --row-mt=0 "
    elif ((CTC_VERSION in ['6.0']) and (clip.file_class in ['E', 'G1']) and (test_cfg == "RA")):
        args += " --tile-rows=1 --tile-columns=1 --threads=4 --row-mt=0 "
    else:
        args += " --tile-columns=0 --threads=1 "

    if EnableOpenGOP:
        args += " --enable-fwd-kf=1 "
    else:
        args += " --enable-fwd-kf=0 "

    if EnableTemporalFilter:
        args += " --enable-keyframe-filtering=1 "
    else:
        args += " --enable-keyframe-filtering=0 "

    # CWG-D082
    if CTC_VERSION in ['6.0']:
        if clip.file_class in ['B2']:
            args += " --tune-content=screen"
        else:
            args += " --enable-intrabc-ext=2"

    if test_cfg == "AI" or test_cfg == "STILL":
        args += " --kf-min-dist=0 --kf-max-dist=0 "
    elif test_cfg == "RA" or test_cfg == "AS":
        args += " --min-gf-interval=%d --max-gf-interval=%d --gf-min-pyr-height=%d" \
                " --gf-max-pyr-height=%d --kf-min-dist=%d --kf-max-dist=%d" \
                " --lag-in-frames=%d --auto-alt-ref=1 " % \
                (SUB_GOP_SIZE, SUB_GOP_SIZE, math.log2(SUB_GOP_SIZE), math.log2(SUB_GOP_SIZE),
                 GOP_SIZE, GOP_SIZE, SUB_GOP_SIZE + 3)
    elif test_cfg == "LD":
        args += " --kf-min-dist=9999 --kf-max-dist=9999 --lag-in-frames=0" \
                " --min-gf-interval=%d --max-gf-interval=%d --gf-min-pyr-height=%d " \
                " --gf-max-pyr-height=%d --subgop-config-str=ld " \
                % (SUB_GOP_SIZE, SUB_GOP_SIZE, math.log2(SUB_GOP_SIZE), math.log2(SUB_GOP_SIZE))
    else:
        print("Unsupported Test Configuration %s" % test_cfg)

    if (clip.file_class == 'G1' or clip.file_class == 'G2'):
        args += "--color-primaries=bt2020 --transfer-characteristics=smpte2084 "\
                "--matrix-coefficients=bt2020ncl --chroma-sample-position=colocated "

    args += " -o %s %s" % (outfile, clip.file_path)
    cmd = AOMENC + args + "> %s 2>&1"%enc_log
    if (EnableTimingInfo):
        if Platform == "Windows":
            cmd = "ptime " + cmd + " >%s"%enc_perf
        elif Platform == "Darwin":
            cmd = "gtime --verbose --output=%s "%enc_perf + cmd
        else:
            if UsePerfUtil:
                cmd = "3>%s perf stat --log-fd 3 " % enc_perf + cmd
            else:
                cmd = "/usr/bin/time --verbose --output=%s "%enc_perf + cmd
    ExecuteCmd(cmd, LogCmdOnly)

def EncodeWithAOM_AV1(clip, test_cfg, QP, framenum, outfile, preset, enc_perf,
                      enc_log, LogCmdOnly=False):
    args = " --verbose --codec=av1 -v --psnr --obu --frame-parallel=0" \
           " --cpu-used=%s --limit=%d --passes=1 --end-usage=q --i%s " \
           " --use-fixed-qp-offsets=1 --deltaq-mode=0 " \
           " --enable-tpl-model=0 --fps=%d/%d " \
           " --input-bit-depth=%d --bit-depth=%d --cq-level=%d -w %d -h %d" \
           % (preset, framenum, clip.fmt, clip.fps_num, clip.fps_denom,
              clip.bit_depth, clip.bit_depth, QP, clip.width, clip.height)

    # For 4K clip, encode with 2 tile columns using two threads.
    # --tile-columns value is in log2.
    if (clip.width >= 3840 and clip.height >= 2160):
        args += " --tile-columns=1 --threads=2 --row-mt=0 "
    else:
        args += " --tile-columns=0 --threads=1 "

    if EnableTemporalFilter:
        args += " --enable-keyframe-filtering=1 "
    else:
        args += " --enable-keyframe-filtering=0 "

    if test_cfg == "AI" or test_cfg == "STILL":
        args += " --kf-min-dist=0 --kf-max-dist=0 "
    elif test_cfg == "RA" or test_cfg == "AS":
        if EnableOpenGOP:
            args += " --fwd-kf-dist=%d " % (GOP_SIZE)
        else:
            args += " --kf-min-dist=%d --kf-max-dist=%d" % (GOP_SIZE, GOP_SIZE)

        args += " --min-gf-interval=%d --max-gf-interval=%d --gf-min-pyr-height=%d" \
                " --gf-max-pyr-height=%d --lag-in-frames=%d --auto-alt-ref=1 " % \
                (SUB_GOP_SIZE, SUB_GOP_SIZE, math.log2(SUB_GOP_SIZE), math.log2(SUB_GOP_SIZE),
                 SUB_GOP_SIZE + 3)
    elif test_cfg == "LD":
        args += " --kf-min-dist=9999 --kf-max-dist=9999 --lag-in-frames=0" \
                " --min-gf-interval=%d --max-gf-interval=%d --gf-min-pyr-height=%d " \
                " --gf-max-pyr-height=%d --subgop-config-str=ld " \
                % (SUB_GOP_SIZE, SUB_GOP_SIZE, math.log2(SUB_GOP_SIZE), math.log2(SUB_GOP_SIZE))
    else:
        print("Unsupported Test Configuration %s" % test_cfg)

    if (clip.file_class == 'G1' or clip.file_class == 'G2'):
        args += "--color-primaries=bt2020 --transfer-characteristics=smpte2084 "\
                "--matrix-coefficients=bt2020ncl --chroma-sample-position=colocated "

    args += " -o %s %s" % (outfile, clip.file_path)
    cmd = AV1ENC + args + "> %s 2>&1"%enc_log
    if (EnableTimingInfo):
        if Platform == "Windows":
            cmd = "ptime " + cmd + " >%s"%enc_perf
        elif Platform == "Darwin":
            cmd = "gtime --verbose --output=%s "%enc_perf + cmd
        else:
            if UsePerfUtil:
                cmd = "3>%s perf stat --log-fd 3 " % enc_perf + cmd
            else:
                cmd = "/usr/bin/time --verbose --output=%s "%enc_perf + cmd
    ExecuteCmd(cmd, LogCmdOnly)

def EncodeWithSVT_AV1(clip, test_cfg, QP, framenum, outfile, preset, enc_perf,
                      enc_log, LogCmdOnly=False):
    #TODO: update svt parameters
    # -enable-tpl-la 0 to disable the content based per layer QP adjustment(i.e.use
    # fixed offsets @ QP scaling ), and the content based per block QP adjustment(i.e.TPL
    # OFF).
    args = " --preset %s --scm 2 --lookahead 0 -n %d " \
           " --rc 0 -q %d -w %d -h %d  --fps-num %d " \
           " --fps-denom %d --input-depth %d " \
           " --adaptive-quantization 0 --enable-tpl-la 0" \
           % (str(preset), framenum, QP, clip.width, clip.height,
              clip.fps_num, clip.fps_denom, clip.bit_depth)

    if EnableOpenGOP:
        args += " --irefresh-type 1"
    else:
        args += " --irefresh-type 2"

    # For 4K clip, encode with 2 tile columns using two threads.
    # --tile-columns value is in log2.
    if (clip.width >= 3840 and clip.height >= 2160):
        args += " --tile-columns 1 "
    else:
        args += " --tile-columns 0 "

    if test_cfg == "AI" or test_cfg == "STILL":
        args += " --keyint 255 "
    elif test_cfg == "RA" or test_cfg == "AS":
        args += " --keyint %d --hierarchical-levels %d --pred-struct 2 " \
                % (GOP_SIZE-1, math.log2(SUB_GOP_SIZE))
    elif test_cfg == "LD":
        args += " --keyint 9999 --hierarchical-levels %d --pred-struct 1 " \
                % math.log2(SUB_GOP_SIZE)
    else:
        print("Unsupported Test Configuration %s" % test_cfg)

    if (clip.file_class == 'G1' or clip.file_class == 'G2'):
        args += "--enable-hdr 1 "

    args += "-i %s -b %s"%(clip.file_path,  outfile)
    cmd = SVTAV1 + args + "> %s 2>&1"%enc_log
    if EnableTimingInfo:
        if Platform == "Windows":
            cmd = "ptime " + cmd + " >%s"%enc_perf
        elif Platform == "Darwin":
            cmd = "gtime --verbose --output=%s "%enc_perf + cmd
        else:
            if UsePerfUtil:
                cmd = "3>%s perf stat --log-fd 3 " % enc_perf + cmd
            else:
                cmd = "/usr/bin/time --verbose --output=%s "%enc_perf + cmd
    ExecuteCmd(cmd, LogCmdOnly)


def EncodeWithHM_HEVC(clip, test_cfg, QP, framenum, outfile, preset, enc_perf,
                      enc_log, LogCmdOnly=False):
    input_yuv_file = GetShortContentName(outfile, False) + ".yuv"
    bs_path = os.path.dirname(outfile)
    input_yuv_file = os.path.join(bs_path, input_yuv_file)
    ConvertY4MToYUV(clip, input_yuv_file, LogCmdOnly)

    args = " -c %s -i %s -b %s --SourceWidth=%d --SourceHeight=%d --InputBitDepth=%d --InternalBitDepth=%d " \
           " --InputChromaFormat=420 --FrameRate=%d --GOPSize=%d --FramesToBeEncoded=%d --QP=%d " \
           % (HEVCCfgFile, input_yuv_file, outfile, clip.width, clip.height, clip.bit_depth, clip.bit_depth,
              clip.fps, SUB_GOP_SIZE, framenum, QP)

    args += " --ConformanceWindowMode=1 " #needed to support non multiple of 8 resolutions.

    #enable open Gop
    if EnableOpenGOP:
        args += " --DecodingRefreshType=1 "
    else:
        args += " --DecodingRefreshType=2 "

    if EnableTemporalFilter:
        args += " --TemporalFilter=1 "
    else:
        args += " --TemporalFilter=0 "

    if test_cfg == "AI" or test_cfg == "STILL":
        args += " --IntraPeriod=1 "
    elif test_cfg == "RA" or test_cfg == "AS":
        args += " --IntraPeriod=%d " % GOP_SIZE
    elif test_cfg == "LD":
        args += " --IntraPeriod=-1 "
    else:
        print("Unsupported Test Configuration %s" % test_cfg)

    cmd = HMENC + args + "> %s 2>&1"%enc_log
    if (EnableTimingInfo):
        if Platform == "Windows":
            cmd = "ptime " + cmd + " >%s"%enc_perf
        elif Platform == "Darwin":
            cmd = "gtime --verbose --output=%s "%enc_perf + cmd
        else:
            if UsePerfUtil:
                cmd = "3>%s perf stat --log-fd 3 " % enc_perf + cmd
            else:
                cmd = "/usr/bin/time --verbose --output=%s "%enc_perf + cmd
    ExecuteCmd(cmd, LogCmdOnly)

    DeleteFile(input_yuv_file, LogCmdOnly)

def VideoEncode(EncodeMethod, CodecName, clip, test_cfg, QP, framenum, outfile,
                preset, enc_perf, enc_log, LogCmdOnly=False):
    Utils.CmdLogger.write("::Encode\n")
    if CodecName == 'av2':
        if EncodeMethod == "aom":
            EncodeWithAOM_AV2(clip, test_cfg, QP, framenum, outfile, preset,
                              enc_perf, enc_log, LogCmdOnly)
    elif CodecName == 'av1':
        if EncodeMethod == 'aom':
            EncodeWithAOM_AV1(clip, test_cfg, QP, framenum, outfile, preset,
                              enc_perf, enc_log, LogCmdOnly)
        elif EncodeMethod == "svt":
            EncodeWithSVT_AV1(clip, test_cfg, QP, framenum, outfile, preset,
                              enc_perf, enc_log, LogCmdOnly)
        else:
            raise ValueError("invalid parameter for encode.")
    elif CodecName == 'hevc':
        if EncodeMethod == 'hm':
            EncodeWithHM_HEVC(clip, test_cfg, QP, framenum, outfile, preset,
                              enc_perf, enc_log, LogCmdOnly)
        else:
            raise ValueError("invalid parameter for encode.")
    else:
        raise ValueError("invalid parameter for encode.")
