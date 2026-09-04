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

import csv
import math
import os
import re
import shutil
from itertools import cycle

import Config
import matplotlib.pyplot as plt
import numpy as np
import openpyxl
import pandas as pd
import Utils
from CalcBDRate import BD_RATE
from Config import (
    CTC_ASXLSTemplate,
    CTC_ECFXLSTemplate,
    CTC_RegularXLSTemplate,
    EnableECF,
    InterpolatePieces,
    QPs,
    UsePCHIPInterpolation,
)
from matplotlib.backends.backend_pdf import PdfPages
from tabulate import tabulate
from Utils import (
    convex_hull,
    Interpolate_Bilinear,
    Interpolate_PCHIP,
    ParseCSVFile,
    plot_rd_curve,
)

CTC_RESULT_PATH = "../ctc_result"

qtys = [
    "psnr_y",
    "psnr_u",
    "psnr_v",
    "overall_psnr",
    "ssim_y",
    "ms_ssim_y",
    "vmaf",
    "vmaf_neg",
    "psnr_hvs",
    "ciede2k",
    "apsnr_y",
    "apsnr_u",
    "apsnr_v",
    "overall_apsnr",
    "lpips",
    "dists",
    "cvvdp",
]

# Metrics where a LOWER score means better quality. BD_RATE assumes the
# opposite, so these are negated before it is called; without that a genuine
# improvement would be reported with the wrong sign.
LOWER_IS_BETTER_QTYS = {"lpips", "dists"}

csv_paths = {
    "v01.0.0": [
        "v01.0.0",
        "av2",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v1.0.0-alt-anchor-r4.0"),
    ],
    "v01.0.0-scale": [
        "v01.0.0",
        "av2",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v1.0.0-alt-anchor-r4.0-scale"),
    ],
    "v02.0.0": [
        "v02.0.0",
        "av2",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v2.0.0"),
    ],
    "v03.0.0": [
        "v03.0.0",
        "av2",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v3.0.0"),
    ],
    "v04.0.0": [
        "v04.0.0",
        "av2",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v4.0.0"),
    ],
    "v05.0.0": [
        "v05.0.0",
        "av2",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v5.0.0"),
    ],
    "v06.0.0": [
        "v06.0.0",
        "av2",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v6.0.0"),
    ],
    "v07.0.0": [
        "v07.0.0",
        "av2",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v7.0.0"),
    ],
    "v08.0.0": [
        "v08.0.0",
        "av2",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v8.0.0"),
    ],
    "v09.0.0": [
        "v09.0.0",
        "av2",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v9.0.0"),
    ],
    "v10.0.0": [
        "v10.0.0",
        "av2",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v10.0.0"),
    ],
    "v11.0.0": [
        "v11.0.0",
        "av2",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v11.0.0"),
    ],
    "v12.0.0": [
        "v12.0.0",
        "av2",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v12.0.0"),
    ],
    "v13.0.0": [
        "v13.0.0",
        "av2",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v13.0.0-scale"),
    ],
    "v14.0.0": [
        "v14.0.0",
        "av2",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v14.0.0"),
    ],
    "v15.0.0": [
        "v15.0.0",
        "av2",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v15.0.0"),
    ],
}
"""
    "libaom-v3.12.0": [
        "libaom-v3.12.0",
        "av1",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV1-CTC-v3.12.0-constrained"),
    ],
    "libaom-v3.12.0-unconstrained": [
        "libaom-v3.12.0-unconstrained",
        "av1",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV1-CTC-v3.12.0-unconstrained"),
    ],
    """

CONFIG = ["AI", "LD", "RA", "Still", "AS"]

start_row = {
    "AI": 2,
    "AS": 2,
    "RA": 2,
    "Still": 2,
    "LD": 50,
}

# key is the release
formats = {
    "v01.0.0": ["r", "-", "o"],
    "v02.0.0": ["g", "-", "*"],
    "v03.0.0": ["b", "--", "^"],
    "v04.0.0": ["c", "--", "o"],
    "v05.0.0": ["m", "-.", "*"],
    "v06.0.0": ["y", "-.", "o"],
    "v07.0.0": ["k", ":", "+"],
    "v08.0.0": ["w", ":", "^"],
    # "libaom-v3.12.0": ["r", "--", "<"],
    # "libaom-v3.12.0-unconstrained": ["g", "--", "<"],
    "v09.0.0": ["b", "-.", "^"],
    "v10.0.0": ["c", "-.", "+"],
    "v11.0.0": ["m", "-.", "o"],
    "v12.0.0": ["r", ":", "x"],
    "v13.0.0": ["g", ":", "+"],
    "v14.0.0": ["b", ":", "x"],
    "v15.0.0": ["c", ":", "o"],
}

# key is the release
dates = {
    "v01.0.0": "01/16/2021",
    "v01.0.0-scale": "01/16/2021",
    "v02.0.0": "08/27/2021",
    "v03.0.0": "05/27/2022",
    "v04.0.0": "04/04/2023",
    "v05.0.0": "08/14/2023",
    "v06.0.0": "01/05/2024",
    "v07.0.0": "05/20/2024",
    "v08.0.0": "09/16/2024",
    "v09.0.0": "01/24/2025",
    "v10.0.0": "06/02/2025",
    "v11.0.0": "08/29/2025",
    "v12.0.0": "10/27/2025",
    "v13.0.0": "12/19/2025",
    "v14.0.0": "04/01/2026",
    "v15.0.0": "05/05/2026",
    # "libaom-v3.12.0": "02/10/2025",
    # "libaom-v3.12.0-unconstrained": "01/10/2025",
}


