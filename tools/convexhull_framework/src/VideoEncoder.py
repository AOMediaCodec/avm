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

import math
import os

import Utils
from Config import (
    AVMENC,
    AV2ENC,
    CTC_VERSION,
    EnableOpenGOP,
    EnableTemporalFilter,
    EnableTimingInfo,
    EnableVerificationTestConfig,
    GOP_SIZE,
    HEVCCfgFile,
    HMENC,
    Platform,
    SUB_GOP_SIZE,
    SVTAV2,
    UsePerfUtil,
)
from Utils import ConvertY4MToYUV, DeleteFile, ExecuteCmd, GetShortContentName


def get_qindex_from_QP(QP):
    quantizer_to_qindex = [
        0,
        4,
        8,
        12,
        16,
        20,
        24,
        28,
        32,
        36,
        40,
        44,
        48,
        52,
        56,
        60,
        64,
        68,
        72,
        76,
        80,
        84,
        88,
        92,
        96,
        100,
        104,
        108,
        112,
        116,
        120,
        124,
        128,
        132,
        136,
        140,
        144,
        148,
        152,
        156,
        160,
        164,
        168,
        172,
        176,
        180,
        184,
        188,
        192,
        196,
        200,
        204,
        208,
        212,
        216,
        220,
        224,
        228,
        232,
        236,
        240,
        244,
        249,
        255,
    ]
    if QP > 63:
        print(" QP %d is out of range (0 to 63), clamp to 63", QP)
        return quantizer_to_qindex[63]
    return quantizer_to_qindex[QP]


