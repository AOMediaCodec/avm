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
"""
Perceptual quality metrics (LPIPS, DISTS, ColorVideoVDP) for the AVM CTC framework.

Shaped to mirror CalcQtyWithVmafTool.py so it plugs into the same dispatch:
one function issues the measurement command, another gathers results from the
log it produced. The actual computation happens in PerceptualMetricsRunner.py,
invoked as a subprocess via Utils.ExecuteCmd so it is captured into the
per-job shell scripts used for cluster dispatch.
"""
__author__ = "maggie.sun@intel.com, ryanlei@meta.com"

import json
import logging
import math
import os
import sys

from Config import (
    CVVDPDisplayMap,
    FFMPEG,
    LoggerName,
    PerceptualDevice,
    PerceptualFrameStep,
    PerceptualHDRClasses,
    PerceptualLpipsNet,
    PerceptualMetricsList,
)
from Utils import ExecuteCmd, GetShortContentName

subloggername = "CalcPerceptualMetrics"
loggername = LoggerName + "." + "%s" % subloggername
logger = logging.getLogger(loggername)

# Canonical order of the perceptual metrics. Kept separate from
# Config.QualityList on purpose: AV2CTCProgress.WriteSheet copies the RD CSV
# into the CTC .xlsm templates by absolute column index, so inserting these
# into QualityList would shift the timing and MD5 columns and corrupt every
# generated workbook. They are appended after DecMD5 instead.
PerceptualMetricsFullList = ["LPIPS", "DISTS", "CVVDP"]

# Metrics that are sequence-level only and so are absent from the per-frame CSV.
# ColorVideoVDP does produce a per-frame JOD series, but it is excluded here
# only if the runner could not derive one.
PerceptualPerFrameCapable = ["LPIPS", "DISTS", "CVVDP"]

RUNNER = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "PerceptualMetricsRunner.py")


def IsHDRClip(file_class):
    """HDR-ness is not carried on the Clip object; it is implied by video class."""
    return file_class in PerceptualHDRClasses


def GetPerceptualLogFile(recfile, path):
    filename = GetShortContentName(recfile, False) + "_perceptual.json"
    return os.path.join(path, filename)


def GetPerceptualExecLogFile(recfile, path):
    filename = GetShortContentName(recfile, False) + "_perceptualexec.log"
    return os.path.join(path, filename)


def SelectCVVDPDisplay(width, height, is_hdr):
    """Pick the ColorVideoVDP display preset for this clip.

    The preset describes the *display*, not the image, so a mismatched preset
    changes the score: 1080p content judged against a 4K preset is modelled as
    occupying only the centre quarter of the screen.
    """
    if is_hdr:
        return CVVDPDisplayMap.get("hdr", "standard_hdr_pq")
    if height >= 2160:
        return CVVDPDisplayMap.get("2160p", "standard_4k")
    if height >= 1080:
        return CVVDPDisplayMap.get("1080p", "standard_fhd")
    return CVVDPDisplayMap.get("default", "standard_fhd")


def SelectMetrics(is_hdr):
    """LPIPS and DISTS are trained on 8-bit sRGB content.

    On PQ/BT.2020 material they are out of distribution and their scores are
    not defensible, so only ColorVideoVDP runs on the HDR classes.
    """
    metrics = [m for m in PerceptualMetricsList if m in PerceptualMetricsFullList]
    if is_hdr:
        metrics = [m for m in metrics if m == "CVVDP"]
    return metrics


################################################################################
##################### Exposed Functions ########################################
def Perceptual_CalQualityMetrics(
    origfile, recfile, clip, QualityLogPath, PerceptualLogPath, LogCmdOnly=False
):
    """Emit the command that measures the perceptual metrics for one encode."""
    is_hdr = IsHDRClip(getattr(clip, "file_class", ""))
    metrics = SelectMetrics(is_hdr)
    if not metrics:
        logger.info("no perceptual metrics enabled for class %s"
                    % getattr(clip, "file_class", "?"))
        return

    perceptual_log = GetPerceptualLogFile(recfile, QualityLogPath)
    exec_log = GetPerceptualExecLogFile(recfile, PerceptualLogPath)
    display = SelectCVVDPDisplay(clip.width, clip.height, is_hdr)

    args = " --ref %s --dist %s --out %s" % (origfile, recfile, perceptual_log)
    args += " --metrics %s" % ",".join(metrics)
    args += " --frame-step %d" % PerceptualFrameStep
    args += " --device %s" % PerceptualDevice
    args += " --cvvdp-display %s" % display
    args += " --lpips-net %s" % PerceptualLpipsNet
    args += " --ffmpeg %s" % FFMPEG
    if is_hdr:
        args += " --hdr"

    cmd = "%s %s%s" % (sys.executable, RUNNER, args)
    cmd += "> %s 2>&1" % exec_log
    ExecuteCmd(cmd, LogCmdOnly)


def ParsePerceptualLogFile(perceptual_log):
    """Return (aggregates in PerceptualMetricsFullList order, per-frame rows).

    A metric that was deliberately skipped comes back as NaN so it can be
    written as an empty CSV cell, which is distinct from a genuine 0.0.
    """
    try:
        with open(perceptual_log, "r") as f:
            payload = json.load(f)
    except (IOError, ValueError) as e:
        logger.error("cannot read perceptual log %s: %s" % (perceptual_log, e))
        return [], []

    metrics = payload.get("metrics", {})
    n_frames = int(payload.get("n_frames", 0))

    results = []
    for name in PerceptualMetricsFullList:
        entry = metrics.get(name)
        if entry is None:
            results.append(float("nan"))
        else:
            try:
                results.append(float(entry.get("aggregate", float("nan"))))
            except (TypeError, ValueError):
                results.append(float("nan"))

    # Per-frame values, aligned to frame index, one list per frame in
    # PerceptualMetricsFullList order. Returned unjoined so the caller can
    # select the enabled subset without having to re-split; frames that were
    # not evaluated (frame_step > 1) get an empty string rather than an
    # interpolated value.
    per_frame_log = []
    for idx in range(n_frames):
        fields = []
        for name in PerceptualMetricsFullList:
            entry = metrics.get(name)
            if entry is None:
                fields.append("")
                continue
            # JSON object keys are strings.
            val = entry.get("per_frame", {}).get(str(idx))
            fields.append("" if val is None else "%f" % float(val))
        per_frame_log.append(fields)

    print_str = "Perceptual quality metrics: "
    for name, val in zip(PerceptualMetricsFullList, results):
        print_str += "%s = %s, " % (name, "n/a" if math.isnan(val) else "%2.5f" % val)
    logger.info(print_str)

    return results, per_frame_log


def Perceptual_GatherQualityMetrics(recfile, logfilePath):
    perceptual_log = GetPerceptualLogFile(recfile, logfilePath)
    if not os.path.exists(perceptual_log):
        return [], []
    return ParsePerceptualLogFile(perceptual_log)


def EmptyPerceptualResults():
    """All-NaN aggregates, for encodes where the metrics were not run."""
    return [float("nan")] * len(PerceptualMetricsFullList)