# key is the test configuration. The colors are validated for color-vision
# deficiency as a set, and the marker shape repeats the identity on a second,
# non-color channel. Keyed by name so a configuration keeps its color on charts
# where only some of the configurations are present.
cfg_styles = {
    "AI": ("#2a78d6", "o"),
    "AS": ("#eda100", "s"),
    "LD": ("#e87ba4", "^"),
    "RA": ("#008300", "D"),
    "Still": ("#4a3aa7", "v"),
}

# key is the video class, value is what the class is made of. Used to expand the
# class name in chart legends; a class with no entry here is legended bare.
class_content = {
    "A1": "2160p",
    "A2": "1080p",
    "A3": "720p",
    "A4": "360p",
    "A5": "270p",
    "B1": "1080p Synthetic",
    "B2": "1080p Screen Content",
    "E": "UGC",
    "G1": "2160p HDR",
    "G2": "1080p HDR",
    "F1": "High Resolution Image",
    "F2": "Medium Resolution Image",
}

# quality metrics drawn as per-configuration trend lines, key is the csv column
trend_metrics = {
    "psnr_y": "PSNR-Y",
    "overall_psnr": "PSNR-YUV",
    "ssim_y": "SSIM",
    "ms_ssim_y": "MS-SSIM",
    "vmaf": "VMAF",
    "vmaf_neg": "VMAF-NEG",
}


AS_formats = {
    "3840x2160": ["r", "-.", "o"],
    "2560x1440": ["g", "-.", "*"],
    "1920x1080": ["b", "-.", "^"],
    "1280x720": ["y", "-.", "+"],
    "960x540": ["c", "-.", "x"],
    "640x360": ["k", "-.", "<"],
}

rd_curve_pdf = os.path.join(CTC_RESULT_PATH, "rdcurve.pdf")
combined_rd_curve_pdf = os.path.join(CTC_RESULT_PATH, "combined_rdcurve.pdf")
combined_runtime_pdf = os.path.join(CTC_RESULT_PATH, "combined_runtime.pdf")
bdrate_summary = os.path.join(CTC_RESULT_PATH, "Bdrate-Summary-AV1-vs-AV2.csv")
avg_bdrate_by_tag_class = os.path.join(
    CTC_RESULT_PATH, "AverageBdrateByTagClass-Summary-AV1-vs-AV2.csv"
)
avg_bdrate_by_tag = os.path.join(
    CTC_RESULT_PATH, "AverageBdrateByTag-Summary-AV1-vs-AV2.csv"
)
per_video_bdrate = os.path.join(
    CTC_RESULT_PATH, "PerVideoBdrate-Summary-AV1-vs-AV2.csv"
)
avg_bdrate_by_tag_pdf = os.path.join(
    CTC_RESULT_PATH, "AverageBdrateByTag-Summary-AV1-vs-AV2.pdf"
)
avg_bdrate_by_tag_class_pdf = os.path.join(
    CTC_RESULT_PATH, "AverageBdrateByTagClass-Summary-AV1-vs-AV2.pdf"
)
per_video_bdrate_by_tag_class_pdf = os.path.join(
    CTC_RESULT_PATH, "PerVideoBdrate-Summary-AV1-vs-AV2.pdf"
)
colors = cycle("bgrycmkw")
markers = cycle("o*^+<x>.")


def populate_stats_files():
    stats_files = {}
    for tag in csv_paths.keys():
        release = csv_paths[tag][0]
        codec = csv_paths[tag][1]
        encoder = csv_paths[tag][2]
        preset = csv_paths[tag][3]
        path = csv_paths[tag][4]
        stats_files[tag] = {}
        stats_files[tag]["release"] = release
        stats_files[tag]["config"] = {}
        for cfg in CONFIG:
            if cfg == "Still":
                stats_files[tag]["config"][cfg] = os.path.join(
                    path,
                    "RDResults_%s_%s_STILL_Preset_%s.csv" % (encoder, codec, preset),
                )
            else:
                stats_files[tag]["config"][cfg] = os.path.join(
                    path,
                    "RDResults_%s_%s_%s_Preset_%s.csv" % (encoder, codec, cfg, preset),
                )
    return stats_files


def WriteSheet(csv_file, sht, start_row):
    csv = open(csv_file, "rt")
    row = start_row
    for line in csv:
        if not line.startswith("TestCfg"):
            words = re.split(",", line.strip())
            col = 1
            for word in words:
                mycell = sht.cell(row=row, column=col)
                if col >= 12 and col <= 30 and word != "":
                    mycell.value = float(word)
                else:
                    mycell.value = word
                col += 1
            row += 1
    csv.close()


def get_anchor_tag(tag):
    if tag in ["v01.0.0-scale"]:
        anchor_tag = "None"
    elif tag in ["v13.0.0", "v14.0.0", "v15.0.0"]:
        anchor_tag = "v01.0.0-scale"
    else:
        anchor_tag = "v01.0.0"
    return anchor_tag