def EncodeWithAVM_AV2(
    clip,
    test_cfg,
    QP,
    framenum,
    outfile,
    preset,
    enc_perf,
    enc_log,
    start_frame=0,
    LogCmdOnly=False,
):
    args = (
        " --verbose --codec=av2 -v --psnr --obu --frame-parallel=0"
        " --cpu-used=%s --limit=%d --skip=%d --passes=1 --end-usage=q --i%s "
        " --use-fixed-qp-offsets=1 --deltaq-mode=0 "
        " --enable-tpl-model=0 --fps=%d/%d -w %d -h %d"
        % (
            preset,
            framenum,
            start_frame,
            clip.fmt,
            clip.fps_num,
            clip.fps_denom,
            clip.width,
            clip.height,
        )
    )

    # config enoding bitdepth
    if (CTC_VERSION in ["6.0", "7.0", "8.0"]) and (
        clip.file_class in ["A2", "A4", "B1"]
    ):
        # CWG-D088
        args += " --input-bit-depth=%d --bit-depth=10" % (clip.bit_depth)
    else:
        args += " --input-bit-depth=%d --bit-depth=%d" % (
            clip.bit_depth,
            clip.bit_depth,
        )

    if CTC_VERSION in ["2.0", "3.0", "4.0", "5.0", "6.0", "7.0", "8.0"]:
        args += " --qp=%d" % QP
    else:
        args += " --use-16bit-internal --cq-level=%d" % QP

    # For 4K clip, encode with 2 tile columns using two threads.
    # --tile-columns value is in log2.
    if (
        (CTC_VERSION in ["4.0"])
        and (clip.file_class in ["A2", "B1"])
        and (test_cfg == "LD")
    ):
        args += " --tile-rows=1 --threads=2 --row-mt=0 "
    elif (
        (CTC_VERSION in ["5.0", "6.0"])
        and (clip.file_class in ["A2", "B1"])
        and (test_cfg == "LD")
    ):
        args += " --tile-rows=1 --tile-columns=1 --threads=4 --row-mt=0 "
    elif (
        (CTC_VERSION in ["6.0"])
        and (clip.file_class in ["E", "G1"])
        and (test_cfg == "RA")
    ):
        args += " --tile-rows=1 --tile-columns=1 --threads=4 --row-mt=0 "
    elif CTC_VERSION in ["7.0", "8.0"]:
        if EnableVerificationTestConfig:
            args += " --tile-rows=0 --tile-columns=0 --threads=1 --row-mt=0 "
        elif test_cfg == "RA":
            if clip.file_class in ["A1", "E", "G1"]:
                # 4 column tiles should be used
                args += " --tile-rows=0 --tile-columns=2 --threads=4 --row-mt=0 "
            elif clip.file_class in ["A2", "B1"]:
                # 2 column tiles should be used
                args += " --tile-rows=0 --tile-columns=1 --threads=2 --row-mt=0 "
            else:
                # 1 tile should be used
                args += " --tile-rows=0 --tile-columns=0 --threads=1 --row-mt=0 "
        elif test_cfg == "AS":
            # use the same configuration as RA
            if (clip.width == 3840 and clip.height == 2160) or (
                clip.width == 2560 and clip.height == 1440
            ):
                # 4 column tiles should be used
                args += " --tile-rows=0 --tile-columns=2 --threads=4 --row-mt=0 "
            elif clip.width == 1920 and clip.height == 1080:
                # 2 column tiles should be used
                args += " --tile-rows=0 --tile-columns=1 --threads=2 --row-mt=0 "
            else:
                # 1 tile should be used
                args += " --tile-rows=0 --tile-columns=0 --threads=1 --row-mt=0 "
        elif test_cfg == "LD":
            if clip.file_class in ["A2", "B1"]:
                # 8 tiles should be used
                args += " --tile-rows=1 --tile-columns=2 --threads=8 --row-mt=0 "
            elif clip.file_class in ["A3"]:
                # 2 column tiles should be used
                args += " --tile-rows=0 --tile-columns=1 --threads=2 --row-mt=0 "
            else:
                # 1 tile should be used
                args += " --tile-rows=0 --tile-columns=0 --threads=1 --row-mt=0 "
        elif (test_cfg in ["AI", "STILL"]) and (
            clip.width >= 3840 and clip.height >= 2160
        ):
            # 2 tiles should be used
            args += " --tile-rows=0 --tile-columns=1 --threads=2 --row-mt=0 "
    elif clip.width >= 3840 and clip.height >= 2160:
        args += " --tile-rows=0 --tile-columns=1 --threads=2 --row-mt=0 "
    else:
        args += " --tile-rows=0 --tile-columns=0 --threads=1 --row-mt=0 "

    if EnableOpenGOP:
        args += " --enable-fwd-kf=1 "
    else:
        args += " --enable-fwd-kf=0 "

    if EnableTemporalFilter:
        args += " --enable-keyframe-filtering=1 "
    else:
        args += " --enable-keyframe-filtering=0 "

    # CWG-D082, CWG-F222
    if CTC_VERSION in ["8.0"]:
        if clip.file_class in ["B2"]:
            args += " --tune-content=screen --enable-intrabc-ext=1"
        elif clip.file_class in ["A4", "A5"]:
            args += " --enable-intrabc-ext=2"
        else:
            args += " --enable-intrabc-ext=1"
    elif CTC_VERSION in ["6.0", "7.0"]:
        if clip.file_class in ["B2"]:
            args += " --tune-content=screen --enable-intrabc-ext=1"
        else:
            args += " --enable-intrabc-ext=2"

    if test_cfg == "AI" or test_cfg == "STILL":
        args += " --kf-min-dist=0 --kf-max-dist=0 "
    elif test_cfg == "RA" or test_cfg == "AS":
        args += (
            " --min-gf-interval=%d --max-gf-interval=%d --gf-min-pyr-height=%d"
            " --gf-max-pyr-height=%d --kf-min-dist=%d --kf-max-dist=%d"
            " --lag-in-frames=%d --auto-alt-ref=1 "
            % (
                SUB_GOP_SIZE,
                SUB_GOP_SIZE,
                math.log2(SUB_GOP_SIZE),
                math.log2(SUB_GOP_SIZE),
                GOP_SIZE,
                GOP_SIZE,
                SUB_GOP_SIZE + 3,
            )
        )
    elif test_cfg == "LD":
        args += (
            " --kf-min-dist=9999 --kf-max-dist=9999 --lag-in-frames=0"
            " --min-gf-interval=%d --max-gf-interval=%d --gf-min-pyr-height=%d "
            " --gf-max-pyr-height=%d --subgop-config-str=ld "
            % (
                SUB_GOP_SIZE,
                SUB_GOP_SIZE,
                math.log2(SUB_GOP_SIZE),
                math.log2(SUB_GOP_SIZE),
            )
        )
    else:
        print("Unsupported Test Configuration %s" % test_cfg)

    if clip.file_class == "G1" or clip.file_class == "G2":
        args += " --color-primaries=bt2020 --transfer-characteristics=smpte2084 --matrix-coefficients=bt2020ncl"
        if CTC_VERSION in ["8.0"]:
            args += " --chroma-sample-position=topleft "
        else:
            args += " --chroma-sample-position=colocated "

    args += " -o %s %s" % (outfile, clip.file_path)
    cmd = AVMENC + args + "> %s 2>&1" % enc_log
    if EnableTimingInfo:
        if Platform == "Windows":
            cmd = "ptime " + cmd + " >%s" % enc_perf
        elif Platform == "Darwin":
            cmd = "gtime --verbose --output=%s " % enc_perf + cmd
        else:
            if UsePerfUtil:
                cmd = "3>%s perf stat --log-fd 3 " % enc_perf + cmd
            else:
                cmd = "/usr/bin/time --verbose --output=%s " % enc_perf + cmd
    ExecuteCmd(cmd, LogCmdOnly)