def FillXlsFile(csv_files):
    for tag in csv_files.keys():
        if tag in ["v01.0.0", "v01.0.0-scale"]:
            continue
        else:
            release = csv_files[tag]["release"]
            anchor = get_anchor_tag(tag)
            anchor_release = csv_files[anchor]["release"]

            print("Processing %s..." % release)
            for cfg in csv_files[anchor]["config"].keys():
                if cfg not in CONFIG:
                    continue
                anchor_sht_name = "Anchor-%s" % cfg
                test_sht_name = "Test-%s" % cfg
                if EnableECF and cfg == "AI":
                    xls_template = CTC_ECFXLSTemplate
                    xls_file = os.path.join(
                        CTC_RESULT_PATH,
                        "CTC_ECF_%s-%s.xlsm" % (anchor_release, release),
                    )
                    shutil.copyfile(xls_template, xls_file)
                elif cfg == "AS":
                    xls_template = CTC_ASXLSTemplate
                    xls_file = os.path.join(
                        CTC_RESULT_PATH, "CTC_AS_%s-%s.xlsm" % (anchor_release, release)
                    )
                    shutil.copyfile(xls_template, xls_file)
                    anchor_sht_name = "Anchor"
                    test_sht_name = "Test"
                elif cfg == "AI":
                    xls_template = CTC_RegularXLSTemplate
                    xls_file = os.path.join(
                        CTC_RESULT_PATH,
                        "CTC_Regular_%s-%s.xlsm"
                        % (
                            anchor_release,
                            release,
                        ),
                    )
                    shutil.copyfile(xls_template, xls_file)

                wb = openpyxl.load_workbook(
                    filename=xls_file, read_only=False, keep_vba=True
                )
                anchor_sht = wb[anchor_sht_name]
                anchor_csv = csv_files[anchor]["config"][cfg]
                WriteSheet(anchor_csv, anchor_sht, start_row[cfg])

                test_sht = wb[test_sht_name]
                test_csv = csv_files[tag]["config"][cfg]
                WriteSheet(test_csv, test_sht, start_row[cfg])

                wb.save(xls_file)


def DrawIndividualRDCurve(records, pdf):
    tmp_anchor = "v01.0.0"

    with PdfPages(pdf) as export_pdf:
        for cfg in records[tmp_anchor]["config"].keys():
            videos = records[tmp_anchor]["config"][cfg].keys()
            for video in videos:
                if cfg == "AS":
                    Int_RDPoints = {}
                    # draw individual rd curves
                    for tag in records.keys():
                        Int_RDPoints[tag] = []
                        if video not in records[tag]["config"][cfg].keys():
                            continue

                        record = records[tag]["config"][cfg][video]
                        plt.figure(figsize=(15, 10))
                        plt.suptitle("%s : %s: %s" % (cfg, video, tag))

                        br = {}
                        apsnr = {}
                        for key in record.keys():
                            res = re.split("_", key)[0]
                            if res not in br.keys():
                                br[res] = []
                                apsnr[res] = []
                            br[res].append(record[key].bitrate)
                            apsnr[res].append(record[key].overall_apsnr)

                        for res in br.keys():
                            rdpnts = [
                                (brt, qty) for brt, qty in zip(br[res], apsnr[res])
                            ]
                            if UsePCHIPInterpolation:
                                int_rdpnts = Interpolate_PCHIP(
                                    rdpnts, QPs["AS"][:], InterpolatePieces, True
                                )
                            else:
                                int_rdpnts = Interpolate_Bilinear(
                                    rdpnts, QPs["AS"][:], InterpolatePieces, True
                                )
                            Int_RDPoints[tag] += int_rdpnts
                            plot_rd_curve(
                                br[res],
                                apsnr[res],
                                "overall_apsnr",
                                res,
                                "bitrate(Kbps)",
                                AS_formats[res][0],
                                AS_formats[res][1],
                                AS_formats[res][2],
                            )
                        plt.legend(loc="lower right")
                        plt.grid(True)
                        export_pdf.savefig()
                        plt.close()

                    # draw convex hull
                    plt.figure(figsize=(15, 10))
                    plt.suptitle("%s : %s: convex hull" % (cfg, video))
                    for tag in records.keys():
                        release = records[tag]["release"]
                        lower, upper = convex_hull(Int_RDPoints[tag])
                        br = [h[0] for h in upper]
                        apsnr = [h[1] for h in upper]
                        plot_rd_curve(
                            br,
                            apsnr,
                            "overall_apsnr(dB)",
                            release,
                            "bitrate(kbps)",
                            formats[release][0],
                            formats[release][1],
                            formats[release][2],
                        )
                    plt.legend(loc="lower right")
                    plt.grid(True)
                    export_pdf.savefig()
                    plt.close()
                else:
                    # bit rate to quality
                    plt.figure(figsize=(15, 10))
                    plt.suptitle("%s : %s" % (cfg, video))
                    for tag in records.keys():
                        if video not in records[tag]["config"][cfg].keys():
                            continue

                        release = records[tag]["release"]
                        record = records[tag]["config"][cfg][video]
                        br = [record[key].bitrate for key in record.keys()]
                        apsnr = [record[key].overall_apsnr for key in record.keys()]
                        plot_rd_curve(
                            br,
                            apsnr,
                            "overall_apsnr(dB)",
                            release,
                            "bitrate(kbps)",
                            formats[release][0],
                            formats[release][1],
                            formats[release][2],
                        )
                    plt.legend(loc="lower right")
                    plt.grid(True)
                    export_pdf.savefig()
                    plt.close()