# encode with libavm to achieve highest coding gain
def EncodeWithAVM_AV2_Unconstrained(
    clip,
    test_cfg,
    QP,
    framenum,
    outfile,
    preset,
    enc_perf,
    enc_log,
    start_frame=0,
    LogCmdOnly=False,
):
    args = (
        " --verbose --codec=av2 --profile=0 -v --psnr --obu --frame-parallel=0"
        " --cpu-used=%s --limit=%d --skip=%d --passes=1 --end-usage=q --min-q=0 --max-q=63 --i%s "
        "  --cq-level=%d --enable-tpl-model=1 --fps=%d/%d -w %d -h %d"
        % (
            preset,
            framenum,
            start_frame,
            clip.fmt,
            QP,
            clip.fps_num,
            clip.fps_denom,
            clip.width,
            clip.height,
        )
    )

    # config enoding bitdepth
    if (CTC_VERSION in ["6.0", "7.0", "8.0"]) and (
        clip.file_class in ["A2", "A4", "B1"]
    ):
        # CWG-D088
        args += " --input-bit-depth=%d --bit-depth=10" % (clip.bit_depth)
    else:
        args += " --input-bit-depth=%d --bit-depth=%d" % (
            clip.bit_depth,
            clip.bit_depth,
        )

    # For 4K clip, encode with 2 tile columns using two threads.
    # --tile-columns value is in log2.
    if (
        (CTC_VERSION in ["4.0"])
        and (clip.file_class in ["A2", "B1"])
        and (test_cfg == "LD")
    ):
        args += " --tile-rows=1 --threads=2 --row-mt=0 "
    elif (
        (CTC_VERSION in ["5.0", "6.0"])
        and (clip.file_class in ["A2", "B1"])
        and (test_cfg == "LD")
    ):
        args += " --tile-rows=1 --tile-columns=1 --threads=4 --row-mt=0 "
    elif (
        (CTC_VERSION in ["6.0"])
        and (clip.file_class in ["E", "G1"])
        and (test_cfg == "RA")
    ):
        args += " --tile-rows=1 --tile-columns=1 --threads=4 --row-mt=0 "
    elif CTC_VERSION in ["7.0", "8.0"]:
        if EnableVerificationTestConfig:
            args += " --tile-rows=0 --tile-columns=0 --threads=0 --row-mt=0  "
            args += " --drop-frame=0 --static-thresh=0 --minsection-pct=0 --maxsection-pct=200 --arnr-maxframes=7 --arnr-strength=5 --sharpness=0 "
        elif test_cfg == "RA":
            if clip.file_class in ["A1", "E", "G1"]:
                # 4 column tiles should be used
                args += " --tile-rows=0 --tile-columns=2 --threads=4 --row-mt=0 "
            elif clip.file_class in ["A2", "B1"]:
                # 2 column tiles should be used
                args += " --tile-rows=0 --tile-columns=1 --threads=2 --row-mt=0 "
            else:
                # 1 tile should be used
                args += " --tile-rows=0 --tile-columns=0 --threads=1 --row-mt=0 "
        elif test_cfg == "AS":
            # use the same configuration as RA
            if (clip.width == 3840 and clip.height == 2160) or (
                clip.width == 2560 and clip.height == 1440
            ):
                # 4 column tiles should be used
                args += " --tile-rows=0 --tile-columns=2 --threads=4 --row-mt=0 "
            elif clip.width == 1920 and clip.height == 1080:
                # 2 column tiles should be used
                args += " --tile-rows=0 --tile-columns=1 --threads=2 --row-mt=0 "
            else:
                # 1 tile should be used
                args += " --tile-rows=0 --tile-columns=0 --threads=1 --row-mt=0 "
        elif test_cfg == "LD":
            if clip.file_class in ["A2", "B1"]:
                # 8 tiles should be used
                args += " --tile-rows=1 --tile-columns=2 --threads=8 --row-mt=0 "
            elif clip.file_class in ["A3"]:
                # 2 column tiles should be used
                args += " --tile-rows=0 --tile-columns=1 --threads=2 --row-mt=0 "
            else:
                # 1 tile should be used
                args += " --tile-rows=0 --tile-columns=0 --threads=1 --row-mt=0 "
        elif (test_cfg in ["AI", "STILL"]) and (
            clip.width >= 3840 and clip.height >= 2160
        ):
            # 2 tiles should be used
            args += " --tile-rows=0 --tile-columns=1 --threads=2 --row-mt=0 "
    elif clip.width >= 3840 and clip.height >= 2160:
        args += " --tile-rows=0 --tile-columns=1 --threads=2 --row-mt=0 "
    else:
        args += " --tile-rows=0 --tile-columns=0 --threads=1 --row-mt=0 "

    if EnableOpenGOP:
        args += " --enable-fwd-kf=1 "
    else:
        args += " --enable-fwd-kf=0 "

    if EnableTemporalFilter:
        args += " --enable-keyframe-filtering=1 "
    else:
        args += " --enable-keyframe-filtering=0 "

    if test_cfg == "AI" or test_cfg == "STILL":
        args += " --kf-min-dist=0 --kf-max-dist=0 "
    elif test_cfg == "RA" or test_cfg == "AS":
        args += (
            " --min-gf-interval=%d --max-gf-interval=%d --gf-min-pyr-height=%d"
            " --gf-max-pyr-height=%d --kf-min-dist=%d --kf-max-dist=%d"
            " --lag-in-frames=35 --auto-alt-ref=1 --passes=2 --undershoot-pct=100 --overshoot-pct=100 "
            % (
                SUB_GOP_SIZE,
                SUB_GOP_SIZE,
                math.log2(SUB_GOP_SIZE),
                math.log2(SUB_GOP_SIZE),
                GOP_SIZE,
                GOP_SIZE,
            )
        )
    elif test_cfg == "LD":
        args += (
            " --kf-min-dist=9999 --kf-max-dist=9999 --lag-in-frames=0"
            " --min-gf-interval=%d --max-gf-interval=%d --gf-min-pyr-height=%d "
            " --gf-max-pyr-height=%d --auto-alt-ref=1 --passes=1 --undershoot-pct=25 --overshoot-pct=25 "
            % (
                SUB_GOP_SIZE,
                SUB_GOP_SIZE,
                math.log2(SUB_GOP_SIZE),
                math.log2(SUB_GOP_SIZE),
            )
        )
    else:
        print("Unsupported Test Configuration %s" % test_cfg)

    if clip.file_class == "G1" or clip.file_class == "G2":
        args += (
            "--color-primaries=bt2020 --transfer-characteristics=smpte2084 "
            "--matrix-coefficients=bt2020ncl --chroma-sample-position=colocated "
        )

    args += " -o %s %s" % (outfile, clip.file_path)
    cmd = AV2ENC + args + "> %s 2>&1" % enc_log
    if EnableTimingInfo:
        if Platform == "Windows":
            cmd = "ptime " + cmd + " >%s" % enc_perf
        elif Platform == "Darwin":
            cmd = "gtime --verbose --output=%s " % enc_perf + cmd
        else:
            if UsePerfUtil:
                cmd = "3>%s perf stat --log-fd 3 " % enc_perf + cmd
            else:
                cmd = "/usr/bin/time --verbose --output=%s " % enc_perf + cmd
    ExecuteCmd(cmd, LogCmdOnly)