def CalcOneBDRate(qty, br_anchor, qty_anchor, br_test, qty_test):
    """BD-rate for one metric, handling polarity and missing values.

    BD_RATE assumes higher quality is better. LPIPS and DISTS are the
    opposite, so their values are negated first; the resulting BD-rate then
    keeps the usual convention that negative means a bitrate saving.

    A metric that was not computed (NaN) yields an error code so the caller
    records 0.0 rather than fabricating a comparison from missing data.
    """
    if any(v is None or math.isnan(v) for v in list(qty_anchor) + list(qty_test)):
        return (-1, "metric not available")

    if qty in LOWER_IS_BETTER_QTYS:
        qty_anchor = [-v for v in qty_anchor]
        qty_test = [-v for v in qty_test]

    return BD_RATE(qty, br_anchor, qty_anchor, br_test, qty_test)


def GetQty(record, key, qty):
    if qty == "psnr_y":
        q = record[key].psnr_y
    elif qty == "psnr_u":
        q = record[key].psnr_u
    elif qty == "psnr_v":
        q = record[key].psnr_v
    elif qty == "overall_psnr":
        q = record[key].overall_psnr
    elif qty == "ssim_y":
        q = record[key].ssim_y
    elif qty == "ms_ssim_y":
        q = record[key].ms_ssim_y
    elif qty == "vmaf":
        q = record[key].vmaf_y
    elif qty == "vmaf_neg":
        q = record[key].vmaf_y_neg
    elif qty == "psnr_hvs":
        q = record[key].psnr_hvs
    elif qty == "ciede2k":
        q = record[key].ciede2k
    elif qty == "apsnr_y":
        q = record[key].apsnr_y
    elif qty == "apsnr_u":
        q = record[key].apsnr_u
    elif qty == "apsnr_v":
        q = record[key].apsnr_v
    elif qty == "overall_apsnr":
        q = record[key].overall_apsnr
    elif qty == "lpips":
        q = record[key].lpips
    elif qty == "dists":
        q = record[key].dists
    elif qty == "cvvdp":
        q = record[key].cvvdp
    else:
        assert 0
    return q


def CalcBDRate(tag, cfg, cls, video, anchor, test):
    bdrate = {}
    anchor_qty = {}
    test_qty = {}
    br_anchor = []
    br_test = []
    for key in anchor.keys():
        br_anchor.append(anchor[key].bitrate)
    for key in test.keys():
        br_test.append(test[key].bitrate)

    for qty in qtys:
        anchor_qty[qty] = []
        test_qty[qty] = []
        for key in anchor.keys():
            anchor_qty[qty].append(GetQty(anchor, key, qty))
        for key in test.keys():
            test_qty[qty].append(GetQty(test, key, qty))

    bdrate["tag"] = tag
    bdrate["cfg"] = cfg
    bdrate["class"] = cls
    bdrate["video"] = video

    for qty in qtys:
        (err, bd) = CalcOneBDRate(
            qty, br_anchor, anchor_qty[qty], br_test, test_qty[qty]
        )
        if err == 0:
            bdrate[qty] = bd
        else:
            bdrate[qty] = 0.0
    return bdrate


def CalcConvexHull(record):
    br = {}
    quality = {}
    cvx_hull = {}

    for key in record.keys():
        res = re.split("_", key)[0]
        if res not in br.keys():
            br[res] = []
            quality[res] = {}

        br[res].append(record[key].bitrate)
        for q in qtys:
            if q not in quality[res].keys():
                quality[res][q] = []
            quality[res][q].append(GetQty(record, key, q))

    for q in qtys:
        # A metric that was never computed is all-NaN. Interpolating and hulling
        # NaN does not raise, it silently yields a meaningless hull, so skip it
        # here and let the caller record "not available".
        if all(
            math.isnan(v) for res in br.keys() for v in quality[res][q]
        ):
            cvx_hull[q] = []
            continue

        Int_RDPoints = []
        for res in br.keys():
            rdpnts = [(brt, qty) for brt, qty in zip(br[res], quality[res][q])]
            if UsePCHIPInterpolation:
                int_rdpnts = Interpolate_PCHIP(
                    rdpnts, QPs["AS"][:], InterpolatePieces, True
                )
            else:
                int_rdpnts = Interpolate_Bilinear(
                    rdpnts, QPs["AS"][:], InterpolatePieces, True
                )
            Int_RDPoints += int_rdpnts

        lower, upper = convex_hull(Int_RDPoints)
        cvx_hull[q] = upper

    return cvx_hull


def CalcASBDRate(tag, cfg, cls, video, anchor, test):
    # first produce convex hull for each quality
    convex_hull = {}
    convex_hull["anchor"] = CalcConvexHull(anchor)
    convex_hull["test"] = CalcConvexHull(test)

    # calculate bdrate for each quality metric
    bdrate = {}
    bdrate["tag"] = tag
    bdrate["cfg"] = cfg
    bdrate["class"] = cls
    bdrate["video"] = video

    for q in qtys:
        # Empty hull means the metric was not computed for this content.
        if not convex_hull["anchor"][q] or not convex_hull["test"][q]:
            bdrate[q] = 0.0
            continue

        br_anchor = list(zip(*convex_hull["anchor"][q]))[0]
        br_test = list(zip(*convex_hull["test"][q]))[0]
        quality_anchor = list(zip(*convex_hull["anchor"][q]))[1]
        quality_test = list(zip(*convex_hull["test"][q]))[1]

        (err, bd) = CalcOneBDRate(
            q, br_anchor, quality_anchor, br_test, quality_test
        )
        if err == 0:
            bdrate[q] = bd
        else:
            bdrate[q] = 0.0
    return bdrate