# encode with libavm to compliant with CTC configuration
def EncodeWithAVM_AV2_Constrained(
    clip,
    test_cfg,
    QP,
    framenum,
    outfile,
    preset,
    enc_perf,
    enc_log,
    start_frame=0,
    LogCmdOnly=False,
):
    args = (
        " --verbose --codec=av2 -v --psnr --obu --frame-parallel=0"
        " --cpu-used=%s --limit=%d --skip=%d --passes=1 --end-usage=q --i%s "
        " --use-fixed-qp-offsets=1 --deltaq-mode=0 --cq-level=%d"
        " --enable-tpl-model=0 --fps=%d/%d -w %d -h %d"
        % (
            preset,
            framenum,
            start_frame,
            clip.fmt,
            QP,
            clip.fps_num,
            clip.fps_denom,
            clip.width,
            clip.height,
        )
    )

    # config enoding bitdepth
    if (CTC_VERSION in ["6.0", "7.0", "8.0"]) and (
        clip.file_class in ["A2", "A4", "B1"]
    ):
        # CWG-D088
        args += " --input-bit-depth=%d --bit-depth=10" % (clip.bit_depth)
    else:
        args += " --input-bit-depth=%d --bit-depth=%d" % (
            clip.bit_depth,
            clip.bit_depth,
        )

    # For 4K clip, encode with 2 tile columns using two threads.
    # --tile-columns value is in log2.
    if (
        (CTC_VERSION in ["4.0"])
        and (clip.file_class in ["A2", "B1"])
        and (test_cfg == "LD")
    ):
        args += " --tile-rows=1 --threads=2 --row-mt=0 "
    elif (
        (CTC_VERSION in ["5.0", "6.0"])
        and (clip.file_class in ["A2", "B1"])
        and (test_cfg == "LD")
    ):
        args += " --tile-rows=1 --tile-columns=1 --threads=4 --row-mt=0 "
    elif (
        (CTC_VERSION in ["6.0"])
        and (clip.file_class in ["E", "G1"])
        and (test_cfg == "RA")
    ):
        args += " --tile-rows=1 --tile-columns=1 --threads=4 --row-mt=0 "
    elif CTC_VERSION in ["7.0", "8.0"]:
        if EnableVerificationTestConfig:
            args += " --tile-rows=0 --tile-columns=0 --threads=1 --row-mt=0 "
        elif test_cfg == "RA":
            if clip.file_class in ["A1", "E", "G1"]:
                # 4 column tiles should be used
                args += " --tile-rows=0 --tile-columns=2 --threads=4 --row-mt=0 "
            elif clip.file_class in ["A2", "B1"]:
                # 2 column tiles should be used
                args += " --tile-rows=0 --tile-columns=1 --threads=2 --row-mt=0 "
            else:
                # 1 tile should be used
                args += " --tile-rows=0 --tile-columns=0 --threads=1 --row-mt=0 "
        elif test_cfg == "AS":
            # use the same configuration as RA
            if (clip.width == 3840 and clip.height == 2160) or (
                clip.width == 2560 and clip.height == 1440
            ):
                # 4 column tiles should be used
                args += " --tile-rows=0 --tile-columns=2 --threads=4 --row-mt=0 "
            elif clip.width == 1920 and clip.height == 1080:
                # 2 column tiles should be used
                args += " --tile-rows=0 --tile-columns=1 --threads=2 --row-mt=0 "
            else:
                # 1 tile should be used
                args += " --tile-rows=0 --tile-columns=0 --threads=1 --row-mt=0 "
        elif test_cfg == "LD":
            if clip.file_class in ["A2", "B1"]:
                # 8 tiles should be used
                args += " --tile-rows=1 --tile-columns=2 --threads=8 --row-mt=0 "
            elif clip.file_class in ["A3"]:
                # 2 column tiles should be used
                args += " --tile-rows=0 --tile-columns=1 --threads=2 --row-mt=0 "
            else:
                # 1 tile should be used
                args += " --tile-rows=0 --tile-columns=0 --threads=1 --row-mt=0 "
        elif (test_cfg in ["AI", "STILL"]) and (
            clip.width >= 3840 and clip.height >= 2160
        ):
            # 2 tiles should be used
            args += " --tile-rows=0 --tile-columns=1 --threads=2 --row-mt=0 "
    elif clip.width >= 3840 and clip.height >= 2160:
        args += " --tile-rows=0 --tile-columns=1 --threads=2 --row-mt=0 "
    else:
        args += " --tile-rows=0 --tile-columns=0 --threads=1 --row-mt=0 "

    if EnableOpenGOP:
        args += " --enable-fwd-kf=1 "
    else:
        args += " --enable-fwd-kf=0 "

    if EnableTemporalFilter:
        args += " --enable-keyframe-filtering=1 "
    else:
        args += " --enable-keyframe-filtering=0 "

    if test_cfg == "AI" or test_cfg == "STILL":
        args += " --kf-min-dist=0 --kf-max-dist=0 "
    elif test_cfg == "RA" or test_cfg == "AS":
        args += (
            " --min-gf-interval=%d --max-gf-interval=%d --gf-min-pyr-height=%d"
            " --gf-max-pyr-height=%d --kf-min-dist=%d --kf-max-dist=%d"
            " --lag-in-frames=%d --auto-alt-ref=1 "
            % (
                SUB_GOP_SIZE,
                SUB_GOP_SIZE,
                math.log2(SUB_GOP_SIZE),
                math.log2(SUB_GOP_SIZE),
                GOP_SIZE,
                GOP_SIZE,
                SUB_GOP_SIZE + 3,
            )
        )
    elif test_cfg == "LD":
        args += (
            " --kf-min-dist=9999 --kf-max-dist=9999 --lag-in-frames=0"
            " --min-gf-interval=%d --max-gf-interval=%d --gf-min-pyr-height=%d "
            " --gf-max-pyr-height=%d "
            % (
                SUB_GOP_SIZE,
                SUB_GOP_SIZE,
                math.log2(SUB_GOP_SIZE),
                math.log2(SUB_GOP_SIZE),
            )
        )
    else:
        print("Unsupported Test Configuration %s" % test_cfg)

    if clip.file_class == "G1" or clip.file_class == "G2":
        args += (
            "--color-primaries=bt2020 --transfer-characteristics=smpte2084 "
            "--matrix-coefficients=bt2020ncl --chroma-sample-position=colocated "
        )

    args += " -o %s %s" % (outfile, clip.file_path)
    cmd = AV2ENC + args + "> %s 2>&1" % enc_log
    if EnableTimingInfo:
        if Platform == "Windows":
            cmd = "ptime " + cmd + " >%s" % enc_perf
        elif Platform == "Darwin":
            cmd = "gtime --verbose --output=%s " % enc_perf + cmd
        else:
            if UsePerfUtil:
                cmd = "3>%s perf stat --log-fd 3 " % enc_perf + cmd
            else:
                cmd = "/usr/bin/time --verbose --output=%s " % enc_perf + cmd
    ExecuteCmd(cmd, LogCmdOnly)


def EncodeWithSVT_AV2(
    clip,
    test_cfg,
    QP,
    framenum,
    outfile,
    preset,
    enc_perf,
    enc_log,
    start_frame=0,
    LogCmdOnly=False,
):
    # encode with SVT-AV2 with the best possible quality without constraint
    args = (
        " --preset %s --lp 2 -n %d  -q %d -w %d -h %d  --fps-num %d --fps-denom %d --input-depth %d "
        % (
            str(preset),
            framenum,
            QP,
            clip.width,
            clip.height,
            clip.fps_num,
            clip.fps_denom,
            clip.bit_depth,
        )
    )

    # For 4K clip, encode with 2 tile columns using two threads.
    # --tile-columns value is in log2.
    if EnableVerificationTestConfig:
        args += " "
    elif clip.width >= 3840 and clip.height >= 2160:
        args += " --tile-columns 1 "
    else:
        args += " "

    if test_cfg == "AI" or test_cfg == "STILL":
        args += " --keyint 255 "
    elif test_cfg == "RA" or test_cfg == "AS":
        args += " --keyint %d " % (GOP_SIZE)
    elif test_cfg == "LD":
        args += " --keyint -1 --pred-struct 1 "
    else:
        print("Unsupported Test Configuration %s" % test_cfg)

    if clip.file_class == "G1" or clip.file_class == "G2":
        args += "--enable-hdr 1 "

    args += "-i %s -b %s" % (clip.file_path, outfile)
    cmd = SVTAV2 + args + "> %s 2>&1" % enc_log
    if EnableTimingInfo:
        if Platform == "Windows":
            cmd = "ptime " + cmd + " >%s" % enc_perf
        elif Platform == "Darwin":
            cmd = "gtime --verbose --output=%s " % enc_perf + cmd
        else:
            if UsePerfUtil:
                cmd = "3>%s perf stat --log-fd 3 " % enc_perf + cmd
            else:
                cmd = "/usr/bin/time --verbose --output=%s " % enc_perf + cmd
    ExecuteCmd(cmd, LogCmdOnly)