def HasMatchingQPs(anchor_record, test_record, cfg):
    """Check that anchor and test records have the same number of QPs.
    Actual QP values can differ between anchor and test.
    For non-AS configs, require exactly 6 QPs each.
    For AS configs, require that every resolution present in both has exactly 6 QPs each.
    """
    anchor_keys = set(anchor_record.keys())
    test_keys = set(test_record.keys())

    if cfg == "AS":
        # Group keys by resolution
        anchor_res = {}
        test_res = {}
        for key in anchor_keys:
            res = re.split("_", key)[0]
            anchor_res.setdefault(res, []).append(key)
        for key in test_keys:
            res = re.split("_", key)[0]
            test_res.setdefault(res, []).append(key)
        common_res = set(anchor_res.keys()) & set(test_res.keys())
        if not common_res:
            return False
        for res in common_res:
            if len(anchor_res[res]) != 6 or len(test_res[res]) != 6:
                return False
        return True
    else:
        if len(anchor_keys) != 6 or len(test_keys) != 6:
            return False
        return True


def CalcFullBDRate(csv_files, records):
    bdrate = []
    seq_time = []
    tmp_anchor = "v01.0.0"
    for cfg in csv_files[tmp_anchor]["config"].keys():
        if cfg not in CONFIG:
            continue
        for video in records[tmp_anchor]["config"][cfg].keys():
            for tag in records.keys():
                if video not in records[tag]["config"][cfg].keys():
                    continue

                record = records[tag]["config"][cfg][video]
                release = records[tag]["release"]
                time = 0
                instr = 0
                video_class = ""
                for key in record.keys():
                    time += record[key].enc_time
                    instr += record[key].enc_instr
                    video_class = record[key].file_class

                total_time = {}
                total_time["tag"] = release
                total_time["cfg"] = cfg
                total_time["class"] = video_class
                total_time["video"] = video
                total_time["time"] = time
                total_time["instr"] = instr

                seq_time.append(total_time)
                anchor = get_anchor_tag(tag)
                if tag == anchor or anchor == "None":
                    continue

                # Skip if anchor and test don't have matching QPs
                anchor_record = records[anchor]["config"][cfg][video]
                test_record = records[tag]["config"][cfg][video]
                if not HasMatchingQPs(anchor_record, test_record, cfg):
                    print(
                        "Warning: Skipping %s %s %s - mismatched or missing QPs "
                        "(anchor has %d keys, test has %d keys)"
                        % (cfg, video, tag, len(anchor_record), len(test_record))
                    )
                    continue

                if cfg == "AS":
                    bd = CalcASBDRate(
                        tag,
                        cfg,
                        video_class,
                        video,
                        anchor_record,
                        test_record,
                    )
                else:
                    bd = CalcBDRate(
                        tag,
                        cfg,
                        video_class,
                        video,
                        anchor_record,
                        test_record,
                    )
                bdrate.append(bd)

    return (bdrate, seq_time)


def write_bdrate(bdrate, bdrate_csv):
    # Open the CSV file for writing
    with open(bdrate_csv, "w", encoding="utf8", newline="") as csvfile:
        fc = csv.DictWriter(
            csvfile,
            fieldnames=bdrate[0].keys(),
            lineterminator="\n",
        )
        fc.writeheader()
        for row in bdrate:
            fc.writerow(
                {k: v.strip() if isinstance(v, str) else v for k, v in row.items()}
            )


def MeanAggFields(df):
    """Metric columns to average, in `qtys` order.

    Restricted to columns actually present, so a bdrate CSV produced before the
    perceptual metrics existed still aggregates instead of raising KeyError.
    """
    return [q for q in qtys if q in df.columns]


def MeanAggSpec(df):
    """pandas .agg() spec taking the mean of every metric column present."""
    return {q: ["mean"] for q in MeanAggFields(df)}


def write_avg_bdrate(
    bdrate_csv, avg_bdrate_by_tag_csv, avg_bdrate_by_tag_class_csv, per_video_bdrate_csv
):
    df = pd.read_csv(bdrate_csv)
    average_bdrate_by_tag_class = df.groupby(["tag", "cfg", "class"]).agg(
        MeanAggSpec(df)
    )

    # Derived from the same source as the aggregation spec, and in the same
    # order, so the two cannot drift apart. They are matched positionally
    # (`.columns = fields_name`), so a mismatch would silently mislabel every
    # metric column.
    fields_name = MeanAggFields(df)
    # print(average_bdrate_by_tag_class)
    # print(tabulate(average_bdrate_by_tag_class, headers='keys', tablefmt='psql'))

    # Write output summary csv file
    average_bdrate_by_tag_class.columns = fields_name
    average_bdrate_by_tag_class.reset_index(inplace=True)
    average_bdrate_by_tag_class.to_csv(avg_bdrate_by_tag_class_csv, index=False)

    average_bdrate_by_tag = df.groupby(["tag", "cfg"]).agg(
        MeanAggSpec(df)
    )

    # print(average_bdrate_by_tag)
    # print(tabulate(average_bdrate_by_tag, headers='keys', tablefmt='psql'))
    # Write output summary csv file
    average_bdrate_by_tag.columns = fields_name
    average_bdrate_by_tag.reset_index(inplace=True)
    average_bdrate_by_tag.to_csv(avg_bdrate_by_tag_csv, index=False)

    average_bdrate_by_video = df.groupby(["cfg", "class", "video", "tag"]).agg(
        MeanAggSpec(df)
    )

    average_bdrate_by_video.columns = fields_name
    average_bdrate_by_video.reset_index(inplace=True)
    average_bdrate_by_video.to_csv(per_video_bdrate_csv, index=False)


def plot_bdrate_trend_by_cfg(df, tags, qty, title, export_pdf):
    """Write one BDRATE trend page: x is the release date, one line per
    configuration. Configurations absent from df are skipped, so the caller can
    pass a slice covering only some of them."""
    date_labels = [
        pd.to_datetime(dates[tag]).strftime("%m/%d/%y") if tag in dates else tag
        for tag in tags
    ]
    release_labels = [re.sub(r"^v0*(\d+).*", r"v\1", tag) for tag in tags]
    x_positions = list(range(len(tags)))

    fig, ax = plt.subplots(figsize=(16, 15))

    series = {}
    for cfg, (color, marker) in cfg_styles.items():
        cfg_df = df[df["cfg"] == cfg]
        if cfg_df.empty:
            continue
        series[cfg] = [
            cfg_df.loc[tag, qty] if tag in cfg_df.index else np.nan for tag in tags
        ]
        ax.plot(
            x_positions,
            series[cfg],
            color=color,
            marker=marker,
            markersize=10,
            linewidth=2,
            label=cfg,
        )

    if not series:
        plt.close()
        return

    # Tag the topmost point at each release, so the version reads once per
    # x position instead of being repeated on every line.
    for x, release in zip(x_positions, release_labels):
        column = [series[cfg][x] for cfg in series if pd.notna(series[cfg][x])]
        if not column:
            continue
        ax.annotate(
            release,
            (x, max(column)),
            textcoords="offset points",
            xytext=(0, 14),
            ha="center",
            fontsize=16,
            color="#52514e",
        )

    ax.set_title(title, fontsize=40)
    ax.set_xlabel("Release Date", fontsize=20)
    ax.set_ylabel("BDRATE (%)", fontsize=20)
    ax.set_xticks(x_positions)
    ax.set_xticklabels(
        date_labels,
        rotation=45,
        rotation_mode="anchor",
        ha="right",
        fontsize=16,
    )
    ax.tick_params(axis="y", labelsize=16)
    ax.grid(True, linestyle="--", color="#e1e0d9", linewidth=0.8)
    ax.set_axisbelow(True)

    # Open space at the bottom so the legend never lands on a line.
    ymin, ymax = ax.get_ylim()
    ax.set_ylim(ymin - 0.12 * (ymax - ymin), ymax)
    # A lone line is already named by the title; a legend box would be noise.
    if len(series) > 1:
        ax.legend(loc="lower left", fontsize=20, framealpha=0.9)

    plt.tight_layout()
    export_pdf.savefig()
    plt.close()