def EncodeWithHM_HEVC(
    clip,
    test_cfg,
    QP,
    framenum,
    outfile,
    preset,
    enc_perf,
    enc_log,
    start_frame=0,
    LogCmdOnly=False,
):
    input_yuv_file = GetShortContentName(outfile, False) + ".yuv"
    bs_path = os.path.dirname(outfile)
    input_yuv_file = os.path.join(bs_path, input_yuv_file)
    ConvertY4MToYUV(clip, input_yuv_file, LogCmdOnly)

    args = (
        " -c %s -i %s -b %s --SourceWidth=%d --SourceHeight=%d --InputBitDepth=%d --InternalBitDepth=%d "
        " --InputChromaFormat=420 --FrameRate=%d --GOPSize=%d --FramesToBeEncoded=%d --QP=%d "
        % (
            HEVCCfgFile,
            input_yuv_file,
            outfile,
            clip.width,
            clip.height,
            clip.bit_depth,
            clip.bit_depth,
            clip.fps,
            SUB_GOP_SIZE,
            framenum,
            QP,
        )
    )

    args += " --ConformanceWindowMode=1 "  # needed to support non multiple of 8 resolutions.

    # enable open Gop
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

    cmd = HMENC + args + "> %s 2>&1" % enc_log
    if EnableTimingInfo:
        if Platform == "Windows":
            cmd = "ptime " + cmd + " >%s" % enc_perf
        elif Platform == "Darwin":
            cmd = "gtime --verbose --output=%s " % enc_perf + cmd
        else:
            if UsePerfUtil:
                cmd = "3>%s perf stat --log-fd 3 " % enc_perf + cmd
            else:
                cmd = "/usr/bin/time --verbose --output=%s " % enc_perf + cmd
    ExecuteCmd(cmd, LogCmdOnly)

    DeleteFile(input_yuv_file, LogCmdOnly)


def VideoEncode(
    EncodeMethod,
    CodecName,
    clip,
    test_cfg,
    QP,
    framenum,
    outfile,
    preset,
    enc_perf,
    enc_log,
    start_frame=0,
    LogCmdOnly=False,
):
    Utils.CmdLogger.write("::Encode\n")
    if CodecName == "av2":
        if EncodeMethod == "avm":
            EncodeWithAVM_AV2(
                clip,
                test_cfg,
                QP,
                framenum,
                outfile,
                preset,
                enc_perf,
                enc_log,
                start_frame,
                LogCmdOnly,
            )
    elif CodecName == "av2":
        if EncodeMethod == "avm":
            EncodeWithAVM_AV2_Constrained(
                clip,
                test_cfg,
                QP,
                framenum,
                outfile,
                preset,
                enc_perf,
                enc_log,
                start_frame,
                LogCmdOnly,
            )
        elif EncodeMethod == "svt":
            EncodeWithSVT_AV2(
                clip,
                test_cfg,
                QP,
                framenum,
                outfile,
                preset,
                enc_perf,
                enc_log,
                start_frame,
                LogCmdOnly,
            )
        else:
            raise ValueError("invalid parameter for encode.")
    elif CodecName == "hevc":
        if EncodeMethod == "hm":
            EncodeWithHM_HEVC(
                clip,
                test_cfg,
                QP,
                framenum,
                outfile,
                preset,
                enc_perf,
                enc_log,
                start_frame,
                LogCmdOnly,
            )
        else:
            raise ValueError("invalid parameter for encode.")
    else:
        raise ValueError("invalid parameter for encode.")