def plot_avg_bdrate_by_tag(avg_bdrate_by_tag_csv, avg_bdrate_by_tag_pdf):
    df = pd.read_csv(avg_bdrate_by_tag_csv, index_col=0)
    # print(df)
    with PdfPages(avg_bdrate_by_tag_pdf) as export_pdf:
        # Existing bar charts - one chart per configuration and quality metric
        for cfg in df["cfg"].unique().tolist():
            for qty in ["overall_psnr", "ssim_y", "vmaf"]:
                ax = df[df["cfg"] == cfg][qty].plot(
                    kind="bar", figsize=(30, 15), fontsize=20
                )
                ax.set_title("BDRATE-%s for %s" % (qty, cfg), fontsize=40)
                ax.set_xlabel("Tag", fontsize=20)
                ax.set_ylabel("BDRATE", fontsize=20)
                xticks = ax.get_xticks()
                xtickslabels = [label.get_text() for label in ax.get_xticklabels()]
                xtickslabels = [f"{tag}\n{dates[tag]}" for tag in xtickslabels]
                ax.set_xticks(xticks, labels=xtickslabels)

                for q in ax.containers:
                    ax.bar_label(q, fontsize=20)
                plt.xticks(rotation=30, horizontalalignment="center")
                plt.grid(True)
                # plt.show()
                export_pdf.savefig()
                plt.close()

        # New line charts - one chart per configuration with all 3 quality metrics
        line_colors = {
            "overall_psnr": "#1f77b4",  # Blue
            "ssim_y": "#ff7f0e",  # Orange
            "vmaf": "#2ca02c",  # Green
        }
        line_labels = {
            "overall_psnr": "PSNR-Overall",
            "ssim_y": "SSIM-Y",
            "vmaf": "VMAF",
        }
        line_markers = {"overall_psnr": "o", "ssim_y": "s", "vmaf": "^"}

        for cfg in df["cfg"].unique().tolist():
            fig, ax = plt.subplots(figsize=(30, 15))

            cfg_df = df[df["cfg"] == cfg].copy()
            tags = cfg_df.index.tolist()
            x_labels = [f"{tag}\n{dates[tag]}" for tag in tags]
            x_positions = range(len(tags))

            # Plot each quality metric as a line
            for qty in ["overall_psnr", "ssim_y", "vmaf"]:
                y_values = cfg_df[qty].tolist()
                ax.plot(
                    x_positions,
                    y_values,
                    color=line_colors[qty],
                    marker=line_markers[qty],
                    markersize=12,
                    linewidth=2.5,
                    label=line_labels[qty],
                )
                # Add data labels on each point
                for x, y in zip(x_positions, y_values):
                    ax.annotate(
                        f"{y:.2f}",
                        (x, y),
                        textcoords="offset points",
                        xytext=(0, 10),
                        ha="center",
                        fontsize=14,
                    )

            ax.set_title(f"BDRATE Trends for {cfg} Configuration", fontsize=40)
            ax.set_xlabel("Release Tag / Date", fontsize=20)
            ax.set_ylabel("BDRATE (%)", fontsize=20)
            ax.set_xticks(x_positions)
            ax.set_xticklabels(x_labels, rotation=30, ha="center", fontsize=16)
            ax.tick_params(axis="y", labelsize=16)
            ax.grid(True, linestyle="--", alpha=0.7)
            ax.legend(loc="lower left", fontsize=20, framealpha=0.9)

            plt.tight_layout()
            export_pdf.savefig()
            plt.close()

        # New line charts - one chart per quality metric, one line per configuration
        tags = df.index.unique().tolist()
        for qty, qty_label in trend_metrics.items():
            plot_bdrate_trend_by_cfg(
                df,
                tags,
                qty,
                f"BDRATE {qty_label} by Configuration",
                export_pdf,
            )


def plot_avg_bdrate_by_tag_class(
    avg_bdrate_by_tag_class_csv, avg_bdrate_by_tag_class_pdf
):
    df = pd.read_csv(avg_bdrate_by_tag_class_csv, index_col=0)
    # print(df)
    with PdfPages(avg_bdrate_by_tag_class_pdf) as export_pdf:
        # Existing bar charts - one chart per configuration, class, and quality metric
        for cfg in df["cfg"].unique().tolist():
            # print(df["class"].unique().tolist())
            for cls in df["class"].unique().tolist():
                for qty in ["overall_psnr", "ssim_y", "vmaf"]:
                    selected_rows = df[(df["cfg"] == cfg) & (df["class"] == cls)]
                    if selected_rows.empty:
                        continue
                    # print(selected_rows)
                    ax = selected_rows[qty].plot(
                        kind="bar", figsize=(30, 15), fontsize=20
                    )
                    ax.set_title(
                        "BDRATE-%s for %s class %s" % (qty, cfg, cls), fontsize=40
                    )
                    ax.set_xlabel("Tag", fontsize=20)
                    ax.set_ylabel("BDRATE", fontsize=20)
                    xticks = ax.get_xticks()
                    xtickslabels = [label.get_text() for label in ax.get_xticklabels()]
                    xtickslabels = [f"{tag}\n{dates[tag]}" for tag in xtickslabels]
                    ax.set_xticks(xticks, labels=xtickslabels)

                    for q in ax.containers:
                        ax.bar_label(q, fontsize=20)
                    plt.xticks(rotation=30, horizontalalignment="center")
                    plt.grid(True)
                    # plt.show()
                    export_pdf.savefig()
                    plt.close()

        # New line charts - one chart per configuration and quality metric
        # Each chart has multiple series (one per video class)
        # Hue comes from a color-vision-validated set, indexed off the full
        # class list so a class keeps its color on every chart. A configuration
        # can code more than eight classes, in which case a hue repeats; those
        # classes are drawn dashed and with their own marker so the pair sharing
        # a hue stays distinguishable.
        class_hues = [
            "#2a78d6",  # Blue
            "#eb6834",  # Orange
            "#1baf7a",  # Aqua
            "#eda100",  # Yellow
            "#e87ba4",  # Magenta
            "#008300",  # Green
            "#4a3aa7",  # Violet
            "#e34948",  # Red
        ]
        class_markers = ["o", "s", "^", "D", "v", "p", "h", "*", "X", "P", "<", ">"]
        class_styles = {
            cls: (
                class_hues[i % len(class_hues)],
                class_markers[i % len(class_markers)],
                "-" if i < len(class_hues) else "--",
            )
            for i, cls in enumerate(df["class"].unique().tolist())
        }

        for cfg in df["cfg"].unique().tolist():
            cfg_df = df[df["cfg"] == cfg]
            # AS codes only class A1, so a by-class chart of it is a single line
            # with nothing to compare against. Skip any such configuration.
            if cfg_df["class"].nunique() < 2:
                continue

            for qty, qty_label in trend_metrics.items():
                fig, ax = plt.subplots(figsize=(16, 15))

                # Get unique tags and classes for this configuration
                tags = cfg_df.index.unique().tolist()
                classes = cfg_df["class"].unique().tolist()
                x_labels = [
                    (
                        f"{tag}\n{pd.to_datetime(dates[tag]).strftime('%m/%d/%y')}"
                        if tag in dates
                        else tag
                    )
                    for tag in tags
                ]
                x_positions = range(len(tags))

                # Plot each class as a separate line
                for cls in classes:
                    cls_data = cfg_df[cfg_df["class"] == cls]
                    if cls_data.empty:
                        continue

                    # Get y values for each tag (may have missing values)
                    y_values = []
                    valid_x = []
                    for j, tag in enumerate(tags):
                        if tag in cls_data.index:
                            val = cls_data.loc[tag, qty]
                            if pd.notna(val):
                                y_values.append(val)
                                valid_x.append(j)

                    if not y_values:
                        continue

                    color, marker, linestyle = class_styles[cls]
                    label = (
                        f"{cls} - {class_content[cls]}" if cls in class_content else cls
                    )

                    ax.plot(
                        valid_x,
                        y_values,
                        color=color,
                        marker=marker,
                        linestyle=linestyle,
                        markersize=10,
                        linewidth=2,
                        label=label,
                    )

                ax.set_title(
                    f"BDRATE {qty_label} Trends by Class for {cfg} Configuration",
                    fontsize=32,
                )
                ax.set_xlabel("Release Tag / Date", fontsize=20)
                ax.set_ylabel(f"BDRATE {qty_label} for {cfg} (%)", fontsize=20)
                ax.set_xticks(x_positions)
                ax.set_xticklabels(
                    x_labels,
                    rotation=45,
                    rotation_mode="anchor",
                    ha="right",
                    fontsize=16,
                )
                ax.tick_params(axis="y", labelsize=16)
                ax.grid(True, linestyle="--", alpha=0.7)

                # Open space at the bottom so the legend never lands on a line.
                # Scaled by series count, since the legend grows with it.
                ymin, ymax = ax.get_ylim()
                pad = 0.04 * max(1, len(ax.get_lines()))
                ax.set_ylim(ymin - pad * (ymax - ymin), ymax)
                ax.legend(loc="lower left", fontsize=18, framealpha=0.9)

                plt.tight_layout()
                export_pdf.savefig()
                plt.close()


def plot_per_video_bdrate_by_tag_class(
    per_video_bdrate_csv, per_video_bdrate_by_tag_class_pdf
):
    df = pd.read_csv(per_video_bdrate_csv)
    # print(df)
    with PdfPages(per_video_bdrate_by_tag_class_pdf) as export_pdf:
        for cfg in df["cfg"].unique().tolist():
            for cls in df["class"].unique().tolist():
                for video in df["video"].unique().tolist():
                    for qty in ["overall_psnr", "ssim_y", "vmaf"]:
                        selected_rows = df[
                            (df["cfg"] == cfg)
                            & (df["class"] == cls)
                            & (df["video"] == video)
                        ]
                        if selected_rows.empty:
                            continue
                        # print(selected_rows)
                        tags = selected_rows["tag"].unique().tolist()
                        idx = np.asarray([i for i in range(len(tags))])
                        ax = selected_rows[qty].plot(
                            kind="bar", figsize=(30, 15), fontsize=20
                        )
                        ax.set_title(
                            "BDRATE-%s for %s class %s %s" % (qty, cfg, cls, video),
                            fontsize=40,
                        )
                        ax.set_xlabel("Tag", fontsize=20)
                        ax.set_ylabel("BDRATE", fontsize=20)
                        xticks = ax.get_xticks()
                        xtickslabels = [f"{tag}\n{dates[tag]}" for tag in tags]
                        ax.set_xticks(xticks, labels=xtickslabels)

                        for q in ax.containers:
                            ax.bar_label(q, fontsize=20)
                        ax.set_xticks(idx)
                        ax.set_xticklabels(
                            tags, rotation=30, horizontalalignment="center"
                        )
                        plt.grid(True)
                        plt.tight_layout()
                        # plt.show()
                        export_pdf.savefig()
                        plt.close()


######################################
# main
######################################
if __name__ == "__main__":
    csv_files = populate_stats_files()
    records = {}
    for tag in csv_files.keys():
        records[tag] = {}
        records[tag]["config"] = {}
        for test_cfg in csv_files[tag]["config"].keys():
            if test_cfg not in CONFIG:
                continue
            IgnorePerf = test_cfg in ["RA", "AS"]
            records[tag]["config"][test_cfg] = ParseCSVFile(
                csv_files[tag]["config"][test_cfg], IgnorePerf
            )
            records[tag]["release"] = csv_files[tag]["release"]

    FillXlsFile(csv_files)

    DrawIndividualRDCurve(records, rd_curve_pdf)

    # Calculate BDRate and collect total time
    (bdrate, seq_time) = CalcFullBDRate(csv_files, records)

    write_bdrate(bdrate, bdrate_summary)

    write_avg_bdrate(
        bdrate_summary, avg_bdrate_by_tag, avg_bdrate_by_tag_class, per_video_bdrate
    )

    plot_avg_bdrate_by_tag(avg_bdrate_by_tag, avg_bdrate_by_tag_pdf)

    plot_avg_bdrate_by_tag_class(avg_bdrate_by_tag_class, avg_bdrate_by_tag_class_pdf)

    plot_per_video_bdrate_by_tag_class(
        per_video_bdrate, per_video_bdrate_by_tag_class_pdf
    )
